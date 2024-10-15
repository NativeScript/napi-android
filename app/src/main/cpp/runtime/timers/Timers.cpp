#include "Timers.h"
#include "ArgConverter.h"
#include "Runtime.h"
#include "NativeScriptException.h"
#include <android/looper.h>
#include <unistd.h>
#include <thread>
#include "Util.h"

/**
 * Overall rules when modifying this file:
 * `sortedTimers_` must always be sorted by dueTime
 * `sortedTimers_`. `deletedTimers_` and `stopped` modifications MUST be done while locked with the mutex
 * `threadLoop` must not access anything that is not `sortedTimers_` or `stopped` or any atomic var
 * ALL changes and scheduling of a TimerTask MUST be done when locked in an isolate to ensure consistency
 */

// Takes a value and transform into a positive number
// returns a negative number if the number is negative or invalid
inline static double ToMaybePositiveValue(napi_env env, napi_value v) {
    double value = -1;
    if (napi_util::is_null_or_undefined(env, v)) {
        return -1;
    }
    napi_value numberValue;
    napi_status status = napi_coerce_to_number(env, v, &numberValue);
    if (status == napi_ok) {
        status = napi_get_value_double(env, numberValue, &value);
        if (status != napi_ok || isnan(value)) {
            value = -1;
        }
    }
    return value;
}

static double now_ms() {
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;
}

using namespace ns;

void Timers::Init(napi_env env, napi_value global) {
    env_ = env;
    // TODO: remove the __ns__ prefix once this is validated
    napi_util::napi_set_function(env, global, "__ns__setTimeout", SetTimeoutCallback, this);
    napi_util::napi_set_function(env, global, "__ns__setInterval", SetIntervalCallback, this);
    napi_util::napi_set_function(env, global, "__ns__clearTimeout", ClearTimer, this);
    napi_util::napi_set_function(env, global, "__ns__clearInterval", ClearTimer, this);

    napi_add_finalizer(env, global, this, [](napi_env env, void *finalizeData, void *finalizeHint) {
        auto thiz = reinterpret_cast<Timers *>(finalizeData);
        delete thiz;
    }, nullptr, nullptr);

    auto res = pipe(fd_);
    assert(res != -1);
    res = fcntl(fd_[1], F_SETFL, O_NONBLOCK);
    assert(res != -1);
    // TODO: check success of fd
    looper_ = ALooper_prepare(0);
    ALooper_acquire(looper_);
    ALooper_addFd(looper_, fd_[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                  PumpTimerLoopCallback, this);
    ALooper_wake(looper_);
    watcher_ = std::thread(&Timers::threadLoop, this);
    stopped = false;
}

void Timers::addTask(const std::shared_ptr<TimerTask>& task) {
    if (task->queued_) {
        return;
    }
    auto now = now_ms();
    task->nestingLevel_ = nesting + 1;
    task->queued_ = true;
    // theoretically this should be >5 on the spec, but we're following chromium behavior here again
    if (task->nestingLevel_ >= 5 && task->frequency_ < 4) {
        task->frequency_ = 4;
        task->startTime_ = now;
    }
    timerMap_.emplace(task->id_, task);
    auto newTime = task->NextTime(now);
    task->dueTime_ = newTime;
    bool needsScheduling = true;
    if (!isBufferFull.load() && task->dueTime_ <= now) {
        auto result = write(fd_[1], &task->id_, sizeof(int));
        if (result != -1 || errno != EAGAIN) {
            needsScheduling = false;
        } else {
            isBufferFull = true;
        }
    }
    if (needsScheduling) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = sortedTimers_.begin();
            auto dueTime = task->dueTime_;
            it = std::upper_bound(sortedTimers_.begin(), sortedTimers_.end(), dueTime,
                                  [](const double &value,
                                     const std::shared_ptr<TimerReference> &ref) {
                                      return ref->dueTime > value;
                                  });
            auto ref = std::make_shared<TimerReference>();
            ref->dueTime = task->dueTime_;
            ref->id = task->id_;
            sortedTimers_.insert(it, ref);
        }
        taskReady.notify_one();
    }
}

void Timers::removeTask(const std::shared_ptr<TimerTask> &task) {
    removeTask(task->id_);
}

void Timers::removeTask(const int &taskId) {
    auto it = timerMap_.find(taskId);
    if (it != timerMap_.end()) {
        auto wasScheduled = it->second->queued_;
        it->second->Unschedule();
        timerMap_.erase(it);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (wasScheduled) {
                // was scheduled, notify the thread so it doesn't trigger again
                this->deletedTimers_.insert(taskId);
            } else {
                // was not scheduled, remove it to reduce memory footprint
                this->deletedTimers_.erase(taskId);
            }
        }
    }
}

void Timers::threadLoop() {
    std::unique_lock<std::mutex> lk(mutex);
    while (!stopped) {
        if (!sortedTimers_.empty()) {
            auto timer = sortedTimers_.at(0);
            if (deletedTimers_.find(timer->id) != deletedTimers_.end()) {
                sortedTimers_.erase(sortedTimers_.begin());
                deletedTimers_.erase(timer->id);
                continue;
            }
            auto now = now_ms();
            // timer has reached its time, fire it and keep going
            if (timer->dueTime <= now) {
                sortedTimers_.erase(sortedTimers_.begin());
                auto result = write(fd_[1], &timer->id, sizeof(int));
                if (result == -1 && errno == EAGAIN) {
                    isBufferFull = true;
                    while (!stopped && deletedTimers_.find(timer->id) != deletedTimers_.end() &&
                           write(fd_[1], &timer->id, sizeof(int)) == -1 && errno == EAGAIN) {
                        bufferFull.wait(lk);
                    }
                } else if (isBufferFull.load() &&
                           (sortedTimers_.empty() || sortedTimers_.at(0)->dueTime > now)) {
                    // we had a successful write and the next timer is not due
                    // mark the buffer as free to re-enable the setTimeout with 0 optimization
                    isBufferFull = false;
                }
            } else {
                taskReady.wait_for(lk, std::chrono::milliseconds((int) (timer->dueTime - now)));
            }
        } else {
            taskReady.wait(lk);
        }
    }
}

void Timers::Destroy() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopped) {
            return;
        }
        stopped = true;
    }
    bufferFull.notify_one();
    taskReady.notify_all();
    watcher_.join();
    auto mainLooper = Runtime::GetMainLooper();
    ALooper_removeFd(mainLooper, fd_[0]);
    close(fd_[0]);
    timerMap_.clear();
    ALooper_release(looper_);
}

Timers::~Timers() {
    Destroy();
}

napi_value Timers::SetTimeoutCallback(napi_env env, napi_callback_info info) {
    return Timers::SetTimer(env, info, false);
}

napi_value Timers::SetIntervalCallback(napi_env env, napi_callback_info info) {
    return Timers::SetTimer(env, info, true);
}

napi_value Timers::ClearTimer(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    void *data;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);

    int id = -1;
    if (argc > 0) {
        id = (int) ToMaybePositiveValue(env, args[0]);
    }
    // ids start at 1
    if (id > 0) {
        auto thiz = reinterpret_cast<Timers *>(data);
        thiz->removeTask(id);
    }

    return nullptr;
}

napi_value Timers::SetTimer(napi_env env, napi_callback_info info, bool repeatable) {
    NAPI_CALLBACK_BEGIN_VARGS()

    auto thiz = reinterpret_cast<Timers *>(data);

    int id = ++thiz->currentTimerId;
    if (argc >= 1) {

        if (!napi_util::is_of_type(env, argv[0], napi_function)) {
            napi_value result;
            napi_get_undefined(env, &result);
            return result;
        }

        napi_value handler = argv[0];
        long timeout = 0;
        if (argc >= 2) {
            timeout = (long) ToMaybePositiveValue(env, argv[1]);
            if (timeout < 0) {
                timeout = 0;
            }
        }

        std::shared_ptr<std::vector<napi_ref>> argArray;
        if (argc >= 3) {
            auto otherArgLength = argc - 2;
            argArray = std::make_shared<std::vector<napi_ref>>(otherArgLength);
            for (size_t i = 0; i < otherArgLength; i++) {
                (*argArray)[i] = napi_util::make_ref(env, argv[i + 2]);
            }
        }

        auto task = std::make_shared<TimerTask>(env, napi_util::make_ref(env, handler), timeout,
                                                repeatable, argArray,
                                                napi_util::make_ref(env, jsThis), id, now_ms());
        thiz->addTask(task);
    }
    napi_value result;
    napi_create_int32(env, id, &result);
    return result;
}

/**
 * ALooper callback.
 * Responsible for checking if the callback is still scheduled and entering the isolate to trigger it
 */
int Timers::PumpTimerLoopCallback(int fd, int events, void *data) {
    int timerId;
    read(fd, &timerId, sizeof(int));

    auto thiz = static_cast<Timers *>(data);
    auto env = thiz->env_;
    if (thiz->stopped || env == nullptr) {
        return 0;
    }

    auto it = thiz->timerMap_.find(timerId);
    if (it != thiz->timerMap_.end()) {
        auto task = it->second;
        // task is no longer in queue to be executed
        task->queued_ = false;
        thiz->nesting = task->nestingLevel_;
        if (task->repeats_) {
            // the reason we're doing this in kind of a convoluted way is to follow more closely the chromium implementation than the node implementation
            // imagine an interval of 1000ms
            // node's setInterval drifts slightly (1000, 2001, 3001, 4002, some busy work 5050, 6050)
            // chromium will be consistent: (1000, 2001, 3000, 4000, some busy work 5050, 6000)
            task->startTime_ = task->dueTime_;
            thiz->addTask(task);
        }


        napi_value global;
        napi_get_global(env, &global);

        napi_value cb = napi_util::get_ref_value(env, task->callback_);
        size_t argc = task->args_ == nullptr ? 0 : task->args_->size();
        if (argc > 0) {
            std::vector<napi_value> argv(argc);
            for (size_t i = 0; i < argc; i++) {
                argv[i] = napi_util::get_ref_value(env, task->args_->at(i));
            }
            napi_call_function(env, napi_util::get_ref_value(env, task->thisArg), cb, argc,
                               argv.data(), nullptr);
        } else {
            napi_call_function(env, napi_util::get_ref_value(env, task->thisArg), cb, 0, nullptr,
                               nullptr);
        }

        // task is not queued, so it's either a setTimeout or a cleared setInterval
        // ensure we remove it
        if (!task->queued_) {
            thiz->removeTask(task);
            if (argc > 0) {
                for (size_t i = 0; i < argc; i++) {
                    napi_delete_reference(env, task->args_->at(i));
                }
            }
            napi_delete_reference(env, task->thisArg);
        }

        thiz->nesting = 0;
    }
    thiz->bufferFull.notify_one();
    return 1;
}

void Timers::InitStatic(napi_env env, napi_value global) {
    auto timers = new Timers();
    timers->Init(env, global);

}
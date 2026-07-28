#include "Timers.h"
#include "ArgConverter.h"
#include "Runtime.h"
#include "NativeScriptException.h"
#include "JEnv.h"
#include <cmath>
#include <sstream>
#include "Util.h"
#include "NativeScriptAssert.h"

/**
 * Timers ride the runtime thread's Java MessageQueue via a per-runtime
 * com.tns.TimerHandler bound to the isolate's Looper:
 *  - each scheduled timer enqueues one anonymous "due token" message via
 *    sendMessageAtTime, so timers share a single queue with Handler.post/
 *    postDelayed and fire in exact MessageQueue order;
 *  - a native list (sortedTimers_) sorted by exact (sub-millisecond) due time
 *    picks the earliest-due timer per token, preserving the relative ordering of
 *    JS timers despite the millisecond-quantized Java queue.
 *
 * Everything below runs on the runtime thread (Init, the setTimeout/clear
 * callbacks, and FireTimer via TimerHandler.handleMessage), so no locking is
 * needed — sortedTimers_/timerMap_ are only touched there.
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

using namespace tns;

jclass Timers::TIMER_HANDLER_CLASS = nullptr;
jmethodID Timers::TIMER_HANDLER_CTOR = nullptr;
jmethodID Timers::TIMER_HANDLER_POST = nullptr;
jmethodID Timers::TIMER_HANDLER_RELEASE = nullptr;

void Timers::Init(napi_env env, napi_value global) {
    env_ = env;
    // TODO: remove the __ns__ prefix once this is validated
    napi_util::napi_set_function(env, global, "__ns__setTimeout", SetTimeoutCallback, this);
    napi_util::napi_set_function(env, global, "__ns__setInterval", SetIntervalCallback, this);
    napi_util::napi_set_function(env, global, "__ns__clearTimeout", ClearTimer, this);
    napi_util::napi_set_function(env, global, "__ns__clearInterval", ClearTimer, this);

    napi_status status;
    // Non-fatal to timer operation if this fails (only affects GC cleanup of
    // `this`), so log for diagnostics but continue with handler setup.
    NAPI_GUARD(napi_add_finalizer(env, global, this,
                                  [](napi_env env, void *finalizeData, void *finalizeHint) {
                                      auto thiz = reinterpret_cast<Timers *>(finalizeData);
                                      delete thiz;
                                  }, nullptr, nullptr)) {}

    JEnv jEnv;
    if (TIMER_HANDLER_CLASS == nullptr) {
        TIMER_HANDLER_CLASS = jEnv.FindClass("com/tns/TimerHandler");
        assert(TIMER_HANDLER_CLASS != nullptr);
        TIMER_HANDLER_CTOR = jEnv.GetMethodID(TIMER_HANDLER_CLASS, "<init>", "(J)V");
        TIMER_HANDLER_POST = jEnv.GetMethodID(TIMER_HANDLER_CLASS, "post", "(J)V");
        TIMER_HANDLER_RELEASE = jEnv.GetMethodID(TIMER_HANDLER_CLASS, "release", "()V");
    }

    // Bind a TimerHandler to the current (runtime) thread's Looper.
    jobject localHandler = jEnv.NewObject(TIMER_HANDLER_CLASS, TIMER_HANDLER_CTOR,
                                          reinterpret_cast<jlong>(this));
    handler_ = jEnv.NewGlobalRef(localHandler);
    stopped_ = false;
}

void Timers::postTimer(const std::shared_ptr<TimerTask> &task, double now) {
    // Due-now timers post at (long)now so they tie (FIFO) with a same-ms
    // postDelayed(0); future timers post at ceil(dueTime) so they never fire early.
    jlong when = task->dueTime_ <= now ? (jlong) now : (jlong) std::ceil(task->dueTime_);
    JEnv jEnv;
    jEnv.CallVoidMethod(handler_, TIMER_HANDLER_POST, when);
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
    task->dueTime_ = task->NextTime(now);

    auto it = std::upper_bound(sortedTimers_.begin(), sortedTimers_.end(), task->dueTime_,
                               [](const double &value, const TimerReference &ref) {
                                   return ref.dueTime > value;
                               });
    sortedTimers_.insert(it, TimerReference{task->id_, task->dueTime_});

    postTimer(task, now);
}

void Timers::removeTask(const std::shared_ptr<TimerTask> &task) {
    removeTask(task->id_);
}

void Timers::removeTask(const int &taskId) {
    auto it = timerMap_.find(taskId);
    if (it == timerMap_.end()) {
        return;
    }
    auto task = it->second;
    if (task->queued_) {
        // Remove the pending due-token reference (matched by id at its dueTime).
        auto lo = std::lower_bound(sortedTimers_.begin(), sortedTimers_.end(), task->dueTime_,
                                   [](const TimerReference &ref, const double &value) {
                                       return ref.dueTime < value;
                                   });
        for (auto ref = lo; ref != sortedTimers_.end() && ref->dueTime == task->dueTime_; ++ref) {
            if (ref->id == taskId) {
                sortedTimers_.erase(ref);
                break;
            }
        }
        // A token already enqueued in the Java queue is left to no-op in FireTimer.
    }
    task->Unschedule();
    timerMap_.erase(it);
}

void Timers::FireTimer() {
    if (stopped_ || env_ == nullptr) {
        return;
    }

    NapiScope scope(env_);

    if (sortedTimers_.empty()) {
        return; // leftover token
    }
    auto ref = sortedTimers_.front();
    if (ref.dueTime > now_ms()) {
        return; // front not due yet → leftover token
    }
    sortedTimers_.erase(sortedTimers_.begin());

    auto it = timerMap_.find(ref.id);
    if (it == timerMap_.end()) {
        return;
    }
    auto task = it->second;
    task->queued_ = false;
    nesting = task->nestingLevel_;

    if (task->repeats_) {
        // Follow chromium's non-drifting interval scheduling: anchor the next
        // fire to the ideal dueTime rather than "now".
        task->startTime_ = task->dueTime_;
        addTask(task);
    }

    napi_value cb = napi_util::get_ref_value(env_, task->callback_);
    napi_value recv = napi_util::get_ref_value(env_, task->thisArg);
    size_t argc = task->args_ == nullptr ? 0 : task->args_->size();
    napi_status status;
    if (argc > 0) {
        std::vector<napi_value> argv(argc);
        for (size_t i = 0; i < argc; i++) {
            argv[i] = napi_util::get_ref_value(env_, task->args_->at(i));
        }
        NAPI_GUARD(napi_call_function(env_, recv, cb, argc, argv.data(), nullptr)) {}
    } else {
        NAPI_GUARD(napi_call_function(env_, recv, cb, 0, nullptr, nullptr)) {}
    }

    // task is not queued, so it's either a setTimeout or a cleared setInterval:
    // remove it (which releases its JS references via Unschedule).
    if (!task->queued_) {
        removeTask(task);
    }

    nesting = 0;

    // A pending exception left on the env fails every subsequent napi call
    // (NAPI_PREAMBLE returns napi_pending_exception), which would silently stop
    // all timer dispatch. Clear it and surface it to Java instead — cleanup
    // above must run first so the task's references are released.
    bool pendingException = false;
    napi_is_exception_pending(env_, &pendingException);
    if (pendingException) {
        napi_value error = nullptr;
        NAPI_GUARD(napi_get_and_clear_last_exception(env_, &error)) {}
        if (error != nullptr) {
            throw NativeScriptException(env_, error, "Error in timer callback");
        }
    }
}

void Timers::Destroy() {
    if (stopped_) {
        return;
    }
    stopped_ = true;

    if (handler_ != nullptr) {
        JEnv jEnv;
        jEnv.CallVoidMethod(handler_, TIMER_HANDLER_RELEASE);
        jEnv.DeleteGlobalRef(handler_);
        handler_ = nullptr;
    }

    // Release any references still held by pending tasks.
    for (auto &entry: timerMap_) {
        entry.second->Unschedule();
    }
    timerMap_.clear();
    sortedTimers_.clear();
    env_ = nullptr;
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
    void *data = nullptr;
    napi_status status;
    NAPI_GUARD(napi_get_cb_info(env, info, &argc, args, nullptr, &data)) {
        return nullptr;
    }

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
    NAPI_CALLBACK_BEGIN_VARGS_FAST(8)

    auto thiz = reinterpret_cast<Timers *>(data);

    int id = ++thiz->currentTimerId;
    if (argc >= 1) {

        if (!napi_util::is_of_type(env, argv[0], napi_function)) {
            napi_value result;
            NAPI_GUARD(napi_get_undefined(env, &result)) {
                return nullptr;
            }
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
    NAPI_GUARD(napi_create_int32(env, id, &result)) {
        return nullptr;
    }
    return result;
}

void Timers::InitStatic(napi_env env, napi_value global) {
    auto timers = new Timers();
    timers->Init(env, global);
}

// Reverse native for com.tns.TimerHandler.nativeFireTimer (bound by symbol name).
extern "C" JNIEXPORT void JNICALL
Java_com_tns_TimerHandler_nativeFireTimer(JNIEnv* env, jclass clazz, jlong timersPtr) {
    try {
        reinterpret_cast<tns::Timers *>(timersPtr)->FireTimer();
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(nullptr);
    } catch (std::exception& e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(nullptr);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(nullptr);
    }
}

#ifndef TEST_APP_TIMERS_H
#define TEST_APP_TIMERS_H

#include <android/looper.h>
#include "js_native_api.h"
#include "ObjectManager.h"
#include "condition_variable"
#include "thread"
#include "robin_hood.h"

namespace ns {
    /**
     * A Timer Task
     * this class is used to store the persistent values and context
     * once Unschedule is called everything is released
     */
    class TimerTask {
    public:
        inline TimerTask(napi_env env, napi_ref callback, double frequency,
                         bool repeats,
                         const std::shared_ptr<std::vector<napi_ref>> &args,
                         napi_ref _thisArg,
                         int id, double startTime)
                : env_(env), callback_(callback), thisArg(_thisArg),
                  frequency_(frequency), repeats_(repeats), args_(args), id_(id),
                  startTime_(startTime) {

        }

        inline double NextTime(double targetTime) {
            if (frequency_ <= 0) {
                return targetTime;
            }
            auto timeDiff = targetTime - startTime_;
            auto div = std::div((long) timeDiff, (long) frequency_);
            return startTime_ + frequency_ * (div.quot + 1);
        }

        inline void Unschedule() {
            callback_ = nullptr;
            args_.reset();
            env_ = nullptr;
            queued_ = false;
        }

        int nestingLevel_ = 0;
        napi_env env_;
        napi_ref callback_;
        std::shared_ptr<std::vector<napi_ref>> args_;
        napi_ref thisArg;
        bool repeats_ = false;
        /**
         * this helper parameter is used in the following way:
         * task scheduled means queued_ = true
         * this is set to false right before the callback is executed
         * if this is false then it's not on the background thread queue
         */
        bool queued_ = false;
        double frequency_ = 0;
        double dueTime_ = -1;
        double startTime_ = -1;
        int id_;
    };

    struct TimerReference {
        int id;
        double dueTime;
    };

    class Timers {
    public:
        /**
         * Initializes the global functions setTimeout, setInterval, clearTimeout and clearInterval
         * also creates helper threads and binds the timers to the executing thread
         * @param env target environment
         * @param globalObjectTemplate global template
         */
        void Init(napi_env env, napi_value global);

        static void InitStatic(napi_env env, napi_value global);

        /**
         * Disposes the timers. This will clear all references and stop all thread.
         * MUST be called in the same thread Init was called
         * This methods blocks until the threads are stopped.
         * This method doesn't need to be called most of the time as it's called on object destruction
         * Reusing this class is not advised
         */
        void Destroy();

        /**
         * Calls Destruct
         */
        ~Timers();

    private:
        static napi_value SetTimeoutCallback(napi_env env, napi_callback_info info);

        static napi_value SetIntervalCallback(napi_env env, napi_callback_info info);

        static napi_value SetTimer(napi_env env, napi_callback_info info, bool repeatable);

        static napi_value ClearTimer(napi_env env, napi_callback_info info);

        void threadLoop();

        static int PumpTimerLoopCallback(int fd, int events, void *data);

        void addTask(const std::shared_ptr<TimerTask>& task);

        void removeTask(const std::shared_ptr<TimerTask> &task);

        void removeTask(const int &taskId);

        napi_env env_ = nullptr;
        ALooper *looper_;
        int currentTimerId = 0;
        int nesting = 0;
        // stores the map of timer tasks
        robin_hood::unordered_map<int, std::shared_ptr<TimerTask>> timerMap_;
        std::vector<std::shared_ptr<TimerReference>> sortedTimers_;
        // sets are faster than vector iteration
        // so we use this to avoid redundant isolate locks and we don't care about the
        // background thread lost cycles
        std::set<int> deletedTimers_;
        int fd_[2];
        std::atomic_bool isBufferFull = ATOMIC_VAR_INIT(false);
        std::condition_variable taskReady;
        std::condition_variable bufferFull;
        std::mutex mutex;
        std::thread watcher_;
        bool stopped = false;
    };

}

#endif //TEST_APP_TIMERS_H

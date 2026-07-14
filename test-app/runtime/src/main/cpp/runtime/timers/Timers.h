#ifndef TEST_APP_TIMERS_H
#define TEST_APP_TIMERS_H

#include <jni.h>
#include "js_native_api.h"
#include "ObjectManager.h"
#include "robin_hood.h"

namespace tns {
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

        // Releases the JS references held by this task. Requires a live napi_env,
        // so it is called from the runtime thread (FireTimer / removeTask).
        inline void Unschedule() {
            if (env_ != nullptr) {
                if (callback_ != nullptr) {
                    napi_delete_reference(env_, callback_);
                }
                if (thisArg != nullptr) {
                    napi_delete_reference(env_, thisArg);
                }
                if (args_ != nullptr) {
                    for (auto ref: *args_) {
                        napi_delete_reference(env_, ref);
                    }
                }
            }
            callback_ = nullptr;
            thisArg = nullptr;
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
         * and binds a Java TimerHandler to the executing thread's Looper.
         * @param env target environment
         * @param global global object
         */
        void Init(napi_env env, napi_value global);

        static void InitStatic(napi_env env, napi_value global);

        /**
         * Fires the earliest-due timer. Invoked from Java (TimerHandler.handleMessage)
         * on the runtime thread, once per posted "due token".
         */
        void FireTimer();

        /**
         * Disposes the timers, releasing all references and the Java handler.
         * MUST be called on the thread Init was called on. Idempotent.
         */
        void Destroy();

        ~Timers();

    private:
        static napi_value SetTimeoutCallback(napi_env env, napi_callback_info info);

        static napi_value SetIntervalCallback(napi_env env, napi_callback_info info);

        static napi_value SetTimer(napi_env env, napi_callback_info info, bool repeatable);

        static napi_value ClearTimer(napi_env env, napi_callback_info info);

        void addTask(const std::shared_ptr<TimerTask>& task);

        void removeTask(const std::shared_ptr<TimerTask> &task);

        void removeTask(const int &taskId);

        // Enqueues one "due token" on the Java MessageQueue for this task. Due-now
        // timers post at (long)now so they tie (FIFO) with a same-ms postDelayed(0);
        // future timers post at ceil(dueTime) so they never fire early.
        void postTimer(const std::shared_ptr<TimerTask> &task, double now);

        napi_env env_ = nullptr;
        int currentTimerId = 0;
        int nesting = 0;
        // stores the map of timer tasks
        robin_hood::unordered_map<int, std::shared_ptr<TimerTask>> timerMap_;
        // sorted by exact (sub-millisecond) dueTime; touched only on the runtime thread
        std::vector<TimerReference> sortedTimers_;
        // global ref to the com.tns.TimerHandler bound to this thread's Looper
        jobject handler_ = nullptr;
        bool stopped_ = false;

        // Cached (process-wide) TimerHandler JNI ids.
        static jclass TIMER_HANDLER_CLASS;
        static jmethodID TIMER_HANDLER_CTOR;
        static jmethodID TIMER_HANDLER_POST;
        static jmethodID TIMER_HANDLER_RELEASE;
    };

}

#endif //TEST_APP_TIMERS_H

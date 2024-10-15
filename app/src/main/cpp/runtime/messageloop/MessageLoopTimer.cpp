#include "MessageLoopTimer.h"
#include <android/looper.h>
#include <unistd.h>
#include <cerrno>
#include <thread>
#include <android/log.h>
#include "NativeScriptAssert.h"
#include "Runtime.h"

using namespace ns;

static const int SLEEP_INTERVAL_MS = 100;

void MessageLoopTimer::Init(napi_env env) {
    this->RegisterStartStopFunctions(env);

}

void MessageLoopTimer::RegisterStartStopFunctions(napi_env env) {

    napi_value timer_start;
    napi_value timer_stop;

    const char * timer_start_name = "__messageLoopTimerStart";
    const char * timer_stop_name = "__messageLoopTimerStart";

    napi_create_function(env, timer_start_name, strlen(timer_start_name), MessageLoopTimer::StartCallback,
                         this, &timer_start);
    napi_create_function(env, timer_stop_name, strlen(timer_stop_name), MessageLoopTimer::StopCallback,
                         this, &timer_stop);

    napi_value global;
    napi_get_global(env, &global);
    napi_set_named_property(env, global, timer_start_name, timer_start);
    napi_set_named_property(env, global, timer_stop_name, timer_stop);
}

napi_value MessageLoopTimer::StartCallback(napi_env env, napi_callback_info info) {

    void * data;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);

    auto self = static_cast<MessageLoopTimer *>(data);

    if (self->m_isRunning) {
        return nullptr;
    }

    self->m_isRunning = true;

    auto looper = ALooper_forThread();
    if (looper == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "NAPI", "Unable to get looper for the current thread");
        return nullptr;
    }

    int status = pipe(self->m_fd);
    if (status != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "NAPI", "Unable to create a pipe: %s", strerror(errno));
        return nullptr;
    }

    ALooper_addFd(looper, self->m_fd[0], 0, ALOOPER_EVENT_INPUT, MessageLoopTimer::PumpMessageLoopCallback, env);

    std::thread worker(MessageLoopTimer::WorkerThreadRun, self);

    worker.detach();

    return nullptr;
}

napi_value MessageLoopTimer::StopCallback(napi_env env, napi_callback_info info) {
    void * data;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
    auto self = static_cast<MessageLoopTimer *>(data);

    if (!self->m_isRunning) {
        return nullptr;
    }

    self->m_isRunning = false;

    return nullptr;
}

int MessageLoopTimer::PumpMessageLoopCallback(int fd, int events, void* data) {
    uint8_t msg;
    read(fd, &msg, sizeof(uint8_t));
    auto env = (napi_env) data;

    napi_run_microtasks(env);

    return 1;
}

void MessageLoopTimer::WorkerThreadRun(MessageLoopTimer* timer) {
    while (timer->m_isRunning) {
        uint8_t msg = 1;
        write(timer->m_fd[1], &msg, sizeof(uint8_t));
        std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_INTERVAL_MS));
    }

    uint8_t msg = 0;
    write(timer->m_fd[1], &msg, sizeof(uint8_t));
}

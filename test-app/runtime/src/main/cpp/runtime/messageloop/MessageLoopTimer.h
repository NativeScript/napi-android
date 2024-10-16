#ifndef MESSAGELOOPTIMER_H
#define MESSAGELOOPTIMER_H

#include "js_native_api.h"

namespace tns {

class MessageLoopTimer {
public:
    void Init(napi_env env);
private:
    bool m_isRunning;
    int m_fd[2];

    void RegisterStartStopFunctions(napi_env env);
    static napi_value StartCallback(napi_env, napi_callback_info info);
    static napi_value StopCallback(napi_env env, napi_callback_info info);
    static int PumpMessageLoopCallback(int fd, int events, void* data);
    static void WorkerThreadRun(MessageLoopTimer* timer);
};

}

#endif //MESSAGELOOPTIMER_H

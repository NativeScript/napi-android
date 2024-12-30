//
// Created by pkanev on 12/8/2017.
//

#ifndef CONSOLE_H
#define CONSOLE_H

#include <js_native_api.h>
#include <string>
#include <vector>
#include <ArgConverter.h>
#include <android/log.h>

#ifdef APPLICATION_IN_DEBUG
// #include "NetworkDomainCallbackHandlers.h"
#include "JsV8InspectorClient.h"
#endif

#ifdef __V8__
typedef void (*ConsoleCallback)(napi_env env, v8_inspector::ConsoleAPIType method, const std::vector<v8::Local<v8::Value>>& args);
#else
typedef void (*ConsoleCallback)(napi_env env, napi_callback_info info);
#endif

namespace tns {
    class Console {
    public:
        static void createConsole(napi_env env, ConsoleCallback callback, int maxLogcatObjectSize, bool forceLog);

        static napi_value assertCallback(napi_env env, napi_callback_info info);
        static napi_value errorCallback(napi_env env, napi_callback_info info);
        static napi_value infoCallback(napi_env env, napi_callback_info info);
        static napi_value logCallback(napi_env env, napi_callback_info info);
        static napi_value warnCallback(napi_env env, napi_callback_info info);
        static napi_value dirCallback(napi_env env, napi_callback_info info);
        static napi_value traceCallback(napi_env env, napi_callback_info info);
        static napi_value timeCallback(napi_env env, napi_callback_info info);
        static napi_value timeEndCallback(napi_env env, napi_callback_info info);

        static void onDisposeEnv(napi_env env);

    private:

        static int m_maxLogcatObjectSize;
        static ConsoleCallback m_callback;
        static const char* LOG_TAG;
        static std::map<napi_env, std::map<std::string, double>> s_envToConsoleTimersMap;

        static void sendToADBLogcat(const std::string& log, android_LogPriority logPriority);
#ifdef __V8__
        static int sendToDevToolsFrontEnd(napi_env env, ConsoleAPIType method, napi_callback_info info);
        static void sendToDevToolsFrontEnd(napi_env env, ConsoleAPIType method, const std::string& args);
#endif
    };

}

#endif //CONSOLE_H
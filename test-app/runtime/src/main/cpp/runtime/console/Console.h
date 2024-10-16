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

namespace tns {
    class Console {
    public:
        static void createConsole(napi_env env, int maxLogcatObjectSize, bool forceLog);

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
        static const char* LOG_TAG;
        static std::map<napi_env, std::map<std::string, double>> s_envToConsoleTimersMap;

        static void sendToADBLogcat(const std::string& log, android_LogPriority logPriority);
        static void sendToDevToolsFrontEnd(napi_env env, napi_callback_info info);
        static void sendToDevToolsFrontEnd(napi_env env, const std::string& args);
    };

}

#endif //CONSOLE_H
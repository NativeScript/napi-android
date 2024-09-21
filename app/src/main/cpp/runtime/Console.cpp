//
// Created by Ammar Ahmed on 21/09/2024.
//

#include "Console.h"
#include "NativeScriptAssert.h"
#include <vector>
#include <string>

using namespace ns;

napi_value Log(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr);

    std::vector<napi_value> args(argc);
    napi_get_cb_info(env, info, &argc, args.data(), nullptr, nullptr);

    std::string logMessage;

    for (size_t i = 0; i < argc; ++i) {
        napi_value str;
        napi_coerce_to_string(env, args[i], &str);

        size_t str_size;
        napi_get_value_string_utf8(env, str, nullptr, 0, &str_size);

        char *buffer = new char[str_size + 1];
        napi_get_value_string_utf8(env, str, buffer, str_size + 1, &str_size);

        logMessage += buffer;
        if (i < argc - 1) {
            logMessage += " ";
        }

        delete[] buffer;
    }

    DEBUG_WRITE("%s", logMessage.c_str());

    return nullptr;
}

napi_status Console::createConsole(napi_env env, napi_value global) {
    napi_value console;
    napi_create_object(env, &console);

    napi_value log_function;
    napi_create_function(env, "log", NAPI_AUTO_LENGTH, Log, nullptr, &log_function);
    napi_set_named_property(env, console, "log", log_function);

    napi_set_named_property(env, global, "console", console);
    return napi_ok;
}

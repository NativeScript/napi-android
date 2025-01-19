//
// Created by pkanev on 12/8/2017.
//

#include <assert.h>
#include <android/log.h>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <GlobalHelpers.h>
#include <NativeScriptException.h>

#include "ArgConverter.h"
#include "Console.h"

using namespace tns;

#include "Console.h"
#include "ArgConverter.h"
#include "JEnv.h"
#include "NativeScriptException.h"
#include <sstream>
#include <android/log.h>

using namespace std;

#ifdef __V8__
using namespace v8_inspector;
#endif


const char *Console::LOG_TAG = "JS";
std::map<napi_env, std::map<std::string, double>> Console::s_envToConsoleTimersMap;
int Console::m_maxLogcatObjectSize;

void Console::createConsole(napi_env env, ConsoleCallback callback, const int maxLogcatObjectSize, const bool forceLog) {
    m_callback = callback;
    m_maxLogcatObjectSize = maxLogcatObjectSize;

    napi_value console;
    napi_create_object(env, &console);

    napi_value global;
    napi_get_global(env, &global);

    napi_value assertFunc, errorFunc, infoFunc, logFunc, warnFunc, dirFunc, traceFunc, timeFunc, timeEndFunc;
    napi_create_function(env, nullptr, 0, assertCallback, nullptr, &assertFunc);
    napi_create_function(env, nullptr, 0, errorCallback, nullptr, &errorFunc);
    napi_create_function(env, nullptr, 0, infoCallback, nullptr, &infoFunc);
    napi_create_function(env, nullptr, 0, logCallback, nullptr, &logFunc);
    napi_create_function(env, nullptr, 0, warnCallback, nullptr, &warnFunc);
    napi_create_function(env, nullptr, 0, dirCallback, nullptr, &dirFunc);
    napi_create_function(env, nullptr, 0, traceCallback, nullptr, &traceFunc);
    napi_create_function(env, nullptr, 0, timeCallback, nullptr, &timeFunc);
    napi_create_function(env, nullptr, 0, timeEndCallback, nullptr, &timeEndFunc);

    napi_set_named_property(env, console, "assert", assertFunc);
    napi_set_named_property(env, console, "error", errorFunc);
    napi_set_named_property(env, console, "info", infoFunc);
    napi_set_named_property(env, console, "log", logFunc);
    napi_set_named_property(env, console, "warn", warnFunc);
    napi_set_named_property(env, console, "dir", dirFunc);
    napi_set_named_property(env, console, "trace", traceFunc);
    napi_set_named_property(env, console, "time", timeFunc);
    napi_set_named_property(env, console, "timeEnd", timeEndFunc);
    napi_set_named_property(env, global, "console", console);
}

std::string transformJSObject(napi_env env, napi_value object) {
    napi_value toStringFunc;
    bool hasToString = false;

    // Check if the object has a toString method
    napi_has_named_property(env, object, "toString", &hasToString);
    if (hasToString) {
        napi_get_named_property(env, object, "toString", &toStringFunc);
        if (napi_util::is_of_type(env, toStringFunc, napi_function)) {
            napi_value result;
            napi_call_function(env, object, toStringFunc, 0, nullptr, &result);
            return napi_util::get_string_value(env, result);
        }
    }
    // If no custom toString method, stringify the object
    return JsonStringifyObject(env, object, false);
}

std::string buildStringFromArg(napi_env env, napi_value val) {
    napi_valuetype type;
    napi_typeof(env, val, &type);

    if (type == napi_function) {
        napi_value funcString;
        napi_coerce_to_string(env, val, &funcString);
        return napi_util::get_string_value(env, funcString);
    } else if (napi_util::is_array(env, val)) {
        napi_value global;
        napi_get_global(env, &global);
        return JsonStringifyObject(env, val, false);
    } else if (type == napi_object) {
        return transformJSObject(env, val);
    } else if (type == napi_symbol) {
        napi_value symString;
        napi_coerce_to_string(env, val, &symString);
        return "Symbol(" + ArgConverter::ConvertToString(env, symString) + ")";
    } else {
        napi_value defaultToString;
        napi_coerce_to_string(env, val, &defaultToString);
        return napi_util::get_string_value(env, defaultToString);
    }
}

std::string buildLogString(napi_env env, napi_callback_info info, int startingIndex = 0) {
    NAPI_CALLBACK_BEGIN_VARGS()

    std::stringstream ss;

    if (argc) {
        for (size_t i = startingIndex; i < argc; i++) {
            // separate args with a space
            if (i != 0) {
                ss << " ";
            }

            std::string argString = buildStringFromArg(env, argv[i]);
            ss << argString;
        }
    } else {
        ss << std::endl;
    }

    return ss.str();
}

napi_value Console::assertCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        bool expressionPasses = false;

        if (argc > 0) {
            if (napi_util::is_of_type(env, args[0], napi_boolean)) {
                napi_get_value_bool(env, args[0], &expressionPasses);
            } else {
                napi_value boolValue;
                napi_coerce_to_bool(env, args[0], &boolValue);
                napi_get_value_bool(env, boolValue, &expressionPasses);
            }
        }

        if (!expressionPasses) {
            std::stringstream assertionError;
            assertionError << "Assertion failed: ";

            if (argc > 1) {
                assertionError << buildLogString(env, info, 1);
            } else {
                assertionError << "console.assert";
            }

            std::string log = assertionError.str();
            sendToADBLogcat(log, ANDROID_LOG_ERROR);
#ifdef __V8__
            sendToDevToolsFrontEnd(env, ConsoleAPIType::kAssert, info);
#endif
        }
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

napi_value Console::errorCallback(napi_env env, napi_callback_info info) {
    try {
        std::string log = "CONSOLE ERROR: ";
        log += buildLogString(env, info);
        sendToADBLogcat(log, ANDROID_LOG_ERROR);
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kError, info);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value Console::infoCallback(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::string log = "CONSOLE INFO: ";
        log += buildLogString(env, info);

        sendToADBLogcat(log, ANDROID_LOG_INFO);
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kInfo, info);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

napi_value Console::logCallback(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::string log = "CONSOLE LOG: ";
        log += buildLogString(env, info);

        sendToADBLogcat(log, ANDROID_LOG_INFO);
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kLog, info);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

napi_value Console::warnCallback(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::string log = "CONSOLE WARN: ";
        log += buildLogString(env, info);

        sendToADBLogcat(log, ANDROID_LOG_WARN);
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kWarning, info);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

napi_value Console::dirCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::stringstream ss;

        if (argc > 0) {
            napi_value arg = args[0];
            if (napi_util::is_object(env, arg)) {
                ss << "==== object dump start ====" << std::endl;

                napi_value propNames;
                napi_get_property_names(env, arg, &propNames);

                uint32_t propertiesLen;
                napi_get_array_length(env, propNames, &propertiesLen);

                for (uint32_t i = 0; i < propertiesLen; i++) {
                    napi_value propertyName;
                    napi_get_element(env, propNames, i, &propertyName);

                    napi_value propertyValue;
                    napi_get_property(env, arg, propertyName, &propertyValue);

                    bool propIsFunction = napi_util::is_of_type(env, propertyValue, napi_function);

                    ss << napi_util::get_string_value(env, propertyName);

                    if (propIsFunction) {
                        ss << "()";
                    } else if (napi_util::is_array(env, propertyValue)) {
                        std::string jsonStringifiedArray = buildStringFromArg(env, propertyValue);
                        ss << ": " << jsonStringifiedArray;
                    } else if (napi_util::is_of_type(env, propertyValue, napi_object)) {
                        std::string jsonStringifiedObject = transformJSObject(env, propertyValue);
                        // if object prints out as the error string for circular references, replace with #CR instead for brevity
                        if (jsonStringifiedObject.find("circular structure") != std::string::npos) {
                            jsonStringifiedObject = "#CR";
                        }
                        ss << ": " << jsonStringifiedObject;
                    } else {
                        ss << ": \"" << napi_util::get_string_value(env, propertyValue) << "\"";
                    }

                    ss << std::endl;
                }

                ss << "==== object dump end ====" << std::endl;
            } else {
                std::string logString = buildLogString(env, info);
                ss << logString;
            }
        } else {
            ss << std::endl;
        }

        std::string log = ss.str();

        sendToADBLogcat(log, ANDROID_LOG_INFO);
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kDir, info);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value Console::traceCallback(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::stringstream ss;

        std::string logString = buildLogString(env, info);

        if (logString.compare("\n") == 0) {
            ss << "Trace";
        } else {
            ss << "Trace: " << logString;
        }

        ss << std::endl;

        // Create an error object to get the stack trace
        napi_value error;
        napi_value errorMessage;
        napi_create_string_utf8(env, "Trace", NAPI_AUTO_LENGTH, &errorMessage);
        napi_create_error(env, nullptr, errorMessage, &error);

        napi_value stack;
        napi_get_named_property(env, error, "stack", &stack);

        size_t stackLength;
        napi_get_value_string_utf8(env, stack, nullptr, 0, &stackLength);
        std::string stackStr(stackLength + 1, '\0');
        napi_get_value_string_utf8(env, stack, &stackStr[0], stackLength + 1, &stackLength);

        ss << stackStr << std::endl;

        std::string log = ss.str();
        __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, log.c_str());
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kTrace, info);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value Console::timeCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::string label = "default";

        if (argc > 0 && napi_util::is_of_type(env, args[0], napi_string)) {
            label = napi_util::get_string_value(env, args[0]);
        }

        auto it = Console::s_envToConsoleTimersMap.find(env);
        if (it == Console::s_envToConsoleTimersMap.end()) {
            // throw?
        }

        auto nano = std::chrono::time_point_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now());
        double timeStamp = nano.time_since_epoch().count();

        it->second.insert(std::make_pair(label, timeStamp));
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value Console::timeEndCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        std::string label = "default";

        if (argc > 0 && napi_util::is_of_type(env, args[0], napi_string)) {
            label = napi_util::get_string_value(env, args[0]);
        }

        auto it = Console::s_envToConsoleTimersMap.find(env);
        if (it == Console::s_envToConsoleTimersMap.end()) {
            // throw?
        }

        std::map<std::string, double> timersMap = it->second;

        auto itTimersMap = timersMap.find(label);
        if (itTimersMap == timersMap.end()) {
            std::string warning = std::string(
                    "No such label '" + label + "' for console.timeEnd()");

            __android_log_write(ANDROID_LOG_WARN, LOG_TAG, warning.c_str());
#ifdef __V8__
            sendToDevToolsFrontEnd(env, ConsoleAPIType::kWarning, warning);
#endif

            return nullptr;
        }

        auto nano = std::chrono::time_point_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now());
        double endTimeStamp = nano.time_since_epoch().count();
        double startTimeStamp = itTimersMap->second;

        it->second.erase(label);

        double diffMicroseconds = endTimeStamp - startTimeStamp;
        double diffMilliseconds = diffMicroseconds / 1000.0;

        std::stringstream ss;
        ss << "CONSOLE TIME: " << label << ": " << std::fixed << std::setprecision(3)
           << diffMilliseconds << "ms";
        std::string log = ss.str();

        __android_log_write(ANDROID_LOG_INFO, LOG_TAG, log.c_str());
#ifdef __V8__
        sendToDevToolsFrontEnd(env, ConsoleAPIType::kTimeEnd, log);
#endif
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

void Console::onDisposeEnv(napi_env env) {
    s_envToConsoleTimersMap.erase(env);
}

void Console::sendToADBLogcat(const std::string &message, android_LogPriority logPriority) {
    // limit the size of the message that we send to logcat using the predefined value in package.json
    auto messageToLog = message;
    if (messageToLog.length() > m_maxLogcatObjectSize) {
        messageToLog = messageToLog.erase(m_maxLogcatObjectSize, std::string::npos);
        messageToLog = messageToLog + "...";
    }

    // split strings into chunks of 4000 characters
    // __android_log_write can't send more than 4000 to the stdout at a time
    auto messageLength = messageToLog.length();
    int maxStringLength = 4000;

    if (messageLength < maxStringLength) {
        __android_log_write(logPriority, Console::LOG_TAG, messageToLog.c_str());
    } else {
        for (int i = 0; i < messageLength; i += maxStringLength) {
            auto messagePart = messageToLog.substr(i, maxStringLength);

            __android_log_write(logPriority, Console::LOG_TAG, messagePart.c_str());
        }
    }
}

#ifdef __V8__
int Console::sendToDevToolsFrontEnd(napi_env env, ConsoleAPIType method, napi_callback_info info) {
    if (!m_callback) {
        return 0;
    }
    NAPI_CALLBACK_BEGIN_VARGS()

    v8::Local<v8::Value> *v8_args = reinterpret_cast<v8::Local<v8::Value>*>(const_cast<napi_value *>(argv.data()));

    std::vector<v8::Local<v8::Value>> arg_vector;
    unsigned nargs = argc;
    arg_vector.reserve(nargs);
    for (unsigned ix = 0; ix < nargs; ix++)
        arg_vector.push_back(v8_args[ix]);

    m_callback(env, method, arg_vector);
    return 0;
}

void Console::sendToDevToolsFrontEnd(napi_env env, ConsoleAPIType method,  const std::string &message) {
    if (!m_callback) {
        return;
    }

    std::vector<v8::Local<v8::Value>> args{tns::ConvertToV8String(env->isolate, message)};
    m_callback(env, method, args);
}
#endif

ConsoleCallback Console::m_callback = nullptr;

#include "GlobalHelpers.h"
#include "ArgConverter.h"
#include "CallbackHandlers.h"
#include "Constants.h"
#include "JEnv.h"
#include "NativeScriptException.h"
#include <sstream>
#include "robin_hood.h"
#include "Util.h"
#include <regex>

using namespace std;

static robin_hood::unordered_map<napi_env, napi_ref> envToPersistentSmartJSONStringify = robin_hood::unordered_map<napi_env, napi_ref>();

napi_value GetSmartJSONStringifyFunction(napi_env env) {
    napi_status status;
    auto it = envToPersistentSmartJSONStringify.find(env);
    if (it != envToPersistentSmartJSONStringify.end()) {
        napi_value smartStringifyFunction;
        NAPI_GUARD(napi_get_reference_value(env, it->second, &smartStringifyFunction)) {
            return nullptr;
        }
        return smartStringifyFunction;
    }

    const char * smartStringifyFunctionScript = R"(
    (function () {
    function smartStringify(object, handleCirculars) {
        if (!handleCirculars) {
            return JSON.stringify(object, null, 2);
        }

        const seen = [];
        var replacer = function (key, value) {
            if (value != null && typeof value == "object") {
                if (seen.indexOf(value) >= 0) {
                    if (key) {
                        return "[Circular]";
                    }
                    return;
                }
                seen.push(value);
            }
            return value;
        };
        return JSON.stringify(object, replacer, 2);
    }
    return smartStringify;
})();
)";


    napi_value source;
    NAPI_GUARD(napi_create_string_utf8(env, smartStringifyFunctionScript, strlen(smartStringifyFunctionScript), &source)) {
        return nullptr;
    }

    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {}

    napi_value result;
    status = js_execute_script(env, source, "<json_helper>", &result);
    if (status != napi_ok) {
        return nullptr;
    }

    if (!napi_util::is_of_type(env, result, napi_function)) {
        return nullptr;
    }

    napi_ref smartStringifyPersistentFunction;
    NAPI_GUARD(napi_create_reference(env, result, 1, &smartStringifyPersistentFunction)) {
        return nullptr;
    }

    envToPersistentSmartJSONStringify.emplace(env, smartStringifyPersistentFunction);

    return result;
}



std::string tns::JsonStringifyObject(napi_env env, napi_value value, bool handleCircularReferences) {
    if (value == nullptr) {
        return "";
    }

    napi_value smartJSONStringifyFunction = GetSmartJSONStringifyFunction(env);
    std::string result;
    if (smartJSONStringifyFunction != nullptr) {
        napi_value resultValue;
        napi_value args[2];
        args[0] = value;
        args[1] = handleCircularReferences ? napi_util::get_true(env) : napi_util::get_false(env);
        napi_status status = napi_call_function(env, napi_util::global(env), smartJSONStringifyFunction, 2, args, &resultValue);
        if (status != napi_ok) {
            napi_value exception;
            NAPI_GUARD(napi_get_and_clear_last_exception(env, &exception)) {}
            if (!napi_util::is_null_or_undefined(env, exception)) {
                throw NativeScriptException(env, exception, "Error converting object to json");
            } else {
                throw NativeScriptException("Error converting object to json");
            }
        }
        result = ArgConverter::ConvertToString(env, resultValue);
    }

    return result;
}

napi_value tns::JsonParseString(napi_env env, const std::string& value) {
    napi_status status;
    napi_value global;
    napi_value json;
    napi_value parse;

    NAPI_GUARD(napi_get_global(env, &global)) {
        return nullptr;
    }
    NAPI_GUARD(napi_get_named_property(env, global, "JSON", &json)) {
        return nullptr;
    }
    NAPI_GUARD(napi_get_named_property(env, json, "parse", &parse)) {
        return nullptr;
    }

    napi_value args[1];
    args[0] = ArgConverter::convertToJsString(env, value);
    napi_value result;
    status = napi_call_function(env, json, parse, 1, args, &result);
    if (status != napi_ok) {
        napi_value exception;
        NAPI_GUARD(napi_get_and_clear_last_exception(env, &exception)) {}
        if (!napi_util::is_null_or_undefined(env, exception)) {
            throw NativeScriptException(env, exception, "Error converting json string to object");
        } else {
            throw NativeScriptException("Error converting json string to object");
        }
    }
    return result;
}

std::vector<tns::JsStacktraceFrame> tns::BuildStacktraceFrames(napi_env env, napi_value error, int size) {
    napi_status status;
    std::vector<tns::JsStacktraceFrame> frames;
    napi_value stack;
    if (error != nullptr) {
        NAPI_GUARD(napi_get_named_property(env, error, "stack", &stack)) {
            return frames;
        }
    } else {
#ifndef __HERMES__
        napi_value err;
        napi_value msg;
        NAPI_GUARD(napi_create_string_utf8(env, "Error", strlen("Error"), &msg)) {
            return frames;
        }
        #ifdef __PRIMJS__
        napi_value error_ctor;
        NAPI_GUARD(napi_get_named_property(env, napi_util::global(env), "Error", &error_ctor)) {
            return frames;
        }

        NAPI_GUARD(napi_new_instance(env, error_ctor, 1, &msg, &err)) {
            return frames;
        }
        #else
        NAPI_GUARD(napi_create_error(env, msg, msg, &err)) {
            return frames;
        }
        #endif
        NAPI_GUARD(napi_get_named_property(env, err, "stack", &stack)) {
            return frames;
        }
#else
        napi_value global;
        NAPI_GUARD(napi_get_global(env, &global)) {
            return frames;
        }
        napi_value getErrorStack;
        NAPI_GUARD(napi_get_named_property(env, global, "getErrorStack", &getErrorStack)) {
            return frames;
        }
        NAPI_GUARD(napi_call_function(env, global, getErrorStack, 0, nullptr, &stack)) {
            return frames;
        }
#endif
    }

    if (napi_util::is_null_or_undefined(env, stack)) return frames;

    string stackTrace = napi_util::get_string_value(env, stack);
    vector<string> stackLines;
    Util::SplitString(stackTrace, "\n", stackLines);

    // Source modules carry a full "file://…" URL in every frame, so this matches
    // those exactly as before (also covers JSC's "func@file://…:line:col" form).
    const regex schemeRegex(R"((file:.*):(\d+):(\d+))");
#ifdef NS_BYTECODE_ENABLED
    // Bytecode modules embed an app-relative source name (e.g. "shared/index.js")
    // at compile time — see tools/bytecode-compiler/compile-bytecode.js — because
    // the device-absolute path can't be baked in ahead of time. Those frames have
    // no scheme, so match a "(path:line:col)" (or leading-space) form and rebuild
    // the full runtime URL from the app root. A leading "(" or space anchors the
    // path so a function name is never glued on; "@" stays a valid path char so
    // scoped modules (tns_modules/@nativescript/…) survive. Bytecode engines
    // (Hermes/QuickJS/PrimJS) all emit the parenthesised V8-style frame, so the
    // "@"-delimited (JSC) form never reaches here — and JSC has no bytecode.
    const regex bareRegex(R"RE([(\s]([^\s():]+):(\d+):(\d+))RE");
#endif

    int current = 0;
    int count = 0;
    for (auto &frame : stackLines) {
        count++;
#ifdef __HERMES__
            if (error == nullptr && count < 3) continue;
#endif

        smatch match;
        std::string filePath;
        if (regex_search(frame, match, schemeRegex)) {
            filePath = match[1].str();
        }
#ifdef NS_BYTECODE_ENABLED
        else if (regex_search(frame, match, bareRegex)) {
            filePath = "file://" + Constants::APP_ROOT_FOLDER_PATH + match[1].str();
        }
#endif
        else {
            continue;
        }
        current++;
        frames.emplace_back(stoi(match[2].str()),
                            stoi(match[3].str()),
                            filePath,
                            frame);
        if (current == size) break;
    }
    return frames;
}

void tns::GlobalHelpers::onDisposeEnv(napi_env env) {
    napi_status status;
    auto found = envToPersistentSmartJSONStringify.find(env);
    if (found != envToPersistentSmartJSONStringify.end()) {
        NAPI_GUARD(napi_delete_reference(env, found->second)) {}
    }
    envToPersistentSmartJSONStringify.erase(env);
}
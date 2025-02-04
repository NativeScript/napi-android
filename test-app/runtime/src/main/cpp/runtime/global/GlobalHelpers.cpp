#include "GlobalHelpers.h"
#include "ArgConverter.h"
#include "CallbackHandlers.h"
#include "JEnv.h"
#include "NativeScriptException.h"
#include <sstream>
#include "robin_hood.h"
#include "Util.h"
#include <regex>

using namespace std;

static robin_hood::unordered_map<napi_env, napi_ref> envToPersistentSmartJSONStringify = robin_hood::unordered_map<napi_env, napi_ref>();

napi_value GetSmartJSONStringifyFunction(napi_env env) {
    auto it = envToPersistentSmartJSONStringify.find(env);
    if (it != envToPersistentSmartJSONStringify.end()) {
        napi_value smartStringifyFunction;
        napi_get_reference_value(env, it->second, &smartStringifyFunction);
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
    napi_create_string_utf8(env, smartStringifyFunctionScript, strlen(smartStringifyFunctionScript), &source);

    napi_value global;
    napi_get_global(env, &global);

    napi_value result;
    napi_status status = js_execute_script(env, source, "<json_helper>", &result);
    if (status != napi_ok) {
        return nullptr;
    }

    if (!napi_util::is_of_type(env, result, napi_function)) {
        return nullptr;
    }

    napi_ref smartStringifyPersistentFunction;
    napi_create_reference(env, result, 1, &smartStringifyPersistentFunction);

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
            napi_get_and_clear_last_exception(env, &exception);
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
    napi_value global;
    napi_value json;
    napi_value parse;

    napi_get_global(env, &global);
    napi_get_named_property(env, global, "JSON", &json);
    napi_get_named_property(env, json, "parse", &parse);

    napi_value args[1];
    args[0] = ArgConverter::convertToJsString(env, value);
    napi_value result;
    napi_status status = napi_call_function(env, json, parse, 1, args, &result);
    if (status != napi_ok) {
        napi_value exception;
        napi_get_and_clear_last_exception(env, &exception);
        if (!napi_util::is_null_or_undefined(env, exception)) {
            throw NativeScriptException(env, exception, "Error converting json string to object");
        } else {
            throw NativeScriptException("Error converting json string to object");
        }
    }
    return result;
}

std::vector<tns::JsStacktraceFrame> tns::BuildStacktraceFrames(napi_env env, napi_value error, int size) {
    std::vector<tns::JsStacktraceFrame> frames;
    napi_value stack;
    if (error != nullptr) {
        napi_get_named_property(env, error, "stack", &stack);
    } else {
#ifndef __HERMES__
        napi_value err;
        napi_value msg;
        napi_create_string_utf8(env, "Error", strlen("Error"), &msg);
//        napi_value ex;
//        napi_get_and_clear_last_exception(env, &ex);
        napi_create_error(env, msg, msg, &err);
        napi_get_named_property(env, err, "stack", &stack);
#else
        napi_value global;
        napi_get_global(env, &global);
        napi_value getErrorStack;
        napi_get_named_property(env, global, "getErrorStack", &getErrorStack);
        napi_call_function(env, global, getErrorStack, 0, nullptr, &stack);
#endif
    }

    if (napi_util::is_null_or_undefined(env, stack)) return frames;

    string stackTrace = napi_util::get_string_value(env, stack);
    vector<string> stackLines;
    Util::SplitString(stackTrace, "\n", stackLines);

    int current = 0;
    int count = 0;
    for (auto &frame : stackLines) {
        count++;
#ifdef __HERMES__
            if (error == nullptr && count < 3) continue;
#endif

#ifdef __JSC__
        regex frameRegex(R"((.*):(\d+):(\d+))");
#else
        regex frameRegex(R"(\((.*):(\d+):(\d+)\))");
#endif
        smatch match;
        if (regex_search(frame, match, frameRegex)) {
            current++;
            frames.emplace_back(stoi(match[2].str()),
                                stoi(match[3].str()),
                                match[1].str(),
                                frame);
            if (current == size) break;
        }
    }
    return frames;
}

void tns::GlobalHelpers::onDisposeEnv(napi_env env) {
    auto found = envToPersistentSmartJSONStringify.find(env);
    if (found != envToPersistentSmartJSONStringify.end()) {
        napi_delete_reference(env, found->second);
    }
    envToPersistentSmartJSONStringify.erase(env);
}
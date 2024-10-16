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

    napi_value script;
    napi_status status = napi_run_script(env, source, "<json_helper>", &script);
    if (status != napi_ok) {
        return nullptr;
    }

    napi_value result;
    status = napi_call_function(env, global, script, 0, nullptr, &result);
    if (status != napi_ok || result == nullptr) {
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
        napi_status status;
        napi_value args[2];
        args[0] = value;
        args[1] = handleCircularReferences ? napi_util::get_true(env) : napi_util::get_false(env);
        status = napi_call_function(env, nullptr, smartJSONStringifyFunction, 1, args, &resultValue);
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
    return result;
}

std::vector<tns::JsStacktraceFrame> tns::BuildStacktraceFrames(napi_env env, napi_value error, int size) {
    std::vector<tns::JsStacktraceFrame> frames;
    napi_value err, stack;
    if (error != nullptr) {
        err = error;
    } else {
        napi_value msg;
        napi_create_string_utf8(env, "", 0, &msg);
        napi_create_error(env, nullptr, msg, &err);
    }

    napi_get_named_property(env, error, "stack", &stack);

    if (napi_util::is_null_or_undefined(env, stack)) return frames;

    string stackTrace = ArgConverter::ConvertToString(env, stack);
    vector<string> stackLines;
    Util::SplitString(stackTrace, "\n", stackLines);

    int current = 0;
    for (auto &frame : stackLines) {
        regex frameRegex(R"(\((.*):(\d+):(\d+)\))");
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
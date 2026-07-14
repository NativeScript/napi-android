#include "NativeScriptException.h"
#include "JSONObjectHelper.h"
#include "ArgConverter.h"
#include <sstream>
#include <string>
#include <cassert>

using namespace tns;

void JSONObjectHelper::RegisterFromFunction(napi_env env, napi_value value) {
    napi_status status;
    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, value, &type)) {
        return;
    }
    if (type != napi_function && type != napi_object) {
        return;
    }

    bool hasProperty;
    NAPI_GUARD(napi_has_named_property(env, value, "from", &hasProperty)) {
        return;
    }
    if (hasProperty) {
        return;
    }

    napi_value from = CreateFromFunction(env);
    NAPI_GUARD(napi_set_named_property(env, value, "from", from)) {}
}


napi_value JSONObjectHelper::CreateFromFunction(napi_env env) {
    static const char* source = R"((() => function from(data) {
            if (!data) throw new Error("Expected one parameter");
            let store;
            switch (typeof data) {
                case "string":
                case "boolean":
                case "number": {
                    return data;
                }
                case "object": {
                    if (!data) {
                        return null;
                    }

                    if (data instanceof Date) {
                        return data.toJSON();
                    }

                    if (Array.isArray(data)) {
                        store = new org.json.JSONArray();
                        data.forEach((item) => store.put(from(item)));
                        return store;
                    }

                    store = new org.json.JSONObject();
                    Object.keys(data).forEach((key) => store.put(key, from(data[key])));
                    return store;
                }
                default:
                    return null;
            }
        })();)";

    napi_status status;
    napi_value script;
    NAPI_GUARD(napi_create_string_utf8(env, source, NAPI_AUTO_LENGTH, &script)) {
        return nullptr;
    }

    napi_value result;
    NAPI_GUARD(js_execute_script(env, script, "<from_function>", &result)) {
        return nullptr;
    }

    return result;
}
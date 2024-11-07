#include "NativeScriptException.h"
#include "JSONObjectHelper.h"
#include "ArgConverter.h"
#include <sstream>
#include <string>
#include <cassert>

using namespace tns;

void JSONObjectHelper::RegisterFromFunction(napi_env env, napi_value value) {
    if (!napi_util::is_of_type(env, value, napi_function)) {
        return;
    }

    bool hasProperty;
    napi_has_named_property(env, value, "from", &hasProperty);
    if (hasProperty) {
        return;
    }

    napi_value from = CreateFromFunction(env);
    napi_set_named_property(env, value, "from", from);
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

    napi_value script;
    napi_create_string_utf8(env, source, NAPI_AUTO_LENGTH, &script);

    napi_value result;
    napi_run_script(env, script, "<from_function>", &result);

    return result;
}
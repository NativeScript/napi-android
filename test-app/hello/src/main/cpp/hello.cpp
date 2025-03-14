#include <assert.h>
#include "node-api.h"

static napi_value Method(napi_env env, napi_callback_info info) {
    napi_status status;
    napi_value world;
    status = napi_create_string_utf8(env, "world", 5, &world);
    assert(status == napi_ok);
    return world;
}

#define DECLARE_NAPI_METHOD(name, func)                                        \
  { name, 0, func, 0, 0, 0, napi_default, 0 }


NAPI_MODULE_INIT() {
    napi_status status;
    napi_property_descriptor desc = DECLARE_NAPI_METHOD("hello", Method);

    status = napi_define_properties(env, exports, 1, &desc);
    assert(status == napi_ok);
    return exports;
}
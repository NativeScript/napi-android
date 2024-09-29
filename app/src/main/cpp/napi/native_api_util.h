#include "js_native_api.h"
#include <dlfcn.h>
#include <sstream>
#include "NativeScriptAssert.h"

#define NAPI_EXPORT __attribute__((visibility("default")))

#define NAPI_PREAMBLE napi_status status;

#define NAPI_CALLBACK_BEGIN(n_args)                                            \
  NAPI_PREAMBLE                                                                \
  napi_value argv[n_args];                                                     \
  size_t argc = n_args;                                                        \
  napi_value jsThis;                                                           \
  void *data;                                                                  \
  NAPI_GUARD(napi_get_cb_info(env, info, &argc, argv, &jsThis, &data)) {     \
    NAPI_THROW_LAST_ERROR                                                      \
    return NULL;                                                               \
  }

#define NAPI_ERROR_INFO                                                        \
  const napi_extended_error_info *error_info =                                 \
      (napi_extended_error_info *)malloc(sizeof(napi_extended_error_info));    \
  napi_get_last_error_info(env, &error_info);

#define NAPI_THROW_LAST_ERROR                                                  \
  NAPI_ERROR_INFO                                                              \
  napi_throw_error(env, NULL, error_info->error_message);


#ifndef DEBUG


#define NAPI_GUARD(expr)                                                       \
  status = expr;                                                               \
  if (status != napi_ok) {                                                     \
    NAPI_ERROR_INFO                                                            \
    std::stringstream msg; \
    msg << "Node-API returned error: " << status << "\n    " << #expr    \
              << "\n    ^\n    "                                               \
              << "at " << __FILE__ << ":" << __LINE__ << "";            \
              DEBUG_WRITE("%s", msg.str().c_str());\
  }                                                                            \
  if (status != napi_ok)

#else

#define NAPI_GUARD(expr)                                                       \
  status = expr;                                                               \
  if (status != napi_ok)

#endif

#define NAPI_MODULE_REGISTER                                                   \
  napi_value napi_register_module_v1(napi_env env, napi_value exports)

#define NAPI_FUNCTION(name)                                                    \
  napi_value JS_##name(napi_env env, napi_callback_info cbinfo)

#define NAPI_FUNCTION_DESC(name)                                               \
  { #name, NULL, JS_##name, NULL, NULL, NULL, napi_enumerable, NULL }




namespace napi_util {

    const char * PROTOTYPE = "prototype";
    const char * OBJECT = "Object";
    const char * SET_PROTOTYPE_OF = "setPrototypeOf";
    const char * CONSTRUCTOR = "CONSTRUCTOR";

    inline napi_ref make_ref(napi_env env, napi_value value,
                             uint32_t initialCount = 1) {
        napi_ref ref;
        napi_create_reference(env, value, initialCount, &ref);
        return ref;
    }

    inline napi_value get_ref_value(napi_env env, napi_ref ref) {
        napi_value value;
        napi_get_reference_value(env, ref, &value);
        return value;
    }

    inline napi_value get_proto(napi_env env, napi_value constructor) {
        napi_value proto;
        napi_get_prototype(env, constructor, &proto);
        return proto;
    }

    inline char * get_string_value(napi_env env, napi_value str, size_t size = 256) {
        size_t str_size = size;
        if (str_size == 0) {
            napi_get_value_string_utf8(env, str, nullptr, 0, &str_size);
        }
        char *buffer = new char[str_size];
        napi_get_value_string_utf8(env, str, buffer, str_size, nullptr);
        return buffer;
    }

    inline napi_status define_property(napi_env env, napi_value object, const char* propertyName, napi_value value = nullptr, napi_callback getter = nullptr, napi_callback setter = nullptr, void* data = nullptr) {
        napi_property_descriptor desc = {
                propertyName,  // utf8name
                nullptr,       // name
                nullptr,       // method
                getter,        // getter
                setter,        // setter
                value,         // value
                napi_default,  // attributes
                data           // data
        };

        return napi_define_properties(env, object, 1, &desc);
    }

    inline void napi_inherits(napi_env env, napi_value ctor,
                              napi_value super_ctor) {
        napi_value global, global_object, set_proto, ctor_proto_prop,
                super_ctor_proto_prop;
        napi_value argv[2];

        napi_get_global(env, &global);
        napi_get_named_property(env, global, OBJECT, &global_object);
        napi_get_named_property(env, global_object, SET_PROTOTYPE_OF, &set_proto);
        napi_get_named_property(env, ctor, PROTOTYPE, &ctor_proto_prop);
        napi_get_named_property(env, super_ctor,PROTOTYPE, &super_ctor_proto_prop);

        argv[0] = ctor_proto_prop;
        argv[1] = super_ctor_proto_prop;
        napi_call_function(env, global, set_proto, 2, argv, nullptr);

        argv[0] = ctor;
        argv[1] = super_ctor;
        napi_call_function(env, global, set_proto, 2, argv, nullptr);
    }

    inline bool is_of_type(napi_env env, napi_value value, napi_valuetype expected_type) {
        napi_valuetype type;
        napi_typeof(env, value, &type);
        return type == expected_type;
    }

    inline bool is_undefined(napi_env env, napi_value value) {
        napi_valuetype type;
        napi_typeof(env, value, &type);
        return type == napi_undefined;
    }

    inline bool is_null(napi_env env, napi_value value) {
        napi_valuetype type;
        napi_typeof(env, value, &type);
        return type == napi_null;
    }

    inline napi_value get_true(napi_env env) {
        napi_value trueValue;
        napi_get_boolean(env, true, &trueValue);
        return trueValue;
    }

    inline napi_value get_false(napi_env env) {
        napi_value falseValue;
        napi_get_boolean(env, true, &falseValue);
        return falseValue;
    } 

    inline bool get_bool(napi_env env, napi_value value) {
        bool result;
        napi_get_value_bool(env, value, &result);
        return result;
    }

    // Same as Object.create()`
    inline napi_value object_create_from(napi_env env, napi_value object) {
      napi_value new_object;
      napi_create_object(env, &new_object);
      napi_set_named_property(env, new_object, "prototype", object);
      return new_object;
    }

    inline bool strict_equal(napi_env env, napi_value v1, napi_value v2) {
      bool equal;
      napi_strict_equals(env, v1, v2, &equal);
      return equal;
    }
    
}




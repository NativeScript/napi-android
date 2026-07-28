#ifndef NATIVE_API_UTIL_H_
#define NATIVE_API_UTIL_H_

#include "js_native_api.h"
#include <dlfcn.h>
#include <sstream>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifndef NAPI_PREAMBLE
#define NAPI_PREAMBLE napi_status status;
#endif

#define NAPI_CALLBACK_BEGIN(n_args)                                    \
  napi_status status;                                                  \
  napi_value argv[n_args];                                             \
  size_t argc = n_args;                                                \
  napi_value jsThis;                                                   \
  void *data;                                                          \
  NAPI_GUARD(napi_get_cb_info(env, info, &argc, argv, &jsThis, &data)) \
  {                                                                    \
    NAPI_THROW_LAST_ERROR                                              \
    return NULL;                                                       \
  }

#define NAPI_CALLBACK_BEGIN_VARGS()                                               \
  napi_status status;                                                             \
  size_t argc;                                                                    \
  void *data;                                                                     \
  napi_value jsThis;                                                              \
  NAPI_GUARD(napi_get_cb_info(env, info, &argc, nullptr, &jsThis, &data))         \
  {                                                                               \
    NAPI_THROW_LAST_ERROR                                                         \
    return NULL;                                                                  \
  }                                                                               \
  std::vector<napi_value> argv(argc);                                             \
  if (argc > 0)                                                                   \
  {                                                                               \
    NAPI_GUARD(napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr)) \
    {                                                                             \
      NAPI_THROW_LAST_ERROR                                                       \
      return NULL;                                                                \
    }                                                                             \
  }

// Faster varargs prologue for hot callbacks. Reads arguments into a fixed stack
// buffer with a SINGLE napi_get_cb_info call (no heap allocation, no redundant
// argc-probe call), falling back to a heap vector only when the real arity
// exceeds the inline capacity `stackn`. Exposes `napi_value *argv` + `size_t
// argc`, so call sites use `argv`/`argv[i]` (a pointer) instead of a vector.
#define NAPI_CALLBACK_BEGIN_VARGS_FAST(stackn)                                    \
  napi_status status;                                                             \
  size_t argc = (stackn);                                                         \
  void *data;                                                                     \
  napi_value jsThis;                                                              \
  napi_value __argv_stack[(stackn)];                                              \
  NAPI_GUARD(napi_get_cb_info(env, info, &argc, __argv_stack, &jsThis, &data))    \
  {                                                                               \
    NAPI_THROW_LAST_ERROR                                                         \
    return NULL;                                                                  \
  }                                                                               \
  std::vector<napi_value> __argv_heap;                                            \
  napi_value *argv = __argv_stack;                                                \
  if (argc > (stackn))                                                            \
  {                                                                               \
    __argv_heap.resize(argc);                                                     \
    NAPI_GUARD(                                                                   \
        napi_get_cb_info(env, info, &argc, __argv_heap.data(), nullptr, nullptr)) \
    {                                                                             \
      NAPI_THROW_LAST_ERROR                                                       \
      return NULL;                                                                \
    }                                                                             \
    argv = __argv_heap.data();                                                    \
  }

#define NAPI_ERROR_INFO                                                     \
  const napi_extended_error_info *error_info =                              \
      (napi_extended_error_info *)malloc(sizeof(napi_extended_error_info)); \
  napi_get_last_error_info(env, &error_info);

#define NAPI_THROW_LAST_ERROR \
  NAPI_ERROR_INFO             \
  napi_throw_error(env, NULL, error_info->error_message);

#ifdef __ANDROID__
#define NAPI_LOG_ERROR(status_val, expr_str)                              \
  __android_log_print(ANDROID_LOG_ERROR, "TNS.Native",                    \
                      "Node-API returned error: %d\n    %s\n    ^\n    at %s:%d", \
                      (int)(status_val), (expr_str), __FILE__, __LINE__)
#else
#define NAPI_LOG_ERROR(status_val, expr_str) ((void)0)
#endif

// NAPI_GUARD(expr) { ...on-error block... }
// Assigns the result of `expr` to the in-scope `status`, logs a diagnostic
// (status, expression, file:line) on failure so an invalid runtime state is
// traceable, and runs the trailing block when the call did not return napi_ok.
// napi_pending_exception is JS-level control flow (a callback threw), not an
// invalid runtime state, so it is intentionally not logged — the caller's
// exception handling deals with it.
#define NAPI_GUARD(expr)                                              \
  status = expr;                                                      \
  if (status != napi_ok && status != napi_pending_exception)         \
  {                                                                   \
    NAPI_LOG_ERROR(status, #expr);                                    \
  }                                                                   \
  if (status != napi_ok)

#define NAPI_FUNCTION(name) \
  napi_value JS_##name(napi_env env, napi_callback_info cbinfo)

#define NAPI_FUNCTION_DESC(name) \
  {#name, NULL, JS_##name, NULL, NULL, NULL, napi_enumerable, NULL}

#define PROTOTYPE "prototype"
#define OBJECT "Object"
#define SET_PROTOTYPE_OF "setPrototypeOf"
#define CONSTRUCTOR "constructor"

#define UNDEFINED \
napi_util::undefined(env);

// The helpers below are convenience wrappers around the raw Node-API. Every one
// of them guards the status of the underlying napi_* call via NAPI_GUARD and
// returns a well-defined fallback on failure instead of reading a possibly
// uninitialized out-parameter:
//   - value accessors return nullptr,
//   - boolean predicates return false,
//   - numeric accessors return 0.
// This keeps a single failed napi_* call (e.g. an unclassifiable value, or a
// call made while a JS exception is pending) from producing undefined behavior
// in the caller.
namespace napi_util {

    inline napi_value undefined(napi_env env) {
        napi_status status;
        napi_value undefined = nullptr;
        NAPI_GUARD(napi_get_undefined(env, &undefined)) {
            return nullptr;
        }
        return undefined;
    }

    inline napi_value null(napi_env env) {
        napi_status status;
        napi_value null = nullptr;
        NAPI_GUARD(napi_get_null(env, &null)) {
            return nullptr;
        }
        return null;
    }

    inline napi_ref make_ref(napi_env env, napi_value value,
                             uint32_t initialCount = 1) {
        napi_status status;
        napi_ref ref = nullptr;
        NAPI_GUARD(napi_create_reference(env, value, initialCount, &ref)) {
            return nullptr;
        }
        return ref;
    }

    inline napi_value get_ref_value(napi_env env, napi_ref ref) {
        napi_status status;
        napi_value value = nullptr;
        NAPI_GUARD(napi_get_reference_value(env, ref, &value)) {
            return nullptr;
        }
        return value;
    }

    inline napi_value get__proto__(napi_env env, napi_value object) {
        napi_status status;
        napi_value proto = nullptr;
        NAPI_GUARD(napi_get_named_property(env, object, "__proto__", &proto)) {
            return nullptr;
        }
        return proto;
    }

    inline void set__proto__(napi_env env, napi_value object, napi_value __proto__) {
        napi_status status;
        NAPI_GUARD(napi_set_named_property(env, object, "__proto__", __proto__)) {}
    }

    inline napi_value getPrototypeOf(napi_env env, napi_value object) {
        napi_status status;
        napi_value proto = nullptr;
        NAPI_GUARD(napi_get_prototype(env, object, &proto)) {
            return nullptr;
        }
        return proto;
    }

    inline napi_value get_prototype(napi_env env, napi_value object) {
        napi_status status;
        napi_value prototype = nullptr;
        NAPI_GUARD(napi_get_named_property(env, object, "prototype", &prototype)) {
            return nullptr;
        }
        return prototype;
    }

    inline void set_prototype(napi_env env, napi_value object, napi_value prototype) {
        napi_status status;
        NAPI_GUARD(napi_set_named_property(env, object, "prototype", prototype)) {}
    }

    inline char *get_string_value(napi_env env, napi_value str, size_t size = 0) {
        napi_status status;
        size_t str_size = size;
        if (str_size == 0) {
            // Probe the required length first; bail out (rather than allocating a
            // garbage-sized buffer) if the value is not a readable string.
            NAPI_GUARD(napi_get_value_string_utf8(env, str, nullptr, 0, &str_size)) {
                return nullptr;
            }
        }
        char *buffer = new char[str_size + 1];
        NAPI_GUARD(napi_get_value_string_utf8(env, str, buffer, str_size + 1, nullptr)) {
            delete[] buffer;
            return nullptr;
        }
        return buffer;
    }

    inline napi_status define_property(napi_env env, napi_value object, const char *propertyName,
                                       napi_value value = nullptr, napi_callback getter = nullptr,
                                       napi_callback setter = nullptr, void *data = nullptr, napi_property_attributes attributes = napi_default_jsproperty) {
        napi_property_descriptor desc = {
                propertyName, // utf8name
                nullptr,      // name
                nullptr,      // method
                getter,       // getter
                setter,       // setter
                value,        // value
                attributes, // attributes
                data          // data
        };

        return napi_define_properties(env, object, 1, &desc);
    }

    inline napi_status define_property_value(napi_env env, napi_value object, const char *propertyName,
                                       napi_value value = nullptr, napi_property_attributes attributes = napi_default_jsproperty, void *data = nullptr) {
        return napi_util::define_property(env, object, propertyName, value, nullptr, nullptr, data, attributes);
    }

    inline napi_status define_property_get_set(napi_env env, napi_value object, const char *propertyName,
                                       napi_callback getter, napi_callback setter, napi_property_attributes attributes = napi_default_jsproperty, void *data = nullptr) {
        return napi_util::define_property(env, object, propertyName, nullptr, getter, setter, data, attributes);
    }

    inline void setPrototypeOf(napi_env env, napi_value object, napi_value prototype) {
        napi_status status;
        napi_value global = nullptr, global_object = nullptr, set_proto = nullptr;

        // Get the global object
        NAPI_GUARD(napi_get_global(env, &global)) { return; }

        // Get the Object global object
        NAPI_GUARD(napi_get_named_property(env, global, OBJECT, &global_object)) { return; }

        // Get the setPrototypeOf function from the Object global object
        NAPI_GUARD(napi_get_named_property(env, global_object, SET_PROTOTYPE_OF, &set_proto)) { return; }

        // Prepare the arguments for the setPrototypeOf call
        napi_value argv[] {
            object,
            prototype
        };
        // Call setPrototypeOf(object, prototype)
        NAPI_GUARD(napi_call_function(env, global, set_proto, 2, argv, nullptr)) {}
    }



    inline bool is_object_explicit(napi_env env, napi_value value) {
        napi_status status;
        napi_valuetype type = napi_undefined;
        NAPI_GUARD(napi_typeof(env, value, &type)) {
            return false;
        }
        return type == napi_object;
    }

    inline bool is_object(napi_env env, napi_value value) {
        napi_status status;
        napi_valuetype type = napi_undefined;
        NAPI_GUARD(napi_typeof(env, value, &type)) {
            return false;
        }
        return type == napi_object || type == napi_function;
    }

    inline bool is_of_type(napi_env env, napi_value value, napi_valuetype expected_type) {
        // napi_typeof can fail (and leave `type` unwritten) for values it cannot
        // classify, e.g. an engine exception sentinel; guarding the status keeps
        // this from returning a spurious match on an uninitialized value.
        napi_status status;
        napi_valuetype type = napi_undefined;
        NAPI_GUARD(napi_typeof(env, value, &type)) {
            return false;
        }
        return type == expected_type;
    }

    inline bool is_number_object(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        napi_value numberCtor = nullptr;
        napi_value global = nullptr;
        NAPI_GUARD(napi_get_global(env, &global)) { return false; }
        NAPI_GUARD(napi_get_named_property(env, global, "Number", &numberCtor)) { return false; }
        NAPI_GUARD(napi_instanceof(env, value, numberCtor, &result)) { return false; }
        return result;
    }

    inline napi_value valueOf(napi_env env, napi_value value) {
        napi_status status;
        napi_value valueOf = nullptr, result = nullptr;
        NAPI_GUARD(napi_get_named_property(env, value, "valueOf", &valueOf)) { return nullptr; }
        NAPI_GUARD(napi_call_function(env, value, valueOf, 0, nullptr, &result)) { return nullptr; }
        return result;
    }

    inline bool is_string_object(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        napi_value stringCtor = nullptr;
        napi_value global = nullptr;
        NAPI_GUARD(napi_get_global(env, &global)) { return false; }
        NAPI_GUARD(napi_get_named_property(env, global, "String", &stringCtor)) { return false; }
        NAPI_GUARD(napi_instanceof(env, value, stringCtor, &result)) { return false; }
        return result;
    }

    inline bool is_boolean_object(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        napi_value booleanCtor = nullptr;
        napi_value global = nullptr;
        NAPI_GUARD(napi_get_global(env, &global)) { return false; }
        NAPI_GUARD(napi_get_named_property(env, global, "Boolean", &booleanCtor)) { return false; }
        NAPI_GUARD(napi_instanceof(env, value, booleanCtor, &result)) { return false; }
        return result;
    }


    inline bool is_array(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        NAPI_GUARD(napi_is_array(env, value, &result)) {
            return false;
        }
        return result;
    }

    inline bool is_arraybuffer(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        NAPI_GUARD(napi_is_arraybuffer(env, value, &result)) {
            return false;
        }
        return result;
    }

    inline bool is_dataview(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        NAPI_GUARD(napi_is_dataview(env, value, &result)) {
            return false;
        }
        return result;
    }

    inline bool is_typedarray(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        NAPI_GUARD(napi_is_typedarray(env, value, &result)) {
            return false;
        }
        return result;
    }

    inline bool is_date(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;
        NAPI_GUARD(napi_is_date(env, value, &result)) {
            return false;
        }
        return result;
    }


    inline bool is_undefined(napi_env env, napi_value value) {
        if (value == nullptr) return true;
        napi_status status;
        napi_valuetype type = napi_undefined;
        // A value that cannot be classified is unusable; treat it as undefined so
        // callers guarding with is_null_or_undefined skip it (matches the
        // value == nullptr case above).
        NAPI_GUARD(napi_typeof(env, value, &type)) {
            return true;
        }
        return type == napi_undefined;
    }

    inline bool is_null(napi_env env, napi_value value) {
        napi_status status;
        napi_valuetype type = napi_undefined;
        NAPI_GUARD(napi_typeof(env, value, &type)) {
            return false;
        }
        return type == napi_null;
    }

    inline napi_value get_true(napi_env env) {
        napi_status status;
        napi_value trueValue = nullptr;
        NAPI_GUARD(napi_get_boolean(env, true, &trueValue)) {
            return nullptr;
        }
        return trueValue;
    }

    inline napi_value get_false(napi_env env) {
        napi_status status;
        napi_value falseValue = nullptr;
        NAPI_GUARD(napi_get_boolean(env, false, &falseValue)) {
            return nullptr;
        }
        return falseValue;
    }

    inline bool get_bool(napi_env env, napi_value value) {
        napi_status status;
        bool result = false;

        if (!napi_util::is_of_type(env, value, napi_boolean)) return false;

        NAPI_GUARD(napi_get_value_bool(env, value, &result)) {
            return false;
        }
        return result;
    }

    inline bool is_float(napi_env env, napi_value value) {
        napi_status status;
        napi_value global = nullptr, number = nullptr, is_int = nullptr, result = nullptr;
        NAPI_GUARD(napi_get_global(env, &global)) { return false; }
        NAPI_GUARD(napi_get_named_property(env, global, "Number", &number)) { return false; }
        NAPI_GUARD(napi_get_named_property(env, number, "isInteger", &is_int)) { return false; }
        NAPI_GUARD(napi_call_function(env, number, is_int, 1, &value, &result)) { return false; }

        return !napi_util::get_bool(env, result);
    }

    // Same as Object.create()`
    inline napi_value object_create_from(napi_env env, napi_value object) {
        napi_status status;
        napi_value new_object = nullptr;
        NAPI_GUARD(napi_create_object(env, &new_object)) { return nullptr; }
        NAPI_GUARD(napi_set_named_property(env, new_object, "prototype", object)) {}
        return new_object;
    }

    inline bool strict_equal(napi_env env, napi_value v1, napi_value v2) {
        napi_status status;
        bool equal = false;
        NAPI_GUARD(napi_strict_equals(env, v1, v2, &equal)) {
            return false;
        }
        return equal;
    }

    inline double get_number(napi_env env, napi_value value) {
        napi_status status;
        double result = 0;
        NAPI_GUARD(napi_get_value_double(env, value, &result)) {
            return 0;
        }
        return result;
    }

    inline int32_t get_int32(napi_env env, napi_value value) {
        napi_status status;
        int32_t result = 0;
        NAPI_GUARD(napi_get_value_int32(env, value, &result)) {
            return 0;
        }
        return result;
    }

    template<typename Func, typename... Args>
    inline void run_in_handle_scope(napi_env env, Func func, Args &&...args) {
        napi_status status;
        napi_handle_scope scope = nullptr;
        NAPI_GUARD(napi_open_handle_scope(env, &scope)) {
            return;
        }

        // Call the provided function
        func(std::forward<Args>(args)...);

        NAPI_GUARD(napi_close_handle_scope(env, scope)) {}
    }

    template<typename Func, typename... Args>
    inline napi_value run_in_escapable_handle_scope(napi_env env, Func func, Args &&...args) {
        napi_status status;
        napi_escapable_handle_scope scope = nullptr;
        napi_value result = nullptr, escaped = nullptr;

        NAPI_GUARD(napi_open_escapable_handle_scope(env, &scope)) {
            return nullptr;
        }

        // Call the provided function with forwarded arguments and get the result
        result = func(std::forward<Args>(args)...);

        if (result != nullptr) {
            // Escape the result
            NAPI_GUARD(napi_escape_handle(env, scope, result, &escaped)) {}
        }

        NAPI_GUARD(napi_close_escapable_handle_scope(env, scope)) {}

        return escaped;
    }

    inline napi_value
    napi_set_function(napi_env env, napi_value object, const char *name, napi_callback callback,
                      void *data = nullptr) {
        napi_status status;
        napi_value fn = nullptr;
        NAPI_GUARD(napi_create_function(env, name, strlen(name), callback, data, &fn)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_named_property(env, object, name, fn)) {}
        return fn;
    }

//    inline napi_value symbolFor(napi_env env, const char *string) {
//        napi_value symbol;
//        node_api_symbol_for(env, string, strlen(string), &symbol);
//        return symbol;
//    }

    inline bool is_null_or_undefined(napi_env env, napi_value value) {
        return value == nullptr || is_undefined(env, value) || is_null(env, value);
    }

    inline napi_value global(napi_env env) {
        napi_status status;
        napi_value global = nullptr;
        NAPI_GUARD(napi_get_global(env, &global)) {
            return nullptr;
        }
        return global;
    }


    inline void log_value(napi_env env, napi_value value) {
        napi_status status;
        napi_value global = nullptr;
        napi_value console = nullptr;
        napi_value log = nullptr;
        NAPI_GUARD(napi_get_global(env, &global)) { return; }
        NAPI_GUARD(napi_get_named_property(env, global, "console", &console)) { return; }
        NAPI_GUARD(napi_get_named_property(env, console, "log", &log)) { return; }
        napi_value argv[] = {
                value
        };

        NAPI_GUARD(napi_call_function(env, console, log, 1, argv, nullptr)) {}
    }

    inline void napi_inherits(napi_env env, napi_value ctor,
                              napi_value super_ctor) {
        napi_status status;
        napi_value global = nullptr, global_object = nullptr, set_proto = nullptr,
                ctor_proto_prop = nullptr, super_ctor_proto_prop = nullptr;
        napi_value argv[2];

        NAPI_GUARD(napi_get_global(env, &global)) { return; }
        NAPI_GUARD(napi_get_named_property(env, global, OBJECT, &global_object)) { return; }
        NAPI_GUARD(napi_get_named_property(env, global_object, SET_PROTOTYPE_OF, &set_proto)) { return; }
        NAPI_GUARD(napi_get_named_property(env, ctor, PROTOTYPE, &ctor_proto_prop)) { return; }
        NAPI_GUARD(napi_get_named_property(env, super_ctor, PROTOTYPE, &super_ctor_proto_prop)) { return; }

        argv[0] = ctor_proto_prop;
        argv[1] = super_ctor_proto_prop;
        NAPI_GUARD(napi_call_function(env, global, set_proto, 2, argv, nullptr)) { return; }

        argv[0] = ctor;
        argv[1] = super_ctor;
        NAPI_GUARD(napi_call_function(env, global, set_proto, 2, argv, nullptr)) {}
    }

}

#endif /* NATIVE_API_UTIL_H_ */

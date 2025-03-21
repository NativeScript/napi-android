#ifndef SRC_JS_NATIVE_API_H_
#define SRC_JS_NATIVE_API_H_

#include "js_native_api_types.h"
#include <uchar.h>

EXTERN_C_START

#include <stdbool.h> // NOLINT(modernize-deprecated-headers)
#include <stddef.h>  // NOLINT(modernize-deprecated-headers)
#include <stdint.h>  // NOLINT(modernize-deprecated-headers)

#define NAPI_AUTO_LENGTH SIZE_MAX
#define NAPI_VERSION_EXPERIMENTAL 2147483647
#define NAPI_VERSION 8

NAPI_EXTERN napi_status napi_get_last_error_info(napi_env env, const napi_extended_error_info **result);

// Getters for defined singletons
NAPI_EXTERN napi_status NAPI_CDECL napi_get_undefined(napi_env env,
                                                      napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_null(napi_env env,
                                                 napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_global(napi_env env,
                                                   napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_boolean(napi_env env,
                                                    bool value,
                                                    napi_value *result);

// Methods to create Primitive types/Objects
NAPI_EXTERN napi_status NAPI_CDECL napi_create_object(napi_env env,
                                                      napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_array(napi_env env,
                                                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_array_with_length(napi_env env, size_t length, napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_double(napi_env env,
                                                      double value,
                                                      napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_int32(napi_env env,
                                                     int32_t value,
                                                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_uint32(napi_env env,
                                                      uint32_t value,
                                                      napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_int64(napi_env env,
                                                     int64_t value,
                                                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_string_latin1(
    napi_env env, const char *str, size_t length, napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_string_utf8(napi_env env,
                                                           const char *str,
                                                           size_t length,
                                                           napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_string_utf16(napi_env env,
                                                            const char16_t *str,
                                                            size_t length,
                                                            napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_symbol(napi_env env,
                                                      napi_value description,
                                                      napi_value *result);
NAPI_EXTERN napi_status node_api_symbol_for(napi_env env,
                                            const char *utf8description,
                                            size_t length,
                                            napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_function(napi_env env,
                                                        const char *utf8name,
                                                        size_t length,
                                                        napi_callback cb,
                                                        void *data,
                                                        napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_error(napi_env env,
                                                     napi_value code,
                                                     napi_value msg,
                                                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_type_error(napi_env env,
                                                          napi_value code,
                                                          napi_value msg,
                                                          napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_range_error(napi_env env,
                                                           napi_value code,
                                                           napi_value msg,
                                                           napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_syntax_error(napi_env env,
                                                            napi_value code,
                                                            napi_value msg,
                                                            napi_value *result);
// Methods to get the native napi_value from Primitive type
NAPI_EXTERN napi_status NAPI_CDECL napi_typeof(napi_env env,
                                               napi_value value,
                                               napi_valuetype *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_double(napi_env env,
                                                         napi_value value,
                                                         double *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_int32(napi_env env,
                                                        napi_value value,
                                                        int32_t *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_uint32(napi_env env,
                                                         napi_value value,
                                                         uint32_t *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_int64(napi_env env,
                                                        napi_value value,
                                                        int64_t *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_bool(napi_env env,
                                                       napi_value value,
                                                       bool *result);

// Copies LATIN-1 encoded bytes from a string into a buffer.
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_string_latin1(
    napi_env env, napi_value value, char *buf, size_t bufsize, size_t *result);

// Copies UTF-8 encoded bytes from a string into a buffer.
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_string_utf8(
    napi_env env, napi_value value, char *str, size_t length, size_t *result);

// Copies UTF-16 encoded bytes from a string into a buffer.
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_string_utf16(napi_env env,
                                                               napi_value value,
                                                               char16_t *buf,
                                                               size_t bufsize,
                                                               size_t *result);

// Methods to coerce values
// These APIs may execute user scripts
NAPI_EXTERN napi_status NAPI_CDECL napi_coerce_to_bool(napi_env env,
                                                       napi_value value,
                                                       napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_coerce_to_number(napi_env env,
                                                         napi_value value,
                                                         napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_coerce_to_object(napi_env env,
                                                         napi_value value,
                                                         napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_coerce_to_string(napi_env env,
                                                         napi_value value,
                                                         napi_value *result);

// Methods to work with Objects
NAPI_EXTERN napi_status NAPI_CDECL napi_get_prototype(napi_env env,
                                                      napi_value object,
                                                      napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_property_names(napi_env env,
                                                           napi_value object,
                                                           napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_set_property(napi_env env,
                                                     napi_value object,
                                                     napi_value key,
                                                     napi_value value);
NAPI_EXTERN napi_status NAPI_CDECL napi_has_property(napi_env env,
                                                     napi_value object,
                                                     napi_value key,
                                                     bool *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_property(napi_env env,
                                                     napi_value object,
                                                     napi_value key,
                                                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_delete_property(napi_env env,
                                                        napi_value object,
                                                        napi_value key,
                                                        bool *result);

NAPI_EXTERN napi_status NAPI_CDECL napi_has_own_property(napi_env env,
                                                         napi_value object,
                                                         napi_value key,
                                                         bool *result);

NAPI_EXTERN napi_status NAPI_CDECL napi_has_own_named_property(napi_env env,
                                                               napi_value object,
                                                               const char *utf8name,
                                                               bool *result);

NAPI_EXTERN napi_status NAPI_CDECL napi_set_named_property(napi_env env,
                                                           napi_value object,
                                                           const char *utf8name,
                                                           napi_value value);
NAPI_EXTERN napi_status NAPI_CDECL napi_has_named_property(napi_env env,
                                                           napi_value object,
                                                           const char *utf8name,
                                                           bool *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_named_property(napi_env env,
                                                           napi_value object,
                                                           const char *utf8name,
                                                           napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_set_element(napi_env env,
                                                    napi_value object,
                                                    uint32_t index,
                                                    napi_value value);
NAPI_EXTERN napi_status NAPI_CDECL napi_has_element(napi_env env,
                                                    napi_value object,
                                                    uint32_t index,
                                                    bool *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_element(napi_env env,
                                                    napi_value object,
                                                    uint32_t index,
                                                    napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_delete_element(napi_env env,
                                                       napi_value object,
                                                       uint32_t index,
                                                       bool *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_define_properties(napi_env env,
                       napi_value object,
                       size_t property_count,
                       const napi_property_descriptor *properties);

// Methods to work with Arrays
NAPI_EXTERN napi_status NAPI_CDECL napi_is_array(napi_env env,
                                                 napi_value value,
                                                 bool *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_array_length(napi_env env,
                                                         napi_value value,
                                                         uint32_t *result);

// Methods to compare values
NAPI_EXTERN napi_status NAPI_CDECL napi_strict_equals(napi_env env,
                                                      napi_value lhs,
                                                      napi_value rhs,
                                                      bool *result);

// Methods to work with Functions
NAPI_EXTERN napi_status NAPI_CDECL napi_call_function(napi_env env,
                                                      napi_value recv,
                                                      napi_value func,
                                                      size_t argc,
                                                      const napi_value *argv,
                                                      napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_new_instance(napi_env env,
                                                     napi_value constructor,
                                                     size_t argc,
                                                     const napi_value *argv,
                                                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_instanceof(napi_env env,
                                                   napi_value object,
                                                   napi_value constructor,
                                                   bool *result);

// Methods to work with napi_callbacks

// Gets all callback info in a single call. (Ugly, but faster.)
NAPI_EXTERN napi_status NAPI_CDECL napi_get_cb_info(
    napi_env env,              // [in] NAPI environment handle
    napi_callback_info cbinfo, // [in] Opaque callback-info handle
    size_t *argc,              // [in-out] Specifies the size of the provided argv array
                               // and receives the actual count of args.
    napi_value *argv,          // [out] Array of values
    napi_value *this_arg,      // [out] Receives the JS 'this' arg for the call
    void **data);              // [out] Receives the data pointer for the callback.

NAPI_EXTERN napi_status NAPI_CDECL napi_get_new_target(
    napi_env env, napi_callback_info cbinfo, napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_define_class(napi_env env,
                  const char *utf8name,
                  size_t length,
                  napi_callback constructor,
                  void *data,
                  size_t property_count,
                  const napi_property_descriptor *properties,
                  napi_value *result);

// Methods to work with external data objects
NAPI_EXTERN napi_status NAPI_CDECL napi_wrap(napi_env env,
                                             napi_value js_object,
                                             void *native_object,
                                             napi_finalize finalize_cb,
                                             void *finalize_hint,
                                             napi_ref *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_unwrap(napi_env env,
                                               napi_value js_object,
                                               void **result);
NAPI_EXTERN napi_status NAPI_CDECL napi_remove_wrap(napi_env env,
                                                    napi_value js_object,
                                                    void **result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_external(napi_env env,
                     void *data,
                     napi_finalize finalize_cb,
                     void *finalize_hint,
                     napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_external(napi_env env,
                                                           napi_value value,
                                                           void **result);

// Methods to control object lifespan

// Set initial_refcount to 0 for a weak reference, >0 for a strong reference.
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_reference(napi_env env,
                      napi_value value,
                      uint32_t initial_refcount,
                      napi_ref *result);

// Deletes a reference. The referenced value is released, and may
// be GC'd unless there are other references to it.
NAPI_EXTERN napi_status NAPI_CDECL napi_delete_reference(napi_env env,
                                                         napi_ref ref);

// Increments the reference count, optionally returning the resulting count.
// After this call the  reference will be a strong reference because its
// refcount is >0, and the referenced object is effectively "pinned".
// Calling this when the refcount is 0 and the object is unavailable
// results in an error.
NAPI_EXTERN napi_status NAPI_CDECL napi_reference_ref(napi_env env,
                                                      napi_ref ref,
                                                      uint32_t *result);

// Decrements the reference count, optionally returning the resulting count.
// If the result is 0 the reference is now weak and the object may be GC'd
// at any time if there are no other references. Calling this when the
// refcount is already 0 results in an error.
NAPI_EXTERN napi_status NAPI_CDECL napi_reference_unref(napi_env env,
                                                        napi_ref ref,
                                                        uint32_t *result);

// Attempts to get a referenced value. If the reference is weak,
// the value might no longer be available, in that case the call
// is still successful but the result is NULL.
NAPI_EXTERN napi_status NAPI_CDECL napi_get_reference_value(napi_env env,
                                                            napi_ref ref,
                                                            napi_value *result);

NAPI_EXTERN napi_status NAPI_CDECL
napi_open_handle_scope(napi_env env, napi_handle_scope *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_close_handle_scope(napi_env env, napi_handle_scope scope);
NAPI_EXTERN napi_status NAPI_CDECL napi_open_escapable_handle_scope(
    napi_env env, napi_escapable_handle_scope *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_close_escapable_handle_scope(
    napi_env env, napi_escapable_handle_scope scope);

NAPI_EXTERN napi_status NAPI_CDECL
napi_escape_handle(napi_env env,
                   napi_escapable_handle_scope scope,
                   napi_value escapee,
                   napi_value *result);

// Methods to support error handling
NAPI_EXTERN napi_status NAPI_CDECL napi_throw(napi_env env, napi_value error);
NAPI_EXTERN napi_status NAPI_CDECL napi_throw_error(napi_env env,
                                                    const char *code,
                                                    const char *msg);
NAPI_EXTERN napi_status NAPI_CDECL napi_throw_type_error(napi_env env,
                                                         const char *code,
                                                         const char *msg);
NAPI_EXTERN napi_status NAPI_CDECL napi_throw_range_error(napi_env env,
                                                          const char *code,
                                                          const char *msg);
NAPI_EXTERN napi_status NAPI_CDECL napi_is_error(napi_env env,
                                                 napi_value value,
                                                 bool *result);

// Methods to support catching exceptions
NAPI_EXTERN napi_status NAPI_CDECL napi_is_exception_pending(napi_env env,
                                                             bool *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_get_and_clear_last_exception(napi_env env, napi_value *result);

// Methods to work with array buffers and typed arrays
NAPI_EXTERN napi_status NAPI_CDECL napi_is_arraybuffer(napi_env env,
                                                       napi_value value,
                                                       bool *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_create_arraybuffer(napi_env env,
                                                           size_t byte_length,
                                                           void **data,
                                                           napi_value *result);
#ifndef NODE_API_NO_EXTERNAL_BUFFERS_ALLOWED
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_external_arraybuffer(napi_env env,
                                 void *external_data,
                                 size_t byte_length,
                                 napi_finalize finalize_cb,
                                 void *finalize_hint,
                                 napi_value *result);
#endif // NODE_API_NO_EXTERNAL_BUFFERS_ALLOWED
NAPI_EXTERN napi_status NAPI_CDECL napi_get_arraybuffer_info(
    napi_env env, napi_value arraybuffer, void **data, size_t *byte_length);
NAPI_EXTERN napi_status NAPI_CDECL napi_is_typedarray(napi_env env,
                                                      napi_value value,
                                                      bool *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_typedarray(napi_env env,
                       napi_typedarray_type type,
                       size_t length,
                       napi_value arraybuffer,
                       size_t byte_offset,
                       napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_get_typedarray_info(napi_env env,
                         napi_value typedarray,
                         napi_typedarray_type *type,
                         size_t *length,
                         void **data,
                         napi_value *arraybuffer,
                         size_t *byte_offset);

NAPI_EXTERN napi_status NAPI_CDECL napi_create_dataview(napi_env env,
                                                        size_t length,
                                                        napi_value arraybuffer,
                                                        size_t byte_offset,
                                                        napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_is_dataview(napi_env env,
                                                    napi_value value,
                                                    bool *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_get_dataview_info(napi_env env,
                       napi_value dataview,
                       size_t *bytelength,
                       void **data,
                       napi_value *arraybuffer,
                       size_t *byte_offset);

// version management
NAPI_EXTERN napi_status NAPI_CDECL napi_get_version(napi_env env,
                                                    uint32_t *result);

// Promises
NAPI_EXTERN napi_status NAPI_CDECL napi_create_promise(napi_env env,
                                                       napi_deferred *deferred,
                                                       napi_value *promise);
NAPI_EXTERN napi_status NAPI_CDECL napi_resolve_deferred(napi_env env,
                                                         napi_deferred deferred,
                                                         napi_value resolution);
NAPI_EXTERN napi_status NAPI_CDECL napi_reject_deferred(napi_env env,
                                                        napi_deferred deferred,
                                                        napi_value rejection);
NAPI_EXTERN napi_status NAPI_CDECL napi_is_promise(napi_env env,
                                                   napi_value value,
                                                   bool *is_promise);

// Running a script
NAPI_EXTERN napi_status NAPI_CDECL napi_run_script(napi_env env,
                                                     napi_value script,
                                                     napi_value *result);

NAPI_EXTERN napi_status NAPI_CDECL napi_run_script_source(napi_env env,
                                                            napi_value script,
                                                            const char* source_url,
                                                            napi_value* result);

// Memory management
NAPI_EXTERN napi_status NAPI_CDECL napi_adjust_external_memory(
    napi_env env, int64_t change_in_bytes, int64_t *adjusted_value);

#if NAPI_VERSION >= 5

// Dates
NAPI_EXTERN napi_status NAPI_CDECL napi_create_date(napi_env env,
                                                    double time,
                                                    napi_value *result);

NAPI_EXTERN napi_status NAPI_CDECL napi_is_date(napi_env env,
                                                napi_value value,
                                                bool *is_date);

NAPI_EXTERN napi_status NAPI_CDECL napi_get_date_value(napi_env env,
                                                       napi_value value,
                                                       double *result);

// Add finalizer for pointer
NAPI_EXTERN napi_status NAPI_CDECL napi_add_finalizer(napi_env env,
                                                      napi_value js_object,
                                                      void *finalize_data,
                                                      napi_finalize finalize_cb,
                                                      void *finalize_hint,
                                                      napi_ref *result);

#endif // NAPI_VERSION >= 5

#if NAPI_VERSION >= 6

// BigInt
NAPI_EXTERN napi_status NAPI_CDECL napi_create_bigint_int64(napi_env env,
                                                            int64_t value,
                                                            napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_bigint_uint64(napi_env env, uint64_t value, napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL
napi_create_bigint_words(napi_env env,
                         int sign_bit,
                         size_t word_count,
                         const uint64_t *words,
                         napi_value *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_bigint_int64(napi_env env,
                                                               napi_value value,
                                                               int64_t *result,
                                                               bool *lossless);
NAPI_EXTERN napi_status NAPI_CDECL napi_get_value_bigint_uint64(
    napi_env env, napi_value value, uint64_t *result, bool *lossless);
NAPI_EXTERN napi_status NAPI_CDECL
napi_get_value_bigint_words(napi_env env,
                            napi_value value,
                            int *sign_bit,
                            size_t *word_count,
                            uint64_t *words);

// Object
NAPI_EXTERN napi_status NAPI_CDECL
napi_get_all_property_names(napi_env env,
                            napi_value object,
                            napi_key_collection_mode key_mode,
                            napi_key_filter key_filter,
                            napi_key_conversion key_conversion,
                            napi_value *result);

// Instance data
NAPI_EXTERN napi_status NAPI_CDECL napi_set_instance_data(
    napi_env env, void *data, napi_finalize finalize_cb, void *finalize_hint);

NAPI_EXTERN napi_status NAPI_CDECL napi_get_instance_data(napi_env env,
                                                          void **data);
#endif // NAPI_VERSION >= 6

#if NAPI_VERSION >= 7
// ArrayBuffer detaching
NAPI_EXTERN napi_status NAPI_CDECL
napi_detach_arraybuffer(napi_env env, napi_value arraybuffer);

NAPI_EXTERN napi_status NAPI_CDECL
napi_is_detached_arraybuffer(napi_env env, napi_value value, bool *result);
#endif // NAPI_VERSION >= 7

#if NAPI_VERSION >= 8
// Type tagging
NAPI_EXTERN napi_status NAPI_CDECL napi_type_tag_object(
    napi_env env, napi_value value, const napi_type_tag *type_tag);

NAPI_EXTERN napi_status NAPI_CDECL
napi_check_object_type_tag(napi_env env,
                           napi_value value,
                           const napi_type_tag *type_tag,
                           bool *result);
NAPI_EXTERN napi_status NAPI_CDECL napi_object_freeze(napi_env env,
                                                      napi_value object);
NAPI_EXTERN napi_status NAPI_CDECL napi_object_seal(napi_env env,
                                                    napi_value object);

NAPI_EXTERN napi_status NAPI_CDECL napi_is_float(napi_env env, napi_value value, bool *result);
#endif // NAPI_VERSION >= 8

NAPI_EXTERN napi_status NAPI_CDECL js_create_runtime(napi_runtime *runtime);

NAPI_EXTERN napi_status NAPI_CDECL js_create_napi_env(napi_env *env, napi_runtime runtime);

NAPI_EXTERN napi_status NAPI_CDECL js_free_napi_env(napi_env env);

NAPI_EXTERN napi_status NAPI_CDECL js_free_runtime(napi_runtime runtime);

NAPI_EXTERN napi_status NAPI_CDECL js_execute_script(napi_env env,
                                                     napi_value script,
                                                     const char *file,
                                                     napi_value *result);

NAPI_EXTERN napi_status NAPI_CDECL napi_set_gc_being_callback(napi_env env, napi_finalize cb, void *data);

NAPI_EXTERN napi_status NAPI_CDECL js_runtime_after_gc_callback(napi_env env, napi_finalize cb, void *data);


NAPI_EXTERN napi_status NAPI_CDECL js_execute_pending_jobs(napi_env env);

NAPI_EXTERN napi_status NAPI_CDECL js_update_stack_top(napi_env env);

EXTERN_C_END

#endif // SRC_JS_NATIVE_API_H_

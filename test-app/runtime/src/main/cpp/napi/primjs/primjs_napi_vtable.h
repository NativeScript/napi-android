// PrimJS Node-API vtable.
//
// `struct napi_env__` below is the function-pointer table that the prebuilt
// primjs `libnapi.so` installs on every napi_env via the exported
// napi_attach_quickjs(). The adapter (js_native_api_adapter.cc) forwards the
// standard-named napi_* C entry points into these slots.
//
// This is a verbatim mirror of `struct napi_env__` in primjs'
// src/napi/js_native_api.h, built with ENABLE_CODECACHE=ON. KEEP IT IN SYNC:
// append new members at the END only -- never reorder or remove -- to preserve
// the ABI the shipped libnapi.so expects.

#ifndef TEST_APP_PRIMJS_NAPI_VTABLE_H
#define TEST_APP_PRIMJS_NAPI_VTABLE_H

#include "js_native_api.h"  // standard napi types (napi_env, napi_value, ...)

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// --- primjs-internal handle/callback types. Only their pointer/enum size is
// --- relevant here: every use below is a vtable *slot* (function pointer) or a
// --- leading env pointer, so opaque placeholders keep the layout ABI-exact.
typedef void* napi_state;
typedef void* napi_runtime;
typedef void* napi_context;
typedef void* napi_context_scope;
typedef void* napi_error_scope;
typedef void* napi_class;
typedef void* napi_async_work;
typedef void* napi_threadsafe_function;
typedef void (*napi_async_execute_callback)(napi_env env, void* data);
typedef void (*napi_async_complete_callback)(napi_env env, napi_status status,
                                             void* data);
typedef void (*napi_threadsafe_function_call_js)(napi_env env, napi_value cb,
                                                 void* ctx, void* data);
typedef int napi_threadsafe_function_call_mode;
typedef int napi_status_legacy;

typedef enum {
    napi_deferred_resolve,
    napi_deferred_reject,
    napi_deferred_delete
} napi_deferred_release_mode;

struct napi_env__ {
  napi_state state;
  napi_runtime rt;
  napi_context ctx;

  // Warning: Keep in-sync with macros in napi_macro.h!
  // Always append function at the end to keep ABI compatible!

  napi_status (*napi_get_version)(napi_env env, uint32_t* result);

  // ENGINE CALL
  // Getters for defined singletons
  napi_status (*napi_get_undefined)(napi_env env, napi_value* result);
  napi_status (*napi_get_null)(napi_env env, napi_value* result);
  napi_status (*napi_get_global)(napi_env env, napi_value* result);
  napi_status (*napi_get_boolean)(napi_env env, bool value, napi_value* result);

  // Methods to create Primitive types/Objects
  napi_status (*napi_create_object)(napi_env env, napi_value* result);
  napi_status (*napi_create_array)(napi_env env, napi_value* result);
  napi_status (*napi_create_array_with_length)(napi_env env, size_t length,
                                               napi_value* result);
  napi_status (*napi_create_double)(napi_env env, double value,
                                    napi_value* result);
  napi_status (*napi_create_int32)(napi_env env, int32_t value,
                                   napi_value* result);
  napi_status (*napi_create_uint32)(napi_env env, uint32_t value,
                                    napi_value* result);
  napi_status (*napi_create_int64)(napi_env env, int64_t value,
                                   napi_value* result);
  napi_status (*napi_create_string_latin1)(napi_env env, const char* str,
                                           size_t length, napi_value* result);
  napi_status (*napi_create_string_utf8)(napi_env env, const char* str,
                                         size_t length, napi_value* result);
  napi_status (*napi_create_string_utf16)(napi_env env, const char16_t* str,
                                          size_t length, napi_value* result);
  napi_status (*napi_create_symbol)(napi_env env, napi_value description,
                                    napi_value* result);
  napi_status (*napi_create_function)(napi_env env, const char* utf8name,
                                      size_t length, napi_callback cb,
                                      void* data, napi_value* result);
  napi_status (*napi_create_error)(napi_env env, napi_value code,
                                   napi_value msg, napi_value* result);
  napi_status (*napi_create_type_error)(napi_env env, napi_value code,
                                        napi_value msg, napi_value* result);
  napi_status (*napi_create_range_error)(napi_env env, napi_value code,
                                         napi_value msg, napi_value* result);

  // Methods to get the native napi_value from Primitive type
  napi_status (*napi_typeof)(napi_env env, napi_value value,
                             napi_valuetype* result);
  napi_status (*napi_get_value_double)(napi_env env, napi_value value,
                                       double* result);
  napi_status (*napi_get_value_int32)(napi_env env, napi_value value,
                                      int32_t* result);
  napi_status (*napi_get_value_uint32)(napi_env env, napi_value value,
                                       uint32_t* result);
  napi_status (*napi_get_value_int64)(napi_env env, napi_value value,
                                      int64_t* result);
  napi_status (*napi_get_value_bool)(napi_env env, napi_value value,
                                     bool* result);

  // Copies LATIN-1 encoded bytes from a string into a buffer.
  napi_status (*napi_get_value_string_latin1)(napi_env env, napi_value value,
                                              char* buf, size_t bufsize,
                                              size_t* result);

  // Copies UTF-8 encoded bytes from a string into a buffer.
  napi_status (*napi_get_value_string_utf8)(napi_env env, napi_value value,
                                            char* buf, size_t bufsize,
                                            size_t* result);

  // Copies UTF-16 encoded bytes from a string into a buffer.
  napi_status (*napi_get_value_string_utf16)(napi_env env, napi_value value,
                                             char16_t* buf, size_t bufsize,
                                             size_t* result);

  // Methods to coerce values
  // These APIs may execute user scripts
  napi_status (*napi_coerce_to_bool)(napi_env env, napi_value value,
                                     napi_value* result);
  napi_status (*napi_coerce_to_number)(napi_env env, napi_value value,
                                       napi_value* result);
  napi_status (*napi_coerce_to_object)(napi_env env, napi_value value,
                                       napi_value* result);
  napi_status (*napi_coerce_to_string)(napi_env env, napi_value value,
                                       napi_value* result);

  // Methods to work with Objects
  napi_status (*napi_get_prototype)(napi_env env, napi_value object,
                                    napi_value* result);
  napi_status (*napi_get_property_names)(napi_env env, napi_value object,
                                         napi_value* result);
  napi_status (*napi_set_property)(napi_env env, napi_value object,
                                   napi_value key, napi_value value);
  napi_status (*napi_has_property)(napi_env env, napi_value object,
                                   napi_value key, bool* result);
  napi_status (*napi_get_property)(napi_env env, napi_value object,
                                   napi_value key, napi_value* result);
  napi_status (*napi_delete_property)(napi_env env, napi_value object,
                                      napi_value key, bool* result);
  napi_status (*napi_has_own_property)(napi_env env, napi_value object,
                                       napi_value key, bool* result);
  napi_status (*napi_set_named_property)(napi_env env, napi_value object,
                                         const char* utf8name,
                                         napi_value value);
  napi_status (*napi_has_named_property)(napi_env env, napi_value object,
                                         const char* utf8name, bool* result);
  napi_status (*napi_get_named_property)(napi_env env, napi_value object,
                                         const char* utf8name,
                                         napi_value* result);
  napi_status (*napi_set_element)(napi_env env, napi_value object,
                                  uint32_t index, napi_value value);
  napi_status (*napi_has_element)(napi_env env, napi_value object,
                                  uint32_t index, bool* result);
  napi_status (*napi_get_element)(napi_env env, napi_value object,
                                  uint32_t index, napi_value* result);
  napi_status (*napi_delete_element)(napi_env env, napi_value object,
                                     uint32_t index, bool* result);

  napi_status (*napi_define_properties)(
      napi_env env, napi_value object, size_t property_count,
      const napi_property_descriptor* properties);

  // Methods to work with Arrays
  napi_status (*napi_is_array)(napi_env env, napi_value value, bool* result);
  napi_status (*napi_get_array_length)(napi_env env, napi_value value,
                                       uint32_t* result);

  // Methods to compare values
  napi_status (*napi_strict_equals)(napi_env env, napi_value lhs,
                                    napi_value rhs, bool* result);

  // Methods to work with Functions
  napi_status (*napi_call_function)(napi_env env, napi_value recv,
                                    napi_value func, size_t argc,
                                    const napi_value* argv, napi_value* result);
  napi_status (*napi_new_instance)(napi_env env, napi_value constructor,
                                   size_t argc, const napi_value* argv,
                                   napi_value* result);
  napi_status (*napi_instanceof)(napi_env env, napi_value object,
                                 napi_value constructor, bool* result);

  // Methods to work with napi_callbacks

  // Gets all callback info in a single call. (Ugly, but faster.)
  napi_status (*napi_get_cb_info)(
      napi_env env,               // [in] NAPI environment handle
      napi_callback_info cbinfo,  // [in] Opaque callback-info handle
      size_t* argc,  // [in-out] Specifies the size of the provided argv array
                     // and receives the actual count of args.
      napi_value* argv,      // [out] Array of values
      napi_value* this_arg,  // [out] Receives the JS 'this' arg for the call
      void** data);  // [out] Receives the data pointer for the callback.

  napi_status (*napi_get_new_target)(napi_env env, napi_callback_info cbinfo,
                                     napi_value* result);

  napi_status (*napi_define_class)(napi_env env, const char* utf8name,
                                   size_t length, napi_callback constructor,
                                   void* data, size_t property_count,
                                   const napi_property_descriptor* properties,
                                   napi_class super_class, napi_class* result);

  napi_status (*napi_release_class)(napi_env env, napi_class clazz);

  napi_status (*napi_class_get_function)(napi_env env, napi_class clazz,
                                         napi_value* result);

  // Methods to work with external data objects
  napi_status (*napi_wrap)(napi_env env, napi_value js_object,
                           void* native_object, napi_finalize finalize_cb,
                           void* finalize_hint, napi_ref* result);
  napi_status (*napi_unwrap)(napi_env env, napi_value js_object, void** result);
  napi_status (*napi_remove_wrap)(napi_env env, napi_value js_object,
                                  void** result);
  napi_status (*napi_create_external)(napi_env env, void* data,
                                      napi_finalize finalize_cb,
                                      void* finalize_hint, napi_value* result);
  napi_status (*napi_get_value_external)(napi_env env, napi_value value,
                                         void** result);

  // Methods to control object lifespan

  // Set initial_refcount to 0 for a weak reference, >0 for a strong reference.
  napi_status (*napi_create_reference)(napi_env env, napi_value value,
                                       uint32_t initial_refcount,
                                       napi_ref* result);

  // Deletes a reference. The referenced value is released, and may
  // be GC'd unless there are other references to it.
  napi_status (*napi_delete_reference)(napi_env env, napi_ref ref);

  // Increments the reference count, optionally returning the resulting count.
  // After this call the  reference will be a strong reference because its
  // refcount is >0, and the referenced object is effectively "pinned".
  // Calling this when the refcount is 0 and the object is unavailable
  // results in an error.
  napi_status (*napi_reference_ref)(napi_env env, napi_ref ref,
                                    uint32_t* result);

  // Decrements the reference count, optionally returning the resulting count.
  // If the result is 0 the reference is now weak and the object may be GC'd
  // at any time if there are no other references. Calling this when the
  // refcount is already 0 results in an error.
  napi_status (*napi_reference_unref)(napi_env env, napi_ref ref,
                                      uint32_t* result);

  // Attempts to get a referenced value. If the reference is weak,
  // the value might no longer be available, in that case the call
  // is still successful but the result is NULL.
  napi_status (*napi_get_reference_value)(napi_env env, napi_ref ref,
                                          napi_value* result);

  napi_status (*napi_open_handle_scope)(napi_env env,
                                        napi_handle_scope* result);
  napi_status (*napi_close_handle_scope)(napi_env env, napi_handle_scope scope);
  napi_status (*napi_open_escapable_handle_scope)(
      napi_env env, napi_escapable_handle_scope* result);
  napi_status (*napi_close_escapable_handle_scope)(
      napi_env env, napi_escapable_handle_scope scope);

  napi_status (*napi_escape_handle)(napi_env env,
                                    napi_escapable_handle_scope scope,
                                    napi_value escapee, napi_value* result);

  // Methods to support error handling
  napi_status (*napi_throw_)(napi_env env, napi_value error);
  napi_status (*napi_throw_error)(napi_env env, const char* code,
                                  const char* msg);
  napi_status (*napi_throw_type_error)(napi_env env, const char* code,
                                       const char* msg);
  napi_status (*napi_throw_range_error)(napi_env env, const char* code,
                                        const char* msg);
  napi_status (*napi_is_error)(napi_env env, napi_value value, bool* result);

  // Methods to support catching exceptions
  napi_status (*napi_is_exception_pending)(napi_env env, bool* result);
  napi_status (*napi_get_and_clear_last_exception)(napi_env env,
                                                   napi_value* result);

  // Methods to work with array buffers and typed arrays
  napi_status (*napi_is_arraybuffer)(napi_env env, napi_value value,
                                     bool* result);
  napi_status (*napi_create_arraybuffer)(napi_env env, size_t byte_length,
                                         void** data, napi_value* result);
  napi_status (*napi_create_external_arraybuffer)(
      napi_env env, void* external_data, size_t byte_length,
      napi_finalize finalize_cb, void* finalize_hint, napi_value* result);
  napi_status (*napi_get_arraybuffer_info)(napi_env env, napi_value arraybuffer,
                                           void** data, size_t* byte_length);
  napi_status (*napi_is_typedarray)(napi_env env, napi_value value,
                                    bool* result);
  napi_status (*napi_create_typedarray)(napi_env env, napi_typedarray_type type,
                                        size_t length, napi_value arraybuffer,
                                        size_t byte_offset, napi_value* result);

  napi_status (*napi_is_typedarray_of)(napi_env env, napi_value typedarray,
                                       napi_typedarray_type type, bool* result);

  napi_status (*napi_get_typedarray_info)(napi_env env, napi_value typedarray,
                                          napi_typedarray_type* type,
                                          size_t* length, void** data,
                                          napi_value* arraybuffer,
                                          size_t* byte_offset);

  napi_status (*napi_create_dataview)(napi_env env, size_t length,
                                      napi_value arraybuffer,
                                      size_t byte_offset, napi_value* result);
  napi_status (*napi_is_dataview)(napi_env env, napi_value value, bool* result);
  napi_status (*napi_get_dataview_info)(napi_env env, napi_value dataview,
                                        size_t* bytelength, void** data,
                                        napi_value* arraybuffer,
                                        size_t* byte_offset);

  // Promises
  napi_status (*napi_create_promise)(napi_env env, napi_deferred* deferred,
                                     napi_value* promise);
  napi_status (*napi_release_deferred)(napi_env env, napi_deferred deferred,
                                       napi_value resolution,
                                       napi_deferred_release_mode mode);
  napi_status (*napi_is_promise)(napi_env env, napi_value value,
                                 bool* is_promise);

  // Running a script
  napi_status (*napi_run_script)(napi_env env, const char* script,
                                 size_t length, const char* filename,
                                 napi_value* result);

  // Memory management
  napi_status (*napi_adjust_external_memory)(napi_env env,
                                             int64_t change_in_bytes,
                                             int64_t* adjusted_value);

  // Add finalizer for pointer
  napi_status (*napi_add_finalizer)(napi_env env, napi_value js_object,
                                    void* native_object,
                                    napi_finalize finalize_cb,
                                    void* finalize_hint, napi_ref* result);

  // Instance data
  napi_status_legacy (*napi_set_instance_data)(napi_env env, uint64_t key,
                                               void* data,
                                               napi_finalize finalize_cb,
                                               void* finalize_hint);

  napi_status (*napi_get_instance_data)(napi_env env, uint64_t key,
                                        void** data);

  // ENGINE CALL END

  // Universal CALL
  napi_status (*napi_get_last_error_info)(
      napi_env env, const napi_extended_error_info** result);

  napi_status (*napi_add_env_cleanup_hook)(napi_env env, void (*fun)(void* arg),
                                           void* arg);
  napi_status (*napi_remove_env_cleanup_hook)(napi_env env,
                                              void (*fun)(void* arg),
                                              void* arg);

  napi_status (*napi_create_async_work)(napi_env env, napi_value async_resource,
                                        napi_value async_resource_name,
                                        napi_async_execute_callback execute,
                                        napi_async_complete_callback complete,
                                        void* data, napi_async_work* result);

  napi_status (*napi_delete_async_work)(napi_env env, napi_async_work work);
  napi_status (*napi_queue_async_work)(napi_env env, napi_async_work work);
  napi_status (*napi_cancel_async_work)(napi_env env, napi_async_work work);

  // Calling into JS from other threads
  napi_status (*napi_create_threadsafe_function)(
      napi_env env, void* thread_finalize_data,
      napi_finalize thread_finalize_cb, void* context,
      napi_threadsafe_function_call_js call_js_cb,
      napi_threadsafe_function* result);

  napi_status (*napi_get_threadsafe_function_context)(
      napi_threadsafe_function func, void** result);

  napi_status (*napi_call_threadsafe_function)(
      napi_threadsafe_function func, void* data,
      napi_threadsafe_function_call_mode is_blocking);

  // no need to acquire tsfn, just hold the pointer and release in any thread
  napi_status (*napi_acquire_threadsafe_function)(
      napi_threadsafe_function func);

  napi_status (*napi_delete_threadsafe_function)(napi_threadsafe_function func);

  // no need to ref thread
  napi_status (*napi_unref_threadsafe_function)(
      napi_env env, napi_threadsafe_function func);

  // no need to ref thread
  napi_status (*napi_ref_threadsafe_function)(
      napi_env env, napi_threadsafe_function func);

  napi_status (*napi_get_loader)(napi_env env, napi_value* result);

  // UNIVERSAL CALL END

  napi_status (*napi_open_context_scope)(napi_env env,
                                         napi_context_scope* result);
  napi_status_legacy (*napi_close_context_scope)(napi_env env,
                                                 napi_context_scope scope);

  napi_status (*napi_open_error_scope)(napi_env env, napi_error_scope* result);
  napi_status (*napi_close_error_scope)(napi_env env, napi_error_scope scope);

  // loose equals
  napi_status (*napi_equals)(napi_env env, napi_value lhs, napi_value rhs,
                             bool* result);

  napi_status (*napi_get_unhandled_rejection_exception)(napi_env env,
                                                        napi_value* result);
  napi_status (*napi_get_own_property_descriptor)(napi_env env, napi_value obj,
                                                  napi_value prop,
                                                  napi_value* result);
  napi_status (*napi_post_worker_task)(napi_env env,
                                       std::function<void()> task);
  napi_status (*napi_store_code_cache)(napi_env env,
                                       const std::string& filename,
                                       const uint8_t* data, int length);
  napi_status (*napi_get_code_cache)(napi_env env, const std::string& filename,
                                     const uint8_t** data, int* length);
  napi_status (*napi_output_code_cache)(napi_env env,
                                        unsigned int place_holder);
  // this interface may be optimized by
  // discarding the parameter of std::function type
  napi_status (*napi_init_code_cache)(napi_env env, int capacity,
                                      const std::string& cache_file,
                                      std::function<void(bool)> callback);
  napi_status (*napi_dump_code_cache_status)(napi_env env, void* dump_vec);
  napi_status (*napi_run_script_cache)(napi_env env, const char* script,
                                       size_t length, const char* filename,
                                       napi_value* result);
  napi_status (*napi_run_code_cache)(napi_env env, const uint8_t* data,
                                     int length, napi_value* result);
  napi_status (*napi_gen_code_cache)(napi_env env, const char* script,
                                     size_t script_len, const uint8_t** data,
                                     int* length);

  napi_status (*napi_set_instance_data_spec_compliant)(
      napi_env env, uint64_t key, void* data, napi_finalize finalize_cb,
      void* finalize_hint);

  napi_status (*napi_define_properties_spec_compliant)(
      napi_env env, napi_value object, size_t property_count,
      const napi_property_descriptor* properties);

  napi_status (*napi_define_class_spec_compliant)(
      napi_env env, const char* utf8name, size_t length,
      napi_callback constructor, void* data, size_t property_count,
      const napi_property_descriptor* properties, napi_class super_class,
      napi_class* result);

  napi_status (*napi_call_function_spec_compliant)(napi_env env,
                                                   napi_value recv,
                                                   napi_value func, size_t argc,
                                                   const napi_value* argv,
                                                   napi_value* result);

  napi_status (*napi_wrap_spec_compliant)(napi_env env, napi_value js_object,
                                          void* native_object,
                                          napi_finalize finalize_cb,
                                          void* finalize_hint,
                                          napi_ref* result);
  napi_status (*napi_unwrap_spec_compliant)(napi_env env, napi_value js_object,
                                            void** result);
  napi_status (*napi_remove_wrap_spec_compliant)(napi_env env,
                                                 napi_value js_object,
                                                 void** result);

  napi_status (*napi_create_date)(napi_env env, double time,
                                  napi_value* result);

  napi_status (*napi_is_date)(napi_env env, napi_value value, bool* is_date);

  napi_status (*napi_get_date_value)(napi_env env, napi_value value,
                                     double* result);

  napi_status (*napi_get_all_property_names)(napi_env env, napi_value object,
                                             napi_key_collection_mode key_mode,
                                             napi_key_filter key_filter,
                                             napi_key_conversion key_conversion,
                                             napi_value* result);

  napi_status (*napi_create_threadsafe_function_spec_compliant)(
      napi_env env, void* thread_finalize_data,
      napi_finalize thread_finalize_cb, void* context,
      napi_threadsafe_function_call_js call_js_cb, size_t max_queue_size,
      size_t thread_count, napi_threadsafe_function* result);

  napi_status (*napi_create_bigint_int64)(napi_env env, int64_t value,
                                          napi_value* result);
  napi_status (*napi_create_bigint_uint64)(napi_env env, uint64_t value,
                                           napi_value* result);
  napi_status (*napi_get_value_bigint_int64)(napi_env env, napi_value value,
                                             int64_t* result, bool* lossless);
  napi_status (*napi_get_value_bigint_uint64)(napi_env env, napi_value value,
                                              uint64_t* result, bool* lossless);
  napi_status (*napi_create_bigint_words)(napi_env env, int sign_bit,
                                          size_t word_count,
                                          const uint64_t* words,
                                          napi_value* result);
  napi_status (*napi_get_value_bigint_words)(napi_env env, napi_value value,
                                             int* sign_bit, size_t* word_count,
                                             uint64_t* words);
};

#endif  // TEST_APP_PRIMJS_NAPI_VTABLE_H

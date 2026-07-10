#include "jsr.h"
// JSGlobalContextSetUnhandledRejectionCallback lives in this private JSC header.
#include <JavaScriptCore/JSContextRefPrivate.h>

// Native trampoline for JSC's unhandled-promise-rejection callback. JSC invokes
// it with (promise, reason); we forward to the JS-side
// globalThis.onUnhandledPromiseRejectionTracker (installed by ts_helpers.js),
// mirroring how the V8 path routes rejections.
static napi_value JscUnhandledRejectionCallback(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_value global, tracker;
    napi_get_global(env, &global);
    napi_get_named_property(env, global, "onUnhandledPromiseRejectionTracker", &tracker);

    napi_valuetype type;
    napi_typeof(env, tracker, &type);
    if (type == napi_function) {
        napi_call_function(env, global, tracker, argc, args, nullptr);
    }
    return nullptr;
}

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    if (!runtime) return napi_invalid_arg;
    *runtime = (jsr_ns_runtime) JSGlobalContextCreateInGroup(nullptr, nullptr);
    return napi_ok;
}

napi_status js_create_napi_env(napi_env* env, jsr_ns_runtime runtime) {
    if (env == nullptr) return napi_invalid_arg;

    *env = new napi_env__((JSGlobalContextRef) runtime);
    JSGlobalContextRelease((JSGlobalContextRef) runtime);

    napi_value gc;
    napi_create_function(*env, "gc", strlen("gc"), [](napi_env env, napi_callback_info info) -> napi_value {
        JSGarbageCollect(env->context);
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }, nullptr, &gc);
    napi_value global;
    napi_get_global(*env, &global);
    napi_set_named_property(*env, global, "gc", gc);

    // Report unhandled promise rejections. JSC keeps the callback alive (it is
    // stored on and marked by the global object), so no extra protection is
    // needed. JSC only surfaces the "unhandled" event, not a later "handled"
    // retraction, so the JS tracker treats every call as unhandled.
    napi_value rejectionCallback;
    napi_create_function(*env, "onUnhandledRejection", NAPI_AUTO_LENGTH,
                         JscUnhandledRejectionCallback, nullptr, &rejectionCallback);
    JSValueRef rejectionException = nullptr;
    JSGlobalContextSetUnhandledRejectionCallback(
            (*env)->context,
            reinterpret_cast<JSObjectRef>(rejectionCallback),
            &rejectionException);


    return napi_ok;

}

napi_status js_set_runtime_flags(const char* flags) {
    return napi_ok;
}

napi_status js_lock_env(napi_env env) {
    return napi_ok;
}

napi_status js_unlock_env(napi_env env) {
    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    if (env == nullptr) return napi_invalid_arg;
    delete env;
    return  napi_ok;
}

napi_status js_free_runtime(jsr_ns_runtime runtime) {
//    JSContextGroupRelease((JSContextGroupRef) runtime);
    return napi_ok;
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {

    return napi_run_script_source(env, script, file, result);

}

napi_status js_execute_pending_jobs(napi_env env) {
    return napi_ok;
}

napi_status js_get_engine_ptr(napi_env env, int64_t *engine_ptr) {
    *engine_ptr = (int64_t) 0;
    return napi_ok;
}

napi_status js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t *externalMemory) {
    return napi_ok;
}

napi_status js_cache_script(napi_env env, const char *source, const char *file) {
    return napi_ok;
}

napi_status js_run_cached_script(napi_env env, const char *file, napi_value script, void *cache,
                                 napi_value *result) {
    return napi_ok;
}


napi_status js_get_runtime_version(napi_env env, napi_value* version) {
    napi_create_string_utf8(env, "JSC", NAPI_AUTO_LENGTH, version);

    return napi_ok;
}

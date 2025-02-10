#include "jsr.h"

napi_status js_create_runtime(napi_runtime *runtime) {
    if (!runtime) return napi_invalid_arg;
    *runtime = (napi_runtime) JSGlobalContextCreateInGroup(nullptr, nullptr);
    return napi_ok;
}

napi_status js_create_napi_env(napi_env* env, napi_runtime runtime) {
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

napi_status js_free_runtime(napi_runtime runtime) {
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

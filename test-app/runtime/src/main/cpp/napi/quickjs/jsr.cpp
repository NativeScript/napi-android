#include "jsr.h"

JSR::JSR() = default;

std::unordered_map<napi_env, JSR *> JSR::env_to_jsr_cache;

napi_status js_create_runtime(napi_runtime *runtime) {
    return qjs_create_runtime(runtime);
}

napi_status js_create_napi_env(napi_env *env, napi_runtime runtime) {
    napi_status status = qjs_create_napi_env(env, runtime);

    JSR::env_to_jsr_cache.insert(std::make_pair(*env, new JSR()));
    return status;
}

napi_status js_set_runtime_flags(const char* flags) {
    return napi_ok;
}

napi_status js_lock_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }

    itFound->second->lock();

    return napi_ok;
}

napi_status js_unlock_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }

    itFound->second->unlock();

    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound != JSR::env_to_jsr_cache.end()) {
        delete itFound->second;
        JSR::env_to_jsr_cache.erase(env);
    }
    return qjs_free_napi_env(env);
}

napi_status js_free_runtime(napi_runtime runtime) {
    return qjs_free_runtime(runtime);
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    return qjs_execute_script(env, script, file, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    return qjs_execute_pending_jobs(env);
}

napi_status
js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t *externalMemory) {
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
    napi_create_string_utf8(env, "QuickJS", NAPI_AUTO_LENGTH, version);

    return napi_ok;
}

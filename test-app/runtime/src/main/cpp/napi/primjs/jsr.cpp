#include "napi_env_quickjs.h"
#include "napi_env.h"
#include "jsr.h"
#include <string>

JSR::JSR() = default;
tns::SimpleMap<napi_env, JSR *> JSR::env_to_jsr_cache;

struct jsr_ns_runtime__ {
    LEPUSRuntime* runtime;
    LEPUSContext* context;
};

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    auto _runtime = new jsr_ns_runtime__();
    LEPUSRuntime* rt = LEPUS_NewRuntimeWithMode(0);
    LEPUS_SetRuntimeInfo(rt, "Lynx_LepusNG");
    _runtime->context = LEPUS_NewContext(rt);
    LEPUS_SetMaxStackSize(_runtime->context, 1024 * 1024 * 1024);
    _runtime->runtime = rt;
    *runtime = _runtime;

    LEPUS_Eval( _runtime->context, "var a;", strlen("var a;"), "a", LEPUS_EVAL_TYPE_GLOBAL);


    return napi_ok;
}
napi_status js_create_napi_env(napi_env *env, jsr_ns_runtime runtime) {

    *env = napi_new_env();
    napi_attach_quickjs((*env), runtime->context);


    JSR::env_to_jsr_cache.Insert((*env), new JSR());
    LEPUS_Eval( runtime->context, "var a;", strlen("var a;"), "a", LEPUS_EVAL_TYPE_GLOBAL);

    const char *requireFactoryScript = "var a;";

    napi_value source;
    napi_create_string_utf8((*env), requireFactoryScript, strlen(requireFactoryScript), &source);

//    napi_value global;
//    napi_get_global(env, &global);

    napi_value result;
    napi_status status = js_execute_script((*env), source, "a", &result);

    return napi_ok;
}

napi_status js_set_runtime_flags(const char *flags) {
    return napi_ok;
}

napi_status js_lock_env(napi_env env) {
    auto jsr = JSR::env_to_jsr_cache.Get(env);
    if (jsr) jsr->lock();
    return napi_ok;
}

napi_status js_unlock_env(napi_env env) {
    auto jsr = JSR::env_to_jsr_cache.Get(env);
    if (jsr) jsr->unlock();

    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    JSR* jsr = JSR::env_to_jsr_cache.Get(env);
    delete jsr;
    JSR::env_to_jsr_cache.Remove(env);
    napi_detach_quickjs(env);
    return napi_ok;
}

napi_status js_free_runtime(jsr_ns_runtime runtime) {
    LEPUS_FreeContext(runtime->context);
    LEPUS_FreeRuntime(runtime->runtime);
    return napi_ok;
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    // PrimJS exposes napi_run_script as a raw (source, length, filename) entry
    // point, which lets us pass the script source and its filename directly
    // instead of round-tripping through napi_run_script_source.
    size_t length = 0;
    napi_status status = napi_get_value_string_utf8(env, script, nullptr, 0, &length);
    if (status != napi_ok) {
        return status;
    }

    std::string source(length + 1, '\0');
    status = napi_get_value_string_utf8(env, script, &source[0], length + 1, &length);
    if (status != napi_ok) {
        return status;
    }

    return napi_run_script(env, source.c_str(), length, file, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    return primjs_execute_pending_jobs(env);
}

napi_status
js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t *externalMemory) {
    napi_adjust_external_memory(env, changeInBytes, externalMemory);
    return napi_ok;
}

napi_status js_cache_script(napi_env env, const char *source, const char *file) {
    return napi_ok;
}

napi_status js_run_cached_script(napi_env env, const char *file, napi_value script, void *cache,
                                 napi_value *result) {
    return napi_ok;
}


napi_status js_run_bytecode_file(napi_env env, const char *file, const char *source_url,
                                 napi_value *result) {
    // No compile-time bytecode format wired up for this engine yet; fall back to source.
    return napi_cannot_run_js;
}

napi_status js_get_runtime_version(napi_env env, napi_value *version) {
    napi_create_string_utf8(env, "PrimJS", NAPI_AUTO_LENGTH, version);
    return napi_ok;
}

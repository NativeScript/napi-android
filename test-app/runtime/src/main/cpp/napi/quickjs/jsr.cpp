#include "jsr.h"

napi_status js_create_runtime(napi_runtime *runtime)
{
    return qjs_create_runtime(runtime);
}
napi_status js_create_napi_env(napi_env *env, napi_runtime runtime)
{
    return qjs_create_napi_env(env, runtime);
}
napi_status js_free_napi_env(napi_env env)
{
    return qjs_free_napi_env(env);
}
napi_status js_free_runtime(napi_runtime runtime)
{
    return qjs_free_runtime(runtime);
}
napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result)
{
    return qjs_execute_script(env, script, file, result);
}

napi_status js_execute_pending_jobs(napi_env env)
{
    return qjs_execute_pending_jobs(env);
}

napi_status js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t* externalMemory) {
    return napi_ok;
}
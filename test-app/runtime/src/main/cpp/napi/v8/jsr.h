//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include "v8-api.h"
#include "libplatform/libplatform.h"
#include "SimpleAllocator.h"

typedef struct NapiRuntime *napi_runtime;

class JSR {
public:
    JSR();
    v8::Isolate* isolate;
    static bool s_mainThreadInitialized;
    static v8::Platform* platform;
};

napi_status js_create_runtime(napi_runtime* runtime);
napi_status js_create_napi_env(napi_env* env, napi_runtime runtime);
napi_status js_free_napi_env(napi_env env);
napi_status js_free_runtime(napi_runtime runtime);
napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result);

napi_status js_execute_pending_jobs(napi_env env);

napi_status js_get_engine_ptr(napi_env env, int64_t *engine_ptr);

napi_status js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t* externalMemory);

napi_status js_cache_script(napi_env env, const char *source, const char *file);
napi_status js_run_cached_script(napi_env env, const char * file, napi_value script, void* cache, napi_value *result);

#define JSEnterScope    \
v8::Locker locker(env->isolate);   \
v8::Isolate::Scope isolate_scope(env->isolate); \
v8::Context::Scope context_scope(env->context()); \
v8::HandleScope handle_scope(env->isolate);

#define JSEnter    \
v8::Locker locker(env->isolate);   \
v8::Isolate::Scope isolate_scope(env->isolate); \
v8::Context::Scope context_scope(env->context()); \
napi_handle_scope handleScope;   \
napi_open_handle_scope(env, &handleScope);



#define JSLeave \
    napi_close_handle_scope(env, handleScope);

#endif //TEST_APP_JSR_H

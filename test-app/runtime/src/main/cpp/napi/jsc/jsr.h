//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include "jsc-api.h"

typedef struct NapiRuntime *napi_runtime;

napi_status js_create_runtime(napi_runtime* runtime);
napi_status js_create_napi_env(napi_env* env, napi_runtime runtime);
napi_status js_lock_env(napi_env env);
napi_status js_unlock_env(napi_env env);
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

class NapiScope {
public:
    explicit NapiScope(napi_env env)
            : env_(env)
    {
        napi_open_handle_scope(env_, &napiHandleScope_);
    }

    ~NapiScope() {
        napi_close_handle_scope(env_, napiHandleScope_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
};

#define JSEnterScope

#endif //TEST_APP_JSR_H

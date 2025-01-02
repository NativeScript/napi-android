//
// Created by Ammar Ahmed on 16/11/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include "hermes/hermes.h"
#include "hermes/hermes_api.h"
#include "jsi/threadsafe.h"

typedef struct NapiRuntime *napi_runtime;

class JSR {
public:
    JSR();
    std::unique_ptr<facebook::jsi::ThreadSafeRuntime> threadSafeRuntime;
    facebook::hermes::HermesRuntime* rt;
    std::recursive_mutex js_mutex;
    void lock() {
        js_mutex.lock();
    }
    void unlock() {
        js_mutex.unlock();
    }

    static std::unordered_map<napi_env, JSR *> env_to_jsr_cache;
};

napi_status js_create_runtime(napi_runtime* runtime);
napi_status js_lock_env(napi_env env);
napi_status js_unlock_env(napi_env env);
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

class NapiScope {
public:
    explicit NapiScope(napi_env env)
            : env_(env)
    {
        js_lock_env(env_);
        napi_open_handle_scope(env_, &napiHandleScope_);
    }

    ~NapiScope() {
        js_unlock_env(env_);
        napi_close_handle_scope(env_, napiHandleScope_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
};

#define JSEnterScope
#define JSEnter
#define JSLeave

#endif //TEST_APP_JSR_H

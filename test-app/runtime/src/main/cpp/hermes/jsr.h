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
};

napi_status js_create_runtime(napi_runtime* runtime);
napi_status jsr_lock_runtime(napi_runtime runtime);
napi_status jsr_unlock_runtime(napi_runtime runtime);
napi_status js_create_napi_env(napi_env* env, napi_runtime runtime);
napi_status js_free_napi_env(napi_env env);
napi_status js_free_runtime(napi_runtime runtime);
napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result);

napi_status js_execute_pending_jobs(napi_env env);
#endif //TEST_APP_JSR_H

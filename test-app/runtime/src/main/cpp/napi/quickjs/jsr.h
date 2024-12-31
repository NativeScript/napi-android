//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include "js_native_api.h"

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

#define JSEnterScope
#define JSEnter
#define JSLeave

#endif //TEST_APP_JSR_H

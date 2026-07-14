//
// Created by Ammar Ahmed on 17/01/2025.
//

#ifndef TEST_APP_JSR_COMMON_H
#define TEST_APP_JSR_COMMON_H

#include "js_native_api.h"

typedef struct jsr_ns_runtime__ *jsr_ns_runtime;

napi_status js_create_runtime(jsr_ns_runtime* runtime);
napi_status js_create_napi_env(napi_env* env, jsr_ns_runtime runtime);
napi_status js_set_runtime_flags(const char* flags);
napi_status js_lock_env(napi_env env);
napi_status js_unlock_env(napi_env env);
napi_status js_free_napi_env(napi_env env);
napi_status js_free_runtime(jsr_ns_runtime runtime);
napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result);

napi_status js_execute_pending_jobs(napi_env env);

napi_status js_get_engine_ptr(napi_env env, int64_t *engine_ptr);
napi_status js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t* externalMemory);
napi_status js_cache_script(napi_env env, const char *source, const char *file);
napi_status js_run_cached_script(napi_env env, const char * file, napi_value script, void* cache, napi_value *result);

/**
 * Compile-time bytecode support.
 *
 * If `file` holds pre-compiled bytecode this engine can execute (generated at
 * build time, e.g. via hermesc), this loads and runs it and sets *result to the
 * completion value of the module — for a `require`d module that is the wrapper
 * function `(function(module, exports, require, __filename, __dirname){...})`,
 * mirroring exactly what js_execute_script returns for the equivalent source.
 *
 * Returns:
 *   - napi_ok               : `file` was bytecode; it ran; *result is set.
 *   - napi_cannot_run_js    : `file` is NOT bytecode for this engine (the caller
 *                             should fall back to compiling the source). Engines
 *                             without a compile-time bytecode story always
 *                             return this without touching the filesystem.
 *   - napi_pending_exception/other : `file` was bytecode but failed to load or
 *                             threw while executing (surfaced as an error).
 */
napi_status js_run_bytecode_file(napi_env env, const char *file, const char *source_url, napi_value *result);

napi_status js_get_runtime_version(napi_env env, napi_value* version);

#endif //TEST_APP_JSR_COMMON_H

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
 * Compile-time bytecode fast path. The runtime calls this BEFORE reading a
 * module's source, so a precompiled module is never read/compiled as text.
 *
 * `file` is the module's source URL (e.g. "file:///.../app/foo.js"). If it holds
 * precompiled bytecode for this engine, load + run it and set *result to the
 * module wrapper function (mirroring js_execute_script for the equivalent source).
 *
 * The bytecode-loading implementation lives entirely in each engine's jsr.cpp;
 * this is just the thin entry point the module loader calls.
 *
 * Returns:
 *   - napi_ok            : `file` was bytecode; it ran; *result is set.
 *   - napi_cannot_run_js : `file` is NOT bytecode (caller compiles source).
 *                          Detection only peeks the header — no full read — and
 *                          engines without bytecode support always return this.
 *   - other              : `file` was bytecode but failed/threw (do NOT fall back).
 */
napi_status js_run_bytecode_file(napi_env env, const char *file, napi_value *result);

napi_status js_get_runtime_version(napi_env env, napi_value* version);

#endif //TEST_APP_JSR_COMMON_H

#include "jsr.h"
#include "quicks-runtime.h"
#include "bytecode_container.h"
#include "File.h"
#include "NativeScriptAssert.h"

JSR::JSR() = default;
tns::SimpleMap<napi_env, JSR *> JSR::env_to_jsr_cache;

// Engine-agnostic runtime handle for the jsr layer. QuickJS keeps its own
// napi_runtime (the real engine runtime defined in quickjs-api.c); this wrapper
// just points at it so the jsr API can speak jsr_ns_runtime while
// quickjs-api.c / quicks-runtime.h stay unchanged.
struct jsr_ns_runtime__ {
    napi_runtime rt;
};

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    if (!runtime) return napi_invalid_arg;
    auto *wrapper = new jsr_ns_runtime__();
    napi_status status = qjs_create_runtime(&wrapper->rt);
    if (status != napi_ok) {
        delete wrapper;
        return status;
    }
    *runtime = wrapper;
    return napi_ok;
}
napi_status js_create_napi_env(napi_env *env, jsr_ns_runtime runtime) {
    napi_status status = qjs_create_napi_env(env, runtime->rt);
    JSR::env_to_jsr_cache.Insert((*env), new JSR());
    return status;
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
    return qjs_free_napi_env(env);
}

napi_status js_free_runtime(jsr_ns_runtime runtime) {
    napi_status status = qjs_free_runtime(runtime->rt);
    delete runtime;
    return status;
}

#ifdef __QUICKJS_NG__
static const char *kBytecodeMagic = "NSBCNGS";  // 7 chars + NUL = 8-byte magic
static const char *kEngineName = "QuickJS-NG";
#else
static const char *kBytecodeMagic = "NSBCQJS";
static const char *kEngineName = "QuickJS";
#endif

napi_status js_run_bytecode_file(napi_env env, const char *file, napi_value *result) {
    std::string path;
    if (!nsbc::ResolvePath(file, path)) {
        DEBUG_WRITE("[bytecode] Unable to resolve file: %s", path.c_str());
        return napi_cannot_run_js;
    }
    if (!nsbc::HasMagic(path, kBytecodeMagic)) {
        DEBUG_WRITE("[bytecode] Unable to find %s header: %s", kEngineName, path.c_str());
        return napi_cannot_run_js;
    }

    int length = 0;
    auto data = tns::File::ReadBinary(path, length);
    if (!data) return napi_cannot_run_js;
    if (static_cast<size_t>(length) <= nsbc::kHeaderLen) {
        delete[] static_cast<uint8_t *>(data);
        return napi_cannot_run_js;
    }

    DEBUG_WRITE("[bytecode] loading %s bytecode: %s (%d bytes)", kEngineName, file, length);

    // JS_ReadObject copies what it needs, so the buffer can be freed after.
    napi_status status = qjs_run_bytecode(
        env, static_cast<const uint8_t *>(data) + nsbc::kHeaderLen,
        static_cast<size_t>(length) - nsbc::kHeaderLen, file, result);
    delete[] static_cast<uint8_t *>(data);
    return status;
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    DEBUG_WRITE("[script] loading script: %s", file);
    return qjs_execute_script(env, script, file, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    return qjs_execute_pending_jobs(env);
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


napi_status js_get_runtime_version(napi_env env, napi_value *version) {
    napi_create_string_utf8(env, "QuickJS", NAPI_AUTO_LENGTH, version);

    return napi_ok;
}

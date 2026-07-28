#include "napi_env_quickjs.h"
#include "napi_env.h"
#include "jsr.h"
#include "bytecode_container.h"
#include "File.h"
#include "NativeScriptAssert.h"
#include <string>

JSR::JSR() = default;
tns::SimpleMap<napi_env, JSR *> JSR::env_to_jsr_cache;

struct jsr_ns_runtime__ {
    LEPUSRuntime* runtime;
    LEPUSContext* context;
};

static LEPUSValue gc_function(LEPUSContext *ctx, LEPUSValueConst this_val, int argc, LEPUSValueConst *argv) {
    LEPUSRuntime *rt = LEPUS_GetRuntime(ctx);
    LEPUS_RunGC(rt);
    return LEPUS_UNDEFINED;
}

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    auto _runtime = new jsr_ns_runtime__();
    LEPUSRuntime* rt = LEPUS_NewRuntimeWithMode(0);
    LEPUS_SetRuntimeInfo(rt, "Lynx_LepusNG");
    _runtime->context = LEPUS_NewContext(rt);
    LEPUS_SetMaxStackSize(_runtime->context, 1024 * 1024 * 1024);
    _runtime->runtime = rt;
    *runtime = _runtime;

    DEBUG_WRITE_FORCE("[primjs] GC mode: %d, template interpreter (use_primjs): %d",
                      LEPUS_IsGCModeRT(rt) ? 1 : 0,
                      LEPUS_IsPrimjsEnabled(rt) ? 1 : 0);

    LEPUSValue global_obj = LEPUS_GetGlobalObject(_runtime->context);
    LEPUSValue gc_func = LEPUS_NewCFunction(_runtime->context, gc_function, "gc", 0);
    LEPUS_SetPropertyStr(_runtime->context, global_obj, "gc", gc_func);
//    LEPUS_FreeValue(_runtime->context, gc_func);
//    LEPUS_FreeValue(_runtime->context, global_obj);

    return napi_ok;
}
napi_status js_create_napi_env(napi_env *env, jsr_ns_runtime runtime) {
    *env = napi_new_env();
    napi_attach_quickjs((*env), runtime->context);
    JSR::env_to_jsr_cache.Insert((*env), new JSR());
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

static const char *kBytecodeMagic = "NSBCPJS";  // 7 chars + NUL = 8-byte magic

napi_status js_run_bytecode_file(napi_env env, const char *file, napi_value *result) {
    std::string path;
    if (!nsbc::ResolvePath(file, path)) {
        DEBUG_WRITE("[bytecode] Unable to resolve file: %s", path.c_str());
        return napi_cannot_run_js;
    }
    if (!nsbc::HasMagic(path, kBytecodeMagic)) {
        DEBUG_WRITE("[bytecode] Unable to find PrimJS header: %s", path.c_str());
        return napi_cannot_run_js;
    }

    int length = 0;
    auto data = tns::File::ReadBinary(path, length);
    if (!data) return napi_cannot_run_js;
    if (static_cast<size_t>(length) <= nsbc::kHeaderLen) {
        delete[] static_cast<uint8_t *>(data);
        return napi_cannot_run_js;
    }

    DEBUG_WRITE("[bytecode] loading PrimJS bytecode: %s (%d bytes)", file, length);

    LEPUSContext *ctx = napi_get_env_context_quickjs(env);
    // LEPUS_ReadObject copies what it needs, so the buffer can be freed after.
    LEPUSValue fun_obj = LEPUS_ReadObject(
        ctx, static_cast<const uint8_t *>(data) + nsbc::kHeaderLen,
        static_cast<size_t>(length) - nsbc::kHeaderLen, LEPUS_READ_OBJ_BYTECODE);
    delete[] static_cast<uint8_t *>(data);

    // Errors return napi_pending_exception (not napi_cannot_run_js): the file IS
    // bytecode, so surface the error instead of falling back to source. The
    // exception stays pending in the context so napi_is_exception_pending picks
    // it up at the call site.
    if (LEPUS_IsException(fun_obj)) {
        return napi_pending_exception;
    }

    // LEPUS_EvalFunction consumes fun_obj; the completion value is the module
    // wrapper function.
    LEPUSValue eval_result = LEPUS_EvalFunction(ctx, fun_obj, LEPUS_UNDEFINED);
    if (LEPUS_IsException(eval_result)) {
        return napi_pending_exception;
    }

    // napi_quickjs_value_to_js_value takes ownership of eval_result.
    *result = napi_quickjs_value_to_js_value(env, eval_result);
    return napi_ok;
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    DEBUG_WRITE("[script] loading script: %s", file);

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


napi_status js_get_runtime_version(napi_env env, napi_value *version) {
    napi_create_string_utf8(env, "PrimJS", NAPI_AUTO_LENGTH, version);
    return napi_ok;
}

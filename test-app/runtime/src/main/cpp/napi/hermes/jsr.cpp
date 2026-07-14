#include "jsr.h"
#include "File.h"
#include <cstdio>
#include <cstring>

std::unordered_map<napi_env, JSR *> JSR::env_to_jsr_cache;

typedef struct jsr_ns_runtime__ {
    JSR *hermes;
} jsr_ns_runtime__;

JSR::JSR() {
    hermes::vm::RuntimeConfig config =
        hermes::vm::RuntimeConfig::Builder()
            .withMicrotaskQueue(true)
            .withES6BlockScoping(true)
            .withEnableAsyncGenerators(true)
            .withAsyncBreakCheckInEval(true)
            .build();

    threadSafeRuntime = facebook::hermes::makeThreadSafeHermesRuntime(config);
    rt = (facebook::hermes::HermesRuntime *) &threadSafeRuntime->getUnsafeRuntime();
}

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    if (runtime == nullptr) return napi_invalid_arg;
    *runtime = new jsr_ns_runtime__();
    (*runtime)->hermes = new JSR();

    return napi_ok;
}

napi_status js_lock_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }
    itFound->second->lock();

    return napi_ok;
}

napi_status js_unlock_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }
    itFound->second->unlock();

    return napi_ok;
}

napi_status js_create_napi_env(napi_env *env, jsr_ns_runtime runtime) {
    if (env == nullptr) return napi_invalid_arg;

    // Extract the underlying hermes::vm::Runtime from the JSI HermesRuntime via
    // the IHermes interface, then create the Node-API env on top of it. This is
    // the same path Hermes' own tools (repl, test-runner, napi-runner) use and
    // relies only on symbols exported by libhermesvm.so.
    void *vmRuntime =
        facebook::jsi::castInterface<facebook::hermes::IHermes>(runtime->hermes->rt)
            ->getVMRuntimeUnsafe();
    *env = hermes_napi_create_env(vmRuntime);

    JSR::env_to_jsr_cache.insert(std::make_pair(*env, runtime->hermes));
    return napi_ok;
}

napi_status js_set_runtime_flags(const char *flags) {
    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    // The env is owned by the hermes::vm::Runtime and is torn down when the
    // Runtime is destroyed (see js_free_runtime); we only drop our cache entry.
    JSR::env_to_jsr_cache.erase(env);
    return napi_ok;
}

napi_status js_free_runtime(jsr_ns_runtime runtime) {
    if (runtime == nullptr) return napi_invalid_arg;
    runtime->hermes->threadSafeRuntime.reset();
    runtime->hermes->rt = nullptr;
    delete runtime->hermes;
    delete runtime;

    return napi_ok;
}


napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    // Pull the UTF-8 source out of the napi string value and compile+run it via
    // the Hermes NAPI entry point so we can attach the source URL for stack
    // traces.
    size_t len = 0;
    napi_status status = napi_get_value_string_utf8(env, script, nullptr, 0, &len);
    if (status != napi_ok) return status;

    uint8_t *source = new uint8_t[len + 1];
    status = napi_get_value_string_utf8(env, script, reinterpret_cast<char *>(source),
                                        len + 1, &len);
    if (status != napi_ok) {
        delete[] source;
        return status;
    }

    hermes_run_script_flags flags{};
    flags.struct_size = sizeof(flags);
    // Pass size = len + 1 so the trailing '\0' lets Hermes run the source
    // zero-copy. Hermes takes ownership of the buffer and frees it via the
    // finalizer below.
    return hermes_run_script(
        env, source, len + 1,
        [](const uint8_t *data, size_t, void *) { delete[] const_cast<uint8_t *>(data); },
        nullptr, file, &flags, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }
    itFound->second->rt->drainMicrotasks();
    return napi_ok;
}

napi_status js_get_engine_ptr(napi_env env, int64_t *engine_ptr) {
    return napi_ok;
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
    int length = 0;
    // tns::File::ReadBinary allocates with new uint8_t[length].
    auto data = tns::File::ReadBinary(file, length);
    if (!data) {
        return napi_cannot_run_js;
    }

    hermes_bytecode_flags flags{};
    flags.struct_size = sizeof(flags);
    // Hermes takes ownership of the buffer and frees it via the finalizer.
    return hermes_run_bytecode(
        env, static_cast<const uint8_t *>(data), static_cast<size_t>(length),
        [](const uint8_t *d, size_t, void *) { delete[] const_cast<uint8_t *>(d); },
        nullptr, file, &flags, result);
}

// Magic that prefixes every Hermes bytecode (HBC) file. Stored little-endian as
// the first 8 bytes; see hermes BytecodeFileFormat.h (MAGIC).
static constexpr uint64_t HERMES_BYTECODE_MAGIC = 0x1F1903C103BC1FC6ull;

napi_status js_run_bytecode_file(napi_env env, const char *file, const char *source_url,
                                 napi_value *result) {
    // Cheaply peek the header first so that when a module is plain source (e.g.
    // bytecode generation was disabled for this build) we don't read the whole
    // — potentially multi-MB — file just to reject it; the source path re-reads
    // it as text.
    uint8_t header[sizeof(HERMES_BYTECODE_MAGIC)];
    FILE *fp = fopen(file, "rb");
    if (!fp) {
        return napi_cannot_run_js;
    }
    size_t bytesRead = fread(header, 1, sizeof(header), fp);
    fclose(fp);
    if (bytesRead < sizeof(header) ||
        memcmp(header, &HERMES_BYTECODE_MAGIC, sizeof(header)) != 0) {
        return napi_cannot_run_js;
    }

    int length = 0;
    // tns::File::ReadBinary allocates with new uint8_t[length].
    auto data = tns::File::ReadBinary(file, length);
    if (!data) {
        return napi_cannot_run_js;
    }

    hermes_bytecode_flags flags{};
    flags.struct_size = sizeof(flags);
    // App modules live for the whole runtime lifetime, so keep the bytecode
    // resident and let Hermes reference it zero-copy for faster loads.
    flags.persistent = true;
    // Hermes takes ownership of the buffer and frees it via the finalizer.
    return hermes_run_bytecode(
        env, static_cast<const uint8_t *>(data), static_cast<size_t>(length),
        [](const uint8_t *d, size_t, void *) { delete[] const_cast<uint8_t *>(d); },
        nullptr, source_url, &flags, result);
}

napi_status js_get_runtime_version(napi_env env, napi_value *version) {
    napi_create_string_utf8(env, "Hermes", NAPI_AUTO_LENGTH, version);
    return napi_ok;
}

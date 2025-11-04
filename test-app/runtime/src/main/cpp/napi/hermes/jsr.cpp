#include "jsr.h"
#include "js_runtime.h"
#include "File.h"

using namespace facebook::jsi;
std::unordered_map<napi_env, JSR *> JSR::env_to_jsr_cache;

typedef struct napi_runtime__ {
    JSR *hermes;
} napi_runtime__;


#ifdef __SHERMES__
class TaskRunner : public ::hermes::node_api::TaskRunner {
public:
    void post(std::unique_ptr<::hermes::node_api::Task> task) noexcept override {
        printf("%s", "HERMES NAPI CALLBACK POSTED");
    }
};
#endif

JSR::JSR() {
    #ifdef __SHERMES__
        hermes::vm::RuntimeConfig config =
            hermes::vm::RuntimeConfig::Builder().withMicrotaskQueue(true).withES6BlockScoping(true).withEnableAsyncGenerators(true).build();
    #else
         hermes::vm::RuntimeConfig config =
            hermes::vm::RuntimeConfig::Builder().withMicrotaskQueue(true).build();
    #endif
    
    threadSafeRuntime = facebook::hermes::makeThreadSafeHermesRuntime(config);
    rt = (facebook::hermes::HermesRuntime *) &threadSafeRuntime->getUnsafeRuntime();
}

napi_status js_create_runtime(napi_runtime *runtime) {
    if (runtime == nullptr) return napi_invalid_arg;
    *runtime = new napi_runtime__();
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

napi_status js_create_napi_env(napi_env *env, napi_runtime runtime) {
    if (env == nullptr) return napi_invalid_arg;
    #ifdef __SHERMES__
    *env = ::hermes::node_api::createNodeApiEnv(runtime->hermes->rt->getVMRuntimeUnsafe(), std::make_shared<TaskRunner>(), [](napi_env env, napi_value value) {}, 8);
    #else
    runtime->hermes->rt->createNapiEnv(env);
    #endif
    JSR::env_to_jsr_cache.insert(std::make_pair(*env, runtime->hermes));
    return napi_ok;
}

napi_status js_set_runtime_flags(const char *flags) {
    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    JSR::env_to_jsr_cache.erase(env);
    return napi_ok;
}

napi_status js_free_runtime(napi_runtime runtime) {
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
    #ifdef __SHERMES__
    return napi_run_script_source(env, script, file, result);;
    #else
    return jsr_run_script(env, script, file, result);
    #endif
}

napi_status js_execute_pending_jobs(napi_env env) {
    #ifdef __SHERMES__
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }
    itFound->second->rt->drainMicrotasks();
    return napi_ok;
    #else
     bool result;
     return jsr_drain_microtasks(env, 0, &result);
    #endif
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
    auto data = tns::File::ReadBinary(file, length);
    if (!data) {
        return napi_cannot_run_js;
    }

    return napi_run_bytecode(env, data, length, file, result);
}

napi_status js_get_runtime_version(napi_env env, napi_value *version) {
    napi_create_string_utf8(env, "Hermes", NAPI_AUTO_LENGTH, version);
    return napi_ok;
}


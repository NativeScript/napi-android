#include "jsr.h"
#include "js_native_api.h"

typedef struct NapiRuntime {
    JSR* hermes;
} NapiRuntime;

JSR::JSR() {
    hermes::vm::RuntimeConfig config =
            hermes::vm::RuntimeConfig::Builder().withMicrotaskQueue(true).build();
    threadSafeRuntime = facebook::hermes::makeThreadSafeHermesRuntime(config);
    rt = (facebook::hermes::HermesRuntime *)&threadSafeRuntime->getUnsafeRuntime();
}

napi_status js_create_runtime(napi_runtime* runtime) {
    if (runtime == nullptr) return napi_invalid_arg;

    *runtime = static_cast<napi_runtime>(malloc(sizeof(NapiRuntime)));

    (*runtime)->hermes = new JSR();

    return napi_ok;
}

napi_status jsr_lock_runtime(napi_runtime runtime) {
    runtime->hermes->threadSafeRuntime->lock();
    return napi_ok;
}

napi_status jsr_unlock_runtime(napi_runtime runtime) {
    runtime->hermes->threadSafeRuntime->unlock();
    return napi_ok;
}

napi_status js_create_napi_env(napi_env* env, napi_runtime runtime) {
    if (env == nullptr) return napi_invalid_arg;
    return runtime->hermes->rt->createNapiEnv( env);
}

napi_status js_free_napi_env(napi_env env) {
//    return jsr_env_unref(env);
      return napi_ok;
}

napi_status js_free_runtime(napi_runtime runtime) {
    if (runtime == nullptr) return napi_invalid_arg;
    runtime->hermes->threadSafeRuntime.reset();
    runtime->hermes->rt = nullptr;
    delete runtime->hermes;

    free(runtime);

    return napi_ok;
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    return jsr_run_script(env, script, file, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    bool result;
    return jsr_drain_microtasks(env, 0, &result);
}

//
// Created by Ammar Ahmed on 16/11/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include <memory>
#include <mutex>
#include <unordered_map>

// hermes.h transitively provides everything we need on the runtime side:
//   - facebook::hermes::makeThreadSafeHermesRuntime / HermesRuntime
//   - facebook::hermes::IHermes (via <jsi/hermes-interfaces.h>)
//   - facebook::jsi::castInterface (via <jsi/jsi.h>)
//   - hermes::vm::RuntimeConfig (via <hermes/Public/RuntimeConfig.h>)
#include "hermes/hermes.h"
#include "jsi/threadsafe.h"

// Node-API surface exported by libhermesvm.so: hermes_napi_create_env,
// hermes_run_script, hermes_run_bytecode plus the standard napi_* functions.
#include "napi/hermes_napi.h"

#include "jsr_common.h"

class JSR {
public:
    JSR();
    std::unique_ptr<facebook::jsi::ThreadSafeRuntime> threadSafeRuntime;
    facebook::hermes::HermesRuntime* rt;
    std::recursive_mutex js_mutex;
    void lock() {
        threadSafeRuntime->lock();
        js_mutex.lock();
    }
    void unlock() {
        threadSafeRuntime->unlock();
        js_mutex.unlock();
    }

    static std::unordered_map<napi_env, JSR *> env_to_jsr_cache;
};

class NapiScope {
public:
    explicit NapiScope(napi_env env, bool openHandle = true)
            : env_(env)
    {
        js_lock_env(env_);
        if (openHandle) {
            napi_open_handle_scope(env_, &napiHandleScope_);
        } else {
            napiHandleScope_ = nullptr;
        }
    }

    ~NapiScope() {
        if (napiHandleScope_) {
            napi_close_handle_scope(env_, napiHandleScope_);
        }
        js_unlock_env(env_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
};

#define JSEnterScope

#endif //TEST_APP_JSR_H

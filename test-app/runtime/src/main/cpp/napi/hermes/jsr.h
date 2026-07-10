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
    // Depth of nested JS scopes entered from the host (see NapiScope). Hermes is
    // configured with an explicit microtask queue, so promise jobs only run when
    // we drain them; we drain once this returns to 0, i.e. when the native call
    // stack has fully unwound back out of JS.
    int jsEnterState = 0;
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
        auto it = JSR::env_to_jsr_cache.find(env_);
        jsr_ = it != JSR::env_to_jsr_cache.end() ? it->second : nullptr;
        if (jsr_) {
            jsr_->jsEnterState++;
        }
        if (openHandle) {
            napi_open_handle_scope(env_, &napiHandleScope_);
        } else {
            napiHandleScope_ = nullptr;
        }
    }

    ~NapiScope() {
        // Drain the microtask queue only when the outermost JS scope unwinds so
        // that promise continuations (async/await) run — mirroring how a JS
        // engine empties its job queue once control returns to the host. Draining
        // at a nested depth would run continuations while JS is still on the
        // stack. A throwing microtask must never escape a destructor.
        if (jsr_ && --jsr_->jsEnterState <= 0) {
            jsr_->jsEnterState = 0;
            try {
                js_execute_pending_jobs(env_);
            } catch (...) {
            }
        }
        if (napiHandleScope_) {
            napi_close_handle_scope(env_, napiHandleScope_);
        }
        js_unlock_env(env_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
    JSR* jsr_ = nullptr;
};

#define JSEnterScope

#endif //TEST_APP_JSR_H

//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include "jsr_common.h"
#include "jsc-api.h"

class NapiScope {
public:
    explicit NapiScope(napi_env env, bool openHandle = true)
            : env_(env)
    {
        // Serialize this host->JS entry against all other threads (background-thread
        // JNI callbacks, timers, workers). The lock is a per-env recursive mutex, so
        // a nested NapiScope on the same thread re-enters rather than deadlocking.
        // JSC drains its own microtask queue when the outermost API call returns, so
        // unlike QuickJS we don't drain jobs here.
        js_lock_env(env_);
//        napi_open_handle_scope(env_, &napiHandleScope_);
    }

    ~NapiScope() {
//        napi_close_handle_scope(env_, napiHandleScope_);
        js_unlock_env(env_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
};

#define JSEnterScope

#endif //TEST_APP_JSR_H

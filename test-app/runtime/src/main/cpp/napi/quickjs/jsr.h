//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H
#include "js_native_api.h"
#include "jsr_common.h"
#include "mutex"

class JSR {
public:
    JSR();
    std::recursive_mutex js_mutex;
    void lock() {
        js_mutex.lock();
    }
    void unlock() {
        js_mutex.unlock();
    }

    static std::unordered_map<napi_env, JSR *> env_to_jsr_cache;
};

class NapiScope {
public:
    explicit NapiScope(napi_env env)
            : env_(env)
    {
        js_lock_env(env_);
        qjs_update_stack_top(env);
        napi_open_handle_scope(env_, &napiHandleScope_);
    }

    ~NapiScope() {
        js_unlock_env(env_);
        napi_close_handle_scope(env_, napiHandleScope_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
};

#define JSEnterScope

#endif //TEST_APP_JSR_H

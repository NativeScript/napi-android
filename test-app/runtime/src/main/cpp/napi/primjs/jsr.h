//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H
#include "jsr_common.h"
#include "napi_env_quickjs.h"
#include "mutex"
#include <map>
#include "ConcurrentMap.h"

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

    static tns::SimpleMap<napi_env, JSR *> env_to_jsr_cache;
};

class NapiScope {
public:
    explicit NapiScope(napi_env env, bool open_handle = true)
            : env_(env)
    {
        js_lock_env(env_);
        // TODO: UPDATE STACK TOP HERE
        if (open_handle) {
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

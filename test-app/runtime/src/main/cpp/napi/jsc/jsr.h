//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSR_H
#define TEST_APP_JSR_H

#include "jsr_common.h"
#include "jsc-api.h"

typedef struct NapiRuntime *napi_runtime;

class NapiScope {
public:
    explicit NapiScope(napi_env env)
            : env_(env)
    {
        napi_open_handle_scope(env_, &napiHandleScope_);
    }

    ~NapiScope() {
        napi_close_handle_scope(env_, napiHandleScope_);
    }

private:
    napi_env env_;
    napi_handle_scope napiHandleScope_;
};

#define JSEnterScope

#endif //TEST_APP_JSR_H

//
// Created by Ammar Ahmed on 01/03/2025.
//

#ifndef TEST_APP_RUNTIMEMODULES_H
#define TEST_APP_RUNTIMEMODULES_H

#include "js_native_api.h"
#include "URL.h"
#include "URLSearchParams.h"

namespace tns {
    class NSRuntimeModules {
    public:
        static void Init(napi_env env) {
            URL::Init(env);
            URLSearchParams::Init(env);
        }
    };
}

#endif //TEST_APP_RUNTIMEMODULES_H

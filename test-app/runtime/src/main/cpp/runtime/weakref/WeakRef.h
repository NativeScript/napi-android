//
// Created by Ammar Ahmed on 03/12/2024.
//

#ifndef TEST_APP_WEAKREF_H
#define TEST_APP_WEAKREF_H

#include <js_native_api.h>

namespace tns {
    class WeakRef {
    public:
        static void Init(napi_env env);
        static napi_value New(napi_env env, napi_callback_info info);

    private:
        explicit WeakRef(napi_env env, napi_value value);
        ~WeakRef();

        napi_env env_;
        napi_ref ref_;

        static napi_value Get(napi_env env, napi_callback_info info);
        static napi_value Deref(napi_env env, napi_callback_info info);
    };

}
#endif //TEST_APP_WEAKREF_H

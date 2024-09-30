//
// Created by Ammar Ahmed on 21/09/2024.
//

#ifndef TESTAPPNAPI_PERFORMANCE_H
#define TESTAPPNAPI_PERFORMANCE_H
#include <chrono>
#include "js_native_api.h"

napi_value Now(napi_env env, napi_callback_info info) {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    napi_value result;
    napi_create_double(env, static_cast<double>(ms), &result);
    return result;
}

namespace ns {

    class Performance {
    public:
        static void createPerformance(napi_env env, napi_value global) {
            napi_value performance;
            napi_create_object(env, &performance);
            napi_util::napi_set_function(env, performance, "now", Now);
            napi_set_named_property(env, global, "performance", performance);
        }
    };

} // ns

#endif //TESTAPPNAPI_PERFORMANCE_H

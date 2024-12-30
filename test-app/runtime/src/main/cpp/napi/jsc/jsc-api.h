//
// Created by Ammar Ahmed on 01/12/2024.
//

#ifndef TEST_APP_JSC_API_H
#define TEST_APP_JSC_API_H

#include "js_native_api.h"
#include "js_native_api_types.h"
#include <JavaScriptCore/JavaScript.h>
#include <unordered_set>
#include <list>
#include <thread>
#include <cassert>

struct NapiEnvironment {
    JSGlobalContextRef context{};
    JSValueRef last_exception{};
    napi_extended_error_info last_error{nullptr, nullptr, 0, napi_ok};
    std::unordered_set<napi_value> active_ref_values{};
    std::list<napi_ref> strong_refs{};

    const std::thread::id thread_id{std::this_thread::get_id()};

    NapiEnvironment(JSGlobalContextRef context) : context{context} {
        JSGlobalContextRetain(context);
    }

    ~NapiEnvironment() {
        deinit_refs();
        JSGlobalContextRelease(context);
    }

private:
    void deinit_refs();
};

#define RETURN_STATUS_IF_FALSE(env, condition, status) \
  do {                                                 \
    if (!(condition)) {                                \
      return napi_set_last_error((env), (status));     \
    }                                                  \
  } while (0)

#define CHECK_ENV(env)                                    \
  do {                                                    \
    if ((env) == nullptr) {                               \
      return napi_invalid_arg;                            \
    }                                                     \
  } while (0)

#define CHECK_ARG(env, arg) \
  RETURN_STATUS_IF_FALSE((env), ((arg) != nullptr), napi_invalid_arg)

#define CHECK_JSC(env, exception)                \
  do {                                           \
    if ((exception) != nullptr) {                \
      return napi_set_exception(env, exception); \
    }                                            \
  } while (0)

// This does not call napi_set_last_error because the expression
// is assumed to be a NAPI function call that already did.
#define CHECK_NAPI(expr)                  \
  do {                                    \
    napi_status status = (expr);          \
    if (status != napi_ok) return status; \
  } while (0)

#endif //TEST_APP_JSC_API_H

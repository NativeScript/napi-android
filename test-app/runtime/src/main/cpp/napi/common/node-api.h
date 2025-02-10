//
// Created by Ammar Ahmed on 09/02/2025.
//

#ifndef TEST_APP_NODE_API_H
#define TEST_APP_NODE_API_H

#include "jsr_common.h"
#include "native_api_util.h"

#define NAPI_MODULE_EXPORT __attribute__((visibility("default")))
#define NAPI_MODULE_INITIALIZER_BASE napi_register_module_v
#define NODE_API_MODULE_GET_API_VERSION_BASE node_api_module_get_api_version_v

#define NAPI_MODULE_INITIALIZER                                                \
  NAPI_MODULE_INITIALIZER_X(NAPI_MODULE_INITIALIZER_BASE, NAPI_MODULE_VERSION)

#define NODE_API_MODULE_GET_API_VERSION                                        \
  NAPI_MODULE_INITIALIZER_X(NODE_API_MODULE_GET_API_VERSION_BASE,              \
                            NAPI_MODULE_VERSION)

#define NAPI_MODULE_INITIALIZER_X(base, version)                               \
  NAPI_MODULE_INITIALIZER_X_HELPER(base, version)
#define NAPI_MODULE_INITIALIZER_X_HELPER(base, version) base##version


#define NAPI_MODULE_INIT()                                                     \
  EXTERN_C_START                                                               \
  NAPI_MODULE_EXPORT int32_t NODE_API_MODULE_GET_API_VERSION(void) {           \
    return NAPI_VERSION;                                                       \
  }                                                                            \
  NAPI_MODULE_EXPORT napi_value NAPI_MODULE_INITIALIZER(napi_env env,          \
                                                        napi_value exports);   \
  EXTERN_C_END                                                                 \
  napi_value NAPI_MODULE_INITIALIZER(napi_env env, napi_value exports)

#define NAPI_MODULE(modname, regfunc)                                          \
  NAPI_MODULE_INIT() { return regfunc(env, exports); }


#endif //TEST_APP_NODE_API_H

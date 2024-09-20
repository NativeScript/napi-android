/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOUNDATION_ACE_NAPI_NATIVE_ENGINE_NATIVE_API_INTERNAL_H
#define FOUNDATION_ACE_NAPI_NATIVE_ENGINE_NATIVE_API_INTERNAL_H

#include "native_engine.h"

#include "native_api.h"

static inline napi_status napi_clear_last_error(napi_env env)
{
    ((NativeEngine*)env)->ClearLastError();
    return napi_ok;
}

static inline napi_status napi_set_last_error(napi_env env,
                                              napi_status error_code,
                                              uint32_t engine_error_code = 0,
                                              void* engine_reserved = nullptr)
{
    ((NativeEngine*)env)->SetLastError(error_code, engine_error_code, engine_reserved);
    return error_code;
}

#define RETURN_STATUS_IF_FALSE(env, condition, status) \
    if (!(condition)) {                                \
        return napi_set_last_error((env), (status));   \
    }

#define CHECK_ENV(env)           \
    if ((env) == nullptr) {      \
        return napi_invalid_arg; \
    }

#define CHECK_ARG(env, arg) RETURN_STATUS_IF_FALSE((env), ((arg) != nullptr), napi_invalid_arg)

#endif /* FOUNDATION_ACE_NAPI_NATIVE_ENGINE_NATIVE_API_INTERNAL_H */

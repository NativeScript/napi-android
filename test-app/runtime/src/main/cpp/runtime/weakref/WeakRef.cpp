//
// Created by Ammar Ahmed on 03/12/2024.
//

#include "WeakRef.h"
#include "native_api_util.h"

using namespace tns;

WeakRef::WeakRef(napi_env env, napi_value value) : env_(env), ref_(nullptr) {
    napi_status status;
    NAPI_GUARD(napi_create_reference(env, value, 1, &ref_)) {}
}

WeakRef::~WeakRef() {
    if (ref_ != nullptr) {
        napi_status status;
        NAPI_GUARD(napi_delete_reference(env_, ref_)) {}
    }
}

void WeakRef::Init(napi_env env) {
    napi_status status;
    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }
    napi_property_descriptor properties[] = {
            { "get", 0, Deref, 0, 0, 0, napi_default, 0 },
            { "deref", 0, Deref, 0, 0, 0, napi_default, 0 }
    };

    napi_value wr;
    NAPI_GUARD(napi_get_named_property(env, global, "WeakRef", &wr)) {
        return;
    }
   if (napi_util::is_null_or_undefined(env, wr)) {
       napi_value cons;
       NAPI_GUARD(napi_define_class(env, "WeakRef", NAPI_AUTO_LENGTH, New, nullptr, 2, properties, &cons)) {
           return;
       }
       NAPI_GUARD(napi_set_named_property(env, global, "WeakRef", cons)) {}
   }
}

napi_value WeakRef::New(napi_env env, napi_callback_info info) {
    napi_status status;
    napi_value target;
    // Leave a pending exception before bailing so `new WeakRef(...)` throws
    // instead of silently evaluating to undefined.
    NAPI_GUARD(napi_get_new_target(env, info, &target)) {
        napi_throw_error(env, nullptr, "Failed to construct WeakRef.");
        return nullptr;
    }
    bool is_constructor = target != nullptr;

    if (!is_constructor) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "WeakRef must be called as a constructor")) {}
        return nullptr;
    }

    size_t arg_len;
    NAPI_GUARD(napi_get_cb_info(env, info, &arg_len, nullptr, nullptr, nullptr)) {
        napi_throw_error(env, nullptr, "Failed to construct WeakRef.");
        return nullptr;
    }

    if (arg_len != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "WeakRef constructor must be called with one argument")) {}
        return nullptr;
    }

    size_t argc = 1;
    napi_value args[1];
    napi_value jsThis;
    NAPI_GUARD(napi_get_cb_info(env, info, &argc, args, &jsThis, nullptr)) {
        napi_throw_error(env, nullptr, "Failed to construct WeakRef.");
        return nullptr;
    }

    auto obj = new WeakRef(env, args[0]);
    NAPI_GUARD(napi_wrap(env, jsThis, reinterpret_cast<void*>(obj), [](napi_env env, void* data, void* hint) {
        delete reinterpret_cast<WeakRef*>(data);
    }, nullptr, nullptr)) {}

    return jsThis;
}

napi_value WeakRef::Deref(napi_env env, napi_callback_info info) {
    napi_status status;
    napi_value jsThis;
    NAPI_GUARD(napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, nullptr)) {
        return nullptr;
    }

    WeakRef* obj;
    NAPI_GUARD(napi_unwrap(env, jsThis, reinterpret_cast<void**>(&obj))) {
        return nullptr;
    }

    napi_value result;
    NAPI_GUARD(napi_get_reference_value(env, obj->ref_, &result)) {
        return nullptr;
    }

    return result;
}

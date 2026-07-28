// NativeScript-specific Node-API extensions for PrimJS.
//
// These are entry points NativeScript adds on top of what primjs' libnapi.so
// provides. They are ordinary exported C symbols (not vtable slots) and talk to
// the LEPUS engine directly. Keep engine extensions here so the adapter stays a
// pure pass-through to primjs' vtable.
//
// Currently: host objects (napi_create_host_object / napi_is_host_object /
// napi_get_host_object_data), guarded by USE_HOST_OBJECT.[

#include "primjs_napi_vtable.h"  // struct napi_env__ (+ standard napi types)
#include "quickjs.h"
#include "napi_env_quickjs.h"

#ifdef USE_HOST_OBJECT

// ---------------------------------------------------------------------------
// PrimJS host-object implementation
//
// PrimJS is a QuickJS/LEPUS fork.  We register a custom LEPUS class
// (NapiHostObject) with exotic property-trap callbacks that forward to the
// caller-supplied napi_host_object_methods.  The three public NAPI functions
// live outside the vtable — they are called directly as normal C symbols by
// the binding layer.
// ---------------------------------------------------------------------------

struct PrimJSHostObjectInfo {
    napi_env env;
    void *data;
    napi_finalize finalize_cb;
    napi_host_object_methods methods;
};

// Global class ID (process-lifetime; LEPUS_NewClassID is a monotonic counter).
static LEPUSClassID g_napiHostObjectClassId = 0;

static void lepus_host_object_finalizer(LEPUSRuntime *rt, LEPUSValue val) {
    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(val, g_napiHostObjectClassId));
    if (!info) return;
    if (info->finalize_cb) {
        info->finalize_cb(info->env, info->data, nullptr);
    }
    delete info;
}

// PrimJS keeps the napi "pending exception" in a napi_env slot that is SEPARATE
// from the LEPUS context's thrown-exception slot. libnapi's own C-function
// trampoline transfers a napi exception into the ctx before unwinding, but our
// exotic host-object traps are invoked directly by the engine and bypass that
// trampoline. So when a trap's napi callback leaves an exception pending, we must
// move it into the LEPUS ctx ourselves; otherwise returning LEPUS_EXCEPTION makes
// the engine throw the ctx's (unset -> null) exception instead of the real error
// object, and JS `catch` receives null.
//
// Tracing-GC-safe: no manual LEPUS_FreeValue. napi_js_value_to_quickjs_value
// yields an owned value whose ownership is handed to LEPUS_Throw (the engine's
// exception slot); the collector reclaims it.
static bool primjs_sync_pending_exception(napi_env env, LEPUSContext *ctx) {
    bool exc = false;
    env->napi_is_exception_pending(env, &exc);
    if (!exc) return false;
    napi_value pend = nullptr;
    env->napi_get_and_clear_last_exception(env, &pend);
    LEPUSValue ev = pend ? napi_js_value_to_quickjs_value(env, pend) : LEPUS_UNDEFINED;
    LEPUS_Throw(ctx, ev);
    return true;
}

static LEPUSValue lepus_host_object_get(LEPUSContext *ctx,
                                         LEPUSValueConst obj,
                                         JSAtom atom,
                                         LEPUSValueConst /*receiver*/) {
    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(obj, g_napiHostObjectClassId));
    if (!info || !info->methods.get) return LEPUS_UNDEFINED;
    napi_env env = info->env;

    napi_handle_scope scope;
    env->napi_open_handle_scope(env, &scope);

    napi_value host = napi_quickjs_value_to_js_value(env, LEPUS_DupValue(ctx, obj));
    napi_value prop = napi_quickjs_value_to_js_value(env, LEPUS_AtomToValue(ctx, atom));
    napi_value cb_result = info->methods.get(env, host, prop, info->data);

    LEPUSValue ret = LEPUS_UNDEFINED;
    if (cb_result != nullptr) {
        ret = napi_js_value_to_quickjs_value(env, cb_result);
    }

    env->napi_close_handle_scope(env, scope);

    if (primjs_sync_pending_exception(env, ctx)) {
        return LEPUS_EXCEPTION;
    }
    return ret;
}

static int lepus_host_object_set(LEPUSContext *ctx,
                                  LEPUSValueConst obj,
                                  JSAtom atom,
                                  LEPUSValueConst value,
                                  LEPUSValueConst /*receiver*/,
                                  int /*flags*/) {
    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(obj, g_napiHostObjectClassId));
    if (!info || !info->methods.set) return 1; // true = success
    napi_env env = info->env;

    napi_handle_scope scope;
    env->napi_open_handle_scope(env, &scope);

    napi_value host = napi_quickjs_value_to_js_value(env, LEPUS_DupValue(ctx, obj));
    napi_value prop = napi_quickjs_value_to_js_value(env, LEPUS_AtomToValue(ctx, atom));
    napi_value val  = napi_quickjs_value_to_js_value(env, LEPUS_DupValue(ctx, value));
    info->methods.set(env, host, prop, val, info->data);

    env->napi_close_handle_scope(env, scope);

    if (primjs_sync_pending_exception(env, ctx)) return -1;
    return 1;
}

static int lepus_host_object_has(LEPUSContext *ctx,
                                  LEPUSValueConst obj,
                                  JSAtom atom) {
    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(obj, g_napiHostObjectClassId));
    if (!info || !info->methods.has) return 0;
    napi_env env = info->env;

    napi_handle_scope scope;
    env->napi_open_handle_scope(env, &scope);

    napi_value host = napi_quickjs_value_to_js_value(env, LEPUS_DupValue(ctx, obj));
    napi_value prop = napi_quickjs_value_to_js_value(env, LEPUS_AtomToValue(ctx, atom));
    int present = info->methods.has(env, host, prop, info->data);

    env->napi_close_handle_scope(env, scope);

    if (primjs_sync_pending_exception(env, ctx)) return -1;
    return present;
}

static int lepus_host_object_delete(LEPUSContext *ctx,
                                     LEPUSValueConst obj,
                                     JSAtom atom) {
    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(obj, g_napiHostObjectClassId));
    if (!info || !info->methods.delete_property) return 1;
    napi_env env = info->env;

    napi_handle_scope scope;
    env->napi_open_handle_scope(env, &scope);

    napi_value host = napi_quickjs_value_to_js_value(env, LEPUS_DupValue(ctx, obj));
    napi_value prop = napi_quickjs_value_to_js_value(env, LEPUS_AtomToValue(ctx, atom));
    int deleted = info->methods.delete_property(env, host, prop, info->data);

    env->napi_close_handle_scope(env, scope);

    if (primjs_sync_pending_exception(env, ctx)) return -1;
    return deleted;
}

// Implements get_own_property_names so that Object.keys() / for..in enumerate
// the host object's properties via methods.own_keys.
static int lepus_host_object_own_property_names(LEPUSContext *ctx,
                                                 LEPUSPropertyEnum **ptab,
                                                 uint32_t *plen,
                                                 LEPUSValueConst obj) {
    *plen = 0;
    *ptab = nullptr;

    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(obj, g_napiHostObjectClassId));
    if (!info || !info->methods.own_keys) return 0;
    napi_env env = info->env;

    napi_handle_scope scope;
    env->napi_open_handle_scope(env, &scope);

    napi_value host = napi_quickjs_value_to_js_value(env, LEPUS_DupValue(ctx, obj));
    napi_value keys_array = info->methods.own_keys(env, host, info->data);

    int ret = 0;
    if (keys_array != nullptr) {
        uint32_t count = 0;
        env->napi_get_array_length(env, keys_array, &count);
        if (count > 0) {
            *ptab = static_cast<LEPUSPropertyEnum *>(
                    lepus_malloc(ctx, sizeof(LEPUSPropertyEnum) * count,
                                 ALLOC_TAG_LEPUSPropertyEnum));
            if (*ptab) {
                uint32_t added = 0;
                for (uint32_t i = 0; i < count; i++) {
                    napi_value elem = nullptr;
                    env->napi_get_element(env, keys_array, i, &elem);
                    if (!elem) continue;
                    LEPUSValue lv = napi_js_value_to_quickjs_value(env, elem);
                    JSAtom atom = LEPUS_ValueToAtom(ctx, lv);
                    if (atom == 0) continue; // 0 == invalid / JS_ATOM_NULL
                    (*ptab)[added].atom = atom;
                    (*ptab)[added].is_enumerable = 1;
                    added++;
                }
                *plen = added;
            }
        }
    }

    env->napi_close_handle_scope(env, scope);

    if (primjs_sync_pending_exception(env, ctx)) return -1;
    return ret;
}

static LEPUSClassExoticMethods kNapiHostObjectExotic = {
    /* get_own_property        */ nullptr,
    /* get_own_property_names  */ lepus_host_object_own_property_names,
    /* delete_property         */ lepus_host_object_delete,
    /* define_own_property     */ nullptr,
    /* has_property            */ lepus_host_object_has,
    /* get_property            */ lepus_host_object_get,
    /* set_property            */ lepus_host_object_set,
};

static LEPUSClassDef kNapiHostObjectClassDef = {
    /* class_name */ "NapiHostObject",
    /* finalizer  */ lepus_host_object_finalizer,
    /* gc_mark    */ nullptr,
    /* call       */ nullptr,
    /* exotic     */ &kNapiHostObjectExotic,
};

// Register the class on the given runtime if not already registered.
static void ensure_host_object_class(LEPUSContext *ctx) {
    LEPUSRuntime *rt = LEPUS_GetRuntime(ctx);
    if (g_napiHostObjectClassId == 0) {
        LEPUS_NewClassID(&g_napiHostObjectClassId);
    }
    if (!LEPUS_IsRegisteredClass(rt, g_napiHostObjectClassId)) {
        LEPUS_NewClass(rt, g_napiHostObjectClassId, &kNapiHostObjectClassDef);
    }
    LEPUS_SetClassProto(ctx, g_napiHostObjectClassId, LEPUS_NewObject(ctx));
}

napi_status napi_create_host_object(napi_env env,
                                     napi_finalize finalize_cb,
                                     void *data,
                                     const napi_host_object_methods *methods,
                                     napi_value *result) {
    if (!env) return napi_invalid_arg;
    if (!methods || !result) return napi_invalid_arg;
    if (!methods->get || !methods->set) return napi_invalid_arg;

    LEPUSContext *ctx = napi_get_env_context_quickjs(env);
    ensure_host_object_class(ctx);

    LEPUSValue obj = LEPUS_NewObjectClass(ctx, (int) g_napiHostObjectClassId);
    if (LEPUS_IsException(obj)) {
        return napi_pending_exception;
    }

    PrimJSHostObjectInfo *info = new PrimJSHostObjectInfo();
    info->env         = env;
    info->data        = data;
    info->finalize_cb = finalize_cb;
    info->methods     = *methods;
    LEPUS_SetOpaque(obj, info);

    // napi_quickjs_value_to_js_value takes ownership of obj and puts it in the
    // current handle scope.
    *result = napi_quickjs_value_to_js_value(env, obj);
    return napi_ok;
}

napi_status napi_get_host_object_data(napi_env env,
                                       napi_value object,
                                       void **data) {
    if (!env) return napi_invalid_arg;
    if (!object || !data) return napi_invalid_arg;

    // Direct cast: napi_value is LEPUSValue* in a handle scope.
    LEPUSValue lv = *((LEPUSValue *) object);
    if (!LEPUS_IsObject(lv)) return napi_object_expected;

    PrimJSHostObjectInfo *info = static_cast<PrimJSHostObjectInfo *>(
            LEPUS_GetOpaque(lv, g_napiHostObjectClassId));
    if (!info) return napi_invalid_arg;

    *data = info->data;
    return napi_ok;
}

napi_status napi_is_host_object(napi_env env,
                                 napi_value object,
                                 bool *result) {
    if (!env) return napi_invalid_arg;
    if (!object || !result) return napi_invalid_arg;

    *result = false;
    LEPUSValue lv = *((LEPUSValue *) object);
    if (LEPUS_IsObject(lv)) {
        void *opaque = LEPUS_GetOpaque(lv, g_napiHostObjectClassId);
        *result = (opaque != nullptr);
    }
    return napi_ok;
}

#endif  // USE_HOST_OBJECT

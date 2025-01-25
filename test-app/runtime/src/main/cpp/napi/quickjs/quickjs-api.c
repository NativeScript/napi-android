#include <assert.h>
#include <sys/queue.h>
#include <limits.h>
#include <quickjs.h>
#include <js_native_api.h>
#include "libbf.h"

#ifdef __ANDROID__

#include <android/log.h>

#endif

#ifdef USE_MIMALLOC

#include "mimalloc.h"

#else

#include <stdlib.h>

#define mi_malloc(size) \
    malloc(size)

#define mi_calloc(count, size)  \
    calloc(count,size)

#define mi_free(ptr) \
    free(ptr)

#define mi_realloc(ptr, size) \
    realloc(ptr, size)

#endif

#ifdef USE_MIMALLOC

static void *js_mi_calloc(void *opaque, size_t count, size_t size) {
    return mi_calloc(count, size);
}

static void *js_mi_malloc(void *opaque, size_t size) {
    return mi_malloc(size);
}

static void js_mi_free(void *opaque, void *ptr) {
    if (!ptr)
        return;
    mi_free(ptr);
}

static void *js_mi_realloc(void *opaque, void *ptr, size_t size) {
    return mi_realloc(ptr, size);
}

static const JSMallocFunctions mi_mf = {
        js_mi_calloc,
        js_mi_malloc,
        js_mi_free,
        js_mi_realloc,
        mi_malloc_usable_size
};

#endif

/**
 * --------------------------------------
 *      SAFE LIST TRANSVERSAL MACROS
 * --------------------------------------
 */

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = SLIST_FIRST((head)); (var) && ((tvar) = SLIST_NEXT((var), field), 1); (var) = (tvar))
#endif

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = LIST_FIRST((head)); (var) && ((tvar) = LIST_NEXT((var), field), 1); (var) = (tvar))
#endif

/**
 * --------------------------------------
 *         NAPI UNDEFINED AND NULL
 * --------------------------------------
 */
static JSValueConst JSUndefined = JS_UNDEFINED;
static JSValueConst JSNull = JS_NULL;

/**
 * --------------------------------------
 *         NAPI DATA STRUCTURES
 * --------------------------------------
 */

typedef enum {
    HANDLE_STACK_ALLOCATED,
    HANDLE_HEAP_ALLOCATED
} HandleType;

typedef struct Handle {
    JSValue value; // size_t * 2
    SLIST_ENTRY(Handle)
            node; // size_t

    HandleType type;
} Handle;

typedef struct NapiHandleScope {
    LIST_ENTRY(NapiHandleScope)
            node; // size_t
    SLIST_HEAD(, Handle)
            handleList; // size_t
    bool escapeCalled;
    Handle stackHandles[8];
    int handleCount;
    HandleType type;
} NapiHandleScope;

typedef struct NapiReference {
    JSValue value; // size_t * 2
    LIST_ENTRY(NapiReference)
            node;                   // size_t * 2
    uint8_t referenceCount; // 8
} NapiReference;

typedef struct NapiDeferred {
    napi_value resolve;
    napi_value reject;
} NapiDeferred;

typedef struct ExternalInfo {
    void *data;                     // size_t
    void *finalizeHint;             // size_t
    napi_finalize finalizeCallback; // size_t
} ExternalInfo;

typedef struct JsAtoms {
    JSAtom napi_external;
    JSAtom registerFinalizer;
    JSAtom constructor;
    JSAtom prototype;
    JSAtom napi_buffer;
    JSAtom NAPISymbolFor;
    JSAtom object;
    JSAtom freeze;
    JSAtom seal;
    JSAtom Symbol;
    JSAtom length;
    JSAtom is;
    JSAtom byteLength;
    JSAtom buffer;
    JSAtom byteOffset;
    JSAtom name;
    JSAtom napi_typetag;
    JSAtom weakref;
} JsAtoms;

typedef struct NapiEnvironment {
    JSValue referenceSymbolValue; // size_t * 2
    napi_runtime runtime;         // size_t
    JSContext *context;           // size_t
    LIST_HEAD(, NapiHandleScope)
            handleScopeList; // size_t
    LIST_HEAD(, NapiReference)
            referencesList; // size_t
    bool isThrowNull;
    ExternalInfo *instanceData;
    JSValue finalizationRegistry;
    napi_extended_error_info last_error;
    JsAtoms atoms;
    ExternalInfo *gcBefore;
    ExternalInfo *gcAfter;
    int js_enter_state;
} NapiEnvironment;

typedef struct NapiRuntime {
    JSRuntime *runtime;           // size_t
    JSClassID constructorClassId; // uint32_t
    JSClassID functionClassId;    // uint32_t
    JSClassID externalClassId;    // uint32_t
} NapiRuntime;

typedef struct NapiCallbackInfo {
    JSValueConst newTarget; // size_t * 2
    JSValueConst thisArg;   // size_t * 2
    JSValueConst *argv;     // size_t * 2
    void *data;             // size_t
    int argc;
} NapiCallbackInfo;

typedef struct FunctionInfo {
    void *data;             // size_t
    napi_callback callback; // size_t
} FunctionInfo;

typedef struct ExternalBufferInfo {
    void *hint;
    napi_finalize finalize_cb;
} ExternalBufferInfo;

/**
 * -------------------------------------
 *           MICROTASK HANDLING
 * -------------------------------------
 */

static inline void js_enter(napi_env env) {
    env->js_enter_state++;
}

static inline void js_exit(napi_env env) {
    if (--env->js_enter_state <= 0) {
        qjs_execute_pending_jobs(env);
    }
}

/**
 * --------------------------------------
 *       NAPI DATA FINALIZERS
 * --------------------------------------
 */

static void function_finalizer(JSRuntime *rt, JSValue val) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    FunctionInfo *functionInfo = (FunctionInfo *) JS_GetOpaque(val, env->runtime->functionClassId);
    mi_free(functionInfo);
}

static void external_finalizer(JSRuntime *rt, JSValue val) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    ExternalInfo *externalInfo = JS_GetOpaque(val, env->runtime->externalClassId);
    if (externalInfo->finalizeCallback) {
        externalInfo->finalizeCallback(env, externalInfo->data, externalInfo->finalizeHint);
    }
    mi_free(externalInfo);
}

static void buffer_finalizer(JSRuntime *rt, void *opaque, void *data) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    ExternalBufferInfo *external_buffer_info = opaque;
    if (external_buffer_info->finalize_cb) {
        external_buffer_info->finalize_cb(env, data, external_buffer_info->hint);
    }
    mi_free(external_buffer_info);
}

/**
 * -------------------------------------
 *      NAPI ERROR MANAGEMENT
 * -------------------------------------
 */

static inline napi_status napi_set_last_error(napi_env env,
                                              napi_status error_code,
                                              const char *error_message,
                                              uint32_t engine_error_code,
                                              void *engine_reserved) {
    if (error_code == napi_ok && env->last_error.error_code == napi_ok)
        return napi_ok;

    env->last_error.error_code = error_code;
    env->last_error.engine_error_code = engine_error_code || 0;
    env->last_error.engine_reserved = engine_reserved;
    env->last_error.error_message = error_message;

    return error_code;
}

static inline napi_status napi_clear_last_error(napi_env env) {
    if (env->last_error.error_code == napi_ok)
        return napi_ok;
    return napi_set_last_error(env, napi_ok, NULL, 0, NULL);
}

/**
 * --------------------------------------
 *              NAPI MACROS
 * --------------------------------------
 */

#define TRUTHY(expr) \
    __builtin_expect(expr, false)


#define RETURN_STATUS_IF_FALSE(condition, status) \
    if (__builtin_expect(!(condition), false))    \
    {                                             \
        return napi_set_last_error(env, status, NULL, 0, NULL);  \
    }


#define CHECK_NAPI(expr)                                \
    {                                                   \
        napi_status status = expr;                      \
        if (__builtin_expect(status != napi_ok, false)) \
        {                                               \
            return napi_set_last_error(env, status, NULL, 0, NULL);    \
        }                                               \
    }


#define CHECK_ARG(arg)                                     \
    if (__builtin_expect(!(arg), false))                   \
    {                                                      \
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL); \
    }

#define NAPI_PREAMBLE(env)                                                  \
    {                                                                       \
        CHECK_ARG(env)  \
JSValue exceptionValue = JS_GetException((env)->context);   \
if (__builtin_expect(JS_IsException(exceptionValue), false)) {  \
        print_exception(env, exceptionValue); \
return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);    \
}   \
RETURN_STATUS_IF_FALSE(!(env)->isThrowNull, napi_pending_exception) \
    }

#ifndef NDEBUG
static const char *const FUNCTION_CLASS_ID_ZERO = "functionClassId must not be 0.";
#endif

/**
 * --------------------------------------
 *        NAPI HANDLE SCOPES
 * --------------------------------------
 */

static inline napi_status CreateJSValueHandle(napi_env env, JSValue value, struct Handle **result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    RETURN_STATUS_IF_FALSE(!LIST_EMPTY(&env->handleScopeList), napi_handle_scope_empty)

    napi_handle_scope handleScope = LIST_FIRST(&env->handleScopeList);

    if (handleScope->handleCount < 8) {
        Handle *handle = &handleScope->stackHandles[handleScope->handleCount];
        handle->type = HANDLE_STACK_ALLOCATED;
        *result = handle;
        handleScope->handleCount++;
    } else {
        *result = (Handle *) mi_malloc(sizeof(Handle));
        (*result)->type = HANDLE_HEAP_ALLOCATED;
    }

    (*result)->value = value;
    SLIST_INSERT_HEAD(&handleScope->handleList, *result, node);

    return napi_clear_last_error(env);
}

static inline napi_status CreateScopedResult(napi_env env, JSValue value, napi_value *result) {
    struct Handle *jsValueHandle;
    napi_status status = CreateJSValueHandle(env, value, &jsValueHandle);
    if (status != napi_ok) {
        JS_FreeValue(env->context, value);
        return napi_set_last_error(env, status, NULL, 0, NULL);
    }
    *result = (napi_value) &jsValueHandle->value;
    return napi_clear_last_error(env);
}

#define NAPI_OPEN_HANDLE_SCOPE \
        NapiHandleScope handleScope; \
        handleScope.type = HANDLE_STACK_ALLOCATED; \
         handleScope.handleCount = 0;\
         handleScope.escapeCalled = false;                      \
         SLIST_INIT(&handleScope.handleList);    \
        LIST_INSERT_HEAD(&env->handleScopeList, &handleScope, node);

#define NAPI_CLOSE_HANDLE_SCOPE \
        Handle *handle, *tempHandle;    \
        SLIST_FOREACH_SAFE(handle, &handleScope.handleList, node, tempHandle) { \
        JS_FreeValue(env->context, handle->value); \
        handle->value = JSUndefined; \
        SLIST_REMOVE(&handleScope.handleList, handle, Handle, node); \
        mi_free(handle); \
        } \
        LIST_REMOVE(&handleScope, node);

napi_status napi_open_handle_scope(napi_env env, napi_handle_scope *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    NapiHandleScope *handleScope = (NapiHandleScope *) mi_malloc(sizeof(NapiHandleScope));

    RETURN_STATUS_IF_FALSE(handleScope, napi_memory_error)
    handleScope->type = HANDLE_HEAP_ALLOCATED;
    handleScope->handleCount = 0;
    handleScope->escapeCalled = false;
    *result = handleScope;
    SLIST_INIT(&(*result)->handleList);

    LIST_INSERT_HEAD(&env->handleScopeList, *result, node);

    return napi_clear_last_error(env);
}

napi_status napi_close_handle_scope(napi_env env, napi_handle_scope scope) {
    CHECK_ARG(env)
    CHECK_ARG(scope)

    assert(LIST_FIRST(&env->handleScopeList) == scope &&
           "napi_close_handle_scope() or napi_close_escapable_handle_scope() should follow FILO rule.");

    Handle *handle, *tempHandle;
    SLIST_FOREACH_SAFE(handle, &scope->handleList, node, tempHandle) {
        JS_FreeValue(env->context, handle->value);
        handle->value = JSUndefined;

        // Instead of freeing, return the handle to the pool for reuse
        SLIST_REMOVE(&scope->handleList, handle, Handle, node);
        if (handle->type == HANDLE_HEAP_ALLOCATED) {
            mi_free(handle);
        }
    }

    LIST_REMOVE(scope, node);
    mi_free(scope);

    return napi_clear_last_error(env);
}

napi_status napi_open_escapable_handle_scope(napi_env env, napi_escapable_handle_scope *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    NapiHandleScope *handleScope = (NapiHandleScope *) mi_malloc(
            sizeof(NapiHandleScope));
    handleScope->type = HANDLE_HEAP_ALLOCATED;
    handleScope->handleCount = 0;
    RETURN_STATUS_IF_FALSE(handleScope, napi_memory_error)
    SLIST_INIT(&handleScope->handleList);
    handleScope->escapeCalled = false;
    LIST_INSERT_HEAD(&env->handleScopeList, handleScope, node);

    *result = handleScope;

    return napi_clear_last_error(env);
}

napi_status
napi_close_escapable_handle_scope(napi_env env, napi_escapable_handle_scope escapableScope) {
    CHECK_ARG(env)
    CHECK_ARG(escapableScope)

    assert(LIST_FIRST(&env->handleScopeList) == escapableScope &&
           "napi_close_handle_scope() or napi_close_escapable_handle_scope() should follow FILO rule.");

    Handle *handle, *tempHandle;
    SLIST_FOREACH_SAFE(handle, &escapableScope->handleList, node, tempHandle) {
        JS_FreeValue(env->context, handle->value);
        handle->value = JSUndefined;

        // Instead of freeing, return the handle to the pool for reuse
        SLIST_REMOVE(&escapableScope->handleList, handle, Handle, node);
        if (handle->type == HANDLE_HEAP_ALLOCATED) {
            mi_free(handle);
        }
    }

    LIST_REMOVE(escapableScope, node);
    mi_free(escapableScope);

    return napi_clear_last_error(env);
}


napi_status napi_escape_handle(napi_env env, napi_escapable_handle_scope scope, napi_value escapee,
                               napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(scope)
    CHECK_ARG(escapee)

    RETURN_STATUS_IF_FALSE(!scope->escapeCalled, napi_escape_called_twice)
    // Get the outer handle scope
    napi_handle_scope handleScope = LIST_NEXT(scope, node);
    RETURN_STATUS_IF_FALSE(handleScope, napi_handle_scope_empty)

    Handle *handle = (Handle *) mi_malloc(sizeof(Handle));

    RETURN_STATUS_IF_FALSE(handle, napi_memory_error)

    scope->escapeCalled = true;
    handle->value = JS_DupValue(env->context, *((JSValue *) escapee));
    SLIST_INSERT_HEAD(&handleScope->handleList, handle, node);

    if (result != NULL) {
        *result = (napi_value) &handle->value;
    }

    return napi_clear_last_error(env);
}

/**
 * --------------------------------------
 *              EXCEPTIONS
 * --------------------------------------
 */

napi_status napi_throw(napi_env env, napi_value error) {
    CHECK_ARG(env)
    CHECK_ARG(error)

    JS_Throw(env->context, JS_IsNull((*(JSValue *) error)) ? JSNull : JS_DupValue(env->context,
                                                                                  *((JSValue *) error)));

    return napi_clear_last_error(env);
}

napi_status napi_throw_error(napi_env env, const char *code, const char *msg) {
    CHECK_ARG(env)
    CHECK_ARG(msg)

    JS_ThrowInternalError(env->context, "code: %s, message: %s\n", code, msg);

    return napi_clear_last_error(env);
}

napi_status napi_throw_range_error(napi_env env, const char *code, const char *msg) {
    CHECK_ARG(env)
    CHECK_ARG(msg)

    JS_ThrowRangeError(env->context, "code: %s, message: %s\n", code, msg);

    return napi_clear_last_error(env);
}

napi_status napi_throw_type_error(napi_env env, const char *code, const char *msg) {
    CHECK_ARG(env)
    CHECK_ARG(msg)

    JS_ThrowTypeError(env->context, "code: %s, message: %s\n", code, msg);

    return napi_clear_last_error(env);
}


napi_status napi_is_exception_pending(napi_env env, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = JS_HasException(env->context);

    return napi_clear_last_error(env);
}

napi_status napi_get_and_clear_last_exception(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(env->context)

    JSValue exceptionValue = JS_GetException(env->context);

    if (JS_IsNull(exceptionValue)) {
        *result = (napi_value) &JSUndefined;
        return napi_clear_last_error(env);
    }

    return CreateScopedResult(env, exceptionValue, result);
}

napi_status napi_get_last_error_info(napi_env env, const napi_extended_error_info **result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = env->last_error.error_code == napi_ok ? NULL : &env->last_error;

    return napi_clear_last_error(env);
}

napi_status napi_create_error(napi_env env, napi_value code, napi_value msg, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue error = JS_NewError(env->context);
    JSValue msgValue = *((JSValue *) msg);

    JS_DefinePropertyValueStr(env->context, error, "message", JS_DupValue(env->context, msgValue),
                              JS_PROP_C_W_E);

    if (code != NULL) {
        JSValue codeValue = *((JSValue *) code);
        JS_DefinePropertyValueStr(env->context, error, "code", JS_DupValue(env->context, codeValue),
                                  JS_PROP_C_W_E);
    }

    JS_SetStacktrace(env->context, error);

    return CreateScopedResult(env, error, result);
}

napi_status
napi_create_type_error(napi_env env, napi_value code, napi_value msg, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue error = JS_NewError(env->context);

    JSValue msgValue = *((JSValue *) msg);
    JS_DefinePropertyValueStr(env->context, error, "message", JS_DupValue(env->context, msgValue),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(env->context, error, "name", JS_NewString(env->context, "TypeError"),
                              JS_PROP_C_W_E);

    if (code != NULL) {
        JSValue codeValue = *((JSValue *) code);
        JS_DefinePropertyValueStr(env->context, error, "code", JS_DupValue(env->context, codeValue),
                                  JS_PROP_C_W_E);
    }

    return CreateScopedResult(env, error, result);
}

napi_status
napi_create_range_error(napi_env env, napi_value code, napi_value msg, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue error = JS_NewError(env->context);

    JSValue msgValue = *((JSValue *) msg);
    JS_DefinePropertyValueStr(env->context, error, "message", JS_DupValue(env->context, msgValue),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(env->context, error, "name", JS_NewString(env->context, "RangeError"),
                              JS_PROP_C_W_E);

    if (code != NULL) {
        JSValue codeValue = *((JSValue *) code);
        JS_DefinePropertyValueStr(env->context, error, "code", JS_DupValue(env->context, codeValue),
                                  JS_PROP_C_W_E);
    }

    return CreateScopedResult(env, error, result);
}

napi_status
napi_create_syntax_error(napi_env env, napi_value code, napi_value msg, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue error = JS_NewError(env->context);

    JSValue msgValue = *((JSValue *) msg);
    JS_DefinePropertyValueStr(env->context, error, "message", JS_DupValue(env->context, msgValue),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(env->context, error, "name",
                              JS_NewString(env->context, "SyntaxError"), JS_PROP_C_W_E);

    if (code != NULL) {
        JSValue codeValue = *((JSValue *) code);
        JS_DefinePropertyValueStr(env->context, error, "code", JS_DupValue(env->context, codeValue),
                                  JS_PROP_C_W_E);
    }

    return CreateScopedResult(env, error, result);
}


/**
 * --------------------------------------
 *        REFERENCE MANAGEMENT
 * --------------------------------------
 */

napi_status
napi_create_reference(napi_env env, napi_value value, uint32_t initialRefCount, napi_ref *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    *result = (NapiReference *) mi_malloc(sizeof(NapiReference));
    RETURN_STATUS_IF_FALSE(*result, napi_memory_error)

    JSValue jsValue = *((JSValue *) value);

    if (JS_IsUndefined(jsValue)) {
        (*result)->value = JS_UNDEFINED;
        (*result)->referenceCount = 0;
        LIST_INSERT_HEAD(&env->referencesList, *result, node);
        return napi_clear_last_error(env);
    }

    if (initialRefCount == 0) {
        JSValue global = JS_GetGlobalObject(env->context);
        JSValue JS_WeakRef_Ctor = JS_GetProperty(env->context, global, env->atoms.weakref);
        JSValue args[1] = {
                jsValue
        };
        JSValue weak_ref = JS_CallConstructor(env->context, JS_WeakRef_Ctor, 1, args);

        JS_FreeValue(env->context, global);
        JS_FreeValue(env->context, JS_WeakRef_Ctor);
        (*result)->referenceCount = 0;
        (*result)->value = weak_ref;
    } else {
        (*result)->referenceCount = initialRefCount;
        (*result)->value = JS_DupValue(env->context, jsValue);
    }

    LIST_INSERT_HEAD(&env->referencesList, *result, node);

    return napi_clear_last_error(env);
}

napi_status napi_reference_ref(napi_env env, napi_ref ref, uint32_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(ref)

    if (!ref->referenceCount) {
        JSValue value = JS_WeakRef_Deref(env->context, ref->value);
        JS_FreeValue(env->context, ref->value);
        ref->value = value;
    }

    uint8_t count = ++ref->referenceCount;
    if (result) {
        *result = count;
    }

    return napi_clear_last_error(env);
}

napi_status napi_reference_unref(napi_env env, napi_ref ref, uint32_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(ref)

    RETURN_STATUS_IF_FALSE(ref->referenceCount, napi_generic_failure)

    if (ref->referenceCount == 1) {
        JSValue global = JS_GetGlobalObject(env->context);
        JSValue JS_WeakRef_Ctor = JS_GetProperty(env->context, global, env->atoms.weakref);

        JSValue args[1] = {
                ref->value
        };
        JSValue weak_ref = JS_CallConstructor(env->context, JS_WeakRef_Ctor, 1, args);
        JS_FreeValue(env->context, global);
        JS_FreeValue(env->context, JS_WeakRef_Ctor);
        JS_FreeValue(env->context, ref->value);
        ref->value = weak_ref;
    }

    uint8_t count = --ref->referenceCount;
    if (result) {
        *result = count;
    }

    return napi_clear_last_error(env);
}


napi_status napi_get_reference_value(napi_env env, napi_ref ref, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(ref)
    CHECK_ARG(result)

    if (!ref->referenceCount && JS_IsUndefined(ref->value)) {
        *result = (napi_value) &JSUndefined;
    }

    JSValue value;
    if (ref->referenceCount > 0) {
        value = JS_DupValue(env->context, ref->value);
    } else {
        value = JS_WeakRef_Deref(env->context, ref->value);
    }

    return CreateScopedResult(env, value, result);
}

napi_status napi_delete_reference(napi_env env, napi_ref ref) {
    CHECK_ARG(env)
    CHECK_ARG(ref)

    if (!JS_IsUndefined(ref->value)) {
        JS_FreeValue(env->context, ref->value);
    }

    LIST_REMOVE(ref, node);

    mi_free(ref);

    return napi_clear_last_error(env);
}


/**
 * --------------------------------------
 *      NATIVE TO JS VALUE CONVERSION
 * --------------------------------------
 */

napi_status napi_create_string_latin1(napi_env env,
                                      const char *str,
                                      size_t length,
                                      napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(str)

    JSValue value = JS_NewStringLen(env->context, str,
                                    (length == NAPI_AUTO_LENGTH) ? strlen(str) : length);
    return CreateScopedResult(env, value, result);
}

size_t char16_t_length(const char16_t *str) {
    size_t length = 0;

    while (str[length] != 0) {
        ++length;
    }

    return length;
}

napi_status napi_create_string_utf16(napi_env env,
                                     const char16_t *str,
                                     size_t length,
                                     napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(str)

    JSValue value = JS_NewString16(env->context, (uint16_t *) str,
                                   length == NAPI_AUTO_LENGTH ? (int) char16_t_length(str)
                                                              : (int) length);

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_string_utf8(napi_env env,
                                    const char *str,
                                    size_t length,
                                    napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(str)

    JSValue value = JS_NewStringLen(env->context, str,
                                    (length == NAPI_AUTO_LENGTH) ? strlen(str) : length);

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_int32(napi_env env, int32_t value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewInt32(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

napi_status napi_create_uint32(napi_env env, uint32_t value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewInt32(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

napi_status napi_create_int64(napi_env env, int64_t value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewInt64(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

napi_status napi_create_uint64(napi_env env, uint64_t value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewInt64(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

napi_status napi_create_double(napi_env env, double value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewFloat64(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

JSValue
JS_CreateBigIntWords(JSContext *context, int signBit, size_t wordCount, const uint64_t *words) {
    JSValue thisVar = JS_UNDEFINED;
    const size_t Count = 20;
    const size_t Two = 2;
    if (wordCount <= 0 || wordCount > Count || words == NULL) {
        return JS_EXCEPTION;
    }

    JSValue signValue = JS_NewInt32(context, (signBit % Two));
    if (JS_IsException(signValue)) {
        return JS_EXCEPTION;
    }

    JSValue wordsValue = JS_NewArray(context);
    if (JS_IsException(wordsValue)) {
        return JS_EXCEPTION;
    }

    for (size_t i = 0; i < wordCount; i++) {
        // shift 32 bits right to get high bit
        JSValue idxValueHigh = JS_NewUint32(context, (uint32_t) (words[i] >> 32));
        // gets lower 32 bits
        JSValue idxValueLow = JS_NewUint32(context, (uint32_t) (words[i] & 0xFFFFFFFF));
        if (!(JS_IsException(idxValueHigh)) && !(JS_IsException(idxValueLow))) {
            JS_SetPropertyUint32(context, wordsValue, (i * Two), idxValueHigh);
            JS_SetPropertyUint32(context, wordsValue, (i * Two + 1), idxValueLow);
            JS_FreeValue(context, idxValueHigh);
            JS_FreeValue(context, idxValueLow);
        }
    }

    JSValue argv[2] = {signValue, wordsValue};
    JSValue global = JS_GetGlobalObject(context);
    JSValue CreateBigIntWords = JS_GetPropertyStr(context, global, "CreateBigIntWords");

    JSValue ret = JS_Call(context, CreateBigIntWords, thisVar, 2, (JSValue *) &argv);
    JS_FreeValue(context, global);
    JS_FreeValue(context, CreateBigIntWords);
    JS_FreeValue(context, signValue);
    JS_FreeValue(context, wordsValue);
    return ret;
}

bool ParseBigIntWordsInternal(JSContext *context, JSValue value, int *signBit, size_t *wordCount,
                              uint64_t *words) {
    int cntValue = 0;
    if (wordCount == NULL) {
        return false;
    }

    JSValue jsValue = JS_GetPropertyStr(context, value, "count");
    if (!JS_IsException(jsValue)) {
        JS_ToInt32(context, &cntValue, jsValue);
        JS_FreeValue(context, jsValue);
    } else {
        return false;
    }

    if (signBit == NULL && words == NULL) {
        *wordCount = cntValue;
        return true;
    } else if (signBit != NULL && words != NULL) {
        cntValue = (cntValue > *wordCount) ? *wordCount : cntValue;
        jsValue = JS_GetPropertyStr(context, value, "sign");
        if (!JS_IsException(jsValue)) {
            int sigValue = 0;
            JS_ToInt32(context, &sigValue, jsValue);
            *signBit = sigValue;
            JS_FreeValue(context, jsValue);
        }

        jsValue = JS_GetPropertyStr(context, value, "words");
        if (!JS_IsException(jsValue)) {
            JSValue element;
            int64_t cValue = 0;
            for (uint32_t i = 0; i < (uint32_t) cntValue; i++) {
                element = JS_GetPropertyUint32(context, jsValue, i);
                JS_ToInt64Ext(context, &cValue, element);
                JS_FreeValue(context, element);
                words[i] = (uint64_t) cValue;
            }
            JS_FreeValue(context, jsValue);
            *wordCount = cntValue;
            return true;
        }
    }
    return false;
}

bool JS_GetBigIntWords(JSContext *context, JSValue value, int *signBit, size_t *wordCount,
                       uint64_t *words) {

    bool rev = false;
    JSValue thisVar = JS_UNDEFINED;
    if (wordCount == NULL) {
        return false;
    }

    JSValue global = JS_GetGlobalObject(context);
    JSValue GetBigIntWords = JS_GetPropertyStr(context, global, "GetBigIntWords");
    JSValue bigObj = JS_Call(context, GetBigIntWords, JS_UNDEFINED, 1, &value);

    if (!JS_IsException(bigObj)) {
        if (JS_IsObject(bigObj)) {
            rev = ParseBigIntWordsInternal(context, bigObj, signBit, wordCount, words);
        }
    }

    JS_FreeValue(context, global);
    JS_FreeValue(context, GetBigIntWords);
    JS_FreeValue(context, bigObj);
    return rev;
}

typedef struct JS_BigFloatExt {
    JSRefCountHeader header;
    bf_t num;
} JS_BigFloatExt;

bool JS_ToInt64WithBigInt(JSContext *context, JSValueConst value, int64_t *pres, bool *lossless) {
    if (pres == NULL || lossless == NULL) {
        return 0;
    }

    bool rev = false;
    JSValue val = JS_DupValue(context, value);
    JS_BigFloatExt *p = (JS_BigFloatExt *) JS_VALUE_GET_PTR(val);
    if (p) {
        int opFlag = bf_get_int64(pres, &p->num, 0);
        if (lossless != NULL) {
            *lossless = (opFlag == 0);
        }
        rev = true;
    }
    JS_FreeValue(context, val);
    return rev;
}

bool JS_ToUInt64WithBigInt(JSContext *context, JSValueConst value, uint64_t *pres, bool *lossless) {
    if (pres == NULL || lossless == NULL) {
        return false;
    }

    bool rev = false;
    JSValue val = JS_DupValue(context, value);
    JS_BigFloatExt *p = (JS_BigFloatExt *) JS_VALUE_GET_PTR(val);
    if (p) {
        int opFlag = bf_get_uint64(pres, &p->num);
        if (lossless != NULL) {
            *lossless = (opFlag == 0);
        }
        rev = true;
    }
    JS_FreeValue(context, val);
    return rev;
}

napi_status napi_create_bigint_int64(napi_env env, int64_t value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewBigInt64(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

napi_status napi_create_bigint_uint64(napi_env env, uint64_t value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue jsValue = JS_NewBigUint64(env->context, value);
    return CreateScopedResult(env, jsValue, result);
}

napi_status napi_create_bigint_words(napi_env env,
                                     int sign_bit,
                                     size_t word_count,
                                     const uint64_t *words,
                                     napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_CreateBigIntWords(env->context, sign_bit, word_count, words);
    return CreateScopedResult(env, value, result);
}


/**
 * --------------------------------------
 *            OBJECT CREATION
 * --------------------------------------
 */

napi_status napi_create_object(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_NewObject(env->context);

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_array(napi_env env, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_NewArray(env->context);

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_array_with_length(napi_env env,
                                          size_t length,
                                          napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_NewArray(env->context);

    // Set array length
    if (length != 0) {
        JSValue jsLength = JS_NewInt32(env->context, (int32_t) length);
        JS_SetPropertyStr(env->context, value, "length", jsLength);
        JS_FreeValue(env->context, jsLength);
    }

    return CreateScopedResult(env, value, result);
}

napi_status
napi_create_external(napi_env env, void *data, napi_finalize finalize_cb, void *finalize_hint,
                     napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    ExternalInfo *externalInfo = mi_malloc(sizeof(ExternalInfo));

    externalInfo->data = data;
    externalInfo->finalizeHint = finalize_hint;
    externalInfo->finalizeCallback = NULL;

    JSValue object = JS_NewObjectClass(env->context, (int) env->runtime->externalClassId);

    if (JS_IsException(object)) {
        mi_free(externalInfo);
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    JS_SetOpaque(object, externalInfo);

    napi_status status = CreateScopedResult(env, object, result);

    externalInfo->finalizeCallback = finalize_cb;

    return napi_clear_last_error(env);
}


napi_status napi_create_arraybuffer(napi_env env,
                                    size_t byte_length,
                                    void **data,
                                    napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    size_t size = 0;
    JSValue value = JS_NewArrayBufferCopy(env->context, NULL, byte_length);

    if (data) {
        *data = JS_GetArrayBuffer(env->context, &size, value);
    }

    return CreateScopedResult(env, value, result);
}

#define MARK_AS_NAPI_BUFFER \
    JS_SetProperty(env->context, value, env->atoms.napi_buffer, JS_NewBool(env->context, true));

napi_status napi_create_buffer(napi_env env,
                               size_t size,
                               void **data,
                               napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    size_t buf_size = 0;
    JSValue value = JS_NewArrayBufferCopy(env->context, NULL, size);

    if (data) {
        *data = JS_GetArrayBuffer(env->context, &size, value);
    }

    MARK_AS_NAPI_BUFFER

    return CreateScopedResult(env, value, result);
}

static size_t napi_sizeof_typedarray_type(napi_typedarray_type type) {

    switch (type) {
        case napi_int8_array:
        case napi_uint8_array:
        case napi_uint8_clamped_array:
            return sizeof(int8_t);
        case napi_int16_array:
        case napi_uint16_array:
            return sizeof(int16_t);
        case napi_int32_array:
        case napi_uint32_array:
        case napi_float32_array:
            return sizeof(int32_t);
        case napi_float64_array:
            return sizeof(double);
        case napi_bigint64_array:
        case napi_biguint64_array:
            return sizeof(int64_t);
        default:
            // Handle other cases or return an error value
            return 0;
    }
}


const char *typedArrayClassNames[] = {
        "Int8Array",
        "Uint8Array",
        "Uint8ClampedArray",
        "Int16Array",
        "Uint16Array",
        "Int32Array",
        "Uint32Array",
        "Float32Array",
        "Float64Array",
        "BigInt64Array",
        "BigUint64Array",
};

napi_status napi_create_typedarray(napi_env env,
                                   napi_typedarray_type type,
                                   size_t length,
                                   napi_value arraybuffer,
                                   size_t byte_offset,
                                   napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(arraybuffer)

    // Ensure type is within bounds
    if (type < 0 || type >= sizeof(typedArrayClassNames) / sizeof(typedArrayClassNames[0])) {
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);
    }

    if (!JS_IsArrayBuffer2(env->context, *(JSValue *) arraybuffer)) {
        return napi_set_last_error(env, napi_arraybuffer_expected, NULL, 0, NULL);
    }

    size_t size_of_element = napi_sizeof_typedarray_type(type);

    size_t bufferSize = 0;
    void *buffer = JS_GetArrayBuffer(env->context, &bufferSize, *((JSValue *) arraybuffer));

    // It's required that (length * size_of_element) + byte_offset
    // should be <= the size in bytes of the array passed in.
    // If not, a RangeError exception is raised.
    size_t total_size = (length * size_of_element) + byte_offset;
    if (total_size > bufferSize) {
        return napi_throw_range_error(env, "napi_generic_failure", "Invalid typed array length");
    }

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue typedArrayConstructor = JS_GetPropertyStr(env->context, global,
                                                      typedArrayClassNames[type]);
    JS_FreeValue(env->context, global);

    if (JS_IsException(typedArrayConstructor)) {
        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    JSValue params[] = {
            *((JSValue *) arraybuffer),
            JS_NewInt64(env->context, byte_offset),
            JS_NewInt64(env->context, length),
    };

    JSValue value = JS_CallConstructor(env->context, typedArrayConstructor, 3, params);
    JS_FreeValue(env->context, typedArrayConstructor);

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_dataview(napi_env env,
                                 size_t byte_length,
                                 napi_value arraybuffer,
                                 size_t byte_offset,
                                 napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(arraybuffer)

    if (!JS_IsArrayBuffer2(env->context, *(JSValue *) arraybuffer)) {
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);
    }

    size_t bufferSize = 0;
    void *buffer = JS_GetArrayBuffer(env->context, &bufferSize, *((JSValue *) arraybuffer));

    // It is required that byte_length + byte_offset is less
    // than or equal to the size in bytes of the array passed in.
    // If not, a RangeError exception is raised.
    if (byte_length + byte_offset > bufferSize) {
        return napi_throw_range_error(env, "napi_generic_failure", "Invalid DataView length");
    }

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue dataView = JS_GetPropertyStr(env->context, global, "DataView");

    JSValue param[] = {
            *((JSValue *) arraybuffer),
            JS_NewInt64(env->context, byte_offset),
            JS_NewInt64(env->context, byte_length),
    };

    JSValue value = JS_CallConstructor(env->context, dataView, 3, param);

    JS_FreeValue(env->context, dataView);
    JS_FreeValue(env->context, global);

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_buffer_copy(napi_env env,
                                    size_t length,
                                    const void *data,
                                    void **result_data,
                                    napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(data)

    size_t size = 0;

    JSValue value = JS_NewArrayBufferCopy(env->context, (uint8_t *) data, length);

    MARK_AS_NAPI_BUFFER

    if (result_data) {
        *result_data = JS_GetArrayBuffer(env->context, &size, value);
    }

    return CreateScopedResult(env, value, result);
}


napi_status napi_create_external_arraybuffer(napi_env env,
                                             void *external_data,
                                             size_t byte_length,
                                             napi_finalize finalize_cb,
                                             void *finalize_hint,
                                             napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(external_data)
    CHECK_ARG(byte_length)
    CHECK_ARG(result)

    JSValue value;
    if (finalize_cb) {
        ExternalBufferInfo *external_arraybuffer_info = (ExternalBufferInfo *) mi_malloc(
                sizeof(ExternalBufferInfo));
        external_arraybuffer_info->finalize_cb = finalize_cb;
        external_arraybuffer_info->hint = finalize_hint;

        value = JS_NewArrayBuffer(env->context, (uint8_t *) external_data, byte_length,
                                  buffer_finalizer, external_arraybuffer_info, false);
    } else {
        value = JS_NewArrayBuffer(env->context, (uint8_t *) external_data, byte_length, NULL,
                                  NULL, false);
    }

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_external_buffer(napi_env env,
                                        size_t length,
                                        void *data,
                                        napi_finalize finalize_cb,
                                        void *finalize_hint,
                                        napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(data)
    CHECK_ARG(length)
    CHECK_ARG(result)

    JSValue value;
    if (finalize_cb) {
        struct ExternalBufferInfo *external_arraybuffer_info = mi_malloc(
                sizeof(struct ExternalBufferInfo));
        external_arraybuffer_info->finalize_cb = finalize_cb;
        external_arraybuffer_info->hint = finalize_hint;
        value = JS_NewArrayBuffer(env->context, (uint8_t *) data, length, buffer_finalizer,
                                  external_arraybuffer_info, false);
    } else {
        value = JS_NewArrayBuffer(env->context, (uint8_t *) data, length, NULL, NULL, false);
    }

    MARK_AS_NAPI_BUFFER;

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_date(napi_env env,
                             double time,
                             napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_NewDate(env->context, time);

    return CreateScopedResult(env, value, result);
}


napi_status napi_create_symbol(napi_env env,
                               napi_value description,
                               napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue symbol = {0};

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue symbolCotr = JS_GetPropertyStr(env->context, global, "Symbol");

    JSValue jsValue = *((JSValue *) description);

    symbol = JS_Call(env->context, symbolCotr, global, 1, &jsValue);

    JS_FreeValue(env->context, symbolCotr);
    JS_FreeValue(env->context, global);

    return CreateScopedResult(env, symbol, result);
}


napi_status node_api_symbol_for(napi_env env,
                                const char *utf8description,
                                size_t length,
                                napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(utf8description);

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue description = JS_NewString(env->context, utf8description);
    JSValue symbol = JS_Invoke(env->context, global, env->atoms.NAPISymbolFor, 1, &description);
    JS_FreeValue(env->context, global);
    JS_FreeValue(env->context, description);

    return CreateScopedResult(env, symbol, result);
}

/**
 * --------------------------------------
 *     NODE-API TO C TYPES CONVERSION
 * --------------------------------------
 */


napi_status napi_get_array_length(napi_env env,
                                  napi_value value,
                                  uint32_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsArray(env->context, jsValue))
        return napi_set_last_error(env, napi_array_expected, NULL, 0, NULL);

    JSValue lengthValue = JS_GetPropertyStr(env->context, jsValue, "length");

    uint32_t length = 0;
    JS_ToUint32(env->context, &length, lengthValue);
    JS_FreeValue(env->context, lengthValue);

    *result = length;
    return napi_clear_last_error(env);
}

napi_status napi_get_arraybuffer_info(napi_env env,
                                      napi_value arraybuffer,
                                      void **data,
                                      size_t *byte_length) {
    CHECK_ARG(env)
    CHECK_ARG(arraybuffer)
    CHECK_ARG(byte_length)

    size_t size = 0;
    JSValue value = *((JSValue *) arraybuffer);

    if (!JS_IsArrayBuffer2(env->context, value))
        return napi_set_last_error(env, napi_arraybuffer_expected, NULL, 0, NULL);

    if (JS_HasProperty(env->context, value, env->atoms.napi_buffer))
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);;

    if (data) {
        *data = JS_GetArrayBuffer(env->context, &size, value);
        if (byte_length) *byte_length = size;
    } else {
        JS_GetArrayBuffer(env->context, &size, value);
        if (byte_length) *byte_length = size;
    }


    return napi_clear_last_error(env);
}

napi_status napi_get_buffer_info(napi_env env,
                                 napi_value buffer,
                                 void **data,
                                 size_t *length) {
    CHECK_ARG(env)
    CHECK_ARG(buffer)
    CHECK_ARG(data)
    CHECK_ARG(length)

    size_t size = 0;
    JSValue value = *((JSValue *) buffer);

    if (!JS_IsArrayBuffer2(env->context, value))
        return napi_set_last_error(env, napi_arraybuffer_expected, NULL, 0, NULL);

    if (!JS_HasProperty(env->context, value, env->atoms.napi_buffer))
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);

    if (data) *data = JS_GetArrayBuffer(env->context, &size, value);

    if (length) *length = size;

    return napi_clear_last_error(env);
}

napi_status napi_get_prototype(napi_env env,
                               napi_value object,
                               napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue value = *((JSValue *) object);

    if (!JS_IsObject(value))
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);

    JSValue prototype = JS_GetPrototype(env->context, value);

    return CreateScopedResult(env, prototype, result);
}

int findIndex(const char *array[], int size, const char *target) {
    for (int i = 0; i < size; ++i) {
        if (strcmp(array[i], target) == 0) {
            return i; // Return the index if the target is found
        }
    }

    // Return -1 if the target is not found in the array
    return -1;
}

int napi_get_typedarray_type(napi_env env, napi_value typedarray) {
    CHECK_ARG(env)
    CHECK_ARG(typedarray)

    JSValue value = *((JSValue *) typedarray);

    if (!JS_IsObject(value)) {
        return -1;
    }

    JSValue constructor = JS_GetPropertyStr(env->context, value, "constructor");
    JSValue className = JS_GetPropertyStr(env->context, constructor, "name");
    const char *cName = JS_ToCString(env->context, className);
    int typedArrayType = findIndex(typedArrayClassNames,
                                   sizeof(typedArrayClassNames) / sizeof(typedArrayClassNames[0]),
                                   cName);

    JS_FreeCString(env->context, cName);
    JS_FreeValue(env->context, className);
    JS_FreeValue(env->context, constructor);

    return typedArrayType;
}

napi_status napi_get_typedarray_info(napi_env env,
                                     napi_value typedarray,
                                     napi_typedarray_type *type,
                                     size_t *length,
                                     void **data,
                                     napi_value *arraybuffer,
                                     size_t *byte_offset) {
    CHECK_ARG(env)
    CHECK_ARG(typedarray)

    int typedArrayType = napi_get_typedarray_type(env, typedarray);
    if (typedArrayType == -1)
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);

    JSValue value = *((JSValue *) typedarray);

    if (type) {
        *type = (napi_typedarray_type) typedArrayType;
    }

    if (length) {
        JSValue len = JS_GetPropertyStr(env->context, value, "length");
        *length = JS_VALUE_GET_INT(len);
        JS_FreeValue(env->context, len);
    }

    if (data || arraybuffer) {
        JSValue jsArrayBuffer = JS_GetPropertyStr(env->context, value, "buffer");
        if (data) {
            size_t bufferSize;
            *data = JS_GetArrayBuffer(env->context, &bufferSize, jsArrayBuffer);
        }

        if (arraybuffer) {
            CreateScopedResult(env, jsArrayBuffer, arraybuffer);
        } else {
            JS_FreeValue(env->context, jsArrayBuffer);
        }
    }

    if (byte_offset) {
        JSValue byteOffset = JS_GetPropertyStr(env->context, value, "byteOffset");
        uint32_t cValue = 0;
        JS_ToUint32(env->context, &cValue, byteOffset);
        JS_FreeValue(env->context, byteOffset);
        *byte_offset = cValue;
    }

    return napi_clear_last_error(env);
}

bool JS_IsDataView(JSContext *context, JSValue value) {
    bool result = false;
    JSValue constructor = JS_GetPropertyStr(context, value, "constructor");
    JSValue name = JS_GetPropertyStr(context, constructor, "name");
    const char *cName = JS_ToCString(context, name);
    result = !strcmp("DataView", cName ? cName : "");
    JS_FreeCString(context, cName);
    JS_FreeValue(context, name);
    JS_FreeValue(context, constructor);
    return result;
}

napi_status napi_get_dataview_info(napi_env env,
                                   napi_value dataview,
                                   size_t *byte_length,
                                   void **data,
                                   napi_value *arraybuffer,
                                   size_t *byte_offset) {
    CHECK_ARG(env)
    CHECK_ARG(dataview)

    JSValue value = *((JSValue *) dataview);

    if (!JS_IsDataView(env->context, value)) {
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);
    }

    if (byte_length) {
        JSValue byteLength = JS_GetPropertyStr(env->context, value, "byteLength");
        *byte_length = JS_VALUE_GET_INT(byteLength);
        JS_FreeValue(env->context, byteLength);
    }

    if (data || arraybuffer) {
        JSValue jsArrayBuffer = JS_GetPropertyStr(env->context, value, "buffer");
        if (data) {
            size_t bufferSize;
            *data = JS_GetArrayBuffer(env->context, &bufferSize, jsArrayBuffer);
        }

        if (arraybuffer) {
            CreateScopedResult(env, jsArrayBuffer, arraybuffer);
        } else {
            JS_FreeValue(env->context, jsArrayBuffer);
        }
    }

    if (byte_offset) {
        JSValue byteOffset = JS_GetPropertyStr(env->context, value, "byteOffset");
        uint32_t cValue = 0;
        JS_ToUint32(env->context, &cValue, byteOffset);
        JS_FreeValue(env->context, byteOffset);
        *byte_offset = cValue;
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_date_value(napi_env env,
                                napi_value value,
                                double *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (JS_GetClassID(jsValue) != 10) {
        return napi_set_last_error(env, napi_date_expected, NULL, 0, NULL);
    }

    JSValue timeValue = JS_GetPropertyStr(env->context, jsValue, "getTime");
    JSValue time = JS_Call(env->context, timeValue, jsValue, 0, NULL);
    JS_ToFloat64(env->context, result, time);

    JS_FreeValue(env->context, timeValue);
    JS_FreeValue(env->context, time);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_bool(napi_env env,
                                napi_value value,
                                bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsBool(jsValue)) {
        return napi_set_last_error(env, napi_boolean_expected, NULL, 0, NULL);
    }

    *result = JS_VALUE_GET_BOOL(jsValue);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_double(napi_env env,
                                  napi_value value,
                                  double *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    int tag = JS_VALUE_GET_TAG(jsValue);

    if (tag == JS_TAG_INT) {
        *result = JS_VALUE_GET_INT(jsValue);
    } else if (JS_TAG_IS_FLOAT64(tag)) {
        *result = JS_VALUE_GET_FLOAT64(jsValue);
    } else {
        return napi_set_last_error(env, napi_number_expected, NULL, 0, NULL);
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_value_bigint_int64(napi_env env,
                                        napi_value value,
                                        int64_t *result,
                                        bool *lossless) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    if (!JS_IsBigInt(env->context, *(JSValue *) value)) {
        return napi_set_last_error(env, napi_bigint_expected, NULL, 0, NULL);
    }

    JS_ToInt64WithBigInt(env->context, *(JSValue *) value, result, lossless);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_bigint_uint64(napi_env env,
                                         napi_value value,
                                         uint64_t *result,
                                         bool *lossless) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    if (!JS_IsBigInt(env->context, *(JSValue *) value)) {
        return napi_set_last_error(env, napi_bigint_expected, NULL, 0, NULL);
    }

    JS_ToUInt64WithBigInt(env->context, *(JSValue *) value, result, lossless);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_bigint_words(napi_env env,
                                        napi_value value,
                                        int *sign_bit,
                                        size_t *word_count,
                                        uint64_t *words) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(sign_bit)
    CHECK_ARG(word_count)
    CHECK_ARG(words)

    JSValue jsValue = *(JSValue *) value;

    if (!JS_IsBigInt(env->context, jsValue)) {
        return napi_set_last_error(env, napi_bigint_expected, NULL, 0, NULL);
    }

    JS_GetBigIntWords(env->context, jsValue, sign_bit, word_count, words);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_external(napi_env env,
                                    napi_value value,
                                    void **result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    ExternalInfo *external = (ExternalInfo *) JS_GetOpaque(jsValue, env->runtime->externalClassId);

    *result = external ? external->data : NULL;

    return napi_clear_last_error(env);
}

napi_status napi_get_value_int32(napi_env env,
                                 napi_value value,
                                 int32_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsNumber(jsValue)) {
        return napi_set_last_error(env, napi_number_expected, NULL, 0, NULL);
    }

    JS_ToInt32(env->context, result, jsValue);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_int64(napi_env env,
                                 napi_value value,
                                 int64_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsNumber(jsValue)) {
        return napi_set_last_error(env, napi_number_expected, NULL, 0, NULL);
    }

    JS_ToInt64(env->context, result, jsValue);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_string_latin1(napi_env env, napi_value value, char *str, size_t length,
                                         size_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)

    if (!JS_IsString(*((JSValue *) value))) {
        return napi_set_last_error(env, napi_string_expected, NULL, 0, NULL);
    }

    size_t cstr_len = 0;
    const char *cstr = JS_ToCStringLen(env->context, &cstr_len, *((JSValue *) value));

    if (str == NULL) {
        CHECK_ARG(result)
        *result = cstr_len;
    } else if (length != 0) {
        strcpy(str, cstr);
        str[cstr_len] = '\0';
    } else if (result != NULL) {
        *result = 0;
    }

    JS_FreeCString(env->context, cstr);
    return napi_clear_last_error(env);
}

napi_status napi_get_value_string_utf8(napi_env env, napi_value value, char *str, size_t length,
                                       size_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)

    if (!JS_IsString(*((JSValue *) value))) {
        return napi_set_last_error(env, napi_string_expected, NULL, 0, NULL);
    }

    size_t cstr_len = 0;
    const char *cstr = JS_ToCStringLen(env->context, &cstr_len, *((JSValue *) value));

    if (str == NULL) {
        CHECK_ARG(result)
        *result = cstr_len;
    } else if (length != 0) {
        strcpy(str, cstr);
        str[cstr_len] = '\0';
    } else if (result != NULL) {
        *result = 0;
    }

    JS_FreeCString(env->context, cstr);
    return napi_clear_last_error(env);
}

size_t Utf8CodePointLen(uint8_t ch) {
    const uint8_t offset = 3;
    return ((0xe5000000 >> ((ch >> offset) & 0x1e)) & offset) + 1;
}

void Utf8ShiftAndMask(uint32_t *codePoint, const uint8_t byte) {
    *codePoint <<= 6;
    *codePoint |= 0x3F & byte;
}

uint32_t Utf8ToUtf32CodePoint(const char *src, size_t length) {
    uint32_t unicode = 0;
    const
    size_t lengthSizeOne = 1;
    const
    size_t lengthSizeTwo = 2;
    const
    size_t lengthSizeThree = 3;
    const
    size_t lengthSizeFour = 4;
    const
    size_t offsetZero = 0;
    const
    size_t offsetOne = 1;
    const
    size_t offsetTwo = 2;
    const
    size_t offsetThree = 3;
    switch (length) {
        case lengthSizeOne:
            return src[offsetZero];
        case lengthSizeTwo:
            unicode = src[offsetZero] & 0x1f;
            Utf8ShiftAndMask(&unicode, src[offsetOne]);
            return unicode;
        case lengthSizeThree:
            unicode = src[offsetZero] & 0x0f;
            Utf8ShiftAndMask(&unicode, src[offsetOne]);
            Utf8ShiftAndMask(&unicode, src[offsetTwo]);
            return unicode;
        case lengthSizeFour:
            unicode = src[offsetZero] & 0x07;
            Utf8ShiftAndMask(&unicode, src[offsetOne]);
            Utf8ShiftAndMask(&unicode, src[offsetTwo]);
            Utf8ShiftAndMask(&unicode, src[offsetThree]);
            return unicode;
        default:
            return 0xffff;
    }
}

char16_t *Utf8ToUtf16(const char *utf8Str, size_t u8len, char16_t *u16str, size_t u16len) {
    if (u16len == 0) {
        return u16str;
    }
    const char *u8end = utf8Str + u8len;
    const char *u8cur = utf8Str;
    const char16_t *u16end = u16str + u16len - 1;
    const uint8_t offset = 10;
    char16_t *u16cur = u16str;

    while ((u8cur < u8end) && (u16cur < u16end)) {
        size_t len = Utf8CodePointLen(*u8cur);
        uint32_t codepoint = Utf8ToUtf32CodePoint(u8cur, len);
        // Convert the UTF32 codepoint to one or more UTF16 codepoints
        if (codepoint <= 0xFFFF) {
            // Single UTF16 character
            *u16cur++ = (char16_t) codepoint;
        } else {
            // Multiple UTF16 characters with surrogates
            codepoint = codepoint - 0x10000;
            *u16cur++ = (char16_t) ((codepoint >> offset) + 0xD800);
            if (u16cur >= u16end) {
                // Ooops...  not enough room for this surrogate pair.
                return u16cur - 1;
            }
            *u16cur++ = (char16_t) ((codepoint & 0x3FF) + 0xDC00);
        }

        u8cur += len;
    }
    return u16cur;
}

int Utf8ToUtf16Length(const char *str8, size_t str8Len) {
    const char *str8end = str8 + str8Len;
    int utf16len = 0;
    while (str8 < str8end) {
        utf16len++;
        size_t u8charlen = Utf8CodePointLen(*str8);
        if (str8 + u8charlen - 1 >= str8end) {
            return -1;
        }
        uint32_t codepoint = Utf8ToUtf32CodePoint(str8, u8charlen);
        if (codepoint > 0xFFFF) {
            utf16len++; // this will be a surrogate pair in utf16
        }
        str8 += u8charlen;
    }
    if (str8 != str8end) {
        return -1;
    }
    return utf16len;
}

napi_status napi_get_value_string_utf16(napi_env env,
                                        napi_value value,
                                        char16_t *buf,
                                        size_t bufsize,
                                        size_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)

    size_t l = 0;
    const char *str = JS_ToCStringLen(env->context, &l, *((JSValue *) value));

    if (str == NULL) {
        return napi_set_last_error(env, napi_string_expected, NULL, 0, NULL);
    }

    int ret = Utf8ToUtf16Length(str, strlen(str));
    if (ret == -1) {
        JS_FreeCString(env->context, str);
        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    if (result) {
        *result = ret;
    }

    if (buf != NULL) {
        memset(buf, 0, sizeof(char16_t) * bufsize);
        Utf8ToUtf16(str, strlen(str), buf, bufsize);
    }

    JS_FreeCString(env->context, str);
    return napi_clear_last_error(env);
}

napi_status napi_get_value_uint32(napi_env env,
                                  napi_value value,
                                  uint32_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsNumber(jsValue)) {
        return napi_set_last_error(env, napi_number_expected, NULL, 0, NULL);
    }

    JS_ToUint32(env->context, result, jsValue);

    return napi_clear_last_error(env);
}

/**
 * --------------------------------------
 *          GLOBAL INSTANCES
 * --------------------------------------
 */

/**
 * Functions to get global instances
 * https://nodejs.org/api/n-api.html#functions-to-get-global-instances
 */

static JSValue JSTrueValue = JS_TRUE;
static JSValue JSFalseValue = JS_FALSE;

napi_status napi_get_boolean(napi_env env, bool value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = (napi_value) (value ? &JSTrueValue : &JSFalseValue);

    return napi_clear_last_error(env);
}

napi_status napi_get_global(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    JSValue globalValue = JS_GetGlobalObject(env->context);
    return CreateScopedResult(env, globalValue, result);
}

napi_status napi_get_null(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = (napi_value) &JSNull;

    return napi_clear_last_error(env);
}

napi_status napi_get_undefined(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = (napi_value) &JSUndefined;

    return napi_clear_last_error(env);
}


/**
 * --------------------------------------
 *         WORKING WITH JS VALUES
 * --------------------------------------
 */

napi_status napi_coerce_to_bool(napi_env env, napi_value value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);
    int boolValue = JS_ToBool(env->context, jsValue);
    RETURN_STATUS_IF_FALSE(boolValue != -1, napi_pending_exception)

    return CreateScopedResult(env, JS_NewBool(env->context, boolValue), result);
}

napi_status napi_coerce_to_number(napi_env env, napi_value value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    double number;
    JS_ToFloat64(env->context, &number, jsValue);

    return CreateScopedResult(env, JS_NewFloat64(env->context, number), result);
}

napi_status napi_coerce_to_object(napi_env env, napi_value value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    return napi_clear_last_error(env);
}

napi_status napi_coerce_to_string(napi_env env, napi_value value, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);
    JSValue jsResult;
    if (JS_IsSymbol(jsValue)) {
        jsResult = JS_GetPropertyStr(env->context, jsValue, "description");
    } else {
        jsResult = JS_ToString(env->context, jsValue);
    }

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    JS_DupValue(env->context, jsResult);

    return CreateScopedResult(env, jsResult, result);
}

napi_status napi_typeof(napi_env env, napi_value value, napi_valuetype *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);
    if (JS_IsUndefined(jsValue)) {
        *result = napi_undefined;
    } else if (JS_IsNull(jsValue)) {
        *result = napi_null;
    } else if (JS_IsNumber(jsValue)) {
        *result = napi_number;
    } else if (JS_IsBool(jsValue)) {
        *result = napi_boolean;
    } else if (JS_IsString(jsValue)) {
        *result = napi_string;
    } else if (JS_IsSymbol(jsValue)) {
        *result = napi_symbol;
    } else if (JS_IsBigInt(env->context, jsValue)) {
        *result = napi_bigint;
    } else if (JS_IsFunction(env->context, jsValue)) {
        *result = napi_function;
    } else if (JS_GetOpaque(jsValue, env->runtime->externalClassId)) {
        *result = napi_external;
    } else if (JS_IsObject(jsValue)) {
        *result = napi_object;
    } else {
        return napi_set_last_error(env, napi_invalid_arg, NULL, 0, NULL);
    }

    return napi_clear_last_error(env);
}

napi_status napi_instanceof(napi_env env, napi_value object, napi_value constructor, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(constructor)
    CHECK_ARG(result)

    int status = JS_IsInstanceOf(env->context, *((JSValue *) object), *((JSValue *) constructor));
    RETURN_STATUS_IF_FALSE(status != -1, napi_pending_exception);

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_is_float(napi_env env, napi_value value, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)

    JSValue jsValue = *((JSValue *) value);

    RETURN_STATUS_IF_FALSE(JS_IsNumber(jsValue), napi_number_expected)

    *result = JS_VALUE_GET_TAG(jsValue) == JS_TAG_FLOAT64;

    return napi_clear_last_error(env);
}

napi_status napi_is_array(napi_env env, napi_value value, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);
    int status = JS_IsArray(env->context, jsValue);
    RETURN_STATUS_IF_FALSE(status != -1, napi_pending_exception);
    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_is_arraybuffer(napi_env env, napi_value value, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);
    int status = JS_IsArrayBuffer2(env->context, jsValue);
    RETURN_STATUS_IF_FALSE(status != -1, napi_pending_exception);

    if (status && JS_HasProperty(env->context, jsValue, env->atoms.napi_buffer)) {
        *result = false;
        return napi_clear_last_error(env);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_is_buffer(napi_env env, napi_value value, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);
    int status = JS_IsArrayBuffer2(env->context, jsValue);
    RETURN_STATUS_IF_FALSE(status != -1, napi_pending_exception);

    if (status && !JS_HasProperty(env->context, jsValue, env->atoms.napi_buffer)) {
        *result = false;
        return napi_clear_last_error(env);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_is_date(napi_env env, napi_value value, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    bool status = JS_GetClassID(jsValue) == 10;
    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_is_error(napi_env env, napi_value value, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    int status = JS_IsError(env->context, *((JSValue *) value));
    *result = status;
    return napi_clear_last_error(env);
}

napi_status napi_is_typedarray(napi_env env, napi_value value, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    int status = napi_get_typedarray_type(env, value);
    *result = status == -1 ? 0 : 1;

    return napi_clear_last_error(env);
}

napi_status napi_is_dataview(napi_env env, napi_value value, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    int status = JS_IsDataView(env->context, *((JSValue *) value));
    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_strict_equals(napi_env env, napi_value lhs, napi_value rhs, bool *result) {
    CHECK_ARG(env);
    CHECK_ARG(lhs);
    CHECK_ARG(rhs);
    CHECK_ARG(result);

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue object = JS_GetProperty(env->context, global, env->atoms.object);
    JSValue is = JS_GetProperty(env->context, object, env->atoms.is);
    JSValue argv[] = {*(JSValue *) lhs, *(JSValue *) rhs};

    JSValue jsResult = JS_Call(env->context, is, object, 2, argv);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = JS_ToBool(env->context, jsResult);

    JS_FreeValue(env->context, global);
    JS_FreeValue(env->context, object);
    JS_FreeValue(env->context, is);
    JS_FreeValue(env->context, jsResult);

    return napi_clear_last_error(env);
}

napi_status napi_detach_arraybuffer(napi_env env, napi_value arraybuffer) {

    CHECK_ARG(env)
    CHECK_ARG(arraybuffer)

    JSValue jsValue = *((JSValue *) arraybuffer);

    if (!JS_IsArrayBuffer2(env->context, jsValue)) {
        return napi_set_last_error(env, napi_arraybuffer_expected, NULL, 0, NULL);
    }

    JS_DetachArrayBuffer(env->context, jsValue);

    return napi_clear_last_error(env);
}

napi_status napi_is_detached_arraybuffer(napi_env env, napi_value arraybuffer, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(arraybuffer)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) arraybuffer);

    if (!JS_IsArrayBuffer2(env->context, jsValue)) {
        return napi_set_last_error(env, napi_arraybuffer_expected, NULL, 0, NULL);
    }

    void *buffer = NULL;
    size_t bufferSize = 0;
    buffer = JS_GetArrayBuffer(env->context, &bufferSize, jsValue);
    *result = buffer == NULL;

    return napi_clear_last_error(env);
}

/**
 * --------------------------------------
 *          OBJECT PROPERTIES
 * --------------------------------------
 */
napi_status napi_get_all_property_names(napi_env env,
                                        napi_value object,
                                        napi_key_collection_mode key_mode,
                                        napi_key_filter key_filter,
                                        napi_key_conversion key_conversion,
                                        napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    int get_filter = JS_GPN_STRING_MASK;
    if (key_filter == napi_key_all_properties) {
        get_filter = JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY;
    } else {
        if (key_filter & napi_key_skip_strings) {
            get_filter &= ~JS_GPN_STRING_MASK;
        }

        if (key_filter & napi_key_enumerable) {
            get_filter |= JS_GPN_ENUM_ONLY;
        }

        if (!(key_filter & napi_key_skip_symbols)) {
            get_filter |= JS_GPN_SYMBOL_MASK;
        }
    }

    JSValue array = JS_NewArray(env->context);
    JSValue proto = JS_DupValue(env->context, jsValue);

    while (!JS_IsNull(proto)) {

        JSPropertyEnum *tab = NULL;
        uint32_t len = 0;

        JS_GetOwnPropertyNames(env->context, &tab, &len, proto, get_filter);
        for (uint32_t i = 0; i < len; i++) {
            JSValue name = JS_AtomToValue(env->context, tab[i].atom);

            JS_SetPropertyInt64(env->context, array, i, name);
            JS_FreeAtom(env->context, tab[i].atom);
        }

        js_free(env->context, tab);

        if (key_mode == napi_key_include_prototypes) {
            JSValue nextProto = JS_GetPrototype(env->context, proto);
            // Free the prototype.
            JS_FreeValue(env->context, proto);
            proto = nextProto;
        } else {
            proto = JS_NULL;
        }
    }

    return CreateScopedResult(env, array, result);
}

napi_status napi_get_property_names(napi_env env, napi_value object, napi_value *result) {
    return napi_get_all_property_names(
            env,
            object,
            napi_key_include_prototypes,
            (napi_key_filter) (napi_key_enumerable | napi_key_skip_symbols),
            napi_key_numbers_to_strings,
            result);
}

napi_status napi_set_property(napi_env env, napi_value object, napi_value key, napi_value value) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(key)
    CHECK_ARG(value)

    JSValue jsObject = *((JSValue *) object);
    JSValue jsKey = *((JSValue *) key);
    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsObject(jsObject)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int result = JS_SetProperty(env->context, jsObject, keyAtom,
                                JS_DupValue(env->context, jsValue));
    JS_FreeAtom(env->context, keyAtom);

    if (!result) {
        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_property(napi_env env, napi_value object, napi_value key, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(key)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);
    JSValue jsKey = *((JSValue *) key);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    JSValue jsResult = JS_GetProperty(env->context, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    return CreateScopedResult(env, jsResult, result);
}

napi_status napi_has_property(napi_env env, napi_value object, napi_value key, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(key)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);
    JSValue jsKey = *((JSValue *) key);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_HasProperty(env->context, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_delete_property(napi_env env, napi_value object, napi_value key, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(key)

    JSValue jsValue = *((JSValue *) object);
    JSValue jsKey = *((JSValue *) key);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_DeleteProperty(env->context, jsValue, keyAtom, JS_PROP_THROW);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    if (result != NULL) {
        *result = status;
    }

    return napi_clear_last_error(env);
}

napi_status
napi_has_own_named_property(napi_env env, napi_value object, const char *utf8name, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(utf8name)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSValue jsKey = JS_NewString(env->context, utf8name);
    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_GetOwnProperty(env->context, NULL, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_has_own_property(napi_env env, napi_value object, napi_value key, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(key)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);
    JSValue jsKey = *((JSValue *) key);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_GetOwnProperty(env->context, NULL, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status
napi_set_named_property(napi_env env, napi_value object, const char *utf8Name, napi_value value) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(utf8Name)
    CHECK_ARG(value)

    JSValue jsObject = *((JSValue *) object);
    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsObject(jsObject)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    int status = JS_SetPropertyStr(env->context, jsObject, utf8Name,
                                   JS_DupValue(env->context, jsValue));

    if (status == -1) {
        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    return napi_clear_last_error(env);
}

napi_status
napi_get_named_property(napi_env env, napi_value object, const char *utf8Name, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(utf8Name)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSValue jsResult = JS_GetPropertyStr(env->context, jsValue, utf8Name);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    return CreateScopedResult(env, jsResult, result);
}

napi_status
napi_has_named_property(napi_env env, napi_value object, const char *utf8Name, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(utf8Name)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom keyAtom = JS_NewAtom(env->context, utf8Name);
    int status = JS_HasProperty(env->context, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_set_element(napi_env env, napi_value object, uint32_t index, napi_value value) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(value)

    JSValue jsObject = *((JSValue *) object);
    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsObject(jsObject)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    int status = JS_SetPropertyUint32(env->context, jsObject, index, jsValue);

    if (!status) {
        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_element(napi_env env, napi_value object, uint32_t index, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSValue jsResult = JS_GetPropertyUint32(env->context, jsValue, index);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    return CreateScopedResult(env, jsResult, result);
}

napi_status napi_has_element(napi_env env, napi_value object, uint32_t index, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom key = JS_NewAtomUInt32(env->context, index);
    int status = JS_HasProperty(env->context, jsValue, key);
    JS_FreeAtom(env->context, key);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = status;

    return napi_clear_last_error(env);
}

napi_status napi_delete_element(napi_env env, napi_value object, uint32_t index, bool *result) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSAtom key = JS_NewAtomUInt32(env->context, index);
    int status = JS_DeleteProperty(env->context, jsValue, key, JS_PROP_THROW);
    JS_FreeAtom(env->context, key);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    *result = status;

    return napi_clear_last_error(env);
}

static inline void
napi_set_property_descriptor(napi_env env, napi_value object, napi_property_descriptor descriptor) {
    JSAtom key;

    if (descriptor.name) {
        JSValue symbol = *((JSValue *) descriptor.name);
        key = JS_ValueToAtom(env->context, symbol);
    } else {
        key = JS_NewAtom(env->context, descriptor.utf8name);
    }

    JSValue jsObject = *((JSValue *) object);

    int flags = JS_PROP_HAS_WRITABLE | JS_PROP_HAS_ENUMERABLE | JS_PROP_HAS_CONFIGURABLE;

    if ((descriptor.attributes & napi_writable) != 0 || descriptor.getter || descriptor.setter) {
        flags |= JS_PROP_WRITABLE;
    }

    if ((descriptor.attributes & napi_enumerable) != 0) {
        flags |= JS_PROP_ENUMERABLE;
    }

    if ((descriptor.attributes & napi_configurable) != 0) {
        flags |= JS_PROP_CONFIGURABLE;
    }

    JSValue value = JS_UNDEFINED, getterValue = JS_UNDEFINED, setterValue = JS_UNDEFINED;

    if (descriptor.value) {
        flags |= JS_PROP_HAS_VALUE;
        value = JS_DupValue(env->context, *((JSValue *) descriptor.value));
    } else if (descriptor.method) {
        flags |= JS_PROP_HAS_VALUE;
        napi_value function;
        napi_create_function(env, NULL, 0, descriptor.method, descriptor.data, &function);
        if (function) {
            value = JS_DupValue(env->context, *((JSValue *) function));
        }
    } else if (descriptor.getter || descriptor.setter) {
        if (descriptor.getter) {
            napi_value getter = NULL;
            flags |= JS_PROP_HAS_GET;
            napi_create_function(env, NULL, 0, descriptor.getter, descriptor.data, &getter);
            if (getter) {
                getterValue = *((JSValue *) getter);
            }
        }

        if (descriptor.setter) {
            napi_value setter = NULL;
            flags |= JS_PROP_HAS_SET;
            napi_create_function(env, NULL, 0, descriptor.setter, descriptor.data, &setter);
            if (setter) {
                setterValue = *((JSValue *) setter);
            }
        }
    }

    JS_DefineProperty(env->context, jsObject, key, value, getterValue, setterValue, flags);
    JS_FreeAtom(env->context, key);
}

napi_status napi_define_properties(napi_env env, napi_value object, size_t property_count,
                                   const napi_property_descriptor *properties) {
    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(properties)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    for (size_t i = 0; i < property_count; i++) {
        napi_set_property_descriptor(env, object, properties[i]);
    }

    return napi_clear_last_error(env);
}

napi_status napi_object_freeze(napi_env env, napi_value object) {
    CHECK_ARG(env)
    CHECK_ARG(object)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }
    JSValue global = JS_GetGlobalObject(env->context);
    JSValue obj = JS_GetProperty(env->context, global, env->atoms.object);
    JSValue result = JS_Invoke(env->context, obj, env->atoms.freeze, 1, &jsValue);

    JS_FreeValue(env->context, obj);
    JS_FreeValue(env->context, global);

    if (JS_IsException(result)) {
        JS_FreeValue(env->context, result);
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    JS_FreeValue(env->context, result);

    return napi_clear_last_error(env);
}

napi_status napi_object_seal(napi_env env, napi_value object) {
    CHECK_ARG(env)
    CHECK_ARG(object)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue obj = JS_GetProperty(env->context, global, env->atoms.object);
    JSValue result = JS_Invoke(env->context, obj, env->atoms.seal, 1, &jsValue);
    JS_FreeValue(env->context, obj);
    JS_FreeValue(env->context, global);

    if (JS_IsException(result)) {
        JS_FreeValue(env->context, result);
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    JS_FreeValue(env->context, result);

    return napi_clear_last_error(env);
}

/**
 * --------------------------------------
 *              FUNCTIONS
 * --------------------------------------
 */

napi_status napi_call_function(napi_env env, napi_value thisValue, napi_value func, size_t argc,
                               const napi_value *argv, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(func)

    JSValue jsThis = *((JSValue *) thisValue);
    JSValue jsFunction = *(JSValue *) func;

    bool useGlobal = false;

    if (JS_IsUndefined(jsThis)) {
        useGlobal = true;
        jsThis = JS_GetGlobalObject(env->context);
    }

    js_enter(env);
    JSValue *args = NULL;
    JSValue returnValue;

    if (argc > 0) {
        CHECK_ARG(argv)
        JSValue stack_args[8];
        if (argc <= 8) {
            args = stack_args;
        } else {
            args = (JSValue *) mi_malloc(sizeof(JSValue) * argc);
        }

        for (size_t i = 0; i < argc; ++i) {
            args[i] = *((JSValue *) argv[i]);
        }
        returnValue = JS_Call(env->context, jsFunction, jsThis, (int) argc,
                              args);
        if (argc > 8) mi_free(args);
    } else {
        returnValue = JS_Call(env->context, jsFunction, jsThis, 0,
                              NULL);
    }

    js_exit(env);

    if (useGlobal) JS_FreeValue(env->context, jsThis);

    if (JS_IsException(returnValue)) {
        if (result) *result = (napi_value) &JSUndefined;
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    if (result) {
        return CreateScopedResult(env, returnValue, result);
    }

    JS_FreeValue(env->context, returnValue);


    return napi_clear_last_error(env);
}

static JSValue
CallCFunction(JSContext *context, JSValueConst thisVal, int argc, JSValueConst *argv, int magic,
              JSValue *funcData) {
    napi_env env = (napi_env) JS_GetContextOpaque(context);


    bool useGlobalValue = false;
    if (JS_IsUndefined(thisVal)) {
        useGlobalValue = true;
        thisVal = JS_GetGlobalObject(context);
    }

    FunctionInfo *functionInfo = (FunctionInfo *) JS_GetOpaque(funcData[0],
                                                               env->runtime->functionClassId);

    struct NapiCallbackInfo callbackInfo = {JSUndefined, thisVal, argv, functionInfo->data, argc};

    NapiHandleScope handleScope;
    handleScope.type = HANDLE_STACK_ALLOCATED;
    handleScope.handleCount = 0;
    handleScope.escapeCalled = false;
    SLIST_INIT(&handleScope.handleList);
    LIST_INSERT_HEAD(&env->handleScopeList, &handleScope, node);

    napi_value result = functionInfo->callback(env, &callbackInfo);

    if (useGlobalValue) {
        JS_FreeValue(context, thisVal);
    }

    JSValue returnValue = JSUndefined;
    if (result) {
        returnValue = JS_DupValue(context, *((JSValue *) result));
    }

    assert(LIST_FIRST(&env->handleScopeList) == &handleScope &&
           "napi_close_handle_scope() or napi_close_escapable_handle_scope() should follow FILO rule.");

    Handle *handle, *tempHandle;
    SLIST_FOREACH_SAFE(handle, &handleScope.handleList, node, tempHandle) {
        JS_FreeValue(env->context, handle->value);
        handle->value = JSUndefined;
        SLIST_REMOVE(&handleScope.handleList, handle, Handle, node);
        if (handle->type == HANDLE_HEAP_ALLOCATED) {
            mi_free(handle);
        }
    }
    LIST_REMOVE(&handleScope, node);


    if (JS_HasException(context)) {
        JS_FreeValue(context, returnValue);
        return JS_Throw(context, JS_GetException(context));
    }


    return returnValue;
}

napi_status
napi_create_function(napi_env env, const char *utf8name, size_t length, napi_callback cb,
                     void *data,
                     napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(cb)
    CHECK_ARG(result)

    FunctionInfo *functionInfo = (FunctionInfo *) mi_malloc(sizeof(FunctionInfo));
    RETURN_STATUS_IF_FALSE(functionInfo, napi_memory_error)
    functionInfo->data = data;
    functionInfo->callback = cb;

    if (TRUTHY(!env->runtime->functionClassId)) {
        assert(false && FUNCTION_CLASS_ID_ZERO);
        mi_free(functionInfo);

        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    JSValue dataValue = JS_NewObjectClass(env->context, (int) env->runtime->functionClassId);
    if (TRUTHY(JS_IsException(dataValue))) {
        mi_free(functionInfo);

        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    JS_SetOpaque(dataValue, functionInfo);

    JSValue functionValue = JS_NewCFunctionData(env->context, CallCFunction, 0, 0, 1, &dataValue);

    JS_FreeValue(env->context, dataValue);

    RETURN_STATUS_IF_FALSE(!JS_IsException(functionValue), napi_pending_exception)

    if (utf8name && strcmp(utf8name, "") != 0) {
        int returnStatus = JS_DefinePropertyValue(env->context, functionValue, env->atoms.name,
                                                  JS_NewString(env->context, utf8name),
                                                  JS_PROP_CONFIGURABLE);

        if (TRUTHY(returnStatus == -1)) {
            JS_FreeValue(env->context, functionValue);

            return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
        }
    }

    return CreateScopedResult(env, functionValue, result);
}

napi_status
napi_get_cb_info(napi_env env, napi_callback_info callbackInfo, size_t *argc, napi_value *argv,
                 napi_value *thisArg, void **data) {
    CHECK_ARG(env)
    CHECK_ARG(callbackInfo)

    if (argv && argc) {
        size_t i = 0;
        size_t min = callbackInfo->argc<0 || *argc>(size_t)
        callbackInfo->argc ? callbackInfo->argc : *argc;

        for (; i < min; ++i) {
            argv[i] = (napi_value) &callbackInfo->argv[i];
        }

        if (i < *argc) {
            for (; i < *argc; ++i) {
                argv[i] = (napi_value) &JSUndefined;
            }
        }
    }

    if (argc) {
        *argc = callbackInfo->argc;
    }

    if (thisArg) {
        *thisArg = (napi_value) &callbackInfo->thisArg;
    }

    if (data) {
        *data = callbackInfo->data;
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_new_target(napi_env env, napi_callback_info callbackInfo, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(callbackInfo)
    CHECK_ARG(result)

    if (!JS_IsUndefined(callbackInfo->newTarget)) {
        *result = (napi_value) &callbackInfo->newTarget;
    } else {
        *result = NULL;
    }

    return napi_clear_last_error(env);
}

napi_status
napi_new_instance(napi_env env, napi_value constructor, size_t argc, const napi_value *argv,
                  napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(constructor)
    CHECK_ARG(result)

    js_enter(env);
    JSValue *args = NULL;
    JSValue returnValue;

    if (argc > 0) {
        CHECK_ARG(argv)
        JSValue stack_args[8];
        if (argc <= 8) {
            args = stack_args;
        } else {
            args = (JSValue *) mi_malloc(sizeof(JSValue) * argc);
        }

        for (size_t i = 0; i < argc; ++i) {
            args[i] = *((JSValue *) argv[i]);
        }
        returnValue = JS_CallConstructor(env->context, *((JSValue *) constructor), (int) argc,
                                         args);

        if (argc > 8) mi_free(args);
    } else {
        returnValue = JS_CallConstructor(env->context, *((JSValue *) constructor), (int) argc,
                                         args);
    }

    js_exit(env);


    if (JS_IsException(returnValue)) {
        JS_Throw(env->context, returnValue);
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    return CreateScopedResult(env, returnValue, result);
}

/**
 * --------------------------------------
 *             OBJECT WRAP
 * --------------------------------------
 */

static JSValue
CallConstructor(JSContext *context, JSValueConst newTarget, int argc, JSValueConst *argv, int magic,
                JSValue *data) {

    napi_env env = (napi_env) JS_GetContextOpaque(context);

    FunctionInfo *constructorInfo = (FunctionInfo *) JS_GetOpaque(*data,
                                                                  env->runtime->constructorClassId);

    JSValue prototype = JS_GetPropertyStr(context, newTarget, "prototype");
    JSValue thisValue = JS_NewObjectProto(context, prototype);
    JS_FreeValue(context, prototype);

    struct NapiCallbackInfo callbackInfo = {newTarget,
                                            thisValue,
                                            argv,
                                            constructorInfo->data,
                                            argc};

    NapiHandleScope handleScope;
    handleScope.type = HANDLE_STACK_ALLOCATED;
    handleScope.handleCount = 0;
    handleScope.escapeCalled = false;
    SLIST_INIT(&handleScope.handleList);
    LIST_INSERT_HEAD(&env->handleScopeList, &handleScope, node);

    napi_value result = constructorInfo->callback(env, &callbackInfo);

    JSValue returnValue = JS_UNDEFINED;

    if (result) {
        returnValue = *((JSValue *) result);
        JS_DupValue(env->context, returnValue);
        JS_FreeValue(env->context, thisValue);
    }

    assert(LIST_FIRST(&env->handleScopeList) == &handleScope &&
           "napi_close_handle_scope() or napi_close_escapable_handle_scope() should follow FILO rule.");
    Handle *handle, *tempHandle;
    SLIST_FOREACH_SAFE(handle, &handleScope.handleList, node, tempHandle) {
        JS_FreeValue(env->context, handle->value);
        handle->value = JSUndefined;
        SLIST_REMOVE(&handleScope.handleList, handle, Handle, node);
        if (handle->type == HANDLE_HEAP_ALLOCATED) {
            mi_free(handle);
        }
    }
    LIST_REMOVE(&handleScope, node);

    if (JS_HasException(context)) {
        JS_FreeValue(context, returnValue);

        return JS_Throw(context, JS_GetException(context));
    }


    return returnValue;
}

napi_status napi_define_class(napi_env env,
                              const char *utf8name,
                              size_t length,
                              napi_callback constructor,
                              void *data,
                              size_t property_count,
                              const napi_property_descriptor *properties,
                              napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(constructor)
    CHECK_ARG(result)

    FunctionInfo *constructorInfo = (FunctionInfo *) mi_malloc(sizeof(FunctionInfo));

    constructorInfo->data = data;
    constructorInfo->callback = constructor;

    JSValue external = JS_NewObjectClass(env->context, (int) env->runtime->constructorClassId);
    JS_SetOpaque(external, constructorInfo);

    JSValue cls = JS_NewCFunctionData(env->context, CallConstructor, 0, 0, 1, &external);
    JS_SetConstructorBit(env->context, cls, true);

    if (utf8name && strcmp(utf8name, "") != 0) {
        JS_DefinePropertyValue(env->context, cls, env->atoms.name,
                               JS_NewString(env->context, utf8name), JS_PROP_CONFIGURABLE);
    }

    JSValue prototype = JS_NewObject(env->context);

    JS_SetConstructor(env->context, cls, prototype);

    for (size_t i = 0; i < property_count; i++) {
        if (properties[i].attributes & napi_static) {
            napi_set_property_descriptor(env, (napi_value) &cls, properties[i]);
        } else {
            napi_set_property_descriptor(env, (napi_value) &prototype, properties[i]);
        }
    }

    JS_FreeValue(env->context, external);
    JS_FreeValue(env->context, prototype);

    return CreateScopedResult(env, cls, result);
}

napi_status
napi_wrap(napi_env env, napi_value jsObject, void *nativeObject, napi_finalize finalize_cb,
          void *finalize_hint,
          napi_ref *result) {

    CHECK_ARG(env)
    CHECK_ARG(jsObject)
    CHECK_ARG(nativeObject)

    JSValue jsValue = *((JSValue *) jsObject);

    RETURN_STATUS_IF_FALSE(JS_IsObject(jsValue), napi_object_expected)

    int isWrapped = JS_GetOwnProperty(env->context, NULL, jsValue, env->atoms.napi_external);

    RETURN_STATUS_IF_FALSE(isWrapped != -1, napi_pending_exception)

    RETURN_STATUS_IF_FALSE(isWrapped == 0, napi_invalid_arg)

    ExternalInfo *externalInfo = (ExternalInfo *) mi_malloc(sizeof(ExternalInfo));

    externalInfo->data = nativeObject;
    externalInfo->finalizeHint = finalize_hint;
    externalInfo->finalizeCallback = NULL;

    JSValue external = JS_NewObjectClass(env->context, (int) env->runtime->externalClassId);

    if (JS_IsException(external)) {
        mi_free(externalInfo);
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    JS_SetOpaque(external, externalInfo);

    JS_SetProperty(env->context, jsValue, env->atoms.napi_external, external);

    if (result) {
        napi_ref ref;
        napi_create_reference(env, jsObject, 0, &ref);
        *result = ref;
    }

    return napi_clear_last_error(env);
}

napi_status napi_unwrap(napi_env env, napi_value jsObject, void **result) {
    CHECK_ARG(env)
    CHECK_ARG(jsObject)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) jsObject);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSPropertyDescriptor descriptor;

    int isWrapped = JS_GetOwnProperty(env->context, &descriptor, jsValue, env->atoms.napi_external);

    if (isWrapped == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    if (!isWrapped) {
        *result = NULL;
        return napi_set_last_error(env, napi_generic_failure, NULL, 0, NULL);
    }

    JSValue external = descriptor.value;

    if (JS_IsException(external)) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    if (JS_IsObject(external)) {
        ExternalInfo *externalInfo = (ExternalInfo *) JS_GetOpaque(external,
                                                                   env->runtime->externalClassId);
        if (externalInfo) {
            *result = externalInfo->data;
        }
    }

    JS_FreeValue(env->context, descriptor.value);

    return napi_clear_last_error(env);
}

napi_status napi_remove_wrap(napi_env env, napi_value jsObject, void **result) {
    CHECK_ARG(env)
    CHECK_ARG(jsObject)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) jsObject);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSPropertyDescriptor descriptor;
    int isWrapped = JS_GetOwnProperty(env->context, &descriptor, jsValue, env->atoms.napi_external);

    if (isWrapped == -1) {
        return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
    }

    if (!isWrapped) {
        return napi_clear_last_error(env);
    }

    JSValue external = descriptor.value;

    if (JS_IsObject(external)) {
        if (result) {
            ExternalInfo *externalInfo = (ExternalInfo *) JS_GetOpaque(external,
                                                                       env->runtime->externalClassId);
            if (externalInfo) {
                *result = externalInfo->data;
            }
            mi_free(externalInfo);
            JS_SetOpaque(external, NULL);
        }

        int status = JS_DeleteProperty(env->context, jsValue, env->atoms.napi_external, 0);
        if (status == -1) {
            JS_FreeValue(env->context, external);
            JS_FreeValue(env->context, descriptor.value);
            return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
        }

        JS_FreeValue(env->context, descriptor.value);
    }

    return napi_clear_last_error(env);
}

napi_status
napi_add_finalizer(napi_env env, napi_value jsObject, void *nativeObject, napi_finalize finalize_cb,
                   void *finalize_hint, napi_ref *result) {
    CHECK_ARG(env)
    CHECK_ARG(jsObject)
    CHECK_ARG(nativeObject)

    JSValue jsValue = *((JSValue *) jsObject);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    JSValue heldValue = JS_NewObject(env->context);
    ExternalInfo *info = (ExternalInfo *) mi_malloc(sizeof(ExternalInfo));

    info->data = nativeObject;
    info->finalizeCallback = finalize_cb;
    info->finalizeHint = finalize_hint;
    JS_SetOpaque(heldValue, info);

    JSValue params[] = {
            jsValue,
            heldValue};

    JSValue res = JS_Invoke(env->context, env->finalizationRegistry, env->atoms.registerFinalizer,
                            2, params);
    JS_FreeValue(env->context, res);
    JS_FreeValue(env->context, heldValue);

    if (result) {
        napi_ref ref;
        napi_create_reference(env, jsObject, 0, &ref);
        *result = ref;
    }

    return napi_clear_last_error(env);
}

/**
 * --------------------------------------
 *             ENV INSTANCE DATA
 *  --------------------------------------
 */

napi_status
napi_set_instance_data(napi_env env, void *data, napi_finalize finalize_cb, void *finalize_hint) {

    CHECK_ARG(env)
    env->instanceData = (ExternalInfo *) mi_malloc(sizeof(ExternalInfo));
    env->instanceData->data = data;
    env->instanceData->finalizeCallback = finalize_cb;
    env->instanceData->finalizeHint = finalize_hint;

    return napi_clear_last_error(env);
}

napi_status napi_get_instance_data(napi_env env, void **data) {

    CHECK_ARG(env)
    CHECK_ARG(data)

    if (env->instanceData) {
        *data = env->instanceData->data;
    } else {
        *data = NULL;
    }

    return napi_clear_last_error(env);
}

/**
 * --------------------------------------
 *            PROMISES
 * --------------------------------------
 */

void deferred_finalize(napi_env env, void *finalizeData, void *finalizeHint) {
    NapiDeferred *deferred = (NapiDeferred *) finalizeData;
    JS_FreeValue(env->context, *(JSValue *) deferred->resolve);
    JS_FreeValue(env->context, *(JSValue *) deferred->reject);
};

napi_status napi_create_promise(napi_env env, napi_deferred *deferred, napi_value *result) {
    CHECK_ARG(env);
    CHECK_ARG(deferred);
    CHECK_ARG(result);

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(env->context, resolving_funcs);

    *deferred = (NapiDeferred *) mi_malloc(sizeof(NapiDeferred));

    (*deferred)->resolve = (napi_value) &resolving_funcs[0];
    (*deferred)->reject = (napi_value) &resolving_funcs[1];

    JSValue heldValue = JS_NewObject(env->context);
    ExternalInfo *info = (ExternalInfo *) mi_malloc(sizeof(ExternalInfo));
    info->data = deferred;
    info->finalizeCallback = deferred_finalize;
    JS_SetOpaque(heldValue, info);

    JSValue params[] = {
            promise,
            heldValue};

    JSValue res = JS_Invoke(env->context, env->finalizationRegistry, env->atoms.registerFinalizer,
                            2, params);
    JS_FreeValue(env->context, res);

    return CreateScopedResult(env, promise, result);
}

napi_status napi_resolve_deferred(napi_env env, napi_deferred deferred, napi_value resolution) {
    CHECK_ARG(env);
    CHECK_ARG(deferred);

    JSValue value = JS_UNDEFINED;
    if (resolution != NULL) {
        value = *((JSValue *) resolution);
    }
    js_enter(env);
    JSValue jsResult = JS_Call(env->context, *((JSValue *) deferred->resolve), JS_UNDEFINED, 1,
                               &value);
    js_exit(env);
    JS_FreeValue(env->context, jsResult);

    return napi_clear_last_error(env);
}

napi_status napi_reject_deferred(napi_env env, napi_deferred deferred, napi_value rejection) {
    CHECK_ARG(env);
    CHECK_ARG(deferred);

    JSValue value = JS_UNDEFINED;
    if (rejection != NULL) {
        value = *((JSValue *) rejection);
    }
    js_enter(env);
    JSValue jsResult = JS_Call(env->context, *((JSValue *) deferred->reject), JS_UNDEFINED, 1,
                               &value);
    js_exit(env);
    JS_FreeValue(env->context, jsResult);

    return napi_clear_last_error(env);
}

napi_status napi_is_promise(napi_env env,
                            napi_value value,
                            bool *is_promise) {
    CHECK_ARG(env);
    CHECK_ARG(value);

    JSValue constructor = JS_GetProperty(env->context, *((JSValue *) value),
                                         env->atoms.constructor);
    JSValue name = JS_GetProperty(env->context, constructor, env->atoms.name);
    const char *cName = JS_ToCString(env->context, name);
    *is_promise = !strcmp("Promise", cName ? cName : "");
    JS_FreeCString(env->context, cName);
    JS_FreeValue(env->context, name);
    JS_FreeValue(env->context, constructor);


    return napi_clear_last_error(env);
}


/**
 * --------------------------------------
 *             TYPE TAG
 * --------------------------------------
 */
napi_status napi_type_tag_object(napi_env env, napi_value object, const napi_type_tag *tag) {
    CHECK_ARG(env);
    CHECK_ARG(object);

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    const
    uint32_t size = 2;

    bool isTypeTagged = false;

    uint64_t words[size] = {tag->lower, tag->upper};
    isTypeTagged = JS_HasProperty(env->context, jsValue, env->atoms.napi_typetag);
    if (!isTypeTagged) {
        JSValue value = JS_CreateBigIntWords(env->context, 0, size, words);
        JS_SetProperty(env->context, jsValue, env->atoms.napi_typetag,
                       JS_DupValue(env->context, value));
    }

    return napi_clear_last_error(env);
}

napi_status
napi_check_object_type_tag(napi_env env, napi_value object, const napi_type_tag *tag,
                           bool *result) {
    CHECK_ARG(env);
    CHECK_ARG(object);
    CHECK_ARG(result);

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected, NULL, 0, NULL);
    }

    const
    uint32_t size = 2;
    bool isTypeTagged = false;

    isTypeTagged = JS_HasProperty(env->context, jsValue, env->atoms.napi_typetag);
    if (!isTypeTagged) {
        *result = false;
    }

    JSValue value = JS_GetProperty(env->context, jsValue, env->atoms.napi_typetag);
    int sign = 0;
    size_t wordCount = size;
    uint64_t words[size] = {0};
    *result = JS_GetBigIntWords(env->context, value, &sign, &wordCount, words);
    if (result && wordCount >= size) {
        if ((words[0] == tag->lower) && (words[1] == tag->upper)) {
            *result = true;
        }
    }

    return napi_clear_last_error(env);
}


napi_status napi_run_script(napi_env env,
                            napi_value script,
                            napi_value *result) {
    return qjs_execute_script(env, script, NULL, result);
}

/**
 * --------------------------------
 *             JS RUNTIME
 * --------------------------------
 */



napi_status qjs_create_runtime(napi_runtime *runtime) {
    assert(runtime);

    *runtime = mi_malloc(sizeof(NapiRuntime));

#ifdef USE_MIMALLOC
    (*runtime)->runtime = JS_NewRuntime2(&mi_mf, NULL);
#else
    (*runtime)->runtime = JS_NewRuntime();
#endif

    JS_SetMaxStackSize((*runtime)->runtime, 1024 * 1024 * 1024);

    (*runtime)->constructorClassId = 0;
    (*runtime)->functionClassId = 0;
    (*runtime)->externalClassId = 0;

    JSClassDef ExternalClassDef = {"ExternalInfo", external_finalizer, NULL, NULL, NULL};
    JSClassDef FunctionClassDef = {"FunctionInfo", function_finalizer, NULL, NULL, NULL};
    JSClassDef ConstructorClassDef = {"ConstructorInfo", function_finalizer, NULL, NULL, NULL};

    JS_NewClassID((*runtime)->runtime, &(*runtime)->constructorClassId);
    JS_NewClassID((*runtime)->runtime, &(*runtime)->functionClassId);
    JS_NewClassID((*runtime)->runtime, &(*runtime)->externalClassId);

    JS_NewClass((*runtime)->runtime, (*runtime)->externalClassId, &ExternalClassDef);
    JS_NewClass((*runtime)->runtime, (*runtime)->functionClassId, &FunctionClassDef);
    JS_NewClass((*runtime)->runtime, (*runtime)->constructorClassId, &ConstructorClassDef);

    return napi_ok;
}

static void JS_AfterGCCallback(JSRuntime *rt) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    if (env->gcAfter != NULL) {
        env->gcAfter->finalizeCallback(env, env->gcAfter->data, env->gcAfter->finalizeHint);
    }
}

static int JS_BeforeGCCallback(JSRuntime *rt) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    bool hint = true;
    if (env->gcAfter != NULL) {
        env->gcAfter->finalizeCallback(env, env->gcAfter->data, &hint);
    }

    return hint;
}

static JSValue JSRunGCCallback(JSContext *ctx, JSValue
this_val,
                               int argc, JSValue
                               *argv) {
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_TRUE;
}

static JSValue
JSFinalizeValueCallback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    napi_env env = (napi_env) JS_GetContextOpaque(ctx);
    JSValue heldValue = argv[0];
    if (!JS_IsUndefined(heldValue)) {
        ExternalInfo *info = (ExternalInfo *) JS_GetOpaqueUnsafe(
                heldValue);
        if (info != NULL) {
            info->finalizeCallback(env, info->data,
                                   info->finalizeHint);
        }
    }
    return JS_UNDEFINED;
}

napi_status qjs_create_napi_env(napi_env *env, napi_runtime runtime) {
    assert(env && runtime);

    *env = (NapiEnvironment *) mi_malloc(sizeof(struct NapiEnvironment));

    (*env)->runtime = runtime;


    JSContext *context = JS_NewContext(runtime->runtime);
    JS_SetContextOpaque(context, *env);

    (*env)->context = context;

    (*env)->js_enter_state = 0;

    JS_SetRuntimeOpaque(runtime->runtime, *env);

    JS_SetGCAfterCallback(runtime->runtime, JS_AfterGCCallback);

    JS_SetGCBeforeCallback(runtime->runtime, JS_BeforeGCCallback);

    // Create runtime atoms
    (*env)->atoms.napi_external = JS_NewAtom(context, "napi_external");
    (*env)->atoms.registerFinalizer = JS_NewAtom(context, "register");
    (*env)->atoms.name = JS_NewAtom(context, "name");
    (*env)->atoms.constructor = JS_NewAtom(context, "constructor");
    (*env)->atoms.prototype = JS_NewAtom(context, "prototype");
    (*env)->atoms.buffer = JS_NewAtom(context, "buffer");
    (*env)->atoms.length = JS_NewAtom(context, "length");
    (*env)->atoms.object = JS_NewAtom(context, "Object");
    (*env)->atoms.Symbol = JS_NewAtom(context, "Symbol");
    (*env)->atoms.NAPISymbolFor = JS_NewAtom(context, "NAPISymbolFor");
    (*env)->atoms.freeze = JS_NewAtom(context, "freeze");
    (*env)->atoms.is = JS_NewAtom(context, "is");
    (*env)->atoms.byteLength = JS_NewAtom(context, "byteLength");
    (*env)->atoms.byteOffset = JS_NewAtom(context, "byteOffset");
    (*env)->atoms.seal = JS_NewAtom(context, "seal");
    (*env)->atoms.napi_buffer = JS_NewAtom(context, "napi_buffer");
    (*env)->atoms.napi_typetag = JS_NewAtom(context, "napi_typetag");
    (*env)->atoms.weakref = JS_NewAtom(context, "WeakRef");

    JS_SetClassProto(context, runtime->externalClassId, JS_NewObject(context));
    JS_SetClassProto(context, runtime->functionClassId, JS_NewObject(context));
    JS_SetClassProto(context, runtime->constructorClassId, JS_NewObject(context));

    JSValue globalValue = JS_GetGlobalObject(context);

    JSValue gc = JS_NewCFunction(context, JSRunGCCallback, "gc", 0);
    JS_SetPropertyStr(context, globalValue, "gc", gc);


    JSValue FinalizationRegistry = JS_GetPropertyStr(context, globalValue, "FinalizationRegistry");
    JSValue FinalizeCallback = JS_NewCFunction(context, JSFinalizeValueCallback, NULL, 0);
    (*env)->finalizationRegistry = JS_CallConstructor(context, FinalizationRegistry, 1,
                                                      &FinalizeCallback);

    (*env)->instanceData = NULL;
    (*env)->isThrowNull = false;
    (*env)->gcBefore = NULL;
    (*env)->gcAfter = NULL;


    LIST_INIT(&(*env)->handleScopeList);
    LIST_INIT(&(*env)->referencesList);

    static const char script[] = "globalThis.CreateBigIntWords = (sign, word) => { "
                                 " const max_v = BigInt(2 ** 64 - 1);"
                                 " var bg = 0n;"
                                 "  for (var i=0; i<word.length/2; i++) {"
                                 "      bg = bg + (BigInt(word[i*2]) * 2n**32n + BigInt(word[i*2 +1])) * (max_v ** BigInt(i));"
                                 "  }"
                                 "  if (sign  !=  0) {"
                                 "      bg = bg * (-1n);"
                                 "  }"
                                 "  return bg;"
                                 "};"
                                 "globalThis.GetBigIntWords = (big) => {"
                                 "const max_v = BigInt(2 ** 64 - 1);"
                                 "var rev = {};"
                                 "rev.sign = 0;"
                                 "rev.count = 0;"
                                 "rev.words = [];"
                                 "if (big < 0n) {"
                                 "    rev.sign = 1;"
                                 "    big = big * (-1n);"
                                 "}"
                                 "while (big >= max_v) {"
                                 "    rev.words[rev.count] = big % max_v;"
                                 "    big = big / max_v;"
                                 "    rev.count++;"
                                 "}"
                                 "rev.words[rev.count] = big % max_v;"
                                 "rev.count++;"
                                 "return rev;"
                                 "};"
                                 "globalThis.NAPISymbolFor = (description) => {return Symbol.for(description)};";

    JSValue result = JS_Eval((*env)->context, script, strlen(script), "<napi_script>",
                             JS_EVAL_TYPE_GLOBAL);

    JS_FreeValue((*env)->context, result);
    JS_FreeValue(context, FinalizationRegistry);
    JS_FreeValue(context, FinalizeCallback);
    JS_FreeValue(context, globalValue);

    return napi_clear_last_error((*env));
}

napi_status qjs_free_napi_env(napi_env env) {
    CHECK_ARG(env)

    // Free all handle scopes
    napi_handle_scope handleScope, tempHandleScope;
    LIST_FOREACH_SAFE(handleScope, &env->handleScopeList, node, tempHandleScope) {
        struct Handle *handle, *tempHandle;
        SLIST_FOREACH_SAFE(handle, &handleScope->handleList, node, tempHandle) {
            JS_FreeValue(env->context, handle->value);
            if (handle->type == HANDLE_HEAP_ALLOCATED) {
                mi_free(handle);
            }
        }
        LIST_REMOVE(handleScope, node);
        if (handleScope->type == HANDLE_HEAP_ALLOCATED) {
            mi_free(handleScope);
        }
    }

    // Free all references
    napi_ref ref, temp;
    LIST_FOREACH_SAFE(ref, &env->referencesList, node, temp) {
        LIST_REMOVE(ref, node);
        JS_FreeValue(env->context, ref->value);
        mi_free(ref);
    }

    // Free Reference Symbol
    JS_FreeValue(env->context, env->referenceSymbolValue);

    // Free Finalization Registry
    JS_FreeValue(env->context, env->finalizationRegistry);

    // Free Instance Data
    if (env->instanceData && env->instanceData->finalizeCallback) {
        env->instanceData->finalizeCallback(env, env->instanceData->data,
                                            env->instanceData->finalizeHint);
        mi_free(env->instanceData);
    };

    if (env->gcAfter != NULL) {
        mi_free(env->gcAfter);
    }

    if (env->gcBefore != NULL) {
        mi_free(env->gcBefore);
    }

    // Free Atoms
    JS_FreeAtom(env->context, env->atoms.napi_external);
    JS_FreeAtom(env->context, env->atoms.registerFinalizer);
    JS_FreeAtom(env->context, env->atoms.buffer);
    JS_FreeAtom(env->context, env->atoms.napi_buffer);
    JS_FreeAtom(env->context, env->atoms.byteLength);
    JS_FreeAtom(env->context, env->atoms.byteOffset);
    JS_FreeAtom(env->context, env->atoms.constructor);
    JS_FreeAtom(env->context, env->atoms.prototype);
    JS_FreeAtom(env->context, env->atoms.name);
    JS_FreeAtom(env->context, env->atoms.length);
    JS_FreeAtom(env->context, env->atoms.is);
    JS_FreeAtom(env->context, env->atoms.freeze);
    JS_FreeAtom(env->context, env->atoms.seal);
    JS_FreeAtom(env->context, env->atoms.Symbol);
    JS_FreeAtom(env->context, env->atoms.NAPISymbolFor);
    JS_FreeAtom(env->context, env->atoms.object);
    JS_FreeAtom(env->context, env->atoms.napi_typetag);
    JS_FreeAtom(env->context, env->atoms.weakref);

    // Free Context
    JS_FreeContext(env->context);


    return napi_clear_last_error(env);
}

napi_status qjs_free_runtime(napi_runtime runtime) {
    assert(runtime);

    napi_env env = (napi_env) JS_GetRuntimeOpaque(runtime->runtime);

    JS_FreeRuntime(runtime->runtime);

    mi_free(env);

    return napi_clear_last_error(env);
}


napi_status qjs_execute_script(napi_env env,
                               napi_value script,
                               const char *file,
                               napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(script)

    JSValue eval_result;
    const char *cScript = JS_ToCString(env->context, *((JSValue *) script));
    js_enter(env);
    eval_result = JS_Eval(env->context, cScript, strlen(cScript), file, JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(env->context, cScript);
    js_exit(env);
    if (JS_IsException(eval_result)) {
        const char *exceptionMessage = JS_ToCString(env->context, eval_result);
        napi_set_last_error(env, napi_cannot_run_js, exceptionMessage, 0, NULL);
        JS_FreeCString(env->context, exceptionMessage);
        JS_Throw(env->context, eval_result);
        return napi_cannot_run_js;
    }

    if (result) {
        struct Handle *returnHandle;
        CreateJSValueHandle(env, eval_result, &returnHandle);
        *result = (napi_value) &returnHandle->value;
    } else {
        JS_FreeValue(env->context, eval_result);
    }

    return napi_clear_last_error(env);
}


napi_status qjs_runtime_before_gc_callback(napi_env env, napi_finalize cb, void *data) {
    CHECK_ARG(env)
    CHECK_ARG(cb)

    ExternalInfo *info = mi_malloc(sizeof(ExternalInfo));
    info->data = data;
    info->finalizeCallback = cb;
    info->finalizeHint = NULL;
    env->gcBefore = info;

    return napi_clear_last_error(env);
}

napi_status qjs_runtime_after_gc_callback(napi_env env, napi_finalize cb, void *data) {
    CHECK_ARG(env)
    CHECK_ARG(cb)

    ExternalInfo *info = mi_malloc(sizeof(ExternalInfo));
    info->data = data;
    info->finalizeCallback = cb;
    info->finalizeHint = NULL;

    env->gcAfter = info;

    return napi_clear_last_error(env);
}

napi_status qjs_execute_pending_jobs(napi_env env) {
    CHECK_ARG(env)
    int error;
    do {
        JSContext *context;
        error = JS_ExecutePendingJob(JS_GetRuntime(env->context), &context);
        if (error == -1) {
            return napi_set_last_error(env, napi_pending_exception, NULL, 0, NULL);
        }
    } while (error != 0);

    return napi_clear_last_error(env);
}

napi_status qjs_update_stack_top(napi_env env) {
    CHECK_ARG(env)
    JS_UpdateStackTop(env->runtime->runtime);
    return napi_clear_last_error(env);
}
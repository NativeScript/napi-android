#include <cassert>
#include <js_native_api.h>
#include <quickjs.h>
#include <sys/queue.h>
#include <climits>

#ifdef ANDROID

#include <android/log.h>

#endif

extern "C"
{
#include "libbf.h"
}

#define TRUTHY(expr) \
    __builtin_expect(expr, false)

// Macros

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = SLIST_FIRST((head)); (var) && ((tvar) = SLIST_NEXT((var), field), 1); (var) = (tvar))
#endif

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = LIST_FIRST((head)); (var) && ((tvar) = LIST_NEXT((var), field), 1); (var) = (tvar))
#endif

// RETURN STATUS IF CONDITION IS FALSE

#define RETURN_STATUS_IF_FALSE(condition, status) \
    if (__builtin_expect(!(condition), false))    \
    {                                             \
        return napi_set_last_error(env, status);  \
    }

// CHECK NAPI

#define CHECK_NAPI(expr)                                \
    {                                                   \
        napi_status status = expr;                      \
        if (__builtin_expect(status != napi_ok, false)) \
        {                                               \
            return napi_set_last_error(env, status);    \
        }                                               \
    }

// CHECK_ARG

#define CHECK_ARG(arg)                                     \
    if (__builtin_expect(!(arg), false))                   \
    {                                                      \
        return napi_set_last_error(env, napi_invalid_arg); \
    }

// JS_GetException will transfer ownership
// JS_Throw will take ownership
// So if there is an exception, first obtain the ownership, then return the ownership, and the final ownership is still there
// In JSContext, QuickJS currently uses JS null to represent normal. In the future, consider writing it like this
// !JS_IsUndefined(exceptionValue) && !JS_IsNull(exceptionValue)
// NAPI_PREAMBLE will also check env->context

#define NAPI_PREAMBLE(env)                                                  \
    {                                                                       \
        CHECK_ARG(env)  \
JSValue exceptionValue = JS_GetException((env)->context);   \
if (__builtin_expect(JS_IsException(exceptionValue), false)) {  \
        print_exception(env, exceptionValue); \
return napi_set_last_error(env, napi_pending_exception);    \
}   \
RETURN_STATUS_IF_FALSE(!(env)->isThrowNull, napi_pending_exception) \
    }

static JSValueConst undefined = JS_UNDEFINED;
static JSValueConst nullValue = JS_NULL;

struct NAPIReference {
    JSValue value; // size_t * 2
    LIST_ENTRY(NAPIReference)
            node;                   // size_t * 2
    uint8_t referenceCount; // 8
};

struct NAPIDeferred {
    napi_value resolve;
    napi_value reject;
};

struct WeakReference {
    LIST_ENTRY(WeakReference)
            node; // size_t * 2
    // Weak references that are currently valid
    LIST_HEAD(, NAPIReference)
            weakRefList; // size_t
    // NAPIFreeEnv will free all NAPIReference, referenceFinalize() only needs to free its own structure
    bool isEnvFreed;
};

typedef struct {
    void *data;                     // size_t
    void *finalizeHint;             // size_t
    napi_finalize finalizeCallback; // size_t
} ExternalInfo;

struct NAPIAtoms {
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
};

// 1. external -> opaque pointer + finalizer + call a callback
// 2. Function -> JSValue data array + finalizer
// 3. Constructor -> .[[prototype]] = new External()
// 4. Reference -> reference count + setWeak/clearWeak -> referenceSymbolValue

struct NAPIEnvironment {
    JSValue referenceSymbolValue; // size_t * 2
    napi_runtime runtime;         // size_t
    JSContext *context;           // size_t
    LIST_HEAD(, NAPIHandleScope)
            handleScopeList; // size_t
    LIST_HEAD(, WeakReference)
            weakReferenceList; // size_t
    LIST_HEAD(, NAPIReference)
            strongRefList; // size_t
    LIST_HEAD(, NAPIReference)
            valueList; // size_t
    // Tells whether the last thrown error was a null value.
    bool isThrowNull;
    ExternalInfo *instanceData;
    JSValue finalizationRegistry;
    napi_extended_error_info last_error;
    NAPIAtoms atoms;
    ExternalInfo* gcBefore;
    ExternalInfo* gcAfter;
};

struct NAPIJSRuntime {
    JSRuntime *runtime;           // size_t
    JSClassID constructorClassId; // uint32_t
    JSClassID functionClassId;    // uint32_t
    JSClassID externalClassId;    // uint32_t
};

// classId 必须为 0 才会被分配
// static JSClassID functionClassId = 0;

struct NAPICallbackInfo {
    JSValueConst newTarget; // size_t * 2
    JSValueConst thisArg;   // size_t * 2
    JSValueConst *argv;     // size_t * 2
    void *data;             // size_t
    int argc;
};

typedef struct {
    void *data;             // size_t
    napi_callback callback; // size_t
} FunctionInfo;

#ifndef NDEBUG
static const char *const JS_GET_GLOBAL_OBJECT_EXCEPTION = "JS_GetGlobalObject() -> JS_EXCEPTION.";
static const char *const FUNCTION_CLASS_ID_ZERO = "functionClassId must not be 0.";

static const char *const CONSTRUCTOR_CLASS_ID_ZERO = "constructorClassId must not be 0.";
static const char *const NAPI_CLOSE_HANDLE_SCOPE_ERROR = "napi_close_handle_scope() return error.";
#endif


void print_exception(napi_env env, JSValue exception) {
    const char *exceptionMessage = JS_ToCString(env->context, exception);
    const char *stack = JS_ToCString(env->context,
                                     JS_GetPropertyStr(env->context, exception, "stack"));

#ifdef ANDROID
    __android_log_print(ANDROID_LOG_ERROR, "NS.Native", "Napi Exception: %s \nStacktrace: %s",
                        exceptionMessage, stack);
#else
    printf("Napi Exception: %s \nStacktrace: %s", exceptionMessage, stack);
#endif


    JS_FreeCString(env->context, stack);
    JS_FreeCString(env->context, exceptionMessage);
}

inline napi_status napi_clear_last_error(napi_env env) {
    if (env->last_error.error_code == napi_ok)
        return napi_ok;
    env->last_error.error_code = napi_ok;
    env->last_error.engine_error_code = 0;
    env->last_error.engine_reserved = nullptr;
    env->last_error.error_message = nullptr;
    return napi_ok;
}

inline napi_status napi_set_last_error(napi_env env,
                                       napi_status error_code,
                                       const char *error_message = "",
                                       uint32_t engine_error_code = 0,
                                       void *engine_reserved = nullptr) {
    if (error_code == napi_ok && env->last_error.error_code == napi_ok)
        return napi_ok;
    env->last_error.error_code = error_code;
    env->last_error.engine_error_code = engine_error_code;
    env->last_error.engine_reserved = engine_reserved;
    env->last_error.error_message = error_message;

    return error_code;
}

static void function_finalizer(JSRuntime *rt, JSValue val) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    napi_runtime runtime = env->runtime;

    if (TRUTHY(!runtime->functionClassId)) {
        assert(false && FUNCTION_CLASS_ID_ZERO);

        return;
    }
    FunctionInfo *functionInfo = (FunctionInfo *) JS_GetOpaque(val, runtime->functionClassId);
    free(functionInfo);
}

static void external_finalizer(JSRuntime *rt, JSValue val) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    napi_runtime runtime = env->runtime;

    if (TRUTHY(!runtime->externalClassId)) {
        assert(false && FUNCTION_CLASS_ID_ZERO);

        return;
    }
    ExternalInfo *externalInfo = (ExternalInfo *) JS_GetOpaque(val, runtime->externalClassId);
    if (externalInfo && externalInfo->finalizeCallback) {
        externalInfo->finalizeCallback(env, externalInfo->data, externalInfo->finalizeHint);
    }
    free(externalInfo);
}

// static JSRuntime *runtime = NULL;

typedef struct {
    FunctionInfo functionInfo;
    JSClassID classId; // uint32_t
} ConstructorInfo;

// static JSClassID constructorClassId = 0;

static void constructorFinalizer(JSRuntime *rt, JSValue val) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    napi_runtime runtime = env->runtime;
    if (TRUTHY(!runtime->constructorClassId)) {
        assert(false && CONSTRUCTOR_CLASS_ID_ZERO);

        return;
    }
    ConstructorInfo *constructorInfo = (ConstructorInfo *) JS_GetOpaque(val,
                                                                        runtime->constructorClassId);
    free(constructorInfo);
}

napi_status NAPICreateJSRuntime(napi_runtime *runtime) {
    assert(runtime);

    *runtime = (NAPIJSRuntime *) malloc(sizeof(struct NAPIJSRuntime));
    if (TRUTHY(!runtime)) {
        return napi_memory_error;
    }

    (*runtime)->runtime = JS_NewRuntime();
    // JS_NewClassID only accept 0.
    // So we initialize classId field to 0.
    (*runtime)->constructorClassId = 0;
    (*runtime)->functionClassId = 0;
    (*runtime)->externalClassId = 0;

    if (TRUTHY(!(*runtime)->runtime)) {
        free(*runtime);

        return napi_memory_error;
    }

    JS_SetMaxStackSize((*runtime)->runtime, 1024 * 1024 * 1024);

    // 一定成功
    JS_NewClassID((*runtime)->runtime, &(*runtime)->constructorClassId);
    JS_NewClassID((*runtime)->runtime, &(*runtime)->functionClassId);
    JS_NewClassID((*runtime)->runtime, &(*runtime)->externalClassId);
    JSClassDef classDef = {"External", external_finalizer, NULL, NULL, NULL};
    // JS_NewClass -> JS_NewClass1 返回值只有 -1 和 0
    int status = JS_NewClass((*runtime)->runtime, (*runtime)->externalClassId, &classDef);
    if (TRUTHY(status == -1)) {
        JS_FreeRuntime((*runtime)->runtime);
        free(*runtime);

        return napi_generic_failure;
    }

    classDef.class_name = "FunctionData";
    classDef.finalizer = function_finalizer;
    status = JS_NewClass((*runtime)->runtime, (*runtime)->functionClassId, &classDef);
    if (TRUTHY(status == -1)) {
        JS_FreeRuntime((*runtime)->runtime);
        free(*runtime);

        return napi_generic_failure;
    }

    classDef.class_name = "ConstructorPrototype";
    classDef.finalizer = constructorFinalizer;
    status = JS_NewClass((*runtime)->runtime, (*runtime)->constructorClassId, &classDef);
    if (TRUTHY(status == -1)) {
        JS_FreeRuntime((*runtime)->runtime);
        free(*runtime);

        return napi_generic_failure;
    }

    return napi_ok;
}

// NAPIGenericFailure/NAPIMemoryError
napi_status NAPICreateEnv(napi_env *env, napi_runtime runtime) {
    assert(env && runtime);

    // Resource - NAPIEnv
    *env = (NAPIEnvironment *) malloc(sizeof(struct NAPIEnvironment));

    if (TRUTHY(!(*env))) {
        return napi_memory_error;
    }

    (*env)->runtime = runtime;

    JS_SetRuntimeOpaque(runtime->runtime, *env);

    // Resource - JSContext
    JSContext *context = JS_NewContext(runtime->runtime);
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

    (*env)->context = context;
    (*env)->isThrowNull = false;
    (*env)->gcBefore = nullptr;
    (*env)->gcAfter = nullptr;

    //    js_std_add_helpers(context, 0, NULL);
    if (TRUTHY(!context)) {
        free(*env);

        return napi_set_last_error((*env), napi_memory_error);
    }

    JSValue prototype = JS_NewObject(context);
    if (TRUTHY(JS_IsException(prototype))) {
        JS_FreeContext(context);
        free(*env);

        return napi_set_last_error((*env), napi_generic_failure);
    }
    JS_SetClassProto(context, runtime->externalClassId, prototype);

    prototype = JS_NewObject(context);
    if (TRUTHY(JS_IsException(prototype))) {
        JS_FreeContext(context);
        free(*env);

        return napi_set_last_error((*env), napi_generic_failure);
    }

    JS_SetClassProto(context, runtime->constructorClassId, prototype);
    const char *string = "(function () { return Symbol(\"reference\") })();";
    (*env)->referenceSymbolValue =
            JS_Eval(context, string, strlen(string), "https://n-api.com/qjs_reference_symbol.js",
                    JS_EVAL_TYPE_GLOBAL);
    if (TRUTHY(JS_IsException((*env)->referenceSymbolValue))) {
        JS_FreeContext(context);
        free(*env);

        return napi_set_last_error((*env), napi_generic_failure);
    }

    JSValue gc = JS_NewCFunction(context, [](JSContext *ctx, JSValue this_val, int argc, JSValue *argv) -> JSValue {
        JS_RunGC(JS_GetRuntime(ctx));
        return JS_TRUE;
    }, "gc", 0);

    JSValue globalValue = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, globalValue, "gc", gc);

    JSValue jsNativeEngine = (JSValue) JS_MKPTR(JS_TAG_INT, (*env));


    JSValue FinalizationRegistry = JS_GetPropertyStr(context, globalValue, "FinalizationRegistry");
    JSValue FinalizeCallback = JS_NewCFunction(context,
                                               [](JSContext *ctx, JSValueConst this_val, int argc,
                                                  JSValueConst *argv) -> JSValue {

                                                   napi_env env = (napi_env) JS_GetRuntimeOpaque(
                                                           JS_GetRuntime(ctx));
                                                   JSValue heldValue = argv[0];
                                                   if (!JS_IsUndefined(heldValue)) {
                                                       ExternalInfo *info = (ExternalInfo *) JS_GetOpaqueUnsafe(
                                                               heldValue);
                                                       if (info != nullptr) {
                                                           info->finalizeCallback(env, info->data,
                                                                                  info->finalizeHint);
                                                       }
                                                   }
                                                   return JS_UNDEFINED;
                                               }, "FinalizationRegistryCallback",
                                               strlen("FinalizationRegistryCallback"));

    (*env)->finalizationRegistry = JS_CallConstructor(context, FinalizationRegistry, 1,
                                                      &FinalizeCallback);
    JS_FreeValue(context, FinalizationRegistry);
    JS_FreeValue(context, FinalizeCallback);
    JS_FreeValue(context, globalValue);

    LIST_INIT(&(*env)->handleScopeList);
    LIST_INIT(&(*env)->weakReferenceList);
    LIST_INIT(&(*env)->valueList);
    LIST_INIT(&(*env)->strongRefList);

    (*env)->instanceData = NULL;

    const char script[] = "globalThis.CreateBigIntWords = (sign, word) => { "
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

    JSValue func = JS_Eval((*env)->context, script, strlen(script), "<napi_script>",
                           JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue((*env)->context, func);

    return napi_clear_last_error((*env));
}

napi_status NAPIFreeRuntime(napi_runtime runtime) {
    assert(runtime);

    napi_env env = (napi_env) JS_GetRuntimeOpaque(runtime->runtime);

    JS_FreeRuntime(runtime->runtime);

    free(env);

    return napi_ok;
}



// Object lifetime management

// Making handle lifespan shorter than that of the native method

struct Handle {
    JSValue value; // size_t * 2
    SLIST_ENTRY(Handle)
            node; // size_t
};

struct NAPIHandleScope {
    LIST_ENTRY(NAPIHandleScope)
            node; // size_t
    SLIST_HEAD(, Handle)
            handleList; // size_t
};

struct NAPIEscapableHandleScope {
    LIST_ENTRY(NAPIEscapableHandleScope)
            node; // size_t
    struct NAPIHandleScope handleScope;
    bool escapeCalled;
};

/**
 A pool of reusable handles & scopes.
 */
typedef struct NAPIHandlePool {
    LIST_HEAD(ScopeList, NAPIHandleScope)
            scopeList;
    SLIST_HEAD(HandleList, Handle)
            handleList;
    LIST_HEAD(EscapableScopeList, NAPIEscapableHandleScope)
            escapableScopeList;

} NAPIHandlePool;

static NAPIHandlePool globalHandlePool; // Define a global handle pool

/**
 Creates a handle & adds it to current handle scope. This function does not modify the reference count or ownership.
 */
static inline napi_status CreateJSValueHandle(napi_env env, JSValue value, struct Handle **result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    RETURN_STATUS_IF_FALSE(!LIST_EMPTY(&env->handleScopeList), napi_handle_scope_empty)

    // Try to reuse a handle from the pool
    if (!SLIST_EMPTY(&globalHandlePool.handleList)) {
        *result = SLIST_FIRST(&globalHandlePool.handleList);
        SLIST_REMOVE_HEAD(&globalHandlePool.handleList, node);
    } else {
        // Allocate a new handle if the pool is empty
        *result = (struct Handle *) malloc(sizeof(struct Handle));
    }

    RETURN_STATUS_IF_FALSE(*result, napi_memory_error)

    // Initialize the handle with the provided value
    (*result)->value = value;

    // Insert the handle into the current handle scope
    napi_handle_scope handleScope = LIST_FIRST(&env->handleScopeList);
    SLIST_INSERT_HEAD(&handleScope->handleList, *result, node);

    return napi_clear_last_error(env);
}

napi_status napi_open_handle_scope(napi_env env, napi_handle_scope *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    if (LIST_EMPTY(&globalHandlePool.scopeList)) {
        // If the scope pool is empty, allocate a new scope
        NAPIHandleScope *handleScope = (NAPIHandleScope *) malloc(sizeof(NAPIHandleScope));
        RETURN_STATUS_IF_FALSE(handleScope, napi_memory_error)
        *result = handleScope;
        SLIST_INIT(&(*result)->handleList);
    } else {
        // Reuse a scope from the pool
        *result = LIST_FIRST(&globalHandlePool.scopeList);
        LIST_REMOVE(*result, node);
    }

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
        handle->value = undefined;

        // Instead of freeing, return the handle to the pool for reuse
        SLIST_REMOVE(&scope->handleList, handle, Handle, node);
        SLIST_INSERT_HEAD(&globalHandlePool.handleList, handle, node);
    }

    LIST_REMOVE(scope, node);
    LIST_INSERT_HEAD(&globalHandlePool.scopeList, scope, node); // Return the scope to the pool

    return napi_clear_last_error(env);
}

napi_status napi_open_escapable_handle_scope(napi_env env, napi_escapable_handle_scope *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    if (LIST_EMPTY(&globalHandlePool.escapableScopeList)) {
        // If the scope pool is empty, allocate a new scope
        NAPIEscapableHandleScope *handleScope = (NAPIEscapableHandleScope *) malloc(
                sizeof(struct NAPIEscapableHandleScope));
        RETURN_STATUS_IF_FALSE(handleScope, napi_memory_error)
        *result = handleScope;
        SLIST_INIT(&(*result)->handleScope.handleList);
    } else {
        // Reuse a scope from the pool
        *result = LIST_FIRST(&globalHandlePool.escapableScopeList);
        LIST_REMOVE(*result, node);
    }

    (*result)->escapeCalled = false;

    LIST_INSERT_HEAD(&env->handleScopeList, &(*result)->handleScope, node);

    return napi_clear_last_error(env);
}

napi_status
napi_close_escapable_handle_scope(napi_env env, napi_escapable_handle_scope escapableScope) {
    CHECK_ARG(env)
    CHECK_ARG(escapableScope)

    NAPIHandleScope *scope = (NAPIHandleScope *) &escapableScope->handleScope;

    assert(LIST_FIRST(&env->handleScopeList) == scope &&
           "napi_close_handle_scope() or napi_close_escapable_handle_scope() should follow FILO rule.");

    Handle *handle, *tempHandle;
    SLIST_FOREACH_SAFE(handle, &scope->handleList, node, tempHandle) {
        JS_FreeValue(env->context, handle->value);
        handle->value = undefined;

        // Instead of freeing, return the handle to the pool for reuse
        SLIST_REMOVE(&scope->handleList, handle, Handle, node);
        SLIST_INSERT_HEAD(&globalHandlePool.handleList, handle, node);
    }

    LIST_REMOVE(scope, node);
    LIST_INSERT_HEAD(&globalHandlePool.escapableScopeList, escapableScope,
                     node); // Return the scope to the pool

    return napi_clear_last_error(env);
}

// NAPIMemoryError/NAPIEscapeCalledTwice/NAPIHandleScopeEmpty
napi_status napi_escape_handle(napi_env env, napi_escapable_handle_scope scope, napi_value escapee,
                               napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(scope)
    CHECK_ARG(escapee)

    RETURN_STATUS_IF_FALSE(!scope->escapeCalled, napi_escape_called_twice)
    // Get the outer handle scope
    napi_handle_scope handleScope = LIST_NEXT(&scope->handleScope, node);
    RETURN_STATUS_IF_FALSE(handleScope, napi_handle_scope_empty)

    struct Handle *handle;

    // Try to reuse a handle from the pool
    if (!SLIST_EMPTY(&globalHandlePool.handleList)) {
        handle = SLIST_FIRST(&globalHandlePool.handleList);
        SLIST_REMOVE_HEAD(&globalHandlePool.handleList, node);
    } else {
        // Allocate a new handle if the pool is empty
        handle = (struct Handle *) malloc(sizeof(struct Handle));
    }

    RETURN_STATUS_IF_FALSE(handle, napi_memory_error)

    scope->escapeCalled = true;
    handle->value = JS_DupValue(env->context, *((JSValue *) escapee));
    SLIST_INSERT_HEAD(&handleScope->handleList, handle, node);

    if (result != nullptr) {
        *result = (napi_value) &handle->value;
    }

    return napi_clear_last_error(env);
}

// Error Handling

napi_status napi_throw(napi_env env, napi_value error) {

    CHECK_ARG(env)
    CHECK_ARG(error)

    if (!JS_IsNull(*((JSValue *) error))) {
        JS_Throw(env->context, JS_DupValue(env->context, *((JSValue *) error)));
    } else {
        env->isThrowNull = true;
    }

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
    CHECK_ARG(code)
    CHECK_ARG(msg)

    JS_ThrowRangeError(env->context, "code: %s, message: %s\n", code, msg);

    return napi_clear_last_error(env);
}

napi_status napi_throw_type_error(napi_env env, const char *code, const char *msg) {

    CHECK_ARG(env)
    CHECK_ARG(code)
    CHECK_ARG(msg)

    JS_ThrowTypeError(env->context, "code: %s, message: %s\n", code, msg);

    return napi_clear_last_error(env);
}

napi_status napi_is_exception_pending(napi_env env, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue exception = JS_GetException(env->context);

    *result = !JS_IsNull(exception);

    if (!*result) {
        JS_Throw(env->context, exception);
    };

    return napi_clear_last_error(env);
}

napi_status napi_get_and_clear_last_exception(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)
    CHECK_ARG(env->context)

    JSValue exceptionValue = JS_GetException(env->context);
    if (JS_IsNull(exceptionValue) && !env->isThrowNull) {
        *result = (napi_value) &undefined;

        return napi_clear_last_error(env);
    }
    if (env->isThrowNull) {
        env->isThrowNull = false;
        *result = (napi_value) &nullValue;

        return napi_clear_last_error(env);
    }

    struct Handle *exceptionHandle;
    napi_status status = CreateJSValueHandle(env, exceptionValue, &exceptionHandle);
    if (TRUTHY(status != napi_ok)) {
        JS_Throw(env->context, exceptionValue);

        return napi_set_last_error(env, status);
    }
    *result = (napi_value) &exceptionHandle->value;

    return napi_clear_last_error(env);
}

napi_status napi_get_last_error_info(napi_env env, const napi_extended_error_info **result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = env->last_error.error_code == napi_ok ? nullptr : &env->last_error;

    return napi_ok;
}

// References to values with a lifespan longer than that of the native method

// static uint8_t contextCount = 0;

static void reference_finalize(napi_env env, void *finalizeData, void *finalizeHint) {
    // finalizeHint => env is unreliable
    if (!env) {
        assert(false);

        return;
    }

    struct WeakReference *referenceInfo = (WeakReference *) finalizeData;

    // isEnvFreed needs to exist because there are three situations
    // 1. env is destructed
    // 2. env is still there, and there is referenceInfo, but there is no weakRef
    // 3. env is still there, there is also referenceInfo, and there is also weakRef
    if (!referenceInfo->isEnvFreed) {
        napi_ref reference, temp;
        LIST_FOREACH_SAFE(reference, &referenceInfo->weakRefList, node, temp) {
            // If you enter the loop, it means env is valid
            assert(!reference->referenceCount);
            reference->value = JS_UNDEFINED;
            LIST_REMOVE(reference, node);
            // So here finalizeHint is still valid env
            LIST_INSERT_HEAD(&env->valueList, reference, node);
        }
        // Currently not the last GC
        // LIST_REMOVE will affect the env structure
        LIST_REMOVE(referenceInfo, node);
    }

    free(referenceInfo);
}

// NAPINameExpected/NAPIMemoryError/NAPIGenericFailure + napi_create_string_utf8 + napi_get_property + napi_typeof +
// napi_create_external
// + napi_get_value_external + napi_set_property
static napi_status setWeak(napi_env env, napi_value value, napi_ref ref) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(ref)

    RETURN_STATUS_IF_FALSE(JS_IsSymbol(env->referenceSymbolValue), napi_name_expected)

    napi_handle_scope handleScope = NULL;
    napi_open_handle_scope(env, &handleScope);

    napi_value referenceValue;
    CHECK_NAPI(
            napi_get_property(env, value, (napi_value) &env->referenceSymbolValue, &referenceValue))
    napi_valuetype valueType;
    CHECK_NAPI(napi_typeof(env, referenceValue, &valueType))
    RETURN_STATUS_IF_FALSE(valueType == napi_undefined || valueType == napi_external,
                           napi_generic_failure)

    struct WeakReference *referenceInfo;
    if (valueType == napi_undefined) {
        referenceInfo = (WeakReference *) malloc(sizeof(WeakReference));
        RETURN_STATUS_IF_FALSE(referenceInfo, napi_memory_error)
        referenceInfo->isEnvFreed = false;
        LIST_INIT(&referenceInfo->weakRefList);
        {
            napi_status status =
                    napi_create_external(env, referenceInfo, reference_finalize, nullptr,
                                         &referenceValue);
            if (TRUTHY(status != napi_ok)) {
                napi_close_handle_scope(env, handleScope);
                free(referenceInfo);

                return napi_set_last_error(env, status);
            }
        }
        CHECK_NAPI(napi_set_property(env, value, (napi_value) &env->referenceSymbolValue,
                                     referenceValue))
        LIST_INSERT_HEAD(&env->weakReferenceList, referenceInfo, node);
    } else {
        CHECK_NAPI(napi_get_value_external(env, referenceValue, (void **) &referenceInfo))
        if (TRUTHY(!referenceInfo)) {
            napi_close_handle_scope(env, handleScope);
            assert(false);

            return napi_set_last_error(env, napi_generic_failure);
        }
    }
    LIST_INSERT_HEAD(&referenceInfo->weakRefList, ref, node);

    napi_close_handle_scope(env, handleScope);

    return napi_clear_last_error(env);
}

// NAPINameExpected/NAPIGenericFailure + napi_create_string_utf8 + napi_get_property + napi_get_value_external +
// napi_delete_property
static napi_status clearWeak(napi_env env, napi_ref ref) {
    NAPI_PREAMBLE(env)
    CHECK_ARG(ref)

    RETURN_STATUS_IF_FALSE(JS_IsSymbol(env->referenceSymbolValue), napi_name_expected)

    napi_handle_scope handleScope = NULL;
    napi_open_handle_scope(env, &handleScope);

    napi_value externalValue;
    CHECK_NAPI(napi_get_property(env, (napi_value) &ref->value,
                                 (napi_value) &env->referenceSymbolValue, &externalValue))
    struct WeakReference *referenceInfo;
    CHECK_NAPI(napi_get_value_external(env, externalValue, (void **) &referenceInfo))

    napi_close_handle_scope(env, handleScope);

    if (TRUTHY(!referenceInfo)) {
        assert(false);

        return napi_set_last_error(env, napi_generic_failure);
    }
    bool needDelete = LIST_FIRST(&referenceInfo->weakRefList) == ref && !LIST_NEXT(ref, node);
    LIST_REMOVE(ref, node);
    if (needDelete) {
        bool deleteResult;
        // 一定不会在这里触发 GC
        CHECK_NAPI(napi_delete_property(env, (napi_value) &ref->value,
                                        (napi_value) &env->referenceSymbolValue, &deleteResult))
        RETURN_STATUS_IF_FALSE(deleteResult, napi_generic_failure)
    }

    return napi_clear_last_error(env);
}

// NAPIMemoryError + setWeak
napi_status
napi_create_reference(napi_env env, napi_value value, uint32_t initialRefCount, napi_ref *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    *result = (NAPIReference *) malloc(sizeof(NAPIReference));
    RETURN_STATUS_IF_FALSE(*result, napi_memory_error)

    if (JS_IsUndefined(*((JSValue *) value))) {
        (*result)->value = JS_UNDEFINED;
        (*result)->referenceCount = 0;
        return napi_ok;
    }


    // scalar && weak reference
    if (!JS_IsObject(*((JSValue *) value)) && !initialRefCount) {
        (*result)->referenceCount = 0;
        (*result)->value = JS_UNDEFINED;
        LIST_INSERT_HEAD(&env->valueList, *result, node);

        return napi_clear_last_error(env);
    }

    // Object || Strong reference
    (*result)->value = *((JSValue *) value);
    (*result)->referenceCount = initialRefCount;

    // Strong reference
    if (initialRefCount) {
        (*result)->value = JS_DupValue(env->context, (*result)->value);
        LIST_INSERT_HEAD(&env->strongRefList, *result, node);

        return napi_clear_last_error(env);
    }

    // Object && weak reference
    // setWeak
    napi_status status = setWeak(env, value, *result);

    if (TRUTHY(status != napi_ok)) {
        free(*result);

        return napi_set_last_error(env, status);
    }

    return napi_clear_last_error(env);
}

// NAPIGenericFailure + clearWeak
napi_status napi_delete_reference(napi_env env, napi_ref ref) {

    NAPI_PREAMBLE(env)
    CHECK_ARG(ref)

    // Scalar && weak reference (this will also happen if it is GCed)
    if (!JS_IsObject(ref->value) && !ref->referenceCount) {
        LIST_REMOVE(ref, node);
        free(ref);

        return napi_clear_last_error(env);
    }

    // Object || Strong reference
    if (ref->referenceCount) {
        LIST_REMOVE(ref, node);
        JS_FreeValue(env->context, ref->value);
        free(ref);

        return napi_clear_last_error(env);
    }

    // Object && weak reference
    CHECK_NAPI(clearWeak(env, ref))
    free(ref);

    return napi_clear_last_error(env);
}

// NAPIGenericFailure + clearWeak
napi_status napi_reference_ref(napi_env env, napi_ref ref, uint32_t *result) {

    NAPI_PREAMBLE(env)
    CHECK_ARG(ref)

    if (!ref->referenceCount) {
        // 弱引用
        JSValue value = JS_DupValue(env->context, ref->value);
        if (JS_IsObject(ref->value)) {
            CHECK_NAPI(clearWeak(env, ref))
        } else {
            LIST_REMOVE(ref, node);
        }
        LIST_INSERT_HEAD(&env->strongRefList, ref, node);
        ref->value = value;
    }
    uint8_t count = ++ref->referenceCount;
    if (result) {
        *result = count;
    }

    return napi_clear_last_error(env);
}

// NAPIGenericFailure + setWeak
napi_status napi_reference_unref(napi_env env, napi_ref ref, uint32_t *result) {

    NAPI_PREAMBLE(env)
    CHECK_ARG(ref)

    RETURN_STATUS_IF_FALSE(ref->referenceCount, napi_generic_failure)

    if (ref->referenceCount == 1) {
        LIST_REMOVE(ref, node);
        if (JS_IsObject(ref->value)) {
            CHECK_NAPI(setWeak(env, (napi_value) &ref->value, ref))
            JS_FreeValue(env->context, ref->value);
        } else {
            LIST_INSERT_HEAD(&env->valueList, ref, node);
            JS_FreeValue(env->context, ref->value);
            ref->value = JS_UNDEFINED;
        }
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
        *result = (napi_value) &undefined;
    } else {
        JSValue strongValue = ref->value;
        struct Handle *handle;

        napi_status status = CreateJSValueHandle(env, strongValue, &handle);
        if (TRUTHY(status != napi_ok)) {
            JS_FreeValue(env->context, strongValue);

            return napi_set_last_error(env, status);
        }

        *result = (napi_value) &handle->value;
    }

    return napi_clear_last_error(env);
}

// Cleanup on exit of the current Node.js environment

// napi_add_env_cleanup_hook
// napi_remove_env_cleanup_hook
// napi_add_async_cleanup_hook
// napi_remove_async_cleanup_hook

// Object creation function

// Created values must be added to current HandleScope

static inline napi_status CreateScopedResult(napi_env env, JSValue value, napi_value *result) {
    struct Handle *jsValueHandle;
    napi_status status = CreateJSValueHandle(env, value, &jsValueHandle);
    if (status != napi_ok) {
        JS_FreeValue(env->context, value);
        return napi_set_last_error(env, status);
    }
    *result = (napi_value) &jsValueHandle->value;
    return napi_clear_last_error(env);
}

napi_status napi_create_error(napi_env env, napi_value code, napi_value msg, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue error = JS_NewError(env->context);
    JSValue msgValue = *((JSValue *) msg);

    JS_DefinePropertyValueStr(env->context, error, "message", JS_DupValue(env->context, msgValue),
                              JS_PROP_C_W_E);

    if (code != nullptr) {
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

    if (code != nullptr) {
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

    if (code != nullptr) {
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

    if (code != nullptr) {
        JSValue codeValue = *((JSValue *) code);
        JS_DefinePropertyValueStr(env->context, error, "code", JS_DupValue(env->context, codeValue),
                                  JS_PROP_C_W_E);
    }

    return CreateScopedResult(env, error, result);
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

napi_status napi_create_arraybuffer(napi_env env,
                                    size_t byte_length,
                                    void **data,
                                    napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    size_t size = 0;
    JSValue value = JS_NewArrayBufferCopy(env->context, nullptr, byte_length);

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
    JSValue value = JS_NewArrayBufferCopy(env->context, nullptr, size);

    if (data) {
        *data = JS_GetArrayBuffer(env->context, &size, value);
    }

    MARK_AS_NAPI_BUFFER

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

napi_status napi_create_date(napi_env env,
                             double time,
                             napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_NewDate(env->context, time);

    return CreateScopedResult(env, value, result);
}

napi_status
napi_create_external(napi_env env, void *data, napi_finalize finalize_cb, void *finalize_hint,
                     napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    ExternalInfo *externalInfo = (ExternalInfo *) malloc(sizeof(ExternalInfo));
    RETURN_STATUS_IF_FALSE(externalInfo, napi_memory_error)

    externalInfo->data = data;
    externalInfo->finalizeHint = finalize_hint;
    externalInfo->finalizeCallback = NULL;

    if (TRUTHY(!env->runtime->externalClassId)) {
        assert(false && "externalClassId must not be 0.");
        free(externalInfo);

        return napi_set_last_error(env, napi_generic_failure);
    }
    JSValue object = JS_NewObjectClass(env->context, (int) env->runtime->externalClassId);

    if (TRUTHY(JS_IsException(object))) {
        free(externalInfo);

        return napi_set_last_error(env, napi_pending_exception);
    }
    JS_SetOpaque(object, externalInfo);

    napi_status status = CreateScopedResult(env, object, result);

    // Set the callback at last when everything is ok.
    externalInfo->finalizeCallback = finalize_cb;

    return napi_clear_last_error(env);
}

struct ExternalBufferInfo {
    void *hint;
    napi_finalize finalize_cb;
};

void buffer_finalizer(JSRuntime *rt, void *opaque, void *data) {
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);

    auto external_buffer_info = (ExternalBufferInfo *) opaque;
    if (external_buffer_info != nullptr) {
        external_buffer_info->finalize_cb(env, data, external_buffer_info->hint);
        delete external_buffer_info;
    }
}

napi_status napi_create_external_arraybuffer(napi_env env,
                                             void *external_data,
                                             size_t byte_length,
                                             napi_finalize finalize_cb,
                                             void *finalize_hint,
                                             napi_value *result) {

    NAPI_PREAMBLE(env)
    CHECK_ARG(external_data)
    CHECK_ARG(byte_length)
    CHECK_ARG(result)

    JSValue value;
    if (finalize_cb) {
        ExternalBufferInfo *external_arraybuffer_info = (ExternalBufferInfo *) malloc(
                sizeof(ExternalBufferInfo));
        external_arraybuffer_info->finalize_cb = finalize_cb;
        external_arraybuffer_info->hint = finalize_hint;

        value = JS_NewArrayBuffer(env->context, (uint8_t *) external_data, byte_length,
                                  buffer_finalizer, external_arraybuffer_info, false);
    } else {
        value = JS_NewArrayBuffer(env->context, (uint8_t *) external_data, byte_length, nullptr,
                                  nullptr, false);
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
        ExternalBufferInfo *external_arraybuffer_info = (ExternalBufferInfo *) malloc(
                sizeof(ExternalBufferInfo));
        external_arraybuffer_info->finalize_cb = finalize_cb;
        external_arraybuffer_info->hint = finalize_hint;
        value = JS_NewArrayBuffer(env->context, (uint8_t *) data, length, buffer_finalizer,
                                  external_arraybuffer_info, false);
    } else {
        value = JS_NewArrayBuffer(env->context, (uint8_t *) data, length, nullptr, nullptr, false);
    }

    MARK_AS_NAPI_BUFFER;

    return CreateScopedResult(env, value, result);
}

napi_status napi_create_object(napi_env env, napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue value = JS_NewObject(env->context);

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

// Map typed array types to constructor names
const char *typedArrayClassNames[] = {
        "Int8Array",
        "Uint8Array",
        "Uint8ClampedArray",
        "Int16Array",
        "Uint16Array",
        "Int32Array",
        "Uint32Array",
        "BigInt64Array",
        "BigUint64Array",
        "Float32Array",
        "Float64Array",
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
        return napi_set_last_error(env, napi_invalid_arg);
    }

    if (!JS_IsArrayBuffer2(env->context, *(JSValue *) arraybuffer)) {
        return napi_set_last_error(env, napi_arraybuffer_expected);
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
        return napi_set_last_error(env, napi_generic_failure);
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
        return napi_set_last_error(env, napi_invalid_arg);
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

/**
 * Functions to convert from C types to Node-API
 * https://nodejs.org/api/n-api.html#functions-to-convert-from-c-types-to-node-api
 */

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
    constexpr size_t Count = 20;
    constexpr size_t Two = 2;
    if (wordCount <= 0 || wordCount > Count || words == nullptr) {
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
    if (wordCount == nullptr) {
        return false;
    }

    JSValue jsValue = JS_GetPropertyStr(context, value, "count");
    if (!JS_IsException(jsValue)) {
        JS_ToInt32(context, &cntValue, jsValue);
        JS_FreeValue(context, jsValue);
    } else {
        return false;
    }

    if (signBit == nullptr && words == nullptr) {
        *wordCount = cntValue;
        return true;
    } else if (signBit != nullptr && words != nullptr) {
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
    if (wordCount == nullptr) {
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

struct JS_BigFloatExt {
    JSRefCountHeader header;
    bf_t num;
};

bool JS_ToInt64WithBigInt(JSContext *context, JSValueConst value, int64_t *pres, bool *lossless) {
    if (pres == nullptr || lossless == nullptr) {
        return 0;
    }

    bool rev = false;
    JSValue val = JS_DupValue(context, value);
    JS_BigFloatExt *p = (JS_BigFloatExt *) JS_VALUE_GET_PTR(val);
    if (p) {
        int opFlag = bf_get_int64(pres, &p->num, 0);
        if (lossless != nullptr) {
            *lossless = (opFlag == 0);
        }
        rev = true;
    }
    JS_FreeValue(context, val);
    return rev;
}

bool JS_ToUInt64WithBigInt(JSContext *context, JSValueConst value, uint64_t *pres, bool *lossless) {
    if (pres == nullptr || lossless == nullptr) {
        return false;
    }

    bool rev = false;
    JSValue val = JS_DupValue(context, value);
    JS_BigFloatExt *p = (JS_BigFloatExt *) JS_VALUE_GET_PTR(val);
    if (p) {
        int opFlag = bf_get_uint64(pres, &p->num);
        if (lossless != nullptr) {
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


/**
 * Functions to convert from Node-API to C types
 * https://nodejs.org/api/n-api.html#functions-to-convert-from-node-api-to-c-types
 */

napi_status napi_get_array_length(napi_env env,
                                  napi_value value,
                                  uint32_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) value);

    if (!JS_IsArray(env->context, jsValue)) {
        return napi_set_last_error(env, napi_array_expected);
    }

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
    CHECK_ARG(data)
    CHECK_ARG(byte_length)

    size_t size = 0;
    JSValue value = *((JSValue *) arraybuffer);

    if (!JS_IsArrayBuffer2(env->context, value)) {
        return napi_set_last_error(env, napi_arraybuffer_expected);
    }

    if (JS_HasProperty(env->context, value, env->atoms.napi_buffer)) {
        return napi_invalid_arg;
    }

    if (data) {
        *data = JS_GetArrayBuffer(env->context, &size, value);
    }

    if (byte_length) {
        *byte_length = size;
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

    if (!JS_IsArrayBuffer2(env->context, value)) {
        return napi_set_last_error(env, napi_arraybuffer_expected);
    }

    if (!JS_HasProperty(env->context, value, env->atoms.napi_buffer)) {
        return napi_set_last_error(env, napi_invalid_arg);
    }

    if (data) {
        *data = JS_GetArrayBuffer(env->context, &size, value);
    }

    if (length) {
        *length = size;
    }

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
        return napi_set_last_error(env, napi_object_expected);

    JSValue prototype = JS_GetProperty(env->context, value, env->atoms.prototype);

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

// Custom function
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
    if (typedArrayType == -1) {
        return napi_set_last_error(env, napi_invalid_arg);
    }

    JSValue value = *((JSValue *) typedarray);

    if (type) {
        *type = (napi_typedarray_type) typedArrayType;
    }

    if (length) {
        JSValue byteLength = JS_GetPropertyStr(env->context, value, "byteLength");
        *length = JS_VALUE_GET_INT(byteLength);
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
        return napi_set_last_error(env, napi_invalid_arg);
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
        return napi_set_last_error(env, napi_date_expected);
    }

    JSValue timeValue = JS_GetPropertyStr(env->context, jsValue, "getTime");
    JSValue time = JS_Call(env->context, timeValue, jsValue, 0, nullptr);
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
        return napi_set_last_error(env, napi_boolean_expected);
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
        return napi_set_last_error(env, napi_number_expected);
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_value_bigint_int64(napi_env env,
                                        napi_value value,
                                        int64_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    if (!JS_IsBigInt(env->context, *(JSValue *) value)) {
        return napi_set_last_error(env, napi_bigint_expected);
    }

    bool lossless = true;
    JS_ToInt64WithBigInt(env->context, *(JSValue *) value, result, &lossless);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_bigint_uint64(napi_env env,
                                         napi_value value,
                                         uint64_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    if (!JS_IsBigInt(env->context, *(JSValue *) value)) {
        return napi_set_last_error(env, napi_bigint_expected);
    }

    bool lossless = true;
    JS_ToUInt64WithBigInt(env->context, *(JSValue *) value, result, &lossless);

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
        return napi_set_last_error(env, napi_bigint_expected);
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
        return napi_set_last_error(env, napi_object_expected);
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
        return napi_set_last_error(env, napi_number_expected);
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
        return napi_set_last_error(env, napi_number_expected);
    }

    JS_ToInt64(env->context, result, jsValue);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_string_latin1(napi_env env,
                                         napi_value value,
                                         char *buf,
                                         size_t bufsize,
                                         size_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)
    CHECK_ARG(result)

    size_t l = 0;

    const char *str = JS_ToCStringLen(env->context, &l, *((JSValue *) value));
    if (str == nullptr) {
        return napi_set_last_error(env, napi_string_expected);
    }

    if (result != nullptr) {
        *result = l;
    }

    if (buf != nullptr && bufsize != 0) {
        if (l > bufsize) {
            strncpy(buf, str, bufsize);
            buf[bufsize - 1] = '\0';
        } else {
            strcpy(buf, str);
        }
    }

    JS_FreeCString(env->context, str);

    return napi_clear_last_error(env);
}

napi_status napi_get_value_string_utf8(napi_env env, napi_value value, char *str, size_t length,
                                       size_t *result) {
    CHECK_ARG(env)
    CHECK_ARG(value)

    if (!JS_IsString(*((JSValue *) value))) {
        return napi_set_last_error(env, napi_string_expected);
    }

    size_t cstr_len = 0;
    const char *cstr = JS_ToCStringLen(env->context, &cstr_len, *((JSValue *) value));

    if (result != nullptr) {
        *result = cstr_len;
    }

    if (str != nullptr && length != 0) {
        strncpy(str, cstr, cstr_len > length ? length : cstr_len);

        if (cstr_len < length) {
            str[cstr_len] = '\0';
        }
    }
    JS_FreeCString(env->context, cstr);
    return napi_clear_last_error(env);
}

size_t Utf8CodePointLen(uint8_t ch) {
    constexpr uint8_t offset = 3;
    return ((0xe5000000 >> ((ch >> offset) & 0x1e)) & offset) + 1;
}

void Utf8ShiftAndMask(uint32_t *codePoint, const uint8_t byte) {
    *codePoint <<= 6;
    *codePoint |= 0x3F & byte;
}

uint32_t Utf8ToUtf32CodePoint(const char *src, size_t length) {
    uint32_t unicode = 0;
    constexpr size_t lengthSizeOne = 1;
    constexpr size_t lengthSizeTwo = 2;
    constexpr size_t lengthSizeThree = 3;
    constexpr size_t lengthSizeFour = 4;
    constexpr size_t offsetZero = 0;
    constexpr size_t offsetOne = 1;
    constexpr size_t offsetTwo = 2;
    constexpr size_t offsetThree = 3;
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
    constexpr uint8_t offset = 10;
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

    if (str == nullptr) {
        return napi_set_last_error(env, napi_string_expected);
    }

    auto ret = Utf8ToUtf16Length(str, strlen(str));
    if (ret == -1) {
        JS_FreeCString(env->context, str);
        return napi_set_last_error(env, napi_generic_failure);
    }

    if (result) {
        *result = ret;
    }

    if (buf != nullptr) {
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
        return napi_set_last_error(env, napi_number_expected);
    }

    JS_ToUint32(env->context, result, jsValue);

    return napi_clear_last_error(env);
}

/**
 * Functions to get global instances
 * https://nodejs.org/api/n-api.html#functions-to-get-global-instances
 */

static JSValue trueValue = JS_TRUE;
static JSValue falseValue = JS_FALSE;

napi_status napi_get_boolean(napi_env env, bool value, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = (napi_value) (value ? &trueValue : &falseValue);

    return napi_clear_last_error(env);
}

napi_status napi_get_global(napi_env env, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    JSValue globalValue = JS_GetGlobalObject(env->context);

    if (TRUTHY(JS_IsException(globalValue))) {
        assert(false && JS_GET_GLOBAL_OBJECT_EXCEPTION);
        return napi_set_last_error(env, napi_generic_failure);
    }

    return CreateScopedResult(env, globalValue, result);
}

napi_status napi_get_null(napi_env env, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = (napi_value) &nullValue;

    return napi_clear_last_error(env);
}

napi_status napi_get_undefined(napi_env env, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(result)

    *result = (napi_value) &undefined;

    return napi_clear_last_error(env);
}

/**
 * https://nodejs.org/api/n-api.html#working-with-javascript-values-and-abstract-operations
 * Working with JavaScript values and abstract operations
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
        JS_FreeValue(env->context, jsResult);
        return napi_set_last_error(env, napi_pending_exception);
    }

    return CreateScopedResult(env, jsResult, result);
}

// static JSClassID externalClassId = 0;
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
        return napi_set_last_error(env, napi_invalid_arg);
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

    if (status) {

        if (JS_HasProperty(env->context, jsValue, env->atoms.napi_buffer)) {
            *result = false;
            return napi_clear_last_error(env);
        }
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

    if (status) {
        if (!JS_HasProperty(env->context, jsValue, env->atoms.napi_buffer)) {
            *result = false;
            return napi_clear_last_error(env);
        }
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
        return napi_set_last_error(env, napi_object_expected);
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
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_arraybuffer_expected);
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
        return napi_set_last_error(env, napi_arraybuffer_expected);
    }

    void *buffer = nullptr;
    size_t bufferSize = 0;
    buffer = JS_GetArrayBuffer(env->context, &bufferSize, jsValue);
    *result = buffer == nullptr;

    return napi_clear_last_error(env);
}

/**
 * https://nodejs.org/api/n-api.html#working-with-javascript-properties
 * Working with JavaScript properties
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
        return napi_set_last_error(env, napi_object_expected);
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

        JSPropertyEnum *tab = nullptr;
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int result = JS_SetProperty(env->context, jsObject, keyAtom,
                                JS_DupValue(env->context, jsValue));
    JS_FreeAtom(env->context, keyAtom);

    if (!result) {
        return napi_set_last_error(env, napi_generic_failure);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    JSValue jsResult = JS_GetProperty(env->context, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_HasProperty(env->context, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_DeleteProperty(env->context, jsValue, keyAtom, JS_PROP_THROW);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
    }

    if (result != nullptr) {
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSValue jsKey = JS_NewString(env->context, utf8name);
    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_GetOwnProperty(env->context, NULL, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom keyAtom = JS_ValueToAtom(env->context, jsKey);
    int status = JS_GetOwnProperty(env->context, NULL, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    int status = JS_SetPropertyStr(env->context, jsObject, utf8Name,
                                   JS_DupValue(env->context, jsValue));

    if (status == -1) {
        return napi_set_last_error(env, napi_generic_failure);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSValue jsResult = JS_GetPropertyStr(env->context, jsValue, utf8Name);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom keyAtom = JS_NewAtom(env->context, utf8Name);
    int status = JS_HasProperty(env->context, jsValue, keyAtom);
    JS_FreeAtom(env->context, keyAtom);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    int status = JS_SetPropertyUint32(env->context, jsObject, index, jsValue);

    if (!status) {
        return napi_set_last_error(env, napi_generic_failure);
    }

    return napi_clear_last_error(env);
}

napi_status napi_get_element(napi_env env, napi_value object, uint32_t index, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    JSValue jsResult = JS_GetPropertyUint32(env->context, jsValue, index);

    if (JS_IsException(jsResult)) {
        return napi_set_last_error(env, napi_pending_exception);
    }

    return CreateScopedResult(env, jsResult, result);
}

napi_status napi_has_element(napi_env env, napi_value object, uint32_t index, bool *result) {

    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(result)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom key = JS_NewAtomUInt32(env->context, index);
    int status = JS_HasProperty(env->context, jsValue, key);
    JS_FreeAtom(env->context, key);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSAtom key = JS_NewAtomUInt32(env->context, index);
    int status = JS_DeleteProperty(env->context, jsValue, key, JS_PROP_THROW);
    JS_FreeAtom(env->context, key);

    if (status == -1) {
        return napi_set_last_error(env, napi_pending_exception);
    }

    *result = status;

    return napi_clear_last_error(env);
}

static inline void
napi_set_property_descriptor(napi_env env, napi_value object, napi_property_descriptor descriptor) {

    JSValue jsValue = *((JSValue *) object);

    JSAtom key;
    if (descriptor.name) {
        JSValue symbol = *((JSValue *) descriptor.name);
        key = JS_ValueToAtom(env->context, symbol);
    } else {
        key = JS_NewAtom(env->context, descriptor.utf8name);
    }

    if (descriptor.value) {
        JS_DefinePropertyValue(env->context, jsValue, key,
                               JS_DupValue(env->context, *((JSValue *) descriptor.value)),
                               JS_PROP_C_W_E);
    } else if (descriptor.method) {
        napi_value function;
        napi_create_function(env, nullptr, 0, descriptor.method, descriptor.data, &function);
        if (function) {
            JS_DefinePropertyValue(env->context, jsValue, key,
                                   JS_DupValue(env->context, *((JSValue *) function)),
                                   napi_default_method);
        }
    } else if (descriptor.getter || descriptor.setter) {
        auto flags = JS_PROP_C_W_E;

        napi_value getter;
        napi_value setter;

        if (descriptor.getter) {
            flags = flags | JS_PROP_HAS_GET;
            napi_create_function(env, nullptr, 0, descriptor.getter, descriptor.data, &getter);
        } else {
            getter = nullptr;
        }

        if (descriptor.setter) {
            flags = flags | JS_PROP_HAS_SET;
            napi_create_function(env, nullptr, 0, descriptor.setter, descriptor.data, &setter);
        } else {
            setter = nullptr;
        }

        if (setter || getter) {
            JS_DefineProperty(env->context, jsValue, key, JS_UNDEFINED,
                              getter ? *((JSValue *) getter) : JS_UNDEFINED,
                              setter ? *((JSValue *) setter) : JS_UNDEFINED, flags);
        }
    }

    JS_FreeAtom(env->context, key);
}

napi_status napi_define_properties(napi_env env, napi_value object, size_t property_count,
                                   const napi_property_descriptor *properties) {

    CHECK_ARG(env)
    CHECK_ARG(object)
    CHECK_ARG(properties)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    for (size_t i = 0; i < property_count; i++) {
        napi_property_descriptor descriptor = properties[i];

        napi_set_property_descriptor(env, object, descriptor);
    }

    return napi_clear_last_error(env);
}

napi_status napi_object_freeze(napi_env env, napi_value object) {

    CHECK_ARG(env)
    CHECK_ARG(object)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }
    JSValue global = JS_GetGlobalObject(env->context);
    JSValue obj = JS_GetProperty(env->context, global, env->atoms.object);
    JSValue result = JS_Invoke(env->context, obj, env->atoms.freeze, 1, &jsValue);

    JS_FreeValue(env->context, obj);
    JS_FreeValue(env->context, global);

    if (JS_IsException(result)) {
        JS_FreeValue(env->context, result);
        return napi_set_last_error(env, napi_pending_exception);
    }

    JS_FreeValue(env->context, result);

    return napi_clear_last_error(env);
}

napi_status napi_object_seal(napi_env env, napi_value object) {

    CHECK_ARG(env)
    CHECK_ARG(object)

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    JSValue global = JS_GetGlobalObject(env->context);
    JSValue obj = JS_GetProperty(env->context, global, env->atoms.object);
    JSValue result = JS_Invoke(env->context, obj, env->atoms.seal, 1, &jsValue);
    JS_FreeValue(env->context, obj);
    JS_FreeValue(env->context, global);

    if (JS_IsException(result)) {
        JS_FreeValue(env->context, result);
        return napi_set_last_error(env, napi_pending_exception);
    }

    JS_FreeValue(env->context, result);

    return napi_clear_last_error(env);
}

/**
 * https://nodejs.org/api/n-api.html#working-with-javascript-functions
 * Working with javascript functions
 */

napi_status napi_call_function(napi_env env, napi_value thisValue, napi_value func, size_t argc,
                               const napi_value *argv, napi_value *result) {

    CHECK_ARG(env)
    CHECK_ARG(func)

    napi_handle_scope handleScope = NULL;
    napi_open_handle_scope(env, &handleScope);

    JSValue jsFunction = *(JSValue *) func;

    if (!thisValue) {
        CHECK_NAPI(napi_get_global(env, &thisValue))
    }

    JSValue *args = NULL;
    if (argc > 0) {
        RETURN_STATUS_IF_FALSE(argc <= INT_MAX, napi_invalid_arg)
        CHECK_ARG(argv)
        args = (JSValue *) malloc(sizeof(JSValue) * argc);
        RETURN_STATUS_IF_FALSE(args, napi_memory_error)
        for (size_t i = 0; i < argc; ++i) {
            args[i] = *((JSValue *) argv[i]);
        }
    }

    JSValue returnValue = JS_Call(env->context, jsFunction, *((JSValue *) thisValue), (int) argc,
                                  args);

    if (args) {
        free(args);
    }

    if (JS_IsException(returnValue)) {
        napi_close_handle_scope(env, handleScope);
        return napi_set_last_error(env, napi_pending_exception);
    }

    napi_close_handle_scope(env, handleScope);

    if (result) {
        return CreateScopedResult(env, returnValue, result);
    } else {
        JS_FreeValue(env->context, returnValue);
    }

    return napi_clear_last_error(env);
}

static JSValue
CallCFunction(JSContext *ctx, JSValueConst thisVal, int argc, JSValueConst *argv, int magic,
              JSValue *funcData) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);

    // Fixed 1 parameter
    // JS_GetOpaque will not throw an exception
    FunctionInfo *functionInfo = (FunctionInfo *) JS_GetOpaque(funcData[0],
                                                               env->runtime->functionClassId);

    if (!functionInfo || !functionInfo->callback) {
        return undefined;
    }

    // thisVal may be undefined if the function is called directly, such as test() instead of this.test() or globalThis.test()
    bool useGlobalValue = false;
    if (JS_IsUndefined(thisVal)) {
        useGlobalValue = true;
        thisVal = JS_GetGlobalObject(ctx);
    }

    struct NAPICallbackInfo callbackInfo = {undefined, thisVal, argv, functionInfo->data, argc};

    // The failure of napi_open_handle_scope requires fault tolerance, and it needs to be initialized to NULL for judgment here.
    napi_escapable_handle_scope handleScope = NULL;
    //    // Memory allocation fails, HandleScope is done by the outermost layer
    if (napi_open_escapable_handle_scope(env, &handleScope) != napi_ok) {
        return undefined;
    }

    // After callback is called, the return value should belong to the current handleScope management, otherwise the business party will be responsible for the consequences
    napi_value retVal = functionInfo->callback(env, &callbackInfo);
    if (useGlobalValue) {
        // Release the situation of thisVal = JS_GetGlobalObject(ctx);
        JS_FreeValue(ctx, thisVal);
    }
    // Check NULL
    JSValue returnValue = undefined;
    if (retVal) {
        // The normal return value should be held by HandleScope, so the reference count +1 is required
        // After this code is executed, a share of ownership is in handleScope, or undefinedValue
        // One ownership is JS_DupValue()
        returnValue = JS_DupValue(ctx, *((JSValue *) retVal));
    }
    // handleScope
    // After closing handleScope, the return value can only be undefinedValue, or JS_DupValue() -> returnValue
    // Unless handleScope == NULL, but this situation does not affect the execution of subsequent code

    if (handleScope && napi_close_escapable_handle_scope(env, handleScope) != napi_ok) {
        JS_FreeValue(ctx, returnValue);
        return undefined;
    }

    // Check exception
    if (JS_IsException(returnValue)) {
        JS_FreeValue(ctx, returnValue);
        return JS_UNDEFINED;
    }

    return returnValue;
}

// NAPIMemoryError/NAPIPendingException + addValueToHandleScope + napi_create_string_utf8
napi_status
napi_create_function(napi_env env, const char *utf8name, size_t length, napi_callback cb,
                     void *data,
                     napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(cb)
    CHECK_ARG(result)

    FunctionInfo *functionInfo = (FunctionInfo *) malloc(sizeof(FunctionInfo));
    RETURN_STATUS_IF_FALSE(functionInfo, napi_memory_error)
    functionInfo->data = data;
    functionInfo->callback = cb;

    if (TRUTHY(!env->runtime->functionClassId)) {
        assert(false && FUNCTION_CLASS_ID_ZERO);
        free(functionInfo);

        return napi_set_last_error(env, napi_generic_failure);
    }
    // rc: 1
    JSValue dataValue = JS_NewObjectClass(env->context, (int) env->runtime->functionClassId);
    if (TRUTHY(JS_IsException(dataValue))) {
        free(functionInfo);

        return napi_set_last_error(env, napi_pending_exception);
    }
    // functionInfo life cycle is managed by JSValue
    JS_SetOpaque(dataValue, functionInfo);

    // dataValue reference count +1 => rc: 2
    // Created by default name: ""
    JSValue functionValue = JS_NewCFunctionData(env->context, CallCFunction, 0, 0, 1, &dataValue);
    // transfer ownership
    JS_FreeValue(env->context, dataValue);
    // If functionValue fails to be created, dataValue rc
    // will not be +1, and the above reference count -1 => rc is
    // 0, garbage collection occurs, and the FunctionInfo structure is also recycled
    RETURN_STATUS_IF_FALSE(!JS_IsException(functionValue), napi_pending_exception)

    if (utf8name && strcmp(utf8name, "") != 0) {
        // JS_DefinePropertyValueStr -> JS_DefinePropertyValue transfers
        // ownership and automatically passes in JS_PROP_HAS_CONFIGURABLE
        int returnStatus = JS_DefinePropertyValue(env->context, functionValue, env->atoms.name,
                                                  JS_NewString(env->context, utf8name),
                                                  JS_PROP_CONFIGURABLE);
        // Not passing in JS_PROP_THROW does not mean that -1 will not be returned.
        // Passing in JS_PROP_THROW means that all false situations will become exceptions
        if (TRUTHY(returnStatus == -1)) {
            JS_FreeValue(env->context, functionValue);

            return napi_set_last_error(env, napi_pending_exception);
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
        // If there are fewer arguments provided than claimed,
        // the rest of argv is filled with napi_value values that represent undefined.
        // And if there are more arguments than required, the rest of the args are ignored.
        if (i < *argc) {
            for (; i < *argc; ++i) {
                argv[i] = (napi_value) &undefined;
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

    JSValue *args = NULL;
    if (argc > 0) {
        RETURN_STATUS_IF_FALSE(argc <= INT_MAX, napi_invalid_arg)
        CHECK_ARG(argv)
        args = (JSValue *) malloc(sizeof(JSValue) * argc);
        RETURN_STATUS_IF_FALSE(args, napi_memory_error)
        for (size_t i = 0; i < argc; ++i) {
            args[i] = *((JSValue *) argv[i]);
        }
    }

    JSValue returnValue = JS_CallConstructor(env->context, *((JSValue *) constructor), (int) argc,
                                             args);

    if (args) {
        free(args);
    }

    if (JS_IsException(returnValue)) {
        return napi_set_last_error(env, napi_pending_exception);
    }

    return CreateScopedResult(env, returnValue, result);
}

/**
 * Object Wrap
 * https://nodejs.org/api/n-api.html#object-wrap
 */

static JSValue
CallConstructor(JSContext *ctx, JSValueConst newTarget, int argc, JSValueConst *argv) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    napi_env env = (napi_env) JS_GetRuntimeOpaque(rt);
    napi_runtime runtime = env->runtime;
    JSValue prototypeValue = JS_GetProperty(ctx, newTarget, env->atoms.prototype);

    if (JS_IsException(prototypeValue)) {
        assert(false &&
               "callAsConstructor() use JS_GetPropertyStr() to get 'prototype' raise a exception.");

        return prototypeValue;
    }

    if (TRUTHY(!runtime->constructorClassId)) {
        assert(false && CONSTRUCTOR_CLASS_ID_ZERO);

        return undefined;
    }

    ConstructorInfo *constructorInfo = (ConstructorInfo *) JS_GetOpaque(prototypeValue,
                                                                        runtime->constructorClassId);

    if (constructorInfo == nullptr) {
        // If constructorInfo is NULL, it means that one of the super prototypes
        // is a native class which called this C constructor function.
        JSValue superPrototype = JS_GetPrototype(ctx, prototypeValue);
        while (!JS_IsNull(superPrototype) && constructorInfo == nullptr) {
            constructorInfo = (ConstructorInfo *) JS_GetOpaque(superPrototype,
                                                               runtime->constructorClassId);

            if (!constructorInfo) {

                JSValue nextPrototype = JS_GetPrototype(ctx, superPrototype);
                JS_FreeValue(ctx, superPrototype);
                superPrototype = nextPrototype;
            }
        }

        JS_FreeValue(ctx, superPrototype);
    }

    if (TRUTHY(!constructorInfo || !constructorInfo->functionInfo.callback ||
               !constructorInfo->classId)) {
        assert(false);

        return undefined;
    }

    JSValue thisValue = JS_NewObjectProtoClass(env->context, prototypeValue,
                                               constructorInfo->classId);

    if (TRUTHY(JS_IsException(thisValue))) {
        return thisValue;
    }

    struct NAPICallbackInfo callbackInfo = {newTarget, thisValue, argv,
                                            constructorInfo->functionInfo.data, argc};

    napi_handle_scope handleScope = NULL;
    napi_open_handle_scope(env, &handleScope);

    napi_value result =
            constructorInfo->functionInfo.callback(env, &callbackInfo);

    JSValue returnValue = *((JSValue *) result);

    if (result && JS_IsObject(returnValue)) {
        JS_DupValue(ctx, returnValue);
    }
    napi_close_handle_scope(env, handleScope);

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
    // Create constructor info
    ConstructorInfo *constructorInfo = (ConstructorInfo *) malloc(sizeof(ConstructorInfo));
    RETURN_STATUS_IF_FALSE(constructorInfo, napi_memory_error)
    // Set the required data
    constructorInfo->functionInfo.data = data;
    constructorInfo->functionInfo.callback = constructor;
    constructorInfo->classId = 0;

    // Create a new class of the specific constructor.
    JS_NewClassID(env->runtime->runtime, &constructorInfo->classId);
    JSClassDef classDef = {utf8name ?: "", NULL, NULL, NULL, NULL};
    int status = JS_NewClass(env->runtime->runtime, constructorInfo->classId, &classDef);

    if (TRUTHY(status == -1)) {
        free(constructorInfo);

        return napi_set_last_error(env, napi_pending_exception);
    }

    if (TRUTHY(!env->runtime->constructorClassId)) {
        assert(false && CONSTRUCTOR_CLASS_ID_ZERO);

        return napi_set_last_error(env, napi_generic_failure);
    }

    // Create class prototype.
    JSValue prototype = JS_NewObjectClass(env->context, (int) env->runtime->constructorClassId);

    if (TRUTHY(JS_IsException(prototype))) {
        free(constructorInfo);

        return napi_set_last_error(env, napi_pending_exception);
    }
    // Set the constructorInfo on the prototype.
    JS_SetOpaque(prototype, constructorInfo);

    // Create the constructor function
    JSValue constructorValue = JS_NewCFunction2(env->context, CallConstructor, utf8name, 0,
                                                JS_CFUNC_constructor, 0);

    if (TRUTHY(JS_IsException(constructorValue))) {
        // Prototype will free ConstructorInfo structure itself
        JS_FreeValue(env->context, prototype);
        return napi_set_last_error(env, napi_pending_exception);
    }

    for (size_t i = 0; i < property_count; i++) {
        if (properties[i].attributes & napi_static) {
            napi_set_property_descriptor(env, (napi_value) &constructorValue, properties[i]);
        } else {
            napi_set_property_descriptor(env, (napi_value) &prototype, properties[i]);
        }
    }

    // .prototype .constructor
    // Automatic reference count +1
    JS_SetConstructor(env->context, constructorValue, prototype);
    // context -> class_proto
    // transfer ownership
    JS_SetClassProto(env->context, constructorInfo->classId, prototype);

    // Add the constructor the handle scope.
    struct Handle *handle;
    napi_status addStatus = CreateJSValueHandle(env, constructorValue, &handle);
    if (TRUTHY(addStatus != napi_ok)) {
        JS_FreeValue(env->context, constructorValue);
        JS_FreeValue(env->context, prototype);

        return (napi_status) napi_set_last_error(env, addStatus);
    }
    *result = (napi_value) &handle->value;

    //    printf("Registered Native Class %s \n", utf8name);

    return napi_clear_last_error(env);
}

napi_status
napi_wrap(napi_env env, napi_value jsObject, void *nativeObject, napi_finalize finalize_cb,
          void *finalize_hint,
          napi_ref *result) {

    CHECK_ARG(env)
    CHECK_ARG(jsObject)
    CHECK_ARG(nativeObject)

    JSValue jsValue = *((JSValue *) jsObject);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    int isWrapped = JS_GetOwnProperty(env->context, NULL, jsValue, env->atoms.napi_external);

    if (isWrapped == -1) {
        return napi_set_last_error(env, napi_pending_exception);
    }

    if (isWrapped) {
        return napi_set_last_error(env, napi_invalid_arg);
    }

    ExternalInfo *externalInfo = (ExternalInfo *) malloc(sizeof(ExternalInfo));
    RETURN_STATUS_IF_FALSE(externalInfo, napi_memory_error)

    externalInfo->data = nativeObject;
    externalInfo->finalizeHint = finalize_hint;
    externalInfo->finalizeCallback = NULL;

    JSValue external = JS_NewObjectClass(env->context, (int) env->runtime->externalClassId);

    if (JS_IsException(external)) {
        free(externalInfo);
        return napi_set_last_error(env, napi_pending_exception);
    }

    JS_SetOpaque(external, externalInfo);

    JS_SetProperty(env->context, jsValue, env->atoms.napi_external, external);

    if (result) {
        // Create a weak reference.
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSPropertyDescriptor descriptor;

    int isWrapped = JS_GetOwnProperty(env->context, &descriptor, jsValue, env->atoms.napi_external);

    if (isWrapped == -1) {
        return napi_set_last_error(env, napi_pending_exception);
    }

    if (!isWrapped) {
        // Silently fail if value is not wrapped.
        return napi_set_last_error(env, napi_generic_failure);
    }

    JSValue external = descriptor.value;

    if (JS_IsException(external)) {
        return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSPropertyDescriptor descriptor;
    int isWrapped = JS_GetOwnProperty(env->context, &descriptor, jsValue, env->atoms.napi_external);

    if (isWrapped == -1) {
        return napi_set_last_error(env, napi_pending_exception);
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
            free(externalInfo);
            JS_SetOpaque(external, NULL);
        }

        int status = JS_DeleteProperty(env->context, jsValue, env->atoms.napi_external, 0);
        if (status == -1) {
            JS_FreeValue(env->context, external);
            JS_FreeValue(env->context, descriptor.value);
            return napi_set_last_error(env, napi_pending_exception);
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
        return napi_set_last_error(env, napi_object_expected);
    }

    JSValue heldValue = JS_NewObject(env->context);
    ExternalInfo *info = (ExternalInfo *) malloc(sizeof(ExternalInfo));

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

    if (result) {
        // Create a weak reference.
        napi_ref ref;
        napi_create_reference(env, jsObject, 0, &ref);
        *result = ref;
    }

    return napi_clear_last_error(env);
}

napi_status
napi_set_instance_data(napi_env env, void *data, napi_finalize finalize_cb, void *finalize_hint) {

    CHECK_ARG(env)
    env->instanceData = (ExternalInfo *) malloc(sizeof(ExternalInfo));
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
 Promise
 */

void deferred_finalize(napi_env env, void *finalizeData, void *finalizeHint) {
    NAPIDeferred *deferred = (NAPIDeferred *) finalizeData;
    JS_FreeValue(env->context, *(JSValue *) deferred->resolve);
    JS_FreeValue(env->context, *(JSValue *) deferred->reject);
};

napi_status napi_create_promise(napi_env env, napi_deferred *deferred, napi_value *result) {
    CHECK_ARG(env);
    CHECK_ARG(deferred);
    CHECK_ARG(result);

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(env->context, resolving_funcs);

    *deferred = (NAPIDeferred *) malloc(sizeof(NAPIDeferred));

    (*deferred)->resolve = (napi_value) &resolving_funcs[0];
    (*deferred)->reject = (napi_value) &resolving_funcs[1];

    JSValue heldValue = JS_NewObject(env->context);
    ExternalInfo *info = (ExternalInfo *) malloc(sizeof(ExternalInfo));
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
    if (resolution != nullptr) {
        value = *((JSValue *) resolution);
    }

    JSValue jsResult = JS_Call(env->context, *((JSValue *) deferred->resolve), JS_UNDEFINED, 1,
                               &value);
    JS_FreeValue(env->context, jsResult);

    return napi_clear_last_error(env);
}

napi_status napi_reject_deferred(napi_env env, napi_deferred deferred, napi_value rejection) {
    CHECK_ARG(env);
    CHECK_ARG(deferred);

    JSValue value = JS_UNDEFINED;
    if (rejection != nullptr) {
        value = *((JSValue *) rejection);
    }

    JSValue jsResult = JS_Call(env->context, *((JSValue *) deferred->reject), JS_UNDEFINED, 1,
                               &value);
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
 * Type Tag
 */

napi_status napi_type_tag_object(napi_env env, napi_value object, napi_type_tag tag) {
    CHECK_ARG(env);
    CHECK_ARG(object);

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    constexpr uint32_t size = 2;

    bool isTypeTagged = false;

    uint64_t words[size] = {tag.lower, tag.upper};
    isTypeTagged = JS_HasProperty(env->context, jsValue, env->atoms.napi_typetag);
    if (!isTypeTagged) {
        JSValue value = JS_CreateBigIntWords(env->context, 0, size, words);
        JS_SetProperty(env->context, jsValue, env->atoms.napi_typetag,
                       JS_DupValue(env->context, value));
    }

    return napi_clear_last_error(env);
}

napi_status
napi_check_object_type_tag(napi_env env, napi_value object, napi_type_tag tag, bool *result) {
    CHECK_ARG(env);
    CHECK_ARG(object);
    CHECK_ARG(result);

    JSValue jsValue = *((JSValue *) object);

    if (!JS_IsObject(jsValue)) {
        return napi_set_last_error(env, napi_object_expected);
    }

    constexpr uint32_t size = 2;
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
        if ((words[0] == tag.lower) && (words[1] == tag.upper)) {
            *result = true;
        }
    }

    return napi_clear_last_error(env);
}

napi_status napi_run_microtasks(napi_env env) {
    if (TRUTHY(!env))
        return napi_invalid_arg;

    int error = 1;
    do {
        JSContext *context;
        error = JS_ExecutePendingJob(JS_GetRuntime(env->context), &context);
        if (error == -1) {
            // Under normal circumstances, JS_ExecutePendingJob returns -1
            // Represents engine internal exceptions, such as memory allocation failure, etc.
            // TODO(ChasonTang): Test when Promise throws an exception
            JSValue inlineExceptionValue = JS_GetException(context);

            JS_FreeValue(context, inlineExceptionValue);
        }
    } while (error != 0);

    return napi_ok;
}

napi_status napi_free_cstring(napi_env env, const char *cString) {

    CHECK_ARG(env)

    JS_FreeCString(env->context, cString);

    return napi_clear_last_error(env);
}

napi_status napi_run_script(napi_env env,
                            napi_value script,
                            const char * file,
                            napi_value *result) {
    CHECK_ARG(env)
    CHECK_ARG(script)

    JSValue eval_result;
    const char *cScript = JS_ToCString(env->context, *((JSValue *) script));
    eval_result = JS_Eval(env->context, cScript, strlen(cScript), file, JS_EVAL_TYPE_GLOBAL);

    napi_run_microtasks(env);

    // 3. Free the script char *
    JS_FreeCString(env->context, cScript);

    if (JS_IsException(eval_result)) {
        JSValue exception = JS_GetException(env->context);
        const char *exceptionMessage = JS_ToCString(env->context, exception);
        print_exception(env, exception);
        JS_FreeValue(env->context, exception);

        return napi_set_last_error(env, napi_cannot_run_js, exceptionMessage);
    }
    // 5. Set the result to undefined.
    if (result) {
        // Add result to handle scope
        struct Handle *returnHandle;
        CreateJSValueHandle(env, eval_result, &returnHandle);
        *result = (napi_value) &returnHandle->value;
    } else {
        // 6.Free the result from eval.
        JS_FreeValue(env->context, eval_result);
    }

    return napi_clear_last_error(env);
}

napi_status NAPIParseUTF8JSONString(napi_env env, const char *utf8String, napi_value *result) {
    CHECK_ARG(env);
    CHECK_ARG(utf8String);
    CHECK_ARG(result);

    napi_value stringValue;
    CHECK_NAPI(napi_create_string_utf8(env, utf8String, NAPI_AUTO_LENGTH, &stringValue));
    napi_value global;
    CHECK_NAPI(napi_get_global(env, &global));
    napi_value json, parse;
    CHECK_NAPI(napi_get_named_property(env, global, "JSON", &json));
    CHECK_NAPI(napi_get_named_property(env, json, "parse", &parse));
    CHECK_NAPI(napi_call_function(env, json, parse, 1, &stringValue, result));

    return napi_clear_last_error(env);
}

// Free Env
napi_status NAPIFreeEnv(napi_env env) {
    CHECK_ARG(env)

    napi_handle_scope idleHandleScope, idleTempHandleScope;
    LIST_FOREACH_SAFE(idleHandleScope, &globalHandlePool.scopeList, node, idleTempHandleScope) {
        LIST_REMOVE(idleHandleScope, node);
        free(idleHandleScope);
    }

    Handle *idleHandle, *idleTempHandle;
    SLIST_FOREACH_SAFE(idleHandle, &globalHandlePool.handleList, node, idleTempHandle) {
        // This and the previous assert require that env->handleScopeList must be a LIST doubly linked list
        SLIST_REMOVE(&globalHandlePool.handleList, idleHandle, Handle, node);
        free(idleHandle);
    }

    napi_escapable_handle_scope escapableHandleScope, tempEscapableHandleScope;
    LIST_FOREACH_SAFE(escapableHandleScope, &globalHandlePool.escapableScopeList, node,
                      tempEscapableHandleScope) {
        LIST_REMOVE(escapableHandleScope, node);
        free(escapableHandleScope);
    }

    // Free all handle scopes
    napi_handle_scope handleScope, tempHandleScope;
    LIST_FOREACH_SAFE(handleScope, &env->handleScopeList, node, tempHandleScope) {
        struct Handle *handle, *tempHandle;
        SLIST_FOREACH_SAFE(handle, &handleScope->handleList, node, tempHandle) {
            JS_FreeValue(env->context, handle->value);
            free(handle);
        }
        // This and the previous assert require that env->handleScopeList must be a LIST doubly linked list
        LIST_REMOVE(handleScope, node);
        free(handleScope);
    }

    // Free all strong references
    napi_ref ref, temp;
    LIST_FOREACH_SAFE(ref, &env->strongRefList, node, temp) {
        LIST_REMOVE(ref, node);
        JS_FreeValue(env->context, ref->value);
        free(ref);
    }

    // Free all weak references
    struct WeakReference *referenceInfo, *tempReferenceInfo;
    LIST_FOREACH_SAFE(referenceInfo, &env->weakReferenceList, node, tempReferenceInfo) {
        LIST_FOREACH_SAFE(ref, &referenceInfo->weakRefList, node, temp) {
            LIST_REMOVE(ref, node);
            free(ref);
        }
        LIST_REMOVE(referenceInfo, node);
        referenceInfo->isEnvFreed = true;
        // referenceInfo itself is not destroyed, it will be destroyed until the GC stage
    }

    // At this point, all weak references have been released.
    LIST_FOREACH_SAFE(ref, &env->valueList, node, temp) {
        LIST_REMOVE(ref, node);
        free(ref);
    }

    // Free Reference Symbol
    JS_FreeValue(env->context, env->referenceSymbolValue);

    // Free Finalization Registry
    JS_FreeValue(env->context, env->finalizationRegistry);

    // Free Instance Data
    if (env->instanceData && env->instanceData->finalizeCallback) {
        env->instanceData->finalizeCallback(env, env->instanceData->data,
                                            env->instanceData->finalizeHint);
        free(env->instanceData);
    };

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

    // Free Context
    JS_FreeContext(env->context);

    return napi_clear_last_error(env);
}


napi_status napi_set_gc_being_callback(napi_env env, napi_callback cb, void *data) {
    CHECK_ARG(env)
    CHECK_ARG(cb)

    return napi_clear_last_error(env);
}

napi_status napi_set_gc_end_callback(napi_env env, napi_callback cb, void *data) {
    CHECK_ARG(env)
    CHECK_ARG(cb)

    return napi_clear_last_error(env);
}
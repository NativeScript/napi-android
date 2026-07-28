#include "jsr.h"
// JSGlobalContextSetUnhandledRejectionCallback lives in this private JSC header.
#include <JavaScriptCore/JSContextRefPrivate.h>
// Public C API for on-disk program bytecode caching (added by the jsc-android
// build via jsc_android_bytecode_cache.patch).
#include <JavaScriptCore/JSBytecodeCache.h>
#include "NativeScriptAssert.h"
#include <sys/stat.h>
#include <utime.h>
#include <ctime>
#include <cstdio>
#include <string>

// Defined in jsc-api.cpp (same runtime library): records `exception` as the
// env's pending exception and returns napi_pending_exception. This is the same
// helper napi_run_script_source uses via CHECK_JSC; it has no public prototype,
// so we forward-declare it here.
napi_status napi_set_exception(napi_env env, JSValueRef exception);

// Native trampoline for JSC's unhandled-promise-rejection callback. JSC invokes
// it with (promise, reason); we forward to the JS-side
// globalThis.onUnhandledPromiseRejectionTracker (installed by ts_helpers.js),
// mirroring how the V8 path routes rejections.
static napi_value JscUnhandledRejectionCallback(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_value global, tracker;
    napi_get_global(env, &global);
    napi_get_named_property(env, global, "onUnhandledPromiseRejectionTracker", &tracker);

    napi_valuetype type;
    napi_typeof(env, tracker, &type);
    if (type == napi_function) {
        napi_call_function(env, global, tracker, argc, args, nullptr);
    }
    return nullptr;
}

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    if (!runtime) return napi_invalid_arg;
    *runtime = (jsr_ns_runtime) JSGlobalContextCreateInGroup(nullptr, nullptr);
    return napi_ok;
}

napi_status js_create_napi_env(napi_env* env, jsr_ns_runtime runtime) {
    if (env == nullptr) return napi_invalid_arg;

    *env = new napi_env__((JSGlobalContextRef) runtime);
    JSGlobalContextRelease((JSGlobalContextRef) runtime);

    napi_value gc;
    napi_create_function(*env, "gc", strlen("gc"), [](napi_env env, napi_callback_info info) -> napi_value {
        JSGarbageCollect(env->context);
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }, nullptr, &gc);
    napi_value global;
    napi_get_global(*env, &global);
    napi_set_named_property(*env, global, "gc", gc);

    // Report unhandled promise rejections. JSC keeps the callback alive (it is
    // stored on and marked by the global object), so no extra protection is
    // needed. JSC only surfaces the "unhandled" event, not a later "handled"
    // retraction, so the JS tracker treats every call as unhandled.
    napi_value rejectionCallback;
    napi_create_function(*env, "onUnhandledRejection", NAPI_AUTO_LENGTH,
                         JscUnhandledRejectionCallback, nullptr, &rejectionCallback);
    JSValueRef rejectionException = nullptr;
    JSGlobalContextSetUnhandledRejectionCallback(
            (*env)->context,
            reinterpret_cast<JSObjectRef>(rejectionCallback),
            &rejectionException);


    return napi_ok;

}

napi_status js_set_runtime_flags(const char* flags) {
    return napi_ok;
}

napi_status js_lock_env(napi_env env) {
    if (env == nullptr) return napi_invalid_arg;
    env->js_mutex.lock();
    return napi_ok;
}

napi_status js_unlock_env(napi_env env) {
    if (env == nullptr) return napi_invalid_arg;
    env->js_mutex.unlock();
    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    if (env == nullptr) return napi_invalid_arg;
    delete env;
    return  napi_ok;
}

napi_status js_free_runtime(jsr_ns_runtime runtime) {
//    JSContextGroupRelease((JSContextGroupRef) runtime);
    return napi_ok;
}

// ---------------------------------------------------------------------------
// Bytecode code cache
//
// JSC has no ahead-of-time bytecode format, so — exactly like the V8 path — we
// cache the engine's own serialized bytecode next to each source file and reuse
// it on later launches. The heavy lifting (serialize / validate / run-from-cache)
// lives in JSC behind the JSBytecodeCache.h C API; here we only manage the cache
// file: where it lives, when it is stale, and publishing it atomically.
//
// The .cache file is stamped with its source file's mtime; a mismatch means the
// source changed and the cache is ignored (and JSC re-validates internally too).
// ---------------------------------------------------------------------------

// Turn a script's source URL into the on-disk JS path we cache next to. Returns
// false for sources that aren't real files (e.g. the synthetic
// "<require_factory>"), which must never be cached.
static bool NormalizeScriptPath(const char *file, std::string &out) {
    if (file == nullptr) return false;
    std::string f(file);
    static const std::string scheme = "file://";
    if (f.rfind(scheme, 0) == 0) {
        out = f.substr(scheme.size());
    } else if (!f.empty() && f[0] == '/') {
        out = f; // already a plain absolute path
    } else {
        return false;
    }
    return !out.empty();
}

// Publish a freshly written temp cache file: stamp it with the source's mtime so
// js_run_cached_script accepts it, then atomically rename it into place. Any
// failure leaves no cache behind. Best-effort.
static void PublishCacheFile(const std::string &tmpPath, const std::string &cachePath,
                             const std::string &fsPath) {
    struct stat srcStat;
    struct utimbuf new_times;
    new_times.actime = time(nullptr);
    new_times.modtime = (stat(fsPath.c_str(), &srcStat) == 0) ? srcStat.st_mtime : time(nullptr);
    utime(tmpPath.c_str(), &new_times);

    if (rename(tmpPath.c_str(), cachePath.c_str()) != 0) {
        DEBUG_WRITE("[code-cache] failed to publish cache file: %s", cachePath.c_str());
        remove(tmpPath.c_str());
        return;
    }
    DEBUG_WRITE("[code-cache] wrote JSC bytecode cache: %s", cachePath.c_str());
}

// Serialize `sourceStr` to a bytecode cache published next to `fsPath`, stamped
// with the source's mtime. Writes to a temp file first then renames, so a crash
// or a concurrent reader never sees a half-written cache. Best-effort: any
// failure just leaves no cache behind. Returns true if a cache was published.
static bool WriteCacheForSource(napi_env env, JSStringRef sourceStr, JSStringRef sourceUrl,
                                const std::string &fsPath) {
    auto cachePath = fsPath + ".cache";
    auto tmpPath = cachePath + ".tmp";

    JSStringRef errorMessage = nullptr;
    bool ok = JSWriteBytecodeCacheForProgram(env->context, sourceStr, sourceUrl,
                                             tmpPath.c_str(), &errorMessage);
    if (!ok) {
        if (errorMessage) {
            size_t maxSize = JSStringGetMaximumUTF8CStringSize(errorMessage);
            std::string msg(maxSize, '\0');
            JSStringGetUTF8CString(errorMessage, &msg[0], maxSize);
            DEBUG_WRITE("[code-cache] failed to generate bytecode for %s: %s",
                        fsPath.c_str(), msg.c_str());
            JSStringRelease(errorMessage);
        } else {
            DEBUG_WRITE("[code-cache] failed to generate bytecode for %s (no error message)",
                        fsPath.c_str());
        }
        remove(tmpPath.c_str());
        return false;
    }

    PublishCacheFile(tmpPath, cachePath, fsPath);
    return true;
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    // Fast path: run JSC's on-disk bytecode cache when one is present and
    // current. js_run_cached_script returns napi_cannot_run_js on a cache miss
    // (so we fall back to source), or any other status when it actually ran the
    // script (success or a thrown exception) — in which case we must NOT run it
    // again.
    napi_status status = js_run_cached_script(env, file, script, nullptr, result);
    if (status != napi_cannot_run_js) {
        return status;
    }

    // Cold path: publish a bytecode cache for the next launch, then run. When the
    // cache is written we run straight from it (JSC decodes the bytecode instead
    // of parsing again), so the source is compiled only once even on this first
    // launch; otherwise we fall back to running the source directly.
    std::string fsPath;
    if (NormalizeScriptPath(file, fsPath)) {
        JSValueRef exception = nullptr;
        JSStringRef sourceStr = JSValueToStringCopy(env->context,
                                                    reinterpret_cast<JSValueRef>(script),
                                                    &exception);
        if (sourceStr != nullptr && exception == nullptr) {
            JSStringRef sourceUrl = JSStringCreateWithUTF8CString(file);
            DEBUG_WRITE("[code-cache] compiling from source (cold): %s", file);
            bool wrote = WriteCacheForSource(env, sourceStr, sourceUrl, fsPath);
            JSStringRelease(sourceUrl);
            JSStringRelease(sourceStr);
            if (wrote) {
                napi_status cached = js_run_cached_script(env, file, script, nullptr, result);
                if (cached != napi_cannot_run_js) {
                    return cached;
                }
            }
        } else if (sourceStr != nullptr) {
            JSStringRelease(sourceStr);
        }
    }

    return napi_run_script_source(env, script, file, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    return napi_ok;
}

napi_status js_get_engine_ptr(napi_env env, int64_t *engine_ptr) {
    *engine_ptr = (int64_t) 0;
    return napi_ok;
}

napi_status js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t *externalMemory) {
    return napi_ok;
}

// Compile `source` and publish a bytecode cache next to `file`. Retained for the
// jsr interface; the hot path (js_execute_script) caches inline. No-op for
// synthetic / non-file sources.
napi_status js_cache_script(napi_env env, const char *source, const char *file) {
    std::string fsPath;
    if (!NormalizeScriptPath(file, fsPath)) {
        return napi_ok;
    }

    JSStringRef sourceStr = JSStringCreateWithUTF8CString(source);
    JSStringRef sourceUrl = JSStringCreateWithUTF8CString(file);
    WriteCacheForSource(env, sourceStr, sourceUrl, fsPath);
    JSStringRelease(sourceUrl);
    JSStringRelease(sourceStr);
    return napi_ok;
}

napi_status js_run_cached_script(napi_env env, const char *file, napi_value script, void *cache,
                                 napi_value *result) {
    if (env == nullptr || script == nullptr) return napi_invalid_arg;

    std::string fsPath;
    if (!NormalizeScriptPath(file, fsPath)) {
        return napi_cannot_run_js; // synthetic / non-file source: no cache
    }

    auto cachePath = fsPath + ".cache";
    struct stat cacheStat;
    if (stat(cachePath.c_str(), &cacheStat) != 0) {
        DEBUG_WRITE("[code-cache] miss (no cache on disk): %s", fsPath.c_str());
        return napi_cannot_run_js; // no cache written yet
    }
    struct stat srcStat;
    if (stat(fsPath.c_str(), &srcStat) == 0 && srcStat.st_mtime != cacheStat.st_mtime) {
        // Source changed since the cache was written — ignore the stale cache.
        DEBUG_WRITE("[code-cache] miss (source newer than cache): %s", fsPath.c_str());
        return napi_cannot_run_js;
    }

    JSValueRef exception = nullptr;
    JSStringRef sourceStr = JSValueToStringCopy(env->context,
                                                reinterpret_cast<JSValueRef>(script),
                                                &exception);
    if (sourceStr == nullptr || exception != nullptr) {
        if (sourceStr) JSStringRelease(sourceStr);
        return napi_cannot_run_js;
    }
    JSStringRef sourceUrl = JSStringCreateWithUTF8CString(file);

    bool cacheRejected = false;
    JSValueRef exc = nullptr;
    JSValueRef ret = JSEvaluateProgramWithBytecodeCacheFile(env->context, sourceStr, sourceUrl,
                                                            cachePath.c_str(), nullptr,
                                                            &cacheRejected, &exc);
    JSStringRelease(sourceUrl);
    JSStringRelease(sourceStr);

    if (cacheRejected) {
        // The cache was stale/incompatible; JSC did NOT run anything. Report a
        // miss so the caller runs the source and refreshes the cache.
        DEBUG_WRITE("[code-cache] miss (JSC rejected cache as incompatible): %s", fsPath.c_str());
        return napi_cannot_run_js;
    }

    if (exc != nullptr) {
        // The script threw while executing. This is a real run — surface the
        // exception; the caller must NOT execute the source a second time.
        return napi_set_exception__(env, exc);
    }

    DEBUG_WRITE("[code-cache] loaded JSC bytecode cache: %s", fsPath.c_str());
    if (result != nullptr) {
        *result = reinterpret_cast<napi_value>(const_cast<OpaqueJSValue*>(ret));
    }
    return napi_ok;
}


napi_status js_run_bytecode_file(napi_env env, const char *file, napi_value *result) {
    // JSC has no compile-time bytecode format (it caches serialized bytecode at
    // runtime instead, see js_cache_script/js_run_cached_script); always fall
    // back to source.
    return napi_cannot_run_js;
}

napi_status js_get_runtime_version(napi_env env, napi_value* version) {
    napi_create_string_utf8(env, "JSC", NAPI_AUTO_LENGTH, version);

    return napi_ok;
}

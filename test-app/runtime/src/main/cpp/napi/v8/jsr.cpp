#include "jsr.h"
#include "File.h"
#include <libgen.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <ctime>
#include <utime.h>
#include <cstdio>
#include "v8-fast-api-calls.h"
#include "NativeScriptAssert.h"
#include "native_api_util.h"

using namespace v8;
using namespace tns;

tns::SimpleAllocator g_allocator;

JSR::JSR() : isolate(nullptr) {
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = &g_allocator;

    if (!JSR::s_mainThreadInitialized) {
        JSR::platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(JSR::platform.get());
        v8::V8::Initialize();
        JSR::s_mainThreadInitialized = true;
    }
    isolate = v8::Isolate::New(create_params);
}

std::unique_ptr<v8::Platform> JSR::platform = nullptr;
bool JSR::s_mainThreadInitialized = false;
std::unordered_map<napi_env, JSR *> JSR::env_to_jsr_cache;

napi_status js_create_runtime(jsr_ns_runtime *runtime) {
    if (!runtime) return napi_invalid_arg;
    *runtime = (jsr_ns_runtime) new JSR();

    return napi_ok;
}

napi_status js_set_runtime_flags(const char *flags) {
    V8::V8::SetFlagsFromString(flags);
    return napi_ok;
}

napi_status js_lock_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }

    itFound->second->lock();

    return napi_ok;
}

napi_status js_unlock_env(napi_env env) {
    auto itFound = JSR::env_to_jsr_cache.find(env);
    if (itFound == JSR::env_to_jsr_cache.end()) {
        return napi_invalid_arg;
    }

    itFound->second->unlock();

    return napi_ok;
}

napi_status js_create_napi_env(napi_env *env, jsr_ns_runtime runtime) {
    if (env == nullptr) return napi_invalid_arg;
    JSR *jsr = (JSR *) runtime;
    // Must enter explictly
#ifdef __V8_13__
    jsr->isolate->Enter();
#endif
    try {
        v8::HandleScope handle_scope(jsr->isolate);
        v8::Local<v8::Context> context = v8::Context::New(jsr->isolate);
        *env = new napi_env__(context, NAPI_VERSION_EXPERIMENTAL);
        JSR::env_to_jsr_cache.insert(std::make_pair(*env, jsr));

        Local<Object> global = context->Global();
        global->Set(context,
                    String::NewFromUtf8(jsr->isolate, "__promiseUnhandledEvent").ToLocalChecked(),
                    Integer::New(jsr->isolate, PromiseRejectEvent::kPromiseRejectWithNoHandler));
        global->Set(context,
                    String::NewFromUtf8(jsr->isolate, "__promiseHandledEvent").ToLocalChecked(),
                    Integer::New(jsr->isolate,
                                 PromiseRejectEvent::kPromiseHandlerAddedAfterReject));

        jsr->isolate->SetPromiseRejectCallback([](PromiseRejectMessage message) {
            Local<Promise> promise = message.GetPromise();
            Isolate *isolate = promise->GetIsolate();
            Local<Value> value = message.GetValue();
            Local<Integer> event = Integer::New(isolate, message.GetEvent());
            v8::HandleScope handle_scope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            Local<Object> global = context->Global();
            Local<Value> callback = global->Get(context, String::NewFromUtf8(isolate,
                                                                             "onUnhandledPromiseRejectionTracker").ToLocalChecked()).ToLocalChecked();
            if (value.IsEmpty())
                value = Undefined(isolate);
            Local<Value> args[] = {event, promise, value};
            callback.As<Function>()->Call(context, global, 3, args);
        });

        // Must exit explictly
#ifdef __V8_13__
        jsr->isolate->Exit();
#endif
    } catch (...) {
#ifdef __V8_13__
        jsr->isolate->Exit();
#endif
        return napi_generic_failure;
    }
    return napi_ok;
}

napi_status js_free_napi_env(napi_env env) {
    if (env == nullptr) return napi_invalid_arg;
    env->DeleteMe();
    return napi_ok;
}

napi_status js_free_runtime(jsr_ns_runtime runtime) {
    JSR *jsr = (JSR *) runtime;
    jsr->isolate->Dispose();
    delete jsr;
    return napi_ok;
}

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

// Create a code cache from an already-compiled script and publish it next to the
// source, stamped with the source's mtime so js_run_cached_script accepts it.
// Best-effort: any failure just leaves no cache behind.
static void PersistCodeCache(v8::Local<v8::UnboundScript> unbound, const std::string &fsPath) {
    ScriptCompiler::CachedData *cachedData = ScriptCompiler::CreateCodeCache(unbound);
    if (cachedData == nullptr) {
        DEBUG_WRITE("[code-cache] CreateCodeCache produced no data for: %s", fsPath.c_str());
        return;
    }

    auto cachePath = fsPath + ".cache";
    auto tmpPath = cachePath + ".tmp";
    // Write to a temp file then rename into place so a crash or a concurrent
    // reader (e.g. a worker loading the same module) never sees a half-written
    // cache. Stamp the temp file with the source's mtime *before* the rename so
    // the published cache atomically carries the correct staleness marker.
    bool wrote = File::WriteBinary(tmpPath, cachedData->data, cachedData->length);
    int cacheLength = cachedData->length;
    delete cachedData;
    if (!wrote) {
        DEBUG_WRITE("[code-cache] failed to write cache file: %s", tmpPath.c_str());
        remove(tmpPath.c_str());
        return;
    }

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

    DEBUG_WRITE("[code-cache] wrote V8 code cache: %s (%d bytes)", cachePath.c_str(), cacheLength);
}

// Cold path: compile the source once, publish a code cache from that same
// compilation (so we never compile the file twice), then run it.
static napi_status CompileRunAndCache(napi_env env, napi_value script, const char *file,
                                      napi_value *result) {
    NAPI_PREAMBLE(env);
    CHECK_ARG(env, script);
    CHECK_ARG(env, result);

    v8::Local<v8::Value> scriptValue = v8impl::V8LocalValueFromJsValue(script);
    if (!scriptValue->IsString()) {
        return napi_set_last_error(env, napi_string_expected);
    }
    v8::Local<v8::String> sourceString = scriptValue.As<v8::String>();

    std::string fsPath;
    bool cacheable = NormalizeScriptPath(file, fsPath);

    // Set the source URL via ScriptOrigin (stack traces + a stable code-cache
    // key that matches js_run_cached_script's consume side). For non-file
    // sources we still attach the given name for stack traces but don't cache.
    std::string sourceUrl = cacheable ? ("file://" + fsPath)
                                       : (file ? std::string(file) : std::string());
    auto originStr = v8::String::NewFromUtf8(env->isolate, sourceUrl.c_str());
#ifdef __V8_13__
    v8::ScriptOrigin origin(originStr.ToLocalChecked());
#else
    v8::ScriptOrigin origin(env->isolate, originStr.ToLocalChecked());
#endif

    ScriptCompiler::Source source(sourceString, origin);
    Local<Script> compiled;
    if (!ScriptCompiler::Compile(env->context(), &source,
                                 ScriptCompiler::kNoCompileOptions).ToLocal(&compiled)) {
        // Syntax error — surface it (GET_RETURN_STATUS turns the caught exception
        // into napi_pending_exception).
        return GET_RETURN_STATUS(env);
    }

    DEBUG_WRITE("[code-cache] compiling from source (cold): %s (cacheable=%d)", file, cacheable);

    // Publish a code cache from this single compile for the next launch.
    if (cacheable) {
        PersistCodeCache(compiled->GetUnboundScript(), fsPath);
    }

    v8::Local<v8::Value> ret;
    if (!compiled->Run(env->context()).ToLocal(&ret)) {
        return GET_RETURN_STATUS(env); // threw while running
    }

    *result = v8impl::JsValueFromV8LocalValue(ret);
    return GET_RETURN_STATUS(env);
}

napi_status js_execute_script(napi_env env,
                              napi_value script,
                              const char *file,
                              napi_value *result) {
    // Fast path: run V8's on-disk code cache when one is present and current.
    // js_run_cached_script returns napi_cannot_run_js on a cache miss (so we fall
    // back to source), or any other status when it actually ran the script
    // (success or a thrown exception) — in which case we must NOT run it again.
    napi_status status = js_run_cached_script(env, file, script, nullptr, result);
    if (status != napi_cannot_run_js) {
        return status;
    }

    // Cold path: compile + run the source, producing a code cache from that same
    // compilation for the next launch (no second compile).
    return CompileRunAndCache(env, script, file, result);
}

napi_status js_execute_pending_jobs(napi_env env) {
    env->isolate->PerformMicrotaskCheckpoint();
    return napi_ok;
}

napi_status js_get_engine_ptr(napi_env env, int64_t *engine_ptr) {
    *engine_ptr = reinterpret_cast<int64_t>(env->context()->GetIsolate());
    return napi_ok;
}

napi_status
js_adjust_external_memory(napi_env env, int64_t changeInBytes, int64_t *externalMemory) {
    *externalMemory = env->isolate->AdjustAmountOfExternalAllocatedMemory(changeInBytes);
    return napi_ok;
}

// Compile `source` and persist a code cache next to `file`. Retained for the
// jsr interface; the hot path (js_execute_script) caches inline from its own
// single compile, so this is only for out-of-band callers that hold just the
// source text. No-op for synthetic / non-file sources.
napi_status js_cache_script(napi_env env, const char *source, const char *file) {
    std::string fsPath;
    if (!NormalizeScriptPath(file, fsPath)) {
        return napi_ok;
    }

    v8::Local<v8::String> sourceString = v8::String::NewFromUtf8(env->isolate,
                                                                 source).ToLocalChecked();
    std::string sourceUrl = "file://" + fsPath;
    v8::Local<v8::String> fileString = v8::String::NewFromUtf8(env->isolate,
                                                               sourceUrl.c_str()).ToLocalChecked();
#ifdef __V8_13__
    v8::ScriptOrigin origin(fileString);
#else
    v8::ScriptOrigin origin(env->isolate, fileString);
#endif

    // Guard the compile and never let a failed cache write leak a pending
    // exception into the caller.
    v8::TryCatch tc(env->isolate);
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(env->context(), sourceString, &origin).ToLocal(&script)) {
        if (tc.HasCaught()) tc.Reset();
        return napi_ok;
    }

    PersistCodeCache(script->GetUnboundScript(), fsPath);
    return napi_ok;
}

napi_status js_run_cached_script(napi_env env, const char *file, napi_value script, void *cache,
                                 napi_value *result) {
    NAPI_PREAMBLE(env);
    CHECK_ARG(env, script);
    CHECK_ARG(env, result);

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

    int length = 0;
    auto data = File::ReadBinary(cachePath, length);
    if (data == nullptr || length <= 0) {
        if (data) delete[] static_cast<uint8_t *>(data);
        return napi_cannot_run_js;
    }

    v8::Local<v8::Value> scriptValue = v8impl::V8LocalValueFromJsValue(script);
    if (!scriptValue->IsString()) {
        delete[] static_cast<uint8_t *>(data);
        return napi_set_last_error(env, napi_string_expected);
    }
    v8::Local<v8::String> sourceString = scriptValue.As<v8::String>();

    // Source takes ownership of cacheData and frees it.
    auto *cacheData = new ScriptCompiler::CachedData(static_cast<const uint8_t *>(data), length,
                                                     ScriptCompiler::CachedData::BufferOwned);

    std::string sourceUrl = "file://" + fsPath;
    auto originStr = v8::String::NewFromUtf8(env->isolate, sourceUrl.c_str());
#ifdef __V8_13__
    v8::ScriptOrigin origin(originStr.ToLocalChecked());
#else
    v8::ScriptOrigin origin(env->isolate, originStr.ToLocalChecked());
#endif

    ScriptCompiler::Source source(sourceString, origin, cacheData);
    Local<Script> cachedScript;
    if (!ScriptCompiler::Compile(env->context(), &source,
                                 ScriptCompiler::kConsumeCodeCache).ToLocal(&cachedScript)) {
        // Compilation itself failed — the source is genuinely bad (not just a
        // stale cache, which V8 handles by recompiling and flagging `rejected`).
        // Surface the exception so the caller doesn't retry; there is nothing to
        // "fall back" to.
        return GET_RETURN_STATUS(env);
    }

    if (source.GetCachedData() != nullptr && source.GetCachedData()->rejected) {
        // The cache was stale/incompatible; V8 recompiled from source but we did
        // NOT run it. Report a miss so the caller runs the source and refreshes
        // the cache. (No script executed, so no double side effects.)
        DEBUG_WRITE("[code-cache] miss (V8 rejected cache as incompatible): %s", fsPath.c_str());
        return napi_cannot_run_js;
    }

    DEBUG_WRITE("[code-cache] loaded V8 compiled code cache: %s (%d bytes)", fsPath.c_str(), length);

    v8::Local<v8::Value> ret;
    if (!cachedScript->Run(env->context()).ToLocal(&ret)) {
        // The module threw while executing. This is a real run — propagate the
        // exception; the caller must NOT execute the source a second time.
        return GET_RETURN_STATUS(env);
    }

    *result = v8impl::JsValueFromV8LocalValue(ret);
    return GET_RETURN_STATUS(env);
}

napi_status js_run_bytecode_file(napi_env env, const char *file, napi_value *result) {
    // V8 has no compile-time bytecode format (it caches code at runtime instead,
    // see js_cache_script/js_run_cached_script); always fall back to source.
    return napi_cannot_run_js;
}

napi_status js_get_runtime_version(napi_env env, napi_value *version) {

    napi_create_string_utf8(env, "V8", NAPI_AUTO_LENGTH, version);

    return napi_ok;
}

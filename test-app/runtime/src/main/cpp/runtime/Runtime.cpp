#include <unistd.h>
#include <thread>
#include "Runtime.h"
#include <string>
#include <csignal>
#include <sstream>
#include <mutex>
#include <dlfcn.h>
#include "zipconf.h"
#include "NativeScriptException.h"
#include <sys/system_properties.h>
#include "File.h"
#include <android/log.h>
#include "Version.h"
#include "SIGHandler.h"
#include "ArgConverter.h"
#include "NativeScriptAssert.h"
#include "CallbackHandlers.h"
#include "MetadataNode.h"
#include "Console.h"
#include "Util.h"
#include "Performance.h"
#include "JsArgToArrayConverter.h"
#include "ArrayHelper.h"
#include "SimpleProfiler.h"
#include "ManualInstrumentation.h"
#include "GlobalHelpers.h"
#include "Timers.h"
#ifdef __JSC__
#include "WeakRef.h"
#endif

#ifdef APPLICATION_IN_DEBUG
// #include "NetworkDomainCallbackHandlers.h"
#include "JsV8InspectorClient.h"
#endif

using namespace tns;
using namespace std;

bool tns::LogEnabled = false;

void Runtime::Init(JavaVM *vm) {
    __android_log_print(ANDROID_LOG_INFO, "TNS.Runtime",
                        "NativeScript Runtime Version %s, commit %s", NATIVE_SCRIPT_RUNTIME_VERSION,
                        NATIVE_SCRIPT_RUNTIME_COMMIT_SHA);


    if (Runtime::java_vm == nullptr) {
        java_vm = vm;
        JEnv::Init(java_vm);
        NativeScriptException::Init();
    }

    // handle SIGABRT/SIGSEGV only on API level > 20 as the handling is not so efficient in older versions
    if (m_androidVersion > 20) {
        struct sigaction action;
        action.sa_handler = SIGHandler;
        sigaction(SIGABRT, &action, NULL);
        sigaction(SIGSEGV, &action, NULL);
    }
}
/**
 * Returns the runtime based on the current thread id
 * Defaults to returning the main runtime if no runtime is found.
 *
 * One thread can only host a single runtime at the moment. Multiple runtimes
 * on a single thread are not supported.
 * @return
 */
Runtime *Runtime::Current() {
    if (!s_mainThreadInitialized) return nullptr;
    auto id = this_thread::get_id();
    auto rt = Runtime::thread_id_to_rt_cache.Get(id);
    if (rt) return rt;

   return s_main_rt;
}

Runtime::Runtime(JNIEnv *jEnv, jobject runtime, int id)
        : m_id(id), m_lastUsedMemory(0),m_gcFunc(nullptr){
    m_runtime = jEnv->NewGlobalRef(runtime);
    m_objectManager = new ObjectManager(m_runtime);
    m_loopTimer = new MessageLoopTimer();
    id_to_runtime_cache.Insert(id, this);

    js_method_cache = new JSMethodCache(this);

    auto tid = this_thread::get_id();
    Runtime::thread_id_to_rt_cache.Insert(tid, this);
    this->my_thread_id = tid;

    if (GET_USED_MEMORY_METHOD_ID == nullptr) {
        auto RUNTIME_CLASS = jEnv->FindClass("com/tns/Runtime");
        assert(RUNTIME_CLASS != nullptr);
        GET_USED_MEMORY_METHOD_ID = jEnv->GetMethodID(RUNTIME_CLASS, "getUsedMemory", "()J");
        assert(GET_USED_MEMORY_METHOD_ID != nullptr);
    }
}

Runtime *Runtime::GetRuntime(int runtimeId) {
    auto runtime = id_to_runtime_cache.Get(runtimeId);

    if (runtime == nullptr) {
        stringstream ss;
        ss << "Cannot find runtime for id:" << runtimeId;
        throw NativeScriptException(ss.str());
    }

    return runtime;
}

jobject Runtime::GetJavaRuntime() const {
    return m_runtime;
}

void
Runtime::Init(JNIEnv *_env, jobject obj, int runtimeId, jstring filesPath, jstring nativeLibsDir,
              jboolean verboseLoggingEnabled, jboolean isDebuggable, jstring packageName,
              jobjectArray args, jstring callingDir, int maxLogcatObjectSize, bool forceLog) {
    JEnv env(_env);
    auto runtime = new Runtime(env, obj, runtimeId);
    auto enableLog = verboseLoggingEnabled == JNI_TRUE;

    runtime->Init(env, filesPath, nativeLibsDir, enableLog, isDebuggable, packageName, args,
                  callingDir, maxLogcatObjectSize, forceLog);
}

napi_value Runtime::GlobalAccessorCallback(napi_env env, napi_callback_info  info) {
    napi_value global;
    napi_get_global(env, &global);
    return global;
}

void Runtime::Init(JNIEnv *_env, jstring filesPath, jstring nativeLibsDir,
                   bool verboseLoggingEnabled, bool isDebuggable, jstring packageName,
                   jobjectArray args, jstring callingDir, int maxLogcatObjectSize, bool forceLog) {

    LogEnabled = verboseLoggingEnabled;
    auto filesRoot = ArgConverter::jstringToString(filesPath);
    auto nativeLibDirStr = ArgConverter::jstringToString(nativeLibsDir);
    auto packageNameStr = ArgConverter::jstringToString(packageName);
    auto callingDirStr = ArgConverter::jstringToString(callingDir);

    Constants::APP_ROOT_FOLDER_PATH = filesRoot + "/app/";

    DEBUG_WRITE("Initializing NativeScript NAPI Runtime");

    auto flags = ArgConverter::jstringToString(JniLocalRef(_env->GetObjectArrayElement(args, 0)));

    JniLocalRef cacheCode(_env->GetObjectArrayElement(args, 1));
    Constants::CACHE_COMPILED_CODE = (bool) cacheCode;

    JniLocalRef profilerOutputDir(_env->GetObjectArrayElement(args, 2));

    js_set_runtime_flags(flags.c_str());
    js_create_runtime(&rt);
    js_create_napi_env(&env, rt);
#ifdef __V8__
    v8::Locker locker(env->isolate);
    v8::Isolate::Scope isolate_scope(env->isolate);
    v8::Context::Scope context_scope(env->context());
#endif
    napi_open_handle_scope(env, &global_scope);

    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);

    env_to_runtime_cache.Insert(env, this);

    napi_value global;
    napi_get_global(env, &global);

#ifdef __JSC__
    tns::WeakRef::Init(env);
#endif

#ifdef APPLICATION_IN_DEBUG
    Console::createConsole(env, JsV8InspectorClient::consoleLogCallback, maxLogcatObjectSize, forceLog);
#else
    Console::createConsole(env, nullptr, maxLogcatObjectSize, forceLog);
#endif

    Timers::InitStatic(env, global);

    napi_util::napi_set_function(env, global, "__log", CallbackHandlers::LogMethodCallback);
    napi_util::napi_set_function(env, global, "__dumpReferenceTables",
                                 CallbackHandlers::DumpReferenceTablesMethodCallback);
    napi_util::napi_set_function(env, global, "__drainMicrotaskQueue",
                                 CallbackHandlers::DrainMicrotaskCallback);
    napi_util::napi_set_function(env, global, "__enableVerboseLogging",
                                 CallbackHandlers::EnableVerboseLoggingMethodCallback);
    napi_util::napi_set_function(env, global, "__disableVerboseLogging",
                                 CallbackHandlers::DisableVerboseLoggingMethodCallback);
    napi_util::napi_set_function(env, global, "__exit", CallbackHandlers::ExitMethodCallback);

    napi_value rt_version;
    napi_create_string_utf8(env, NATIVE_SCRIPT_RUNTIME_VERSION, NAPI_AUTO_LENGTH, &rt_version);
    napi_set_named_property(env, global, "__runtimeVersion", rt_version);

    napi_value engine;
    js_get_runtime_version(env, &engine);
    napi_set_named_property(env, global, "__engine", engine);


    napi_util::napi_set_function(env, global, "__time", CallbackHandlers::TimeCallback);
    napi_util::napi_set_function(env, global, "__releaseNativeCounterpart",
                                 CallbackHandlers::ReleaseNativeCounterpartCallback);
    napi_util::napi_set_function(env, global, "__postFrameCallback",
                                 CallbackHandlers::PostFrameCallback);
    napi_util::napi_set_function(env, global, "__removeFrameCallback",
                                 CallbackHandlers::RemoveFrameCallback);
    napi_util::napi_set_function(env, global, "__markingMode",
                                 [](napi_env _env, napi_callback_info) -> napi_value {
                                     napi_value mode;
                                     napi_create_int32(_env, 0, &mode);
                                     return mode;
                                 });

    napi_util::napi_set_function(env, global, "napiFunction",
                                 [](napi_env _env, napi_callback_info) -> napi_value {
                                     return nullptr;
                                 });

    SimpleProfiler::Init(env, global);

    CallbackHandlers::CreateGlobalCastFunctions(env);

    CallbackHandlers::Init(env);

    ArgConverter::Init(env);


    m_objectManager->Init(env);

    m_module.Init(env, ArgConverter::jstringToString(callingDir));
    /*
     * Attach `Worker` object constructor only to the main thread (isolate)'s global object
     * Workers should not be created from within other Workers, for now
     */
    if (!s_mainThreadInitialized) {
        m_isMainThread = true;

        s_main_rt = this;
        s_main_thread_id = this_thread::get_id();

        pipe2(m_mainLooper_fd, O_NONBLOCK | O_CLOEXEC);
        m_mainLooper = ALooper_forThread();

        ALooper_acquire(m_mainLooper);

        // try using 2MB
        int ret = fcntl(m_mainLooper_fd[1], F_SETPIPE_SZ, 2 * (1024 * 1024));

        // try using 1MB
        if (ret != 0) {
            ret = fcntl(m_mainLooper_fd[1], F_SETPIPE_SZ, 1 * (1024 * 1024));
        }

        // try using 512KB
        if (ret != 0) {
            ret = fcntl(m_mainLooper_fd[1], F_SETPIPE_SZ, (512 * 1024));
        }

        ALooper_addFd(m_mainLooper, m_mainLooper_fd[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                      CallbackHandlers::RunOnMainThreadFdCallback, nullptr);

        napi_value worker;
        napi_define_class(env, "Worker", strlen("Worker"), CallbackHandlers::NewThreadCallback,
                          nullptr, 0, nullptr, &worker);
        napi_value prototype = napi_util::get_prototype(env, worker);
        napi_util::napi_set_function(env, prototype, "postMessage",
                                     CallbackHandlers::WorkerObjectPostMessageCallback, nullptr);
        napi_util::napi_set_function(env, prototype, "terminate",
                                     CallbackHandlers::WorkerObjectTerminateCallback, nullptr);
        napi_util::napi_set_function(env, prototype, "close",
                                     CallbackHandlers::WorkerGlobalCloseCallback, nullptr);

        napi_set_named_property(env, global, "Worker", worker);
    }
        /*
         * Emulate a `WorkerGlobalScope`
         * Attach 'postMessage', 'close' to the global object
         */
    else {
        m_isMainThread = false;
        napi_util::napi_set_function(env, global, "postMessage",
                                     CallbackHandlers::WorkerGlobalPostMessageCallback, nullptr);
        napi_util::napi_set_function(env, global, "close",
                                     CallbackHandlers::WorkerGlobalCloseCallback, nullptr);
        napi_util::napi_set_function(env, global, "terminate",
                                     CallbackHandlers::WorkerGlobalCloseCallback, nullptr);
        napi_util::define_property(env, global, "__ns__worker", napi_util::get_true(env));
    }

    napi_util::define_property(env, global, "global", nullptr, GlobalAccessorCallback);

    if (!s_mainThreadInitialized) {
        MetadataNode::BuildMetadata(filesRoot);
    } else {
        // Do not set 'self' accessor to main thread
        napi_util::define_property(env, global, "self", nullptr, GlobalAccessorCallback);
    }

    MetadataNode::CreateTopLevelNamespaces(env);

    ArrayHelper::Init(env);

    Performance::createPerformance(env, global);

    m_arrayBufferHelper.CreateConvertFunctions(env, global, m_objectManager);

    m_loopTimer->Init(env);

    s_mainThreadInitialized = true;

    napi_close_handle_scope(env, handleScope);

    DEBUG_WRITE("%s", "NativeScript Runtime Loaded!");
}

int Runtime::GetAndroidVersion() {
    char sdkVersion[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.sdk", sdkVersion);

    std::stringstream strValue;
    strValue << sdkVersion;

    unsigned int intValue;
    strValue >> intValue;

    return intValue;
}

ObjectManager *Runtime::GetObjectManager(napi_env env) {
    return GetRuntime(env)->GetObjectManager();
}

ObjectManager *Runtime::GetObjectManager() const {
    return m_objectManager;
}

Runtime::~Runtime() {
    delete this->m_objectManager;
    delete this->m_loopTimer;

#ifdef __V8__
    js_free_runtime(rt);
#endif

    if (m_isMainThread) {
        if (m_mainLooper_fd[0] != -1) {
            ALooper_removeFd(m_mainLooper, m_mainLooper_fd[0]);
        }
        ALooper_release(m_mainLooper);

        if (m_mainLooper_fd[0] != -1) {
            close(m_mainLooper_fd[0]);
        }

        if (m_mainLooper_fd[1] != -1) {
            close(m_mainLooper_fd[1]);
        }
    }
}

std::string Runtime::ReadFileText(const std::string &filePath) {
#ifdef APPLICATION_IN_DEBUG
    std::lock_guard<std::mutex> lock(m_fileWriteMutex);
#endif
    return File::ReadText(filePath);
}

void Runtime::DestroyRuntime() {
    DEBUG_WRITE_FORCE("%s", "DESTROYING RUNTIME NOW");
    is_destroying = true;
    MetadataNode::onDisposeEnv(env);
    ArgConverter::onDisposeEnv(env);
    tns::GlobalHelpers::onDisposeEnv(env);
    this->js_method_cache->cleanupCache();
    delete this->js_method_cache;
    this->m_module.DeInit();
    Console::onDisposeEnv(env);
    CallbackHandlers::RemoveEnvEntries(env);
    this->m_objectManager->OnDisposeEnv();
    napi_close_handle_scope(env, this->global_scope);
    Runtime::thread_id_to_rt_cache.Remove(this->my_thread_id);
    id_to_runtime_cache.Remove(m_id);
    env_to_runtime_cache.Remove(env);
    js_free_napi_env(env);

#ifndef __V8__
    js_free_runtime(rt);
#endif
}

bool Runtime::NotifyGC(JNIEnv *jEnv, jobject obj, jintArray object_ids) {
    if (this->is_destroying) return true;
    m_objectManager->OnGarbageCollected(jEnv, object_ids);
    bool success = __sync_bool_compare_and_swap(&m_runGC, false, true);
    return success;
}


void Runtime::AdjustAmountOfExternalAllocatedMemory() {
    JEnv jEnv;
    int64_t usedMemory = jEnv.CallLongMethod(m_runtime, GET_USED_MEMORY_METHOD_ID);
    int64_t changeInBytes = usedMemory - m_lastUsedMemory;
    int64_t externalMemory = 0;

    if (changeInBytes != 0) {
       js_adjust_external_memory(env, changeInBytes, &externalMemory);
    }

    DEBUG_WRITE("usedMemory=%" PRId64 " changeInBytes=%" PRId64 " externalMemory=%" PRId64, usedMemory, changeInBytes, externalMemory);

    m_lastUsedMemory = usedMemory;
}

bool Runtime::TryCallGC() {
    if (this->is_destroying) return true;
    napi_value global;
    napi_get_global(env, &global);
    if (!m_gcFunc) {
        napi_value gc;
        napi_get_named_property(env, global, "gc", &gc);
        if (napi_util::is_null_or_undefined(env, gc)) return true;
        napi_create_reference(env, gc, 1, &m_gcFunc);
    }

    bool success = __sync_bool_compare_and_swap(&m_runGC, true, false);

    if (success) {
        napi_value result;
        napi_call_function(env, global, napi_util::get_ref_value(env, m_gcFunc), 0, nullptr, &result);
    }

    return success;
}

void Runtime::RunModule(JNIEnv *_jEnv, jobject obj, jstring scriptFile) {
    JEnv jEnv(_jEnv);
    string filePath = ArgConverter::jstringToString(scriptFile);
    m_module.Load(env, filePath);
}

void Runtime::RunModule(const char *moduleName) {
    m_module.Load(env, moduleName);
}

void Runtime::RunWorker(jstring scriptFile) {
    string filePath = ArgConverter::jstringToString(scriptFile);
    m_module.LoadWorker(env, filePath);
}

jobject Runtime::RunScript(JNIEnv *_env, jobject obj, jstring scriptFile) {
    int status;
    auto filename = ArgConverter::jstringToString(scriptFile);
    auto src = ReadFileText(filename);

    napi_value soureCode;
    napi_create_string_utf8(env, src.c_str(), src.length(), &soureCode);

    napi_value result;
    DEBUG_WRITE("%s", filename.c_str());
    status = js_execute_script(env, soureCode, ModuleInternal::EnsureFileProtocol(filename).c_str(), &result);

    bool pendingException;
    napi_is_exception_pending(env, &pendingException);
    if (status != napi_ok || pendingException) {
        napi_value error = nullptr;
        if (pendingException) {
            napi_get_and_clear_last_exception(env, &error);
        }
        if (error) {
            throw NativeScriptException(env, error, "Error running script " + filename);
        } else {
            throw NativeScriptException("Error running script " + filename);
        }
    }

    return nullptr;
}

napi_env Runtime::GetNapiEnv() {
    return env;
}

napi_runtime Runtime::GetNapiRuntime() {
    return rt;
}

int Runtime::GetId() {
    return this->m_id;
}

int Runtime::GetWriter() {
    return m_mainLooper_fd[1];
}

int Runtime::GetReader() {
    return m_mainLooper_fd[0];
}

jobject
Runtime::CallJSMethodNative(JNIEnv *_jEnv, jobject obj, jint javaObjectID, jclass claz, jstring methodName,
                            jint retType, jboolean isConstructor, jobjectArray packagedArgs) {
    JEnv jEnv(_jEnv);

    DEBUG_WRITE("CallJSMethodNative called javaObjectID=%d", javaObjectID);

    auto jsObject = m_objectManager->GetJsObjectByJavaObject(javaObjectID);

    if (napi_util::is_null_or_undefined(env, jsObject)) {
        stringstream ss;
        ss << "JavaScript object for Java ID " << javaObjectID << " not found." << endl;
        ss << "Attempting to call method " << ArgConverter::jstringToString(methodName) << endl;
        throw NativeScriptException(ss.str());
    }

    if (isConstructor) {
        DEBUG_WRITE("CallJSMethodNative: Updating linked instance with its real class");
        jclass instanceClass = jEnv.GetObjectClass(obj);
        m_objectManager->SetJavaClass(jsObject, instanceClass);
    }

    string method_name = ArgConverter::jstringToString(methodName);

    DEBUG_WRITE("CallJSMethodNative called jsObject %s", method_name.c_str());

    auto jsResult = CallbackHandlers::CallJSMethod(env, jEnv, jsObject, claz,method_name, javaObjectID,packagedArgs);

    if (napi_util::is_null_or_undefined(env, jsResult)) return nullptr;

    int classReturnType = retType;
    jobject javaObject = ConvertJsValueToJavaObject(jEnv, jsResult, classReturnType);

    return javaObject;
}

void
Runtime::CreateJSInstanceNative(JNIEnv *_jEnv, jobject obj, jobject javaObject, jint javaObjectID,
                                jstring className) {
    DEBUG_WRITE("createJSInstanceNative called");
    JEnv jEnv(_jEnv);

    string existingClassName = ArgConverter::jstringToString(className);

    string jniName = Util::ConvertFromCanonicalToJniName(existingClassName);

    napi_value jsInstance;
    napi_value implementationObject;

    auto proxyClassName = m_objectManager->GetClassName(javaObject);

    DEBUG_WRITE("createJSInstanceNative class %s", proxyClassName.c_str());

    jsInstance = MetadataNode::CreateExtendedJSWrapper(env, m_objectManager, proxyClassName, javaObjectID);

    if (napi_util::is_null_or_undefined(env, jsInstance)) {
        throw NativeScriptException(
                string("Failed to create JavaScript extend wrapper for class '" + proxyClassName +
                       "'"));
    }

    implementationObject = MetadataNode::GetImplementationObject(env, jsInstance);

    if (napi_util::is_null_or_undefined(env, implementationObject)) {
        string msg("createJSInstanceNative: implementationObject is empty");
        throw NativeScriptException(msg);
    }

    DEBUG_WRITE("createJSInstanceNative: implementationObject");

    m_objectManager->Link(jsInstance, javaObjectID, nullptr);
}

jint Runtime::GenerateNewObjectId(JNIEnv *jEnv, jobject obj) {
    int objectId = m_objectManager->GenerateNewObjectID();
    return objectId;
}

jobject Runtime::ConvertJsValueToJavaObject(JEnv &jEnv, napi_value value, int classReturnType) {
    JsArgToArrayConverter argConverter(env, value, false /*is implementation object*/,
                                       classReturnType);
    jobject jr = argConverter.GetConvertedArg();
    jobject javaResult = nullptr;
    if (jr != nullptr) {
        javaResult = jEnv.NewLocalRef(jr);
    }

    return javaResult;
}

void
Runtime::PassExceptionToJsNative(JNIEnv *jEnv, jobject obj, jthrowable exception, jstring message,
                                 jstring fullStackTrace, jstring jsStackTrace,
                                 jboolean isDiscarded) {
    napi_env napiEnv = env;

    std::string errMsg = ArgConverter::jstringToString(message);

    napi_value errObj;
    napi_value errMsgNapi;
    napi_create_string_utf8(napiEnv, errMsg.c_str(), NAPI_AUTO_LENGTH, &errMsgNapi);
    napi_create_error(napiEnv, nullptr, errMsgNapi, &errObj);

    // Create a new native exception js object
    jint javaObjectID = m_objectManager->GetOrCreateObjectId((jobject) exception);
    napi_value nativeExceptionObject = m_objectManager->GetJsObjectByJavaObject(javaObjectID);

    if (nativeExceptionObject == nullptr) {
        std::string className = m_objectManager->GetClassName((jobject) exception);
        // Create proxy object that wraps the java err
        nativeExceptionObject = m_objectManager->CreateJSWrapper(javaObjectID, className);
        if (nativeExceptionObject == nullptr) {
            napi_create_object(napiEnv, &nativeExceptionObject);
        }
    }

    // Create a JS error object
    napi_value fullStackTraceNapi;
    napi_create_string_utf8(napiEnv, ArgConverter::jstringToString(fullStackTrace).c_str(),
                            NAPI_AUTO_LENGTH, &fullStackTraceNapi);
    napi_set_named_property(napiEnv, errObj, "nativeException", nativeExceptionObject);
    napi_set_named_property(napiEnv, errObj, "stackTrace", fullStackTraceNapi);
    if (jsStackTrace != nullptr) {
        napi_value jsStackTraceNapi;
        napi_create_string_utf8(napiEnv, ArgConverter::jstringToString(jsStackTrace).c_str(),
                                NAPI_AUTO_LENGTH, &jsStackTraceNapi);
        napi_set_named_property(napiEnv, errObj, "stack", jsStackTraceNapi);
    }

    // Pass err to JS
    NativeScriptException::CallJsFuncWithErr(env, errObj, isDiscarded);

}

void
Runtime::PassUncaughtExceptionFromWorkerToMainHandler(napi_value message, napi_value stackTrace,
                                                      napi_value filename, int lineno) {
    JEnv jEnv;
    auto runtimeClass = jEnv.GetObjectClass(m_runtime);


    auto mId = jEnv.GetStaticMethodID(runtimeClass, "passUncaughtExceptionFromWorkerToMain",
                                      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");

    auto jMsg = ArgConverter::ConvertToJavaString(env, message);
    auto jfileName = ArgConverter::ConvertToJavaString(env, filename);
    auto stckTrace = ArgConverter::ConvertToJavaString(env, stackTrace);

    JniLocalRef jMsgLocal(jMsg);
    JniLocalRef jfileNameLocal(jfileName);
    JniLocalRef stTrace(stckTrace);

    jEnv.CallStaticVoidMethod(runtimeClass, mId, (jstring) jMsgLocal, (jstring) jfileNameLocal,
                              (jstring) stTrace, (jint) lineno);
}

void Runtime::SetManualInstrumentationMode(jstring mode) {
    auto modeStr = ArgConverter::jstringToString(mode);
    if (modeStr == "timeline") {
        tns::instrumentation::Frame::enable();
    }
}

void Runtime::Lock() {
#ifdef APPLICATION_IN_DEBUG
    m_fileWriteMutex.lock();
#endif
}

void Runtime::Unlock() {
#ifdef APPLICATION_IN_DEBUG
    m_fileWriteMutex.unlock();
#endif
}


JavaVM *Runtime::java_vm = nullptr;
jmethodID Runtime::GET_USED_MEMORY_METHOD_ID = nullptr;
tns::ConcurrentMap<int, Runtime *> Runtime::id_to_runtime_cache;
tns::ConcurrentMap<napi_env, Runtime *> Runtime::env_to_runtime_cache;
bool Runtime::s_mainThreadInitialized = false;
int Runtime::m_androidVersion = Runtime::GetAndroidVersion();
ALooper *Runtime::m_mainLooper = nullptr;
tns::ConcurrentMap<std::thread::id, Runtime*> Runtime::thread_id_to_rt_cache;

int Runtime::m_mainLooper_fd[2];

Runtime *Runtime::s_main_rt = nullptr;
std::thread::id Runtime::s_main_thread_id;
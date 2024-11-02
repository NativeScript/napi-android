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

using namespace tns;
using namespace std;

bool tns::LogEnabled = true;

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

Runtime *Runtime::Current() {
    auto id = this_thread::get_id();
    auto size = Runtime::thread_id_to_rt_cache.size();
    auto itFound = Runtime::thread_id_to_rt_cache.find(id);

    if (itFound != Runtime::thread_id_to_rt_cache.end()) {
        return itFound->second;
    }
    return nullptr;
}

Runtime::Runtime(JNIEnv *jEnv, jobject runtime, int id)
        : m_id(id), m_lastUsedMemory(0) {
    m_runtime = jEnv->NewGlobalRef(runtime);
    m_objectManager = new ObjectManager(m_runtime);
    m_loopTimer = new MessageLoopTimer();
    id_to_runtime_cache.emplace(id, this);

    auto tid = this_thread::get_id();
    Runtime::thread_id_to_rt_cache.emplace(tid, this);

    auto size = Runtime::thread_id_to_rt_cache.size();

    if (GET_USED_MEMORY_METHOD_ID == nullptr) {
        auto RUNTIME_CLASS = jEnv->FindClass("com/tns/Runtime");
        assert(RUNTIME_CLASS != nullptr);
        GET_USED_MEMORY_METHOD_ID = jEnv->GetMethodID(RUNTIME_CLASS, "getUsedMemory", "()J");
        assert(GET_USED_MEMORY_METHOD_ID != nullptr);
    }
}

Runtime *Runtime::GetRuntime(int runtimeId) {
    auto itFound = id_to_runtime_cache.find(runtimeId);
    auto runtime = (itFound != id_to_runtime_cache.end())
                   ? itFound->second
                   : nullptr;

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

Runtime *Runtime::GetRuntime(napi_env env) {
    auto runtime = env_to_runtime_cache.at(env);

    if (runtime == nullptr) {
        stringstream ss;
        ss << "Cannot find runtime for napi_env: " << env;
        throw NativeScriptException(ss.str());
    }

    return runtime;
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

    JniLocalRef profilerOutputDir(_env->GetObjectArrayElement(args, 2));

    NAPICreateJSRuntime(&rt);
    NAPICreateEnv(&env, rt);
    napi_open_handle_scope(env, &global_scope);

    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);
    env_to_runtime_cache.emplace(env, this);

    m_objectManager->SetInstanceEnv(env);
    napi_set_instance_data(env, this, nullptr, nullptr);

    napi_value global;
    napi_get_global(env, &global);

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
    napi_create_int32(env, 1, &rt_version);
    napi_set_named_property(env, global, "__runtimeVersion", rt_version);
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
        napi_value prototype = napi_util::get_proto(env, worker);
        napi_util::napi_set_function(env, prototype, "postMessage",
                                     CallbackHandlers::WorkerObjectPostMessageCallback, nullptr);
        napi_util::napi_set_function(env, prototype, "terminate",
                                     CallbackHandlers::WorkerObjectTerminateCallback, nullptr);
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
        napi_util::define_property(env, global, "__ns__worker", napi_util::get_true(env));
    }


    napi_util::define_property(env, global, "global", global);

    if (!s_mainThreadInitialized) {
        MetadataNode::BuildMetadata(filesRoot);
    } else {
        // Do not set 'self' accessor to main thread
        napi_util::define_property(env, global, "self", global);
    }

    MetadataNode::CreateTopLevelNamespaces(env);

    ArrayHelper::Init(env);

    m_arrayBufferHelper.CreateConvertFunctions(env, global, m_objectManager);

    m_loopTimer->Init(env);

    Console::createConsole(env, maxLogcatObjectSize, forceLog);

    Performance::createPerformance(env, global);

    s_mainThreadInitialized = true;
    napi_close_handle_scope(env, handleScope);
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

    CallbackHandlers::RemoveEnvEntries(env);
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
    return File::ReadText(filePath);
}

void Runtime::DestroyRuntime() {
    id_to_runtime_cache.erase(m_id);
    env_to_runtime_cache.erase(env);
    tns::GlobalHelpers::onDisposeEnv(env);
    Runtime::thread_id_to_rt_cache.erase(this_thread::get_id());
    Console::onDisposeEnv(env);
    napi_close_handle_scope(env, this->global_scope);
    NAPIFreeEnv(env);
}

bool Runtime::NotifyGC(JNIEnv *jEnv, jobject obj, jintArray object_ids) {
    m_objectManager->OnGarbageCollected(jEnv, object_ids);
    this->TryCallGC();
    return true;
}

bool Runtime::TryCallGC() {
    napi_value gc;
    napi_value global;
    napi_get_global(env, &global);
    napi_get_named_property(env, global, "gc", &gc);
    napi_value result;
    napi_call_function(env, nullptr, gc, 0, nullptr, &result);
    return true;
}

void Runtime::RunModule(JNIEnv *_jEnv, jobject obj, jstring scriptFile) {
    JEnv jEnv(_jEnv);
    string filePath = ArgConverter::jstringToString(scriptFile);
    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);
    m_module.Load(env, filePath);
    napi_close_handle_scope(env, handleScope);
}

void Runtime::RunModule(const char *moduleName) {
    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);
    m_module.Load(env, moduleName);
    napi_close_handle_scope(env, handleScope);
}

void Runtime::RunWorker(jstring scriptFile) {
    // TODO: Pete: Why do I crash here with a JNI error (accessing bad jni)
    string filePath = ArgConverter::jstringToString(scriptFile);
    m_module.LoadWorker(env, filePath);
}

jobject Runtime::RunScript(JNIEnv *_env, jobject obj, jstring scriptFile) {
    int status;
    auto filename = ArgConverter::jstringToString(scriptFile);
    auto src = ReadFileText(filename);

    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);

    napi_value soureCode;
    napi_create_string_utf8(env, src.c_str(), src.length(), &soureCode);

    napi_value result;
    status = napi_run_script(env, soureCode, filename.c_str(), &result);

    if (status != napi_ok) {
        const napi_extended_error_info *info;
        napi_get_last_error_info(env, &info);
    }

    napi_close_handle_scope(env, handleScope);

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
Runtime::CallJSMethodNative(JNIEnv *_jEnv, jobject obj, jint javaObjectID, jstring methodName,
                            jint retType, jboolean isConstructor, jobjectArray packagedArgs) {

    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);
    JEnv jEnv(_jEnv);

    DEBUG_WRITE("CallJSMethodNative called javaObjectID=%d", javaObjectID);

    auto jsObject = m_objectManager->GetJsObjectByJavaObject(javaObjectID);

    if (jsObject == nullptr || napi_util::is_undefined(env, jsObject)) {
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

    DEBUG_WRITE("CallJSMethodNative called jsObject");

    string method_name = ArgConverter::jstringToString(methodName);
    auto jsResult = CallbackHandlers::CallJSMethod(env, jEnv, jsObject, method_name, packagedArgs);

    int classReturnType = retType;
    jobject javaObject = ConvertJsValueToJavaObject(jEnv, jsResult, classReturnType);
    napi_close_handle_scope(env, handleScope);
    return javaObject;
}

void
Runtime::CreateJSInstanceNative(JNIEnv *_jEnv, jobject obj, jobject javaObject, jint javaObjectID,
                                jstring className) {
    napi_handle_scope handleScope;
    napi_open_handle_scope(env, &handleScope);
    DEBUG_WRITE("createJSInstanceNative called");

    JEnv jEnv(_jEnv);

    string existingClassName = ArgConverter::jstringToString(className);

    string jniName = Util::ConvertFromCanonicalToJniName(existingClassName);

    napi_value jsInstance;
    napi_value implementationObject;

    auto proxyClassName = m_objectManager->GetClassName(javaObject);

    DEBUG_WRITE("createJSInstanceNative class %s", proxyClassName.c_str());

    jsInstance = MetadataNode::CreateExtendedJSWrapper(env, m_objectManager, proxyClassName);

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

    jclass clazz = jEnv.FindClass(jniName);
    m_objectManager->Link(jsInstance, javaObjectID, clazz);
    napi_close_handle_scope(env, handleScope);
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
                              (jstring) stTrace, lineno);
}

void Runtime::SetManualInstrumentationMode(jstring mode) {
    auto modeStr = ArgConverter::jstringToString(mode);
    if (modeStr == "timeline") {
        tns::instrumentation::Frame::enable();
    }
}

JavaVM *Runtime::java_vm = nullptr;
jmethodID Runtime::GET_USED_MEMORY_METHOD_ID = nullptr;
robin_hood::unordered_map<int, Runtime *> Runtime::id_to_runtime_cache;
robin_hood::unordered_map<napi_env, Runtime *> Runtime::env_to_runtime_cache;
bool Runtime::s_mainThreadInitialized = false;
int Runtime::m_androidVersion = Runtime::GetAndroidVersion();
ALooper *Runtime::m_mainLooper = nullptr;
robin_hood::unordered_map<std::thread::id, Runtime*> Runtime::thread_id_to_rt_cache;

int Runtime::m_mainLooper_fd[2];

Runtime *Runtime::s_main_rt = nullptr;
std::thread::id Runtime::s_main_thread_id;
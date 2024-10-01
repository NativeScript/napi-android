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

using namespace ns;
using namespace std;

bool ns::LogEnabled = true;

void Runtime::Init(JavaVM *vm)
{
    __android_log_print(ANDROID_LOG_INFO, "TNS.Runtime",
                        "NativeScript Runtime Version %s, commit %s", NATIVE_SCRIPT_RUNTIME_VERSION,
                        NATIVE_SCRIPT_RUNTIME_COMMIT_SHA);

    if (Runtime::java_vm == nullptr)
    {
        java_vm = vm;

        JEnv::Init(java_vm);
    }

    // handle SIGABRT/SIGSEGV only on API level > 20 as the handling is not so efficient in older versions
    if (m_androidVersion > 20)
    {
        struct sigaction action;
        action.sa_handler = SIGHandler;
        sigaction(SIGABRT, &action, NULL);
        sigaction(SIGSEGV, &action, NULL);
    }
}

Runtime* Runtime::Current() {
    return Runtime::s_current_rt;
}

Runtime::Runtime(JNIEnv *jEnv, jobject runtime, int id)
    : m_id(id), m_lastUsedMemory(0)
{
    m_runtime = jEnv->NewGlobalRef(runtime);
    m_objectManager = new ObjectManager(m_runtime);
    m_loopTimer = new MessageLoopTimer();
    id_to_runtime_cache.emplace(id, this);

    if (GET_USED_MEMORY_METHOD_ID == nullptr)
    {
        auto RUNTIME_CLASS = jEnv->FindClass("org/nativescript/runtime/napi/Runtime");
        assert(RUNTIME_CLASS != nullptr);

        GET_USED_MEMORY_METHOD_ID = jEnv->GetMethodID(RUNTIME_CLASS, "getUsedMemory", "()J");
        assert(GET_USED_MEMORY_METHOD_ID != nullptr);
    }
    Runtime::s_current_rt = this;
}

Runtime *Runtime::GetRuntime(int runtimeId)
{
    auto itFound = id_to_runtime_cache.find(runtimeId);
    auto runtime = (itFound != id_to_runtime_cache.end())
                       ? itFound->second
                       : nullptr;

    if (runtime == nullptr)
    {
        stringstream ss;
        ss << "Cannot find runtime for id:" << runtimeId;
        throw NativeScriptException(ss.str());
    }

    return runtime;
}

jobject Runtime::GetJavaRuntime() const
{
    return m_runtime;
}

Runtime *Runtime::GetRuntime(napi_env env)
{
    auto runtime = env_to_runtime_cache.at(env);

    if (runtime == nullptr)
    {
        stringstream ss;
        ss << "Cannot find runtime for napi_env: " << env;
        throw NativeScriptException(ss.str());
    }

    return runtime;
}

void Runtime::Init(JNIEnv *_env, jobject obj, int runtimeId, jstring filesPath)
{
    JEnv env(_env);

    auto runtime = new Runtime(env, obj, runtimeId);

    runtime->Init(env, filesPath);
}

void Runtime::Init(JNIEnv *jEnv, jstring filesPath)
{

    auto filesRoot = ArgConverter::jstringToString(filesPath);
    Constants::APP_ROOT_FOLDER_PATH = filesRoot + "/app/";

    DEBUG_WRITE("Initializing NativeScript NAPI Runtime");

    NAPICreateJSRuntime(&rt);
    NAPICreateEnv(&env, rt);
    napi_open_handle_scope(env, &global_scope);

    NativeScriptException::Init(env);

    m_objectManager->SetInstanceEnv(env);

    napi_set_instance_data(env, this, nullptr, nullptr);

    napi_value global;
    napi_get_global(env, &global);

    napi_util::napi_set_function(env, global, "__log", CallbackHandlers::LogMethodCallback);
    napi_util::napi_set_function(env, global, "__dumpReferenceTables", CallbackHandlers::DumpReferenceTablesMethodCallback);
    napi_util::napi_set_function(env, global, "__drainMicrotaskQueue", CallbackHandlers::DrainMicrotaskCallback);
    napi_util::napi_set_function(env, global, "__enableVerboseLogging", CallbackHandlers::EnableVerboseLoggingMethodCallback);
    napi_util::napi_set_function(env, global, "__disableVerboseLogging", CallbackHandlers::DisableVerboseLoggingMethodCallback);
    napi_util::napi_set_function(env, global, "__exit", CallbackHandlers::ExitMethodCallback);
    napi_value rt_version;
    napi_create_int32(env, 1, &rt_version);
    napi_set_named_property(env, global, "__runtimeVersion", rt_version);
    napi_util::napi_set_function(env, global, "__time", CallbackHandlers::TimeCallback);
    napi_util::napi_set_function(env, global, "__releaseNativeCounterpart", CallbackHandlers::ReleaseNativeCounterpartCallback);
    napi_util::napi_set_function(env, global, "__postFrameCallback", CallbackHandlers::PostFrameCallback);
    napi_util::napi_set_function(env, global, "__removeFrameCallback", CallbackHandlers::RemoveFrameCallback);
    napi_util::napi_set_function(env, global, "__markingMode", [](napi_env _env, napi_callback_info) -> napi_value
                                 {
        napi_value mode;
        napi_create_int32(_env, 0, &mode);
        return mode; });


    CallbackHandlers::CreateGlobalCastFunctions(env);

    CallbackHandlers::Init(env);
    

    ArgConverter::Init(env);

    m_objectManager->Init(env);

    if (!s_mainThreadInitialized)
    {
        MetadataNode::BuildMetadata(filesRoot);
    }

    MetadataNode::CreateTopLevelNamespaces(env);

    ArrayHelper::Init(env);

    m_arrayBufferHelper.CreateConvertFunctions(env, global, m_objectManager);

    m_loopTimer->Init(env);

    Console::createConsole(env, global);
    Performance::createPerformance(env, global);

    s_mainThreadInitialized = true;
}

int Runtime::GetAndroidVersion()
{
    char sdkVersion[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.sdk", sdkVersion);

    std::stringstream strValue;
    strValue << sdkVersion;

    unsigned int intValue;
    strValue >> intValue;

    return intValue;
}

ObjectManager *Runtime::GetObjectManager(napi_env env)
{
    return GetRuntime(env)->GetObjectManager();
}

ObjectManager *Runtime::GetObjectManager() const
{
    return m_objectManager;
}

Runtime::~Runtime()
{
    //    delete this->m_objectManager;
    delete this->m_loopTimer;
    //    CallbackHandlers::RemoveIsolateEntries(m_isolate);
    if (m_isMainThread)
    {
        if (m_mainLooper_fd[0] != -1)
        {
            ALooper_removeFd(m_mainLooper, m_mainLooper_fd[0]);
        }
        ALooper_release(m_mainLooper);

        if (m_mainLooper_fd[0] != -1)
        {
            close(m_mainLooper_fd[0]);
        }

        if (m_mainLooper_fd[1] != -1)
        {
            close(m_mainLooper_fd[1]);
        }
    }
}

std::string Runtime::ReadFileText(const std::string &filePath)
{
    return File::ReadText(filePath);
}

void Runtime::DestroyRuntime()
{
    id_to_runtime_cache.erase(m_id);
    env_to_runtime_cache.erase(env);
    napi_close_handle_scope(env, this->global_scope);
    NAPIFreeEnv(env);
}

jobject Runtime::RunScript(JNIEnv *_env, jobject obj, jstring scriptFile)
{
    int status;
    auto filename = ArgConverter::jstringToString(scriptFile);
    auto src = ReadFileText(filename + "/app/index.js");

    napi_value soureCode;
    napi_create_string_utf8(env, src.c_str(), src.length(), &soureCode);

    napi_value result;
    status = napi_run_script(env, soureCode, &result);

    if (status != napi_ok)
    {
        DEBUG_WRITE("%s", "Script has thrown an exception");
        const napi_extended_error_info *info;
        napi_get_last_error_info(env, &info);
        DEBUG_WRITE("%s", info->error_message);
    }

    return nullptr;
}

napi_env Runtime::GetNapiEnv()
{
    return env;
}

napi_runtime Runtime::GetNapiRuntime()
{
    return rt;
}

int Runtime::GetId()
{
    return this->m_id;
}

int Runtime::GetWriter()
{
    return m_mainLooper_fd[1];
}

int Runtime::GetReader()
{
    return m_mainLooper_fd[0];
}

jobject Runtime::CallJSMethodNative(JNIEnv *_jEnv, jobject obj, jint javaObjectID, jstring methodName, jint retType, jboolean isConstructor, jobjectArray packagedArgs)
{
    JEnv jEnv(_jEnv);

    DEBUG_WRITE("CallJSMethodNative called javaObjectID=%d", javaObjectID);

    auto jsObject = m_objectManager->GetJsObjectByJavaObject(javaObjectID);

    if (jsObject == nullptr || napi_util::is_undefined(env, jsObject))
    {
        stringstream ss;
        ss << "JavaScript object for Java ID " << javaObjectID << " not found." << endl;
        ss << "Attempting to call method " << ArgConverter::jstringToString(methodName) << endl;

        throw NativeScriptException(ss.str());
    }

    if (isConstructor)
    {
        DEBUG_WRITE("CallJSMethodNative: Updating linked instance with its real class");
        jclass instanceClass = jEnv.GetObjectClass(obj);
        m_objectManager->SetJavaClass(jsObject, instanceClass);
    }

    DEBUG_WRITE("CallJSMethodNative called jsObject=%d", jsObject);

    string method_name = ArgConverter::jstringToString(methodName);
    auto jsResult = CallbackHandlers::CallJSMethod(env, jEnv, jsObject, method_name, packagedArgs);

    int classReturnType = retType;
    jobject javaObject = ConvertJsValueToJavaObject(jEnv, jsResult, classReturnType);
    return javaObject;
}

void Runtime::CreateJSInstanceNative(JNIEnv *_jEnv, jobject obj, jobject javaObject, jint javaObjectID, jstring className)
{

    DEBUG_WRITE("createJSInstanceNative called");

    JEnv jEnv(_jEnv);

    string existingClassName = ArgConverter::jstringToString(className);

    string jniName = Util::ConvertFromCanonicalToJniName(existingClassName);

    napi_value jsInstance;
    napi_value implementationObject;

    auto proxyClassName = m_objectManager->GetClassName(javaObject);

    DEBUG_WRITE("createJSInstanceNative class %s", proxyClassName.c_str());

    jsInstance = MetadataNode::CreateExtendedJSWrapper(env, m_objectManager, proxyClassName);

    if (jsInstance == nullptr || napi_util::is_undefined(env, jsInstance))
    {

        throw NativeScriptException(string("Failed to create JavaScript extend wrapper for class '" + proxyClassName + "'"));
    }

    implementationObject = MetadataNode::GetImplementationObject(env, jsInstance);

    if (implementationObject == nullptr || napi_util::is_undefined(env, implementationObject))
    {
        string msg("createJSInstanceNative: implementationObject is empty");
        throw NativeScriptException(msg);
    }

    DEBUG_WRITE("createJSInstanceNative: implementationObject :%d", implementationObject);

    jclass clazz = jEnv.FindClass(jniName);
    m_objectManager->Link(jsInstance, javaObjectID, clazz);
}

jint Runtime::GenerateNewObjectId(JNIEnv *env, jobject obj)
{
    int objectId = m_objectManager->GenerateNewObjectID();

    return objectId;
}

jobject Runtime::ConvertJsValueToJavaObject(JEnv &jEnv, napi_value value, int classReturnType)
{

    JsArgToArrayConverter argConverter(env, value, false /*is implementation object*/, classReturnType);
    jobject jr = argConverter.GetConvertedArg();
    jobject javaResult = nullptr;
    if (jr != nullptr)
    {
        javaResult = jEnv.NewLocalRef(jr);
    }

    return javaResult;
}

JavaVM *Runtime::java_vm = nullptr;
jmethodID Runtime::GET_USED_MEMORY_METHOD_ID = nullptr;
robin_hood::unordered_map<int, Runtime *> Runtime::id_to_runtime_cache;
robin_hood::unordered_map<napi_env, Runtime *> Runtime::env_to_runtime_cache;
bool Runtime::s_mainThreadInitialized = false;
int Runtime::m_androidVersion = Runtime::GetAndroidVersion();
ALooper *Runtime::m_mainLooper = nullptr;

int Runtime::m_mainLooper_fd[2];

Runtime* Runtime::s_current_rt = nullptr;
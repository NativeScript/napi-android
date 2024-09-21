#include <unistd.h>
#include <thread>
#include "Runtime.h"
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
#include "Constants.h"
#include "NativeScriptAssert.h"
#include "CallbackHandlers.h"
#include "MetadataNode.h"
#include "Console.h"
#include "Performance.h"

using namespace ns;
using namespace std;

bool ns::LogEnabled = true;

void Runtime::Init(JavaVM *vm) {
    __android_log_print(ANDROID_LOG_INFO, "TNS.Runtime",
                        "NativeScript Runtime Version %s, commit %s", NATIVE_SCRIPT_RUNTIME_VERSION,
                        NATIVE_SCRIPT_RUNTIME_COMMIT_SHA);

    if (Runtime::java_vm == nullptr) {
        java_vm = vm;

        JEnv::Init(java_vm);
    }

    // handle SIGABRT/SIGSEGV only on API level > 20 as the handling is not so efficient in older versions
    if (m_androidVersion > 20) {
        struct sigaction action;
        action.sa_handler = SIGHandler;
        sigaction(SIGABRT, &action, NULL);
        sigaction(SIGSEGV, &action, NULL);
    }
}


Runtime::Runtime(JNIEnv *jEnv, jobject runtime, int id)
        : m_id(id), m_lastUsedMemory(0) {
    m_runtime = jEnv->NewGlobalRef(runtime);
//    m_objectManager = new ObjectManager(m_runtime);
    m_loopTimer = new MessageLoopTimer();
    id_to_runtime_cache.emplace(id, this);


//    if (GET_USED_MEMORY_METHOD_ID == nullptr) {
//        auto RUNTIME_CLASS = env->FindClass("com/tns/Runtime");
//        assert(RUNTIME_CLASS != nullptr);
//
//        GET_USED_MEMORY_METHOD_ID = env->GetMethodID(RUNTIME_CLASS, "getUsedMemory", "()J");
//        assert(GET_USED_MEMORY_METHOD_ID != nullptr);
//    }
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

void Runtime::Init(JNIEnv *_env, jobject obj, int runtimeId, jstring filesPath) {
    JEnv env(_env);

    auto runtime = new Runtime(env, obj, runtimeId);

    runtime->Init(env, filesPath);
}

void Runtime::Init(JNIEnv *jEnv, jstring filesPath) {

    auto filesRoot = ArgConverter::jstringToString(filesPath);
    Constants::APP_ROOT_FOLDER_PATH = filesRoot + "/app/";

    DEBUG_WRITE("Initializing NativeScript NAPI Runtime");


//  TODO  NativeScriptException::Init();

    NAPICreateJSRuntime(&rt);
    NAPICreateEnv(&env, rt);
    napi_open_handle_scope(env, &global_scope);

    CallbackHandlers::Init();
    if (!s_mainThreadInitialized) {
        MetadataNode::BuildMetadata(filesRoot);
    }

    MetadataNode::CreateTopLevelNamespaces(env);
    m_loopTimer->Init(env);

    napi_value global;
    napi_get_global(env, &global);

    Console::createConsole(env, global);
    Performance::createPerformance(env, global);

    s_mainThreadInitialized = true;
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

Runtime::~Runtime() {
//    delete this->m_objectManager;
    delete this->m_loopTimer;
//    CallbackHandlers::RemoveIsolateEntries(m_isolate);
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
    napi_close_handle_scope(env, this->global_scope);
    NAPIFreeEnv(env);
}

jobject Runtime::RunScript(JNIEnv *_env, jobject obj, jstring scriptFile) {
    int status;
    auto filename = ArgConverter::jstringToString(scriptFile);
    auto src = ReadFileText(filename + "/app/index.js");

    napi_value soureCode;
    napi_create_string_utf8(env, src.c_str(), src.length(), &soureCode);

    napi_value result;
    status = napi_run_script(env, soureCode, &result);

    if (status != napi_ok) {
        DEBUG_WRITE("%s", "Script has thrown an exception");
        const napi_extended_error_info *info;
        napi_get_last_error_info(env, &info);
        DEBUG_WRITE("%s", info->error_message);
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

JavaVM *Runtime::java_vm = nullptr;
jmethodID Runtime::GET_USED_MEMORY_METHOD_ID = nullptr;
robin_hood::unordered_map<int, Runtime *> Runtime::id_to_runtime_cache;
robin_hood::unordered_map<napi_env, Runtime *> Runtime::env_to_runtime_cache;
bool Runtime::s_mainThreadInitialized = false;
int Runtime::m_androidVersion = Runtime::GetAndroidVersion();
ALooper *Runtime::m_mainLooper = nullptr;

int Runtime::m_mainLooper_fd[2];
#ifndef RUNTIME_H
#define RUNTIME_H

#include "jni.h"
#include <string>
#include "JniLocalRef.h"
#include "MessageLoopTimer.h"
#include <android/looper.h>
#include "js_native_api.h"
#include "robin_hood.h"
#include "ModuleInternal.h"
#include <fcntl.h>
#include "native_api_util.h"
#include "ObjectManager.h"
#include "ArrayBufferHelper.h"
#include <thread>

namespace ns {
    class Runtime {
    public:

        ~Runtime();

        static Runtime* GetRuntime(int runtimeId);

        static Runtime* GetRuntime(napi_env env);

        static void Init(JavaVM *vm);

        static void Init(JNIEnv* _env, jobject obj, int runtimeId, jstring filesPath, jstring nativeLibsDir, jboolean verboseLoggingEnabled, jboolean isDebuggable, jstring packageName, jobjectArray args, jstring callingDir, int maxLogcatObjectSize, bool forceLog);

        void Init(JNIEnv* env, jstring filesPath, jstring nativeLibsDir, bool verboseLoggingEnabled, bool isDebuggable, jstring packageName, jobjectArray args, jstring callingDir, int maxLogcatObjectSize, bool forceLog);

        jobject GetJavaRuntime() const;

        void DestroyRuntime();

        void RunModule(JNIEnv* _env, jobject obj, jstring scriptFile);
        void RunModule(const char *moduleName);
        void RunWorker(jstring scriptFile);
        jobject RunScript(JNIEnv *_env, jobject obj, jstring scriptFile);

        std::string ReadFileText(const std::string &filePath);

        bool NotifyGC(JNIEnv *jEnv, jobject obj, jintArray object_ids);

        bool TryCallGC();

        static int GetWriter();

        static int GetReader();

        static void SetManualInstrumentationMode(jstring mode);

        int GetId();

        static ObjectManager* GetObjectManager(napi_env env);

        ObjectManager* GetObjectManager() const;

        napi_env GetNapiEnv();

        napi_runtime GetNapiRuntime();

        static ALooper *GetMainLooper() {
            return m_mainLooper;
        }

        static Runtime* Current();

        jobject ConvertJsValueToJavaObject(JEnv& env, napi_value value, int classReturnType);
        jint GenerateNewObjectId(JNIEnv* env, jobject obj);
        void CreateJSInstanceNative(JNIEnv* _env, jobject obj, jobject javaObject, jint javaObjectID, jstring className);
        jobject CallJSMethodNative(JNIEnv* _env, jobject obj, jint javaObjectID, jstring methodName, jint retType, jboolean isConstructor, jobjectArray packagedArgs);
        void PassExceptionToJsNative(JNIEnv* env, jobject obj, jthrowable exception, jstring message, jstring fullStackTrace, jstring jsStackTrace, jboolean isDiscarded);
        void PassUncaughtExceptionFromWorkerToMainHandler(napi_value message, napi_value stackTrace, napi_value filename, int lineno);

    private:
        Runtime(JNIEnv* env, jobject runtime, int id);

        int m_id;
        jobject m_runtime;

        napi_runtime rt;
        napi_env env;
        napi_handle_scope global_scope;

        MessageLoopTimer* m_loopTimer;
        int64_t m_lastUsedMemory;
        napi_value m_gcFunc;
        volatile bool m_runGC;


        ObjectManager* m_objectManager;

        ArrayBufferHelper m_arrayBufferHelper;
        
        bool m_isMainThread;

        ModuleInternal m_module;

        static int GetAndroidVersion();

        static int m_androidVersion;

        static JavaVM *java_vm;

        static jmethodID GET_USED_MEMORY_METHOD_ID;

        static bool s_mainThreadInitialized;

        static ALooper *m_mainLooper;

        static int m_mainLooper_fd[2];

        static robin_hood::unordered_map<int, Runtime*> id_to_runtime_cache;

        static robin_hood::unordered_map<napi_env, Runtime*> env_to_runtime_cache;

        static robin_hood::unordered_map<std::thread::id, Runtime*> thread_id_to_rt_cache;

        static Runtime* s_main_rt;
        static std::thread::id s_main_thread_id;


    };

} // ns

#endif //RUNTIME_H
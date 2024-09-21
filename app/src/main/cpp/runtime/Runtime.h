#pragma once

#include "jni.h"
#include <string>
#include "JniLocalRef.h"
#include "MessageLoopTimer.h"
#include <android/looper.h>
#include "js_native_api.h"
#include "robin_hood.h"
#include <fcntl.h>

namespace ns {
    class Runtime {
    public:

        ~Runtime();

        static Runtime* GetRuntime(int runtimeId);

        static Runtime* GetRuntime(napi_env env);

        static void Init(JavaVM *vm);

        static void Init(JNIEnv *_env, jobject obj, int runtimeId, jstring filesPath);

        void Init(JNIEnv *env, jstring filesPath);

//        jint GenerateNewObjectId(JNIEnv *env, jobject obj);

        jobject GetJavaRuntime() const;

        void DestroyRuntime();

        jobject RunScript(JNIEnv *_env, jobject obj, jstring scriptFile);

        std::string ReadFileText(const std::string &filePath);

//        bool NotifyGC(JNIEnv *env, jobject obj);
//
//        bool TryCallGC();

        static int GetWriter();

        static int GetReader();

        int GetId();

        napi_env GetNapiEnv();

        napi_runtime GetNapiRuntime();

        static ALooper *GetMainLooper() {
            return m_mainLooper;
        }

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

// TODO      ObjectManager* m_objectManager;
// TODO      ArrayBufferHelper m_arrayBufferHelper;

        bool m_isMainThread;

        static int GetAndroidVersion();

        static int m_androidVersion;

        static JavaVM *java_vm;

        static jmethodID GET_USED_MEMORY_METHOD_ID;

        static bool s_mainThreadInitialized;

        static ALooper *m_mainLooper;

        static int m_mainLooper_fd[2];

        static robin_hood::unordered_map<int, Runtime*> id_to_runtime_cache;

        static robin_hood::unordered_map<napi_env, Runtime*> env_to_runtime_cache;


    };

} // ns

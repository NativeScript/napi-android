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
#include "jsr.h"
#include "NativeScriptException.h"
#include <sstream>
#include "ConcurrentMap.h"

namespace tns {

    class JSMethodCache;

    class Runtime {
    public:

        ~Runtime();

        static Runtime *GetRuntime(int runtimeId);

        inline static Runtime *GetRuntime(napi_env env) {
            auto runtime = env_to_runtime_cache.Get(env);
            if (runtime) return runtime;

            std::stringstream ss;
            ss << "Cannot find runtime for napi_env: " << env;
            throw NativeScriptException(ss.str());
        }

        inline static Runtime *GetRuntimeUnchecked(napi_env env) {
            return env_to_runtime_cache.Get(env);
        }

        static void Init(JavaVM *vm);

        static void
        Init(JNIEnv *_env, jobject obj, int runtimeId, jstring filesPath, jstring nativeLibsDir,
             jboolean verboseLoggingEnabled, jboolean isDebuggable, jstring packageName,
             jobjectArray args, jstring callingDir, int maxLogcatObjectSize, bool forceLog);

        void Init(JNIEnv *env, jstring filesPath, jstring nativeLibsDir, bool verboseLoggingEnabled,
                  bool isDebuggable, jstring packageName, jobjectArray args, jstring callingDir,
                  int maxLogcatObjectSize, bool forceLog);

        jobject GetJavaRuntime() const;

        void DestroyRuntime();

        void RunModule(JNIEnv *_env, jobject obj, jstring scriptFile);

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

        static ObjectManager *GetObjectManager(napi_env env);

        ObjectManager *GetObjectManager() const;

        napi_env GetNapiEnv();

        napi_runtime GetNapiRuntime();

        static ALooper *GetMainLooper() {
            return m_mainLooper;
        }

        void Lock();

        void Unlock();

        static Runtime *Current();

        jobject ConvertJsValueToJavaObject(JEnv &env, napi_value value, int classReturnType);

        jint GenerateNewObjectId(JNIEnv *env, jobject obj);

        void
        CreateJSInstanceNative(JNIEnv *_env, jobject obj, jobject javaObject, jint javaObjectID,
                               jstring className);

        jobject CallJSMethodNative(JNIEnv *_env, jobject obj, jint javaObjectID, jclass claz,
                                   jstring methodName, jint retType, jboolean isConstructor,
                                   jobjectArray packagedArgs);

        void
        PassExceptionToJsNative(JNIEnv *env, jobject obj, jthrowable exception, jstring message,
                                jstring fullStackTrace, jstring jsStackTrace, jboolean isDiscarded);

        void PassUncaughtExceptionFromWorkerToMainHandler(napi_value message, napi_value stackTrace,
                                                          napi_value filename, int lineno);

        void AdjustAmountOfExternalAllocatedMemory();

        JSMethodCache *js_method_cache;

        bool is_destroying = false;

    private:
        Runtime(JNIEnv *env, jobject runtime, int id);

        static napi_value GlobalAccessorCallback(napi_env env, napi_callback_info info);

        int m_id;
        jobject m_runtime;

        napi_runtime rt;
        napi_env env;
        napi_handle_scope global_scope;

        MessageLoopTimer *m_loopTimer;
        int64_t m_lastUsedMemory;
        napi_ref m_gcFunc;
        volatile bool m_runGC;


        ObjectManager *m_objectManager;

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

        static tns::ConcurrentMap<int, Runtime *> id_to_runtime_cache;

        static tns::ConcurrentMap<napi_env, Runtime *> env_to_runtime_cache;

        static tns::ConcurrentMap<std::thread::id, Runtime *> thread_id_to_rt_cache;

        static Runtime *s_main_rt;
        static std::thread::id s_main_thread_id;


        std::thread::id my_thread_id;

#ifdef APPLICATION_IN_DEBUG
        std::mutex m_fileWriteMutex;
#endif


    };

    class JSMethodCache {
    public:

        explicit JSMethodCache(Runtime *_rt) : rt(_rt) {}

        ~JSMethodCache() {
            cleanupCache();
        }

        void cacheMethod(int javaObjectId, const std::string &methodName, napi_value jsMethod) {
            methodCache[javaObjectId][methodName] = napi_util::make_ref(rt->GetNapiEnv(), jsMethod,
                                                                        0);
        }

        napi_value getCachedMethod(int javaObjectId, const std::string &methodName) {
            napi_env env = rt->GetNapiEnv();

            auto it = methodCache.find(javaObjectId);
            if (it == methodCache.end()) {
                return nullptr;
            }

            auto methodIt = it->second.find(methodName);
            if (methodIt != it->second.end()) {
                if (!methodIt->second) return nullptr;
                napi_value m = napi_util::get_ref_value(env, methodIt->second);
                if (napi_util::is_null_or_undefined(env, m)) {
                    napi_delete_reference(env, methodIt->second);
                    it->second.erase(methodIt->first);
                    return nullptr;
                }
                return m;
            }

            return nullptr;
        }

        void cleanupObject(int javaObjectId) {
            auto it = methodCache.find(javaObjectId);
            if (it != methodCache.end()) {
                for (auto &methodEntry: it->second) {
                    napi_delete_reference(rt->GetNapiEnv(), methodEntry.second);
                    it->second.erase(methodEntry.first);
                }
                methodCache.erase(it);
            }
        }

        void cleanupCache() {
            JEnv env;
            for (auto &classEntry: methodCache) {
                for (auto &methodEntry: classEntry.second) {
                    if (methodEntry.second != nullptr) {
                        napi_delete_reference(rt->GetNapiEnv(), methodEntry.second);
                    }
                    classEntry.second.erase(methodEntry.first);
                }
                methodCache.erase(classEntry.first);
            }
        }


    private:
        Runtime *rt;
        robin_hood::unordered_map<int, robin_hood::unordered_map<std::string, napi_ref>> methodCache;

    };

} // tns

#endif //RUNTIME_H
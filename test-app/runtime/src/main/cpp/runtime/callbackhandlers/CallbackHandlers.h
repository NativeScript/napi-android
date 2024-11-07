#ifndef CALLBACKHANDLERS_H_
#define CALLBACKHANDLERS_H_

#include <string>
#include <map>
#include <vector>
#include "JEnv.h"
#include "ArgsWrapper.h"
#include "MetadataEntry.h"
#include "FieldCallbackData.h"
#include "MetadataTreeNode.h"
#include "NumericCasts.h"
#include "FieldAccessor.h"
#include "ArrayElementAccessor.h"
#include "ObjectManager.h"
#include "robin_hood.h"
#include <errno.h>
#include "NativeScriptAssert.h"
#include "NativeScriptException.h"
#include "Runtime.h"

namespace tns
{
    class CallbackHandlers
    {
    public:
        /*
         * Stores persistent handles of all 'Worker' objects initialized on the main thread
         * Note: No isolates different than that of the main thread should access this map
         */
        static robin_hood::unordered_map<int, napi_ref> id2WorkerMap;

        static int nextWorkerId;

        static void Init(napi_env env);

        static napi_value
        CreateJSWrapper(napi_env env, jint javaObjectID, const std::string &typeName);

        static bool RegisterInstance(napi_env env, napi_value jsObject,
                                     const std::string &fullClassName,
                                     const ArgsWrapper &argWrapper,
                                     napi_value implementationObject,
                                     bool isInterface,
                                     napi_value* jsThisProxy,
                                     const std::string &baseClassName = std::string());

        static jclass ResolveClass(napi_env env, const std::string &baseClassName,
                                   const std::string &fullClassName,
                                   napi_value implementationObject,
                                   bool isInterface);

        static std::string ResolveClassName(napi_env env, jclass &clazz);

        static napi_value
        GetArrayElement(napi_env env, napi_value array, uint32_t index,
                        const std::string &arraySignature);

        static void
        SetArrayElement(napi_env env, napi_value array, uint32_t index,
                        const std::string &arraySignature, napi_value value);

        static int GetArrayLength(napi_env env, napi_value arr);

        static napi_value
        CallJavaMethod(napi_env env, napi_value caller, const std::string &className,
                       const std::string &methodName, MetadataEntry *entry, bool isFromInterface,
                       bool isStatic, bool isSuper, napi_callback_info info);

        static napi_value
        CallJSMethod(napi_env env, JNIEnv *jEnv, napi_value jsObject,
                     const std::string &methodName, jobjectArray args);

        static napi_value
        GetJavaField(napi_env env, napi_value caller,
                     FieldCallbackData *fieldData);

        static void SetJavaField(napi_env env, napi_value target,
                                 napi_value value, FieldCallbackData *fieldData);

        static napi_value RunOnMainThreadCallback(napi_env env, napi_callback_info info);

        static int RunOnMainThreadFdCallback(int fd, int events, void *data);

        static napi_value LogMethodCallback(napi_env env, napi_callback_info info);

        static napi_value TimeCallback(napi_env env, napi_callback_info info);

        static napi_value
        DumpReferenceTablesMethodCallback(napi_env env, napi_callback_info info);

        static napi_value DrainMicrotaskCallback(napi_env env, napi_callback_info info);

        static void DumpReferenceTablesMethod();

        static napi_value ExitMethodCallback(napi_env env, napi_callback_info info);

        static void CreateGlobalCastFunctions(napi_env env);

        static std::vector<std::string> GetTypeMetadata(const std::string &name, int index);

        /*
         * Gets all methods in the implementation object, and packs them in a jobjectArray
         * to pass them to Java Land, so that their corresponding Java callbacks are written when
         * the dexFactory generates the class
         */
        static jobjectArray
        GetMethodOverrides(napi_env env, JEnv &jEnv, napi_value implementationObject);

        /*
         * Gets all interfaces declared in the 'interfaces' array inside the implementation object,
         * and packs them in a jobjectArray to pass them to Java Land, so that they may be
         * implemented when the dexFactory generates the corresponding class
         */
        static jobjectArray
        GetImplementedInterfaces(napi_env env, JEnv &jEnv, napi_value implementationObject);

        static napi_value
        EnableVerboseLoggingMethodCallback(napi_env env, napi_callback_info info);

        static napi_value
        DisableVerboseLoggingMethodCallback(napi_env env, napi_callback_info info);

        static napi_value ReleaseNativeCounterpartCallback(napi_env env, napi_callback_info info);

        static napi_value FindClass(napi_env env, const char *name);

        static napi_value NewThreadCallback(napi_env env, napi_callback_info info);

        /*
         * main -> worker messaging
         * Fired when a Worker instance's postMessage is called
         */
        static napi_value WorkerObjectPostMessageCallback(napi_env env, napi_callback_info info);

        /*
         * main -> worker messaging
         * Fired when worker object has "postMessage" and the worker has implemented "onMessage" handler
         * In case "onMessage" handler isn't implemented no exception is thrown
         */
        static void WorkerGlobalOnMessageCallback(napi_env env, jstring message);

        /*
         * worker -> main thread messaging
         * Fired when a Worker script's "postMessage" is called
         */
        static napi_value WorkerGlobalPostMessageCallback(napi_env env, napi_callback_info info);

        /*
         * worker -> main messaging
         * Fired when worker has sent a message to main and the worker object has implemented "onMessage" handler
         * In case "onMessage" handler isn't implemented no exception is thrown
         */
        static void WorkerObjectOnMessageCallback(napi_env env, jint workerId, jstring message);

        /*
         * Fired when a Worker instance's terminate is called (immediately stops execution of the thread)
         */
        static napi_value WorkerObjectTerminateCallback(napi_env env, napi_callback_info info);

        /*
         * Fired when a Worker script's close is called
         */
        static napi_value WorkerGlobalCloseCallback(napi_env env, napi_callback_info info);

        /*
         * Clears the persistent Worker object handle associated with a workerId
         * Occurs when calling a worker object's `terminate` or a worker thread's global scope `close`
         */
        static void ClearWorkerPersistent(napi_env env, int workerId);

        /*
         * Terminates the currently executing Isolate. No scripts can be executed after this call
         */
        static void TerminateWorkerThread(napi_env env);

        /*
         * Is called when an unhandled exception is thrown inside the worker
         * Will execute 'onerror' if one is provided inside the Worker Scope
         * Will make the exception "bubble up" through to main, to be handled by the Worker Object
         * if 'onerror' isn't implemented or returns false
         */
        static void CallWorkerScopeOnErrorHandle(napi_env env, napi_value tc);

        /*
         * Is called when an unhandled exception bubbles up from the worker scope to the main thread Worker Object
         * Will execute `onerror` if one is implemented for the Worker Object instance
         * Will throw a NativeScript Exception if 'onerror' isn't implemented or returns false
         */
        static void
        CallWorkerObjectOnErrorHandle(napi_env env, jint workerId, jstring message,
                                      jstring stackTrace, jstring filename, jint lineno,
                                      jstring threadName);

        static napi_value PostFrameCallback(napi_env env, napi_callback_info info);

        static napi_value RemoveFrameCallback(napi_env env, napi_callback_info info);

        static void RemoveEnvEntries(napi_env env);

        struct AChoreographer;

        typedef void (*AChoreographer_frameCallback)(long frameTimeNanos, void *data);

        typedef void (*AChoreographer_frameCallback64)(int64_t frameTimeNanos, void *data);

        typedef AChoreographer *(*func_AChoreographer_getInstance)();

        typedef void (*func_AChoreographer_postFrameCallback)(
            AChoreographer *choreographer, AChoreographer_frameCallback callback,
            void *data);

        typedef void (*func_AChoreographer_postFrameCallback64)(
            AChoreographer *choreographer, AChoreographer_frameCallback64 callback,
            void *data);

        typedef void (*func_AChoreographer_postFrameCallbackDelayed)(
            AChoreographer *choreographer, AChoreographer_frameCallback callback,
            void *data, long delayMillis);

        typedef void (*func_AChoreographer_postFrameCallbackDelayed64)(
            AChoreographer *choreographer, AChoreographer_frameCallback64 callback,
            void *data, uint32_t delayMillis);

    private:
        CallbackHandlers()
        {
        }

        static void AdjustAmountOfExternalAllocatedMemory(JEnv &jEnv, napi_env napiEnv);

        /*
         * Helper method that creates a java string array for sending strings over JNI
         */
        static jobjectArray GetJavaStringArray(JEnv &jEnv, int length);

        static void validateProvidedArgumentsLength(napi_env env, napi_callback_info info, int expectedSize);

        static short MAX_JAVA_STRING_ARRAY_LENGTH;

        static jclass RUNTIME_CLASS;

        static jclass JAVA_LANG_STRING;

        static jmethodID RESOLVE_CLASS_METHOD_ID;

        static jfieldID CURRENT_OBJECTID_FIELD_ID;

        static jmethodID MAKE_INSTANCE_STRONG_ID;

        static jmethodID GET_TYPE_METADATA;

        static jmethodID ENABLE_VERBOSE_LOGGING_METHOD_ID;

        static jmethodID DISABLE_VERBOSE_LOGGING_METHOD_ID;

        static jmethodID INIT_WORKER_METHOD_ID;

        static NumericCasts castFunctions;

        static ArrayElementAccessor arrayElementAccessor;

        static FieldAccessor fieldAccessor;

        struct JavaObjectIdScope
        {
            JavaObjectIdScope(JEnv &_jEnv, jfieldID fieldId, jobject runtime, int javaObjectId)
                : jEnv(_jEnv), _fieldID(fieldId), _runtime(runtime)
            {
                jEnv.SetIntField(_runtime, _fieldID, javaObjectId);
            }

            ~JavaObjectIdScope()
            {
                jEnv.SetIntField(_runtime, _fieldID, -1);
            }

        private:
            JEnv jEnv;
            jfieldID _fieldID;
            jobject _runtime;
        };

        static std::atomic_int64_t count_;

        struct Callback
        {
            Callback() {}
            Callback(uint64_t id)
                : id_(id)
            {
            }
            uint64_t id_;
        };

        struct CacheEntry
        {
            CacheEntry(napi_env env, napi_value callback)
                : env_(env)
            {
                napi_create_reference(env, callback, 1, &callback_);
            }

            ~CacheEntry()
            {
                napi_delete_reference(env_, callback_);
            }

            napi_env env_;
            napi_ref callback_;
        };

        static robin_hood::unordered_map<uint64_t, CacheEntry> cache_;

        static std::atomic_uint64_t frameCallbackCount_;

        struct FrameCallbackCacheEntry
        {
            FrameCallbackCacheEntry(napi_env _env, napi_value callback_, uint64_t aId)
                : env(_env),
                  id(aId)
            {
                napi_create_reference(env, callback_, 1, &callback);
            }

            ~FrameCallbackCacheEntry()
            {
                napi_delete_reference(env, callback);
            }

            napi_env env;
            napi_ref callback;
            uint64_t id;

            bool isScheduled()
            {
                return scheduled;
            }

            void markScheduled()
            {
                scheduled = true;
                removed = false;
            }
            void markRemoved()
            {
                // we can never unschedule a callback, so we just mark it as removed
                removed = true;
                uint32_t  result;
            }

            AChoreographer_frameCallback frameCallback_ = [](long ts, void *data)
            {
                execute((double)ts, data);
            };

            AChoreographer_frameCallback64 frameCallback64_ = [](int64_t ts, void *data)
            {
                execute((double)ts, data);
            };

            static void execute(double ts, void *data)
            {
                if (data != nullptr)
                {
                    auto entry = static_cast<FrameCallbackCacheEntry *>(data);
                    if (entry->shouldRemoveBeforeCall())
                    {
                        frameCallbackCache_.erase(entry->id); // invalidates *entry
                        return;
                    }
                    napi_env env = entry->env;

                    napi_value cb = napi_util::get_ref_value(env, entry->callback);

                    napi_value global;
                    napi_get_global(env, &global);

                    entry->markUnscheduled();

                    napi_value args[1];
                    napi_create_double(env, ts, &args[0]);

                    napi_valuetype type;
                    napi_typeof(env, cb, &type);

                    napi_value result;
                    napi_call_function(env, global, cb, 1, args, &result);


                    // check if we should remove it (it should be both unscheduled and removed)
                    if (entry->shouldRemoveAfterCall())
                    {
                        frameCallbackCache_.erase(entry->id); // invalidates *entry
                    }
                }
            }

        private:
            bool removed = false;
            bool scheduled = false;
            void markUnscheduled()
            {
                scheduled = false;
                removed = true;
            }

            bool shouldRemoveBeforeCall()
            {
                return removed;
            }

            bool shouldRemoveAfterCall()
            {
                return !scheduled && removed;
            }
        };

        static robin_hood::unordered_map<uint64_t, FrameCallbackCacheEntry> frameCallbackCache_;

        static void InitChoreographer();

        static void PostCallback(napi_env env, napi_callback_info info,
                                 FrameCallbackCacheEntry *entry);
    };
}

#endif /* CALLBACKHANDLERS_H_ */
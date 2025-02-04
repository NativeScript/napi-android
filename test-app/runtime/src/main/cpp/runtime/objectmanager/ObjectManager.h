#ifndef OBJECTMANAGER_H_
#define OBJECTMANAGER_H_

#include "js_native_api.h"
#include "JEnv.h"
#include "JniLocalRef.h"
#include "JniLocalRef.h"
#include "DirectBuffer.h"
#include "LRUCache.h"
#include <map>
#include <set>
#include <stack>
#include <vector>
#include <string>
#include "Constants.h"


namespace tns {
    class ObjectManager {
    public:
        ObjectManager(jobject javaRuntimeObject);

        void Init(napi_env env);

        JniLocalRef GetJavaObjectByJsObject(napi_value object);

        void UpdateCache(int objectID, jobject obj);

        jclass GetJavaClass(napi_value value);

        void SetJavaClass(napi_value instance, jclass clazz);

        int GetOrCreateObjectId(jobject object);

        napi_value GetJsObjectByJavaObject(int javaObjectID);

        napi_value
        CreateJSWrapper(jint javaObjectID, const std::string &typeName, bool isArray = false);

        napi_value CreateJSWrapper(jint javaObjectID, const std::string &typeName, jobject instance,
                                   bool isArray = false);

        napi_value GetOrCreateProxy(jint javaObjectID, napi_value instance);

        void Link(napi_value object, uint32_t javaObjectID, jclass clazz);


        bool CloneLink(napi_value src, napi_value dest);

        bool IsRuntimeJsObject(napi_value object);

        std::string GetClassName(jobject javaObject);

        std::string GetClassName(jclass clazz);

        int GenerateNewObjectID();

        napi_value GetEmptyObject();

        inline static void MarkObject(napi_env env, napi_value object) {
            napi_value marker;
            napi_get_boolean(env, true, &marker);
            napi_set_named_property(env, object, PRIVATE_IS_NAPI, marker);
        }

        inline static void MarkSuperCall(napi_env env, napi_value object) {
            napi_value marker;
            napi_get_boolean(env, true, &marker);
            napi_set_named_property(env, object, PRIVATE_CALLSUPER, marker);
        }

        void OnGarbageCollected(JNIEnv *jEnv, jintArray object_ids);

        void ReleaseNativeObject(napi_env env, napi_value object);

        inline static void ReleaseObjectNow(napi_env env, int javaObjectId);

    private:
        static napi_value JSObjectConstructorCallback(napi_env env, napi_callback_info info);

        struct JSInstanceInfo {
        public:
            JSInstanceInfo(uint32_t javaObjectID, jclass claz)
                    : JavaObjectID(javaObjectID), ObjectClazz(claz) {
            }

            uint32_t JavaObjectID;
            jclass ObjectClazz;
        };

        struct JSObjectFinalizerHint {
            JSObjectFinalizerHint(ObjectManager *_thisPtr, JSInstanceInfo *_jsInfo,
                                  napi_ref _target)
                    :
                    thisPtr(_thisPtr), jsInfo(_jsInfo), target(_target) {
            }

            ObjectManager *thisPtr;
            JSInstanceInfo *jsInfo;
            napi_ref target;
        };

        struct JSObjectProxyData {
            JSObjectProxyData(ObjectManager *_thisPtr, uint32_t _javaObjectId)
                    :
                    thisPtr(_thisPtr), javaObjectId(_javaObjectId) {
            }

            ObjectManager *thisPtr;
            uint32_t javaObjectId;
        };


        JSInstanceInfo *GetJSInstanceInfo(napi_value object);

        JSInstanceInfo *GetJSInstanceInfoFromRuntimeObject(napi_value object);

        napi_value
        CreateJSWrapperHelper(jint javaObjectID, const std::string &typeName, jclass clazz,
                              bool isArray = false);

        static void JSObjectFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint);

        static void JSObjectProxyFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint);

        jweak GetJavaObjectByID(uint32_t javaObjectID);

        jobject GetJavaObjectByIDImpl(uint32_t javaObjectID);

        static jweak NewWeakGlobalRefCallback(const int &javaObjectID, void *state);

        static void DeleteWeakGlobalRefCallback(const jweak &object, void *state);

        jobject m_javaRuntimeObject;

        napi_env m_env;

        robin_hood::unordered_map<int, napi_ref> m_idToProxy;
        robin_hood::unordered_map<int, napi_ref> m_idToObject;
        robin_hood::unordered_set<int> m_weakObjectIds;
        robin_hood::unordered_set<int> m_markedAsWeakIds;

        LRUCache<int, jweak> m_cache;

        volatile int m_currentObjectId;

        DirectBuffer m_buff;

        DirectBuffer m_outBuff;

        jclass JAVA_LANG_CLASS;

        jmethodID GET_NAME_METHOD_ID;

        jmethodID GET_JAVAOBJECT_BY_ID_METHOD_ID;

        jmethodID GET_OR_CREATE_JAVA_OBJECT_ID_METHOD_ID;

        jmethodID MAKE_INSTANCE_WEAK_BATCH_METHOD_ID;

        jmethodID MAKE_INSTANCE_WEAK_METHOD_ID;

        jmethodID MAKE_INSTANCE_STRONG_METHOD_ID;

        napi_ref m_jsObjectCtor;
        napi_ref m_jsObjectProxyCreator;

    };
}

#endif /* OBJECTMANAGER_H_ */
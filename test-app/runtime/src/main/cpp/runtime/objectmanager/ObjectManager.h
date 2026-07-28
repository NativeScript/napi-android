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

class MetadataNode;

namespace tns {
    class ObjectManager {
    public:
        ObjectManager(jobject javaRuntimeObject);

        void OnDisposeEnv();

        void Init(napi_env env);

        JniLocalRef GetJavaObjectByJsObject(napi_value object, int *objectId = nullptr,
                                            bool *isSuper = nullptr);


        JniLocalRef GetJavaObjectByJsObjectFast(napi_value object);

        void UpdateCache(int objectID, jobject obj);

        jclass GetJavaClass(napi_value value);

        void SetJavaClass(napi_value instance, jclass clazz);

        int GetOrCreateObjectId(jobject object);

        napi_value GetJsObjectByJavaObject(int javaObjectID);

        napi_value
        CreateJSWrapper(jint javaObjectID, const std::string &typeName);

        napi_value
        CreateJSWrapper(jint javaObjectID, const std::string &typeName, jobject instance);

        napi_value GetOrCreateProxy(jint javaObjectID, napi_value instance);

        napi_value GetOrCreateProxyWeak(jint javaObjectID, napi_value instance);

        void Link(napi_value object, uint32_t javaObjectID, jclass clazz,
                  MetadataNode *node = nullptr);

        // Returns the class metadata stored on the per-instance JSInstanceInfo
        // (host proxy's, or the raw instance's wrap). Used by
        // MetadataNode::GetInstanceMetadata under USE_HOST_OBJECT.
        MetadataNode *GetInstanceNode(napi_value object);

        bool CloneLink(napi_value src, napi_value dest);

        bool IsRuntimeJsObject(napi_value object);

        static std::string GetClassName(jobject javaObject);

        static std::string GetClassName(jclass clazz);

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

        bool IsHostObject(napi_value object);

    private:
        static napi_value JSObjectConstructorCallback(napi_env env, napi_callback_info info);

        struct JSInstanceInfo {
        public:
            JSInstanceInfo(uint32_t javaObjectID, jclass claz)
                    : JavaObjectID(javaObjectID), ObjectClazz(claz) {
            }

            uint32_t JavaObjectID;
            jclass ObjectClazz;
            // Cached super-call flag (-1 = unresolved, 0 = false, 1 = true).
            int8_t isSuper = -1;
            // Per-instance class metadata. Under USE_HOST_OBJECT this replaces the
            // "#instance_metadata" property; reachable via GetInstanceNode.
            MetadataNode *node = nullptr;
        };

#ifdef USE_HOST_OBJECT
        // Backing context for a host-object proxy. The new napi_create_host_object
        // takes no target/getter/setter, so everything needed to proxy to the
        // underlying instance is carried here in the `data` pointer.
        struct HostObjectProxy {
            ObjectManager *objectManager;
            JSInstanceInfo *instanceInfo;  // java object id holder
            bool isPrimary;                // owns instanceInfo + marks weak on GC
            napi_env env;
            napi_ref target;               // strong ref to the wrapped instance
            bool isArray;
            std::string arraySignature;    // jni array signature (arrays only)
            int8_t isSuper = -1;           // cached super-call flag (-1=unresolved)
            int64_t arrayLength = -1;      // cached fixed length (arrays only; -1=unresolved)
        };

        napi_value CreateHostObjectProxy(napi_value instance,
                                         JSInstanceInfo *instanceInfo,
                                         bool isPrimary);

        // napi_host_object_methods callbacks (transparent proxy to `target`).
        static napi_value HostObjectGet(napi_env env, napi_value host,
                                        napi_value property, void *data);
        static void HostObjectSet(napi_env env, napi_value host,
                                  napi_value property, napi_value value,
                                  void *data);
        static int HostObjectHas(napi_env env, napi_value host,
                                  napi_value property, void *data);
        static int HostObjectDelete(napi_env env, napi_value host,
                                     napi_value property, void *data);
        static napi_value HostObjectOwnKeys(napi_env env, napi_value host,
                                            void *data);
        // Fast numeric index access: native get/setValueAtIndex (arrays only).
        static napi_value HostObjectIndexedGet(napi_env env, napi_value host,
                                               uint32_t index, void *data);
        static void HostObjectIndexedSet(napi_env env, napi_value host,
                                         uint32_t index, napi_value value,
                                         void *data);
        static napi_value HostObjectSuperGetter(napi_env env,
                                                napi_callback_info info);
        static void HostObjectProxyFinalizer(napi_env env, void *data,
                                             void *hint);
        // Actual cleanup, deferred to the runtime's safe post-GC finalizer drain
        // (Runtime::PostFinalizer) so its reference-deleting Node-API calls are legal.
        static void HostObjectProxyPostFinalizer(napi_env env, void *data,
                                                 void *hint);
#endif


        JSInstanceInfo *GetJSInstanceInfo(napi_value object);

        JSInstanceInfo *GetJSInstanceInfoFromRuntimeObject(napi_value object);

        napi_value
        CreateJSWrapperHelper(jint javaObjectID, const std::string &typeName, jclass clazz);

        static void JSObjectFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint);

        static void
        JSObjectProxyFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint);

        jweak GetJavaObjectByID(uint32_t javaObjectID);

        jobject GetJavaObjectByIDImpl(uint32_t javaObjectID);

        static jweak NewWeakGlobalRefCallback(const int &javaObjectID, void *state);

        static void DeleteWeakGlobalRefCallback(const jweak &object, void *state);

        static bool ValidateWeakGlobalRefCallback(const int &javaObjectID, const jweak &object, void *state);

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

        static jclass JAVA_LANG_CLASS;

        static jmethodID GET_NAME_METHOD_ID;

        jmethodID GET_JAVAOBJECT_BY_ID_METHOD_ID;

        jmethodID GET_OR_CREATE_JAVA_OBJECT_ID_METHOD_ID;

        jmethodID MAKE_INSTANCE_WEAK_BATCH_METHOD_ID;

        jmethodID MAKE_INSTANCE_WEAK_METHOD_ID;

        jmethodID MAKE_INSTANCE_STRONG_METHOD_ID;

        napi_ref m_jsObjectCtor;

        napi_ref m_jsObjectProxyCreator;

        napi_ref jid;
    };
}

#endif /* OBJECTMANAGER_H_ */
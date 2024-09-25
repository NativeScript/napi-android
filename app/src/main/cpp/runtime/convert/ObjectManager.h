#ifndef OBJECTMANAGER_H_
#define OBJECTMANAGER_H_

#include "js_native_api.h"
#include "JEnv.h"
#include "JniLocalRef.h"
//#include "ArgsWrapper.h"
#include "JniLocalRef.h"
#include "DirectBuffer.h"
#include "LRUCache.h"
#include <map>
#include <set>
#include <stack>
#include <vector>
#include <string>


namespace ns {
class ObjectManager {
    public:
        ObjectManager(jobject javaRuntimeObject);

        void Init(napi_env env);

        JniLocalRef GetJavaObjectByJsObject(napi_env env, napi_value object);

        void UpdateCache(int objectID, jobject obj);

        jclass GetJavaClass(napi_value value);

        void SetJavaClass(napi_value instance, jclass clazz);

        int GetOrCreateObjectId(jobject object);

        napi_value GetJsObjectByJavaObject(int javaObjectID);

        napi_value CreateJSWrapper(jint javaObjectID, const std::string& typeName);

        napi_value CreateJSWrapper(jint javaObjectID, const std::string& typeName, jobject instance);

        void Link(napi_value object, uint32_t javaObjectID, jclass clazz);

        void ReleaseNativeCounterpart(napi_value object);

        bool CloneLink(napi_value src, napi_value dest);

        bool IsJsRuntimeObject(napi_env env, napi_value object);

        std::string GetClassName(jobject javaObject);

        std::string GetClassName(jclass clazz);

        int GenerateNewObjectID();

        void SetInstanceEnv(napi_env env);

        napi_value GetEmptyObject(napi_env env);

        enum class MetadataNodeKeys {
            JsInfo,
            CallSuper,
            END
        };

        enum JavaScriptMarkingMode {
            Full,
            None
        };

        JavaScriptMarkingMode GetMarkingMode();

    private:

        struct JSInstanceInfo {
            public:
                JSInstanceInfo(bool isJavaObjectWeak, uint32_t javaObjectID, jclass claz)
                    :IsJavaObjectWeak(isJavaObjectWeak), JavaObjectID(javaObjectID), ObjectClazz(claz) {
                }

                bool IsJavaObjectWeak;
                uint32_t JavaObjectID;
                jclass ObjectClazz;
        };

        struct ObjectWeakCallbackState {
            ObjectWeakCallbackState(ObjectManager* _thisPtr, JSInstanceInfo* _jsInfo, napi_ref _target)
                :
                thisPtr(_thisPtr), jsInfo(_jsInfo), target(_target) {
            }

            ObjectManager* thisPtr;
            JSInstanceInfo* jsInfo;
            napi_ref target;
        };

        struct GarbageCollectionInfo {
            GarbageCollectionInfo(int _numberOfGC)
                :
                numberOfGC(_numberOfGC) {
            }
            std::vector<napi_ref> markedForGC;
            int numberOfGC;
        };

        class PersistentObjectIdSet {
            public:
                PersistentObjectIdSet() {
                    /* TODO: use functors */
                }

                void clear() {
                    m_POs.clear();
                    m_IDs.clear();
                }

                void insert(napi_ref po, int javaObjectId) {
                    m_POs.insert(po);
                    m_IDs.insert(javaObjectId);
                }

                bool contains(napi_ref po) {
                    return m_POs.find(po) != m_POs.end();
                }

                std::set<napi_ref> m_POs;
                std::set<int> m_IDs;
        };

        struct PersistentObjectIdPair {
            PersistentObjectIdPair(napi_ref _po, int _javaObjectId)
                :
                po(_po), javaObjectId(_javaObjectId) {
            }
            napi_ref po;
            int javaObjectId;
        };

        JSInstanceInfo* GetJSInstanceInfo(napi_value object);

        JSInstanceInfo* GetJSInstanceInfoFromRuntimeObject(napi_value object);

        void ReleaseJSInstance(napi_ref po, JSInstanceInfo* jsInstanceInfo);

        void ReleaseRegularObjects();

        void MakeRegularObjectsWeak(const std::set<int>& instances, DirectBuffer& inputBuff);

        void MakeImplObjectsWeak(const std::unordered_map<int, napi_ref>& instances, DirectBuffer& inputBuff);

        void CheckWeakObjectsAreAlive(const std::vector<PersistentObjectIdPair>& instances, DirectBuffer& inputBuff, DirectBuffer& outputBuff);

        napi_value CreateJSWrapperHelper(jint javaObjectID, const std::string& typeName, jclass clazz);

        static void JSObjectWeakCallbackStatic(napi_env env, void *finalizeData, void *finalizeHint);

        static void JSObjectFinalizerStatic(napi_env env, void *finalizeData, void *finalizeHint);

        void JSObjectWeakCallback(napi_env env, ObjectWeakCallbackState* callbackState);

        void JSObjectFinalizer(napi_env env, ObjectWeakCallbackState* callbackState);

        bool HasImplObject(napi_env env, napi_value obj);

        jweak GetJavaObjectByID(uint32_t javaObjectID);

        jobject GetJavaObjectByIDImpl(uint32_t javaObjectID);

        static jweak NewWeakGlobalRefCallback(const int& javaObjectID, void* state);

        static void DeleteWeakGlobalRefCallback(const jweak& object, void* state);

        static napi_value JSWrapperConstructorCallback(napi_env env, napi_callback_info info);

        jobject m_javaRuntimeObject;

        int m_numberOfGC;

        napi_env m_env;

        std::stack<GarbageCollectionInfo> m_markedForGC;

        std::unordered_map<int, napi_ref> m_idToObject;

        PersistentObjectIdSet m_released;

        std::set<unsigned long> m_visited;

        LRUCache<int, jweak> m_cache;

        std::set<napi_value> m_visitedPOs;
        std::vector<PersistentObjectIdPair> m_implObjWeak;
        std::unordered_map<int, napi_value> m_implObjStrong;

        volatile int m_currentObjectId;

        DirectBuffer m_buff;

        DirectBuffer m_outBuff;

        bool m_useGlobalRefs;

        JavaScriptMarkingMode m_markingMode;

        jclass JAVA_LANG_CLASS;

        jmethodID GET_NAME_METHOD_ID;

        jmethodID GET_JAVAOBJECT_BY_ID_METHOD_ID;

        jmethodID GET_OR_CREATE_JAVA_OBJECT_ID_METHOD_ID;

        jmethodID MAKE_INSTANCE_WEAK_BATCH_METHOD_ID;

        jmethodID MAKE_INSTANCE_WEAK_AND_CHECK_IF_ALIVE_METHOD_ID;

        jmethodID RELEASE_NATIVE_INSTANCE_METHOD_ID;

        jmethodID CHECK_WEAK_OBJECTS_ARE_ALIVE_METHOD_ID;

        napi_ref m_poJsWrapperFunc;

        napi_value JSINFO_PROP;
        napi_value CALLSUPER_PROP;
        napi_value IS_NAPI;
};
}

#endif /* OBJECTMANAGER_H_ */
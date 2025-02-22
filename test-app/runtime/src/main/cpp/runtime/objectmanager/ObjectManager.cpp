#include "ObjectManager.h"
#include "NativeScriptAssert.h"
#include "MetadataNode.h"
#include "ArgConverter.h"
#include "Util.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include <algorithm>
#include <sstream>

using namespace std;
using namespace tns;

ObjectManager::ObjectManager(jobject javaRuntimeObject) :
        m_javaRuntimeObject(javaRuntimeObject),
        m_cache(NewWeakGlobalRefCallback, DeleteWeakGlobalRefCallback, 1000, this),
        m_currentObjectId(0),
        m_jsObjectProxyCreator(nullptr),
        m_jsObjectCtor(nullptr),
        m_env(nullptr) {

    JEnv env;
    auto runtimeClass = env.FindClass("com/tns/Runtime");
    assert(runtimeClass != nullptr);

    GET_JAVAOBJECT_BY_ID_METHOD_ID = env.GetMethodID(runtimeClass, "getJavaObjectByID",
                                                     "(I)Ljava/lang/Object;");
    assert(GET_JAVAOBJECT_BY_ID_METHOD_ID != nullptr);

    GET_OR_CREATE_JAVA_OBJECT_ID_METHOD_ID = env.GetMethodID(runtimeClass,
                                                             "getOrCreateJavaObjectID",
                                                             "(Ljava/lang/Object;)I");
    assert(GET_OR_CREATE_JAVA_OBJECT_ID_METHOD_ID != nullptr);

    MAKE_INSTANCE_WEAK_METHOD_ID = env.GetMethodID(runtimeClass, "makeInstanceWeak",
                                                   "(I)V");
    assert(MAKE_INSTANCE_WEAK_METHOD_ID != nullptr);

    MAKE_INSTANCE_WEAK_BATCH_METHOD_ID = env.GetMethodID(runtimeClass, "makeInstanceWeak",
                                                         "(Ljava/nio/ByteBuffer;IZ)V");
    assert(MAKE_INSTANCE_WEAK_BATCH_METHOD_ID != nullptr);

    MAKE_INSTANCE_STRONG_METHOD_ID = env.GetMethodID(runtimeClass, "makeInstanceStrong",
                                                     "(I)V");
    assert(MAKE_INSTANCE_STRONG_METHOD_ID != nullptr);

    JAVA_LANG_CLASS = env.FindClass("java/lang/Class");
    assert(JAVA_LANG_CLASS != nullptr);

    GET_NAME_METHOD_ID = env.GetMethodID(JAVA_LANG_CLASS, "getName", "()Ljava/lang/String;");
    assert(GET_NAME_METHOD_ID != nullptr);
}


void ObjectManager::Init(napi_env env) {
    m_env = env;
    napi_value jsObjectCtor;
    napi_define_class(env, "JSObject", NAPI_AUTO_LENGTH, JSObjectConstructorCallback, nullptr,
                      0,
                      nullptr, &jsObjectCtor);

    napi_set_named_property(env, napi_util::get_prototype(env, jsObjectCtor), PRIVATE_IS_NAPI,
                            napi_util::get_true(env));
    m_jsObjectCtor = napi_util::make_ref(env, jsObjectCtor, 1);
}

ObjectManager::~ObjectManager() {
    JEnv jEnv;
    if (this->m_jsObjectCtor) napi_delete_reference(m_env, this->m_jsObjectCtor);
    if (this->m_jsObjectProxyCreator) napi_delete_reference(m_env, this->m_jsObjectProxyCreator);

    for (auto &entry: m_idToProxy) {
        if (!entry.second) continue;
        napi_delete_reference(m_env, entry.second);
    }
    m_idToProxy.clear();

    for (auto &entry: m_idToObject) {
        if (!entry.second) continue;
        napi_delete_reference(m_env, entry.second);
    }
    m_idToObject.clear();
}

napi_value ObjectManager::GetOrCreateProxyWeak(jint javaObjectID, napi_value instance) {
    napi_value proxy = nullptr;
#ifdef USE_HOST_OBJECT
    bool is_array = false;
    napi_value getter = nullptr;
    napi_value setter = nullptr;

    napi_has_named_property(m_env, instance, "__is__javaArray", &is_array);
    void* data;
    napi_unwrap(m_env, instance, &data);

    if (is_array) {
        napi_value global;
        napi_get_global(m_env, &global);
        napi_get_named_property(m_env, global, "getNativeArrayProp", &getter);
        napi_get_named_property(m_env, global, "setNativeArrayProp", &setter);
    }

    napi_create_host_object(m_env, instance, nullptr, data, is_array, getter, setter, &proxy);
#else
    napi_value argv[2];
    argv[0] = instance;
    napi_create_int32(m_env, javaObjectID, &argv[1]);

    if (!this->m_jsObjectProxyCreator) {
        napi_value jsObjectProxyCreator;
        napi_get_named_property(m_env, napi_util::global(m_env), "__createNativeProxy",
                                &jsObjectProxyCreator);
        this->m_jsObjectProxyCreator = napi_util::make_ref(m_env, jsObjectProxyCreator);
    }

    napi_call_function(m_env, napi_util::global(m_env),
                       napi_util::get_ref_value(m_env, this->m_jsObjectProxyCreator),
                       2, argv, &proxy);

#endif
    return proxy;
}

napi_value ObjectManager::GetOrCreateProxy(jint javaObjectID, napi_value instance) {
    napi_value proxy = nullptr;
    auto it = m_idToProxy.find(javaObjectID);
    if (it != m_idToProxy.end() && it->second != nullptr) {
        proxy = napi_util::get_ref_value(m_env, it->second);
        if (!napi_util::is_null_or_undefined(m_env, proxy)) {
            return proxy;
        } else {
            napi_delete_reference(m_env, it->second);
            m_idToProxy.erase(javaObjectID);
        }
    }

    DEBUG_WRITE("%s %d", "Creating a new proxy for java object with id:", javaObjectID);

#ifdef USE_HOST_OBJECT
    bool is_array = false;
    napi_value getter = nullptr;
    napi_value setter = nullptr;

    napi_has_named_property(m_env, instance, "__is__javaArray", &is_array);

    auto data = new JSInstanceInfo(javaObjectID, nullptr);

    if (is_array) {
        napi_value global;
        napi_get_global(m_env, &global);
        napi_get_named_property(m_env, global, "getNativeArrayProp", &getter);
        napi_get_named_property(m_env, global, "setNativeArrayProp", &setter);
    }

    napi_create_host_object(m_env, instance, JSObjectProxyFinalizerCallback, data, is_array, getter, setter, &proxy);

#else
    napi_value argv[2];
    argv[0] = instance;
    napi_create_int32(m_env, javaObjectID, &argv[1]);

    if (!this->m_jsObjectProxyCreator) {
        napi_value jsObjectProxyCreator;
        napi_get_named_property(m_env, napi_util::global(m_env), "__createNativeProxy",
                                &jsObjectProxyCreator);
        this->m_jsObjectProxyCreator = napi_util::make_ref(m_env, jsObjectProxyCreator);
    }

    napi_call_function(m_env, napi_util::global(m_env),
                       napi_util::get_ref_value(m_env, this->m_jsObjectProxyCreator),
                       2, argv, &proxy);

    if (!proxy) {
        DEBUG_WRITE("Failed to create proxy for javaObjectId %d", javaObjectID);
        return nullptr;
    }


    auto data = new JSInstanceInfo(javaObjectID, nullptr);

    napi_value external;
    napi_create_external(m_env, data, JSObjectProxyFinalizerCallback, data, &external);
    napi_set_named_property(m_env, proxy, "[[external]]", external);


#endif

    auto javaObjectIdFound = m_weakObjectIds.find(javaObjectID);
    if (javaObjectIdFound != m_weakObjectIds.end()) {
        m_weakObjectIds.erase(javaObjectID);
        JEnv jenv;
        jenv.CallVoidMethod(m_javaRuntimeObject,
                            MAKE_INSTANCE_STRONG_METHOD_ID,
                            javaObjectID);
        DEBUG_WRITE("Making instance strong: %d", javaObjectID);
    }

    m_idToProxy.emplace(javaObjectID, napi_util::make_ref(m_env, proxy, 0));

    return proxy;
}

JniLocalRef ObjectManager::GetJavaObjectByJsObject(napi_value object, int *objectId) {
    int32_t javaObjectId = -1;
    if (objectId) {
        javaObjectId = *objectId;
    }

#ifdef USE_HOST_OBJECT
    void* data = nullptr;
    napi_get_host_object_data(m_env, object, &data);
    if (data) {
        auto info = (JSInstanceInfo *) data;
        javaObjectId = info->JavaObjectID;
    } else {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo != nullptr) javaObjectId = jsInstanceInfo->JavaObjectID;
    }
#else
    if (javaObjectId == -1) {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo != nullptr) javaObjectId = jsInstanceInfo->JavaObjectID;
    }
#endif

    if (objectId) {
        *objectId = javaObjectId;
    }

    if (javaObjectId != -1) return {GetJavaObjectByID(javaObjectId), true};

    return {};
}

JniLocalRef ObjectManager::GetJavaObjectByJsObjectFast(napi_value object) {
    void *data = nullptr;

#ifdef USE_HOST_OBJECT
    napi_get_host_object_data(m_env, object, &data);
#endif

    if (!data) napi_unwrap(m_env, object, &data);

    if (data) {
        auto info = reinterpret_cast<JSInstanceInfo *>(data);
        return {GetJavaObjectByID(info->JavaObjectID), true};
    }

    return GetJavaObjectByJsObject(object);
}

ObjectManager::JSInstanceInfo *ObjectManager::GetJSInstanceInfo(napi_value object) {
    if (!IsRuntimeJsObject(object)) return nullptr;

    return GetJSInstanceInfoFromRuntimeObject(object);
}

bool ObjectManager::IsHostObject(napi_value object) {
#ifdef USE_HOST_OBJECT
    bool isHostObject;
    napi_is_host_object(m_env, object, &isHostObject);
    return isHostObject;
#endif
    return false;
}

ObjectManager::JSInstanceInfo *
ObjectManager::GetJSInstanceInfoFromRuntimeObject(napi_value object) {
    napi_value jsInfo;
    napi_get_named_property(m_env, object, PRIVATE_JSINFO, &jsInfo);

    if (napi_util::is_null_or_undefined(m_env, jsInfo)) {
        napi_value proto = napi_util::get__proto__(m_env, object);
        //Typescript object layout has an object instance as child of the actual registered instance. checking for that
        if (!napi_util::is_null_or_undefined(m_env, proto)) {
            if (IsRuntimeJsObject(proto)) {
                napi_get_named_property(m_env, proto, PRIVATE_JSINFO, &jsInfo);
            }
        }
    }

    if (!napi_util::is_null_or_undefined(m_env, jsInfo)) {
        void *data;
        napi_get_value_external(m_env, jsInfo, &data);
        auto info = reinterpret_cast<JSInstanceInfo *>(data);
        return info;
    }
    return nullptr;
}

bool ObjectManager::IsRuntimeJsObject(napi_value object) {
    bool result;
    napi_has_named_property(m_env, object, PRIVATE_IS_NAPI, &result);
    return result;
}

jweak ObjectManager::GetJavaObjectByID(uint32_t javaObjectID) {
    return m_cache(javaObjectID);
}

jobject ObjectManager::GetJavaObjectByIDImpl(uint32_t javaObjectID) {
    JEnv env;
    jobject object = env.CallObjectMethod(m_javaRuntimeObject, GET_JAVAOBJECT_BY_ID_METHOD_ID,
                                          javaObjectID);
    return object;
}

void ObjectManager::UpdateCache(int objectID, jobject obj) {
    m_cache.update(objectID, obj);
}

jclass ObjectManager::GetJavaClass(napi_value value) {
    JSInstanceInfo *jsInfo = GetJSInstanceInfo(value);
    jclass clazz = jsInfo->ObjectClazz;

    return clazz;
}

void ObjectManager::SetJavaClass(napi_value value, jclass clazz) {
    JSInstanceInfo *jsInfo = GetJSInstanceInfo(value);
    jsInfo->ObjectClazz = clazz;
}

int ObjectManager::GetOrCreateObjectId(jobject object) {
    JEnv env;
    jint javaObjectID = env.CallIntMethod(m_javaRuntimeObject,
                                          GET_OR_CREATE_JAVA_OBJECT_ID_METHOD_ID, object);
    return javaObjectID;
}

napi_value ObjectManager::GetJsObjectByJavaObject(int javaObjectID) {
    auto it = m_idToObject.find(javaObjectID);
    if (it == m_idToObject.end()) {
        return nullptr;
    }

    napi_value instance = napi_util::get_ref_value(m_env, it->second);
    if (napi_util::is_null_or_undefined(m_env, instance)) return nullptr;
    return GetOrCreateProxy(javaObjectID, instance);
}


napi_value
ObjectManager::CreateJSWrapper(jint javaObjectID, const std::string &typeName) {
    return CreateJSWrapperHelper(javaObjectID, typeName, nullptr);
}

napi_value
ObjectManager::CreateJSWrapper(jint javaObjectID, const std::string &typeName, jobject instance) {
    JEnv jenv;
    JniLocalRef clazz(jenv.GetObjectClass(instance));

    return CreateJSWrapperHelper(javaObjectID, typeName, clazz);
}

napi_value
ObjectManager::CreateJSWrapperHelper(jint javaObjectID, const std::string &typeName, jclass clazz) {
    auto className = (clazz != nullptr) ? GetClassName(clazz) : typeName;

    auto node = MetadataNode::GetOrCreate(className);
    napi_value proxy = nullptr;
    napi_value jsWrapper = node->CreateJSWrapper(m_env, this);
    if (jsWrapper != nullptr) {
        JEnv jenv;
        auto claz = jenv.FindClass(className);
        Link(jsWrapper, javaObjectID, claz);
        if (node->isArray()) {
            napi_set_named_property(m_env, jsWrapper, "__is__javaArray",
                                    napi_util::get_true(m_env));
        }
        proxy = GetOrCreateProxy(javaObjectID, jsWrapper);
    }

    return proxy;
}

void ObjectManager::Link(napi_value object, uint32_t javaObjectID, jclass clazz) {
    if (!IsRuntimeJsObject(object)) {
        std::string errMsg("Trying to link invalid 'this' to a Java object");
        throw NativeScriptException(errMsg);
    }

    DEBUG_WRITE("Linking js object and java instance id: %d", javaObjectID);

    auto jsInstanceInfo = new JSInstanceInfo(javaObjectID, clazz);

    napi_ref objectHandle = napi_util::make_ref(m_env, object, 1);

    napi_value jsInfo;
    napi_create_external(m_env, jsInstanceInfo, JSObjectFinalizerCallback, jsInstanceInfo, &jsInfo);
    napi_set_named_property(m_env, object, PRIVATE_JSINFO, jsInfo);

    // Wrapped but does not handle data lifecycle. only used for fast access.
    napi_wrap(m_env, object, jsInstanceInfo, [](napi_env env, void *data, void *hint) {}, jsInstanceInfo,
              nullptr);

    m_idToObject.emplace(javaObjectID, objectHandle);
}

bool ObjectManager::CloneLink(napi_value src, napi_value dest) {
    auto jsInfo = GetJSInstanceInfo(src);

    auto success = jsInfo != nullptr;

    if (success) {
        napi_value external;
        napi_create_external(m_env, jsInfo, [](napi_env env, void* d1, void*d2) {}, jsInfo, &external);
        napi_set_named_property(m_env, dest, PRIVATE_JSINFO, external);
        napi_wrap(m_env, dest, jsInfo, [](napi_env env, void *data, void *hint) {}, jsInfo,
                  nullptr);
    }

    return success;
}

string ObjectManager::GetClassName(jobject javaObject) {
    JEnv env;
    JniLocalRef objectClass(env.GetObjectClass(javaObject));

    return GetClassName((jclass) objectClass);
}

bool ObjectManager::GetIsSuper(int objectId, napi_value value) {
    auto it = m_idToSuper.find(objectId);
    if (it != m_idToSuper.end()) return it->second;
    napi_value superValue;
    napi_get_named_property(m_env, value, PRIVATE_CALLSUPER, &superValue);
    bool isSuper = napi_util::get_bool(m_env, superValue);
    m_idToSuper.emplace(objectId, isSuper);
    return isSuper;
}

string ObjectManager::GetClassName(jclass clazz) {
    JEnv env;
    JniLocalRef javaCanonicalName(env.CallObjectMethod(clazz, GET_NAME_METHOD_ID));

    string className = ArgConverter::jstringToString(javaCanonicalName);

    std::replace(className.begin(), className.end(), '.', '/');

    return className;
}

void
ObjectManager::JSObjectFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint) {
    #ifdef __HERMES__
        if (finalizeHint == nullptr) return;
        auto data = reinterpret_cast<JSInstanceInfo *>(finalizeHint);
    #else
        if (finalizeData == nullptr) return;
        auto data = reinterpret_cast<JSInstanceInfo *>(finalizeData);
    #endif

    DEBUG_WRITE("JS Object finalizer called for object id: %d", data->JavaObjectID);
    delete data;
}

void ObjectManager::JSObjectProxyFinalizerCallback(napi_env env, void *finalizeData,
                                                   void *finalizeHint) {

#ifdef __HERMES__
    if (finalizeHint == nullptr) return;
    auto state = reinterpret_cast<JSInstanceInfo *>(finalizeHint);
#else
    if (finalizeData == nullptr) return;
    auto state = reinterpret_cast<JSInstanceInfo *>(finalizeData);
#endif

    auto rt = Runtime::GetRuntime(env);
    if (rt && !rt->is_destroying) {

        auto objManager = rt->GetObjectManager();
        auto itFound = objManager->m_weakObjectIds.find(state->JavaObjectID);

        DEBUG_WRITE("JS Proxy finalizer called for object id: %d", state->JavaObjectID);
        if (itFound == objManager->m_weakObjectIds.end()) {
            objManager->m_weakObjectIds.emplace(state->JavaObjectID);
            JEnv jEnv;
            jEnv.CallVoidMethod(objManager->m_javaRuntimeObject,
                                objManager->MAKE_INSTANCE_WEAK_METHOD_ID,
                                state->JavaObjectID);

        }
    }
    delete state;
}

int ObjectManager::GenerateNewObjectID() {
    const int one = 1;
    int oldValue = __sync_fetch_and_add(&m_currentObjectId, one);
    return oldValue;
}

jweak ObjectManager::NewWeakGlobalRefCallback(const int &javaObjectID, void *state) {
    auto objManager = reinterpret_cast<ObjectManager *>(state);
    JniLocalRef obj(objManager->GetJavaObjectByIDImpl(javaObjectID));
    JEnv jEnv;
    jweak weakRef = jEnv.NewWeakGlobalRef(obj);

    return weakRef;
}

void ObjectManager::DeleteWeakGlobalRefCallback(const jweak &object, void *state) {
    JEnv jEnv;
    jEnv.DeleteWeakGlobalRef(object);
}

napi_value ObjectManager::GetEmptyObject() {
    napi_value emptyObjCtorFunc = napi_util::get_ref_value(m_env, m_jsObjectCtor);

    napi_value ex;
    napi_get_and_clear_last_exception(m_env, &ex);

    napi_value jsWrapper = nullptr;

    napi_new_instance(m_env, emptyObjCtorFunc, 0, nullptr, &jsWrapper);

    if (napi_util::is_null_or_undefined(m_env, jsWrapper)) {
        return nullptr;
    }

    return jsWrapper;
}

napi_value ObjectManager::JSObjectConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    return jsThis;
}

void ObjectManager::ReleaseObjectNow(napi_env env, int javaObjectId) {
    auto rt = Runtime::GetRuntime(env);
    if (rt->is_destroying) return;
    ObjectManager *objMgr = rt->GetObjectManager();

    auto itFound = objMgr->m_weakObjectIds.find(javaObjectId);
    if (itFound == objMgr->m_weakObjectIds.end()) {
        JEnv jEnv;
        jEnv.CallVoidMethod(objMgr->m_javaRuntimeObject, objMgr->MAKE_INSTANCE_WEAK_METHOD_ID,
                            javaObjectId);
        objMgr->m_weakObjectIds.emplace(javaObjectId);
    }

    auto found = objMgr->m_idToProxy.find(javaObjectId);
    if (found != objMgr->m_idToProxy.end()) {
        napi_delete_reference(env, found->second);
        objMgr->m_idToProxy.erase(javaObjectId);
    }

    found = objMgr->m_idToObject.find(javaObjectId);
    if (found != objMgr->m_idToObject.end()) {
        napi_delete_reference(env, found->second);
        objMgr->m_idToObject.erase(javaObjectId);
    }

    Runtime::GetRuntime(env)->js_method_cache->cleanupObject(javaObjectId);
}

void ObjectManager::ReleaseNativeObject(napi_env env, napi_value object) {
    int32_t javaObjectId = -1;
    JSInstanceInfo *jsInstanceInfo;

#ifdef USE_HOST_OBJECT
    void* data;
    napi_get_host_object_data(env, object, &data);
    if (data) {
        jsInstanceInfo = reinterpret_cast<JSInstanceInfo *>(data);
    } else {
#endif
    jsInstanceInfo = GetJSInstanceInfo(object);
#ifdef USE_HOST_OBJECT
    }
#endif

    if (jsInstanceInfo) {
        javaObjectId = jsInstanceInfo->JavaObjectID;
    }

    if (javaObjectId == -1) {
        napi_throw_error(env, "0", "Trying to release a non native object!");
        return;
    }

    ReleaseObjectNow(env, javaObjectId);
}

void ObjectManager::OnGarbageCollected(JNIEnv *jEnv, jintArray object_ids) {
    JEnv jenv(jEnv);
    jsize length = jenv.GetArrayLength(object_ids);
    int *cppArray = jenv.GetIntArrayElements(object_ids, nullptr);
    for (jsize i = 0; i < length; i++) {
        int javaObjectId = cppArray[i];
        auto itFound = this->m_idToObject.find(javaObjectId);
        if (itFound != this->m_idToObject.end()) {
            napi_delete_reference(m_env, itFound->second);
            this->m_idToObject.erase(javaObjectId);
            Runtime::GetRuntime(m_env)->js_method_cache->cleanupObject(javaObjectId);
            DEBUG_WRITE("JS Object released for object id: %d", javaObjectId);
            // auto found = this->m_idToProxy.find(javaObjectId);
            // if (found != this->m_idToProxy.end()) {
            //     napi_delete_reference(m_env, found->second);
            //     this->m_idToProxy.erase(javaObjectId);
            // }
        }

    }
}
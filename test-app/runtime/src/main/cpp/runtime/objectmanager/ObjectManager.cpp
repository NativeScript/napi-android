#include "ObjectManager.h"
#include "NativeScriptAssert.h"
#include "MetadataNode.h"
#include "ArgConverter.h"
#include "Util.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include "CallbackHandlers.h"
#include <algorithm>
#include <sstream>

using namespace std;
using namespace tns;

// GetClassName is static so exception handling can resolve a Java class name
// without retrieving the runtime/ObjectManager (which may be unavailable
// mid-exception). These JNI ids are process-global once looked up.
jclass ObjectManager::JAVA_LANG_CLASS = nullptr;
jmethodID ObjectManager::GET_NAME_METHOD_ID = nullptr;

ObjectManager::ObjectManager(jobject javaRuntimeObject) :
        m_javaRuntimeObject(javaRuntimeObject),
        m_cache(NewWeakGlobalRefCallback, DeleteWeakGlobalRefCallback, ValidateWeakGlobalRefCallback, 1000, this),
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


void ObjectManager::OnDisposeEnv() {
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
    void* data = nullptr;
    napi_unwrap(m_env, instance, &data);
    // Transient (weak) proxy: borrows the instance's existing JSInstanceInfo.
    proxy = CreateHostObjectProxy(instance, reinterpret_cast<JSInstanceInfo*>(data),
                                  /*isPrimary=*/false);
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
    // Primary (cached) proxy: owns a fresh JSInstanceInfo and marks the java
    // instance weak when collected.
    auto info = new JSInstanceInfo(javaObjectID, nullptr);
    // Carry the class metadata from the raw instance's JSInstanceInfo (set in
    // Link) so GetInstanceMetadata resolves it from the proxy.
    void *rawInfo = nullptr;
    napi_unwrap(m_env, instance, &rawInfo);
    if (rawInfo != nullptr) {
        info->node = reinterpret_cast<JSInstanceInfo *>(rawInfo)->node;
    }
    proxy = CreateHostObjectProxy(instance, info, /*isPrimary=*/true);

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

JniLocalRef ObjectManager::GetJavaObjectByJsObject(napi_value object, int *objectId, bool *isSuper) {
    int32_t javaObjectId = (objectId) ? *objectId : -1;
    // Cache slot for the super-call flag on whichever per-object info we resolve;
    // resolved once from PRIVATE_CALLSUPER, then read from the cached field.
    int8_t *superSlot = nullptr;

#ifdef USE_HOST_OBJECT
    void* data = nullptr;
    napi_get_host_object_data(m_env, object, &data);
    if (data) {
        auto proxy = (HostObjectProxy *) data;
        if (proxy->instanceInfo) javaObjectId = proxy->instanceInfo->JavaObjectID;
        superSlot = &proxy->isSuper;
    } else {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo != nullptr) {
            javaObjectId = jsInstanceInfo->JavaObjectID;
            superSlot = &jsInstanceInfo->isSuper;
        }
    }
#else
    if (javaObjectId == -1) {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo != nullptr) {
            javaObjectId = jsInstanceInfo->JavaObjectID;
            superSlot = &jsInstanceInfo->isSuper;
        }
    }
#endif

    if (isSuper) {
        if (superSlot != nullptr) {
            if (*superSlot < 0) {
                napi_value superValue;
                napi_get_named_property(m_env, object, PRIVATE_CALLSUPER, &superValue);
                *superSlot = napi_util::get_bool(m_env, superValue) ? 1 : 0;
            }
            *isSuper = (*superSlot == 1);
        } else {
            *isSuper = false;
        }
    }

    if (objectId) {
        *objectId = javaObjectId;
    }

    if (javaObjectId != -1) {
        try {
            return {GetJavaObjectByID(javaObjectId), true};
        } catch (NativeScriptException &e) {
            // Surface which object failed instead of a bare error — this usually
            // means the id belongs to a different runtime/thread.
            throw NativeScriptException("Failed to get Java object by ID. id=" +
                                        std::to_string(javaObjectId) + ". " + e.what());
        }
    }

    return {};
}

JniLocalRef ObjectManager::GetJavaObjectByJsObjectFast(napi_value object) {
#ifdef USE_HOST_OBJECT
    void *hostData = nullptr;
    napi_get_host_object_data(m_env, object, &hostData);
    if (hostData) {
        auto proxy = reinterpret_cast<HostObjectProxy *>(hostData);
        if (proxy->instanceInfo) {
            return {GetJavaObjectByID(proxy->instanceInfo->JavaObjectID), true};
        }
    }
#endif

    void *data = nullptr;
    napi_unwrap(m_env, object, &data);

    if (data) {
        auto info = reinterpret_cast<JSInstanceInfo *>(data);
        return {GetJavaObjectByID(info->JavaObjectID), true};
    }

    return GetJavaObjectByJsObject(object);
}

ObjectManager::JSInstanceInfo *ObjectManager::GetJSInstanceInfo(napi_value object) {
    #ifdef USE_HOST_OBJECT
    void *hostData = nullptr;
    napi_get_host_object_data(m_env, object, &hostData);
    if (hostData) {
        auto proxy = reinterpret_cast<HostObjectProxy *>(hostData);
        if (proxy->instanceInfo) {
            return proxy->instanceInfo;
        }
    }
    #endif
    
    if (!IsRuntimeJsObject(object)) return nullptr;
    return GetJSInstanceInfoFromRuntimeObject(object);
}

MetadataNode *ObjectManager::GetInstanceNode(napi_value object) {
    JSInstanceInfo *info = GetJSInstanceInfo(object);
    return info != nullptr ? info->node : nullptr;
}

bool ObjectManager::IsHostObject(napi_value object) {
#ifdef USE_HOST_OBJECT
    bool isHostObject;
    napi_is_host_object(m_env, object, &isHostObject);
    return isHostObject;
#endif
    return false;
}

#ifdef USE_HOST_OBJECT
// ----------------------------------------------------------------------------
//  Host object proxy: callbacks + lifecycle
//
//  The new napi_create_host_object takes no target/getter/setter, so the proxy
//  forwards to the wrapped `instance` (kept in HostObjectProxy::target) via
//  these callbacks. Array-like instances route get/set through the JS helpers
//  getNativeArrayProp/setNativeArrayProp, mirroring the old implementation.
// ----------------------------------------------------------------------------

napi_value ObjectManager::HostObjectGet(napi_env env, napi_value host,
                                        napi_value property, void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    // Numeric keys on arrays: straight into the native element accessor. On V8
    // these arrive via the indexed interceptor (HostObjectIndexedGet); engines
    // that route everything through get() (e.g. QuickJS) hit it here.
    if (proxy->isArray && !proxy->arraySignature.empty() &&
        napi_util::is_of_type(env, property, napi_number)) {
        uint32_t index = 0;
        napi_get_value_uint32(env, property, &index);
        return HostObjectIndexedGet(env, host, index, data);
    }

    // Everything else (incl. map/forEach/toString/Symbol.iterator/length, which
    // are now native methods on the array prototype) forwards to the instance.
    napi_value target = napi_util::get_ref_value(env, proxy->target);
    napi_value result = nullptr;
    napi_get_property(env, target, property, &result);
    return result;
}

void ObjectManager::HostObjectSet(napi_env env, napi_value host,
                                  napi_value property, napi_value value,
                                  void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    if (proxy->isArray && !proxy->arraySignature.empty() &&
        napi_util::is_of_type(env, property, napi_number)) {
        uint32_t index = 0;
        napi_get_value_uint32(env, property, &index);
        HostObjectIndexedSet(env, host, index, value, data);
        return;
    }
    napi_value target = napi_util::get_ref_value(env, proxy->target);
    napi_set_property(env, target, property, value);
}

int ObjectManager::HostObjectHas(napi_env env, napi_value host,
                                  napi_value property, void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    napi_value target = napi_util::get_ref_value(env, proxy->target);
    bool result = false;
    napi_has_property(env, target, property, &result);
    return result;
}

int ObjectManager::HostObjectDelete(napi_env env, napi_value host,
                                     napi_value property, void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    napi_value target = napi_util::get_ref_value(env, proxy->target);
    bool result = false;
    napi_delete_property(env, target, property, &result);
    return result;
}

napi_value ObjectManager::HostObjectOwnKeys(napi_env env, napi_value host,
                                            void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    napi_value target = napi_util::get_ref_value(env, proxy->target);
    napi_value names = nullptr;
    napi_get_property_names(env, target, &names);
    return names;
}

// Fast path for numeric indices on java arrays: call straight into the native
// array element accessor (the same code getValueAtIndex/setValueAtIndex run),
// skipping the JS method dispatch entirely. The host object is passed as the
// `array` receiver so CallbackHandlers can resolve the backing java array.
napi_value ObjectManager::HostObjectIndexedGet(napi_env env, napi_value host,
                                               uint32_t index, void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    // The proxy already knows the java object id + ObjectManager, so resolve the
    // backing array directly (no locked env->runtime lookup, no host probe).
    jobject arr = proxy->instanceInfo
                  ? (jobject) proxy->objectManager->GetJavaObjectByID(
                          proxy->instanceInfo->JavaObjectID)
                  : nullptr;
    return CallbackHandlers::GetArrayElement(env, host, index, proxy->arraySignature,
                                             proxy->objectManager, arr);
}

void ObjectManager::HostObjectIndexedSet(napi_env env, napi_value host,
                                         uint32_t index, napi_value value,
                                         void *data) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    jobject arr = proxy->instanceInfo
                  ? (jobject) proxy->objectManager->GetJavaObjectByID(
                          proxy->instanceInfo->JavaObjectID)
                  : nullptr;
    CallbackHandlers::SetArrayElement(env, host, index, proxy->arraySignature,
                                      value, proxy->objectManager, arr);
}

// Mirrors the old "super" accessor: `proxy.super` resolves to `target.super`.
napi_value ObjectManager::HostObjectSuperGetter(napi_env env,
                                                napi_callback_info info) {
    void *data = nullptr;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    napi_value target = napi_util::get_ref_value(env, proxy->target);
    napi_value superValue = nullptr;
    napi_get_named_property(env, target, "super", &superValue);
    return superValue;
}

void ObjectManager::HostObjectProxyFinalizer(napi_env env, void *data,
                                             void *hint) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    if (proxy == nullptr) return;

    // The cleanup deletes a napi_ref, which is illegal from inside the GC
    // finalizer pass (InvokeFinalizerFromGC). Defer it to the safe post-GC pass.
#ifdef __V8__
    node_api_post_finalizer(env, HostObjectProxyPostFinalizer, proxy, hint);
#else
    HostObjectProxyPostFinalizer(env, data, hint);
#endif
}

void ObjectManager::HostObjectProxyPostFinalizer(napi_env env, void *data,
                                                 void *hint) {
    auto *proxy = reinterpret_cast<HostObjectProxy *>(data);
    if (proxy == nullptr) return;

    if (proxy->target) napi_delete_reference(env, proxy->target);

    // Primary (cached) proxies own their JSInstanceInfo and mark the java
    // instance weak on collection (the old JSObjectProxyFinalizerCallback role).
    if (proxy->isPrimary && proxy->instanceInfo) {
        auto rt = Runtime::GetRuntimeUnchecked(env);
        if (rt && !rt->is_destroying) {
            auto objManager = rt->GetObjectManager();
            auto javaObjectID = proxy->instanceInfo->JavaObjectID;
            if (objManager->m_weakObjectIds.find(javaObjectID) ==
                objManager->m_weakObjectIds.end()) {
                objManager->m_weakObjectIds.emplace(javaObjectID);
                JEnv jEnv;
                jEnv.CallVoidMethod(objManager->m_javaRuntimeObject,
                                    objManager->MAKE_INSTANCE_WEAK_METHOD_ID,
                                    javaObjectID);
            }
        }
        delete proxy->instanceInfo;
    }

    delete proxy;
}

napi_value ObjectManager::CreateHostObjectProxy(napi_value instance,
                                                JSInstanceInfo *instanceInfo,
                                                bool isPrimary) {
    auto *proxy = new HostObjectProxy();
    proxy->objectManager = this;
    proxy->instanceInfo = instanceInfo;
    proxy->isPrimary = isPrimary;
    proxy->env = m_env;
    proxy->target = napi_util::make_ref(m_env, instance, 1);
    proxy->isArray = false;

    napi_host_object_methods methods = {
        HostObjectGet,
        HostObjectSet,
        HostObjectHas,
        HostObjectDelete,
        HostObjectOwnKeys,
        nullptr,  // indexed_get (set below for arrays)
        nullptr,  // indexed_set
    };

    napi_has_named_property(m_env, instance, "__is__javaArray", &proxy->isArray);
    if (proxy->isArray) {
        // Cache the jni array signature so numeric index access goes straight
        // into the native element accessor (no JS getValueAtIndex dispatch).
        // node is already on the raw instance's JSInstanceInfo (set in Link).
        MetadataNode *node = GetInstanceNode(instance);
        if (node != nullptr) {
            proxy->arraySignature = node->GetName();
            methods.indexed_get = HostObjectIndexedGet;
            methods.indexed_set = HostObjectIndexedSet;
        }
    }

    napi_value proxyObject = nullptr;
    napi_create_host_object(m_env, HostObjectProxyFinalizer, proxy, &methods,
                            &proxyObject);

    // The napi layer no longer touches the prototype chain or installs the
    // "super" accessor for host objects, so do both here to preserve behaviour
    // (instanceof checks, super dispatch).
    napi_util::setPrototypeOf(m_env, proxyObject,
                              napi_util::getPrototypeOf(m_env, instance));

    napi_property_descriptor superDesc = {
        "super", nullptr, nullptr, HostObjectSuperGetter, nullptr, nullptr,
        napi_default, proxy};
    napi_define_properties(m_env, proxyObject, 1, &superDesc);

    return proxyObject;
}
#endif  // USE_HOST_OBJECT

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
        Link(jsWrapper, javaObjectID, claz, node);
        if (node->isArray()) {
            napi_set_named_property(m_env, jsWrapper, "__is__javaArray",
                                    napi_util::get_true(m_env));
        }
        proxy = GetOrCreateProxy(javaObjectID, jsWrapper);
    }

    return proxy;
}

void ObjectManager::Link(napi_value object, uint32_t javaObjectID, jclass clazz,
                         MetadataNode *node) {
    if (!IsRuntimeJsObject(object)) {
        std::string errMsg("Trying to link invalid 'this' to a Java object");
        throw NativeScriptException(errMsg);
    }

    DEBUG_WRITE("Linking js object and java instance id: %d", javaObjectID);

    auto jsInstanceInfo = new JSInstanceInfo(javaObjectID, clazz);
    jsInstanceInfo->node = node;

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

    auto rt = Runtime::GetRuntimeUnchecked(env);
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

bool ObjectManager::ValidateWeakGlobalRefCallback(const int &javaObjectID, const jweak &object,
                                                  void *state) {
    JEnv jEnv;
    // A weak ref that is now IsSameObject(NULL) points to a collected object and
    // must not be reused; report it as invalid so the cache evicts it.
    return !jEnv.isSameObject(object, NULL);
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
    auto rt = Runtime::GetRuntimeUnchecked(env);
    if (!rt || rt->is_destroying) return;
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
        jsInstanceInfo = reinterpret_cast<HostObjectProxy *>(data)->instanceInfo;
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
        auto rt = Runtime::GetRuntimeUnchecked(m_env);
        if (rt && rt->is_destroying) return;
        int javaObjectId = cppArray[i];
        auto itFound = this->m_idToObject.find(javaObjectId);
        if (itFound != this->m_idToObject.end()) {
            napi_delete_reference(m_env, itFound->second);
            this->m_idToObject.erase(javaObjectId);

            if (rt && !rt->is_destroying) {
                rt->js_method_cache->cleanupObject(javaObjectId);
            }

            DEBUG_WRITE("JS Object released for object id: %d", javaObjectId);
            // auto found = this->m_idToProxy.find(javaObjectId);
            // if (found != this->m_idToProxy.end()) {
            //     napi_delete_reference(m_env, found->second);
            //     this->m_idToProxy.erase(javaObjectId);
            // }
        }

    }
}
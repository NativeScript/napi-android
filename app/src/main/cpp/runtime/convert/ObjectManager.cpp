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
using namespace ns;

const char *js_info_property = "__jsInfo__";

ObjectManager::ObjectManager(jobject javaRuntimeObject) :
        m_javaRuntimeObject(javaRuntimeObject),
        m_numberOfGC(0),
        m_cache(NewWeakGlobalRefCallback, DeleteWeakGlobalRefCallback, 1000, this),
        m_currentObjectId(0) {

    JEnv env;
    auto runtimeClass = env.FindClass("org/nativescript/runtime/napi/Runtime");
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

    auto useGlobalRefsMethodID = env.GetStaticMethodID(runtimeClass, "useGlobalRefs", "()Z");
    assert(useGlobalRefsMethodID != nullptr);

    auto useGlobalRefs = env.CallStaticBooleanMethod(runtimeClass, useGlobalRefsMethodID);
    m_useGlobalRefs = useGlobalRefs == JNI_TRUE;

    // auto getMarkingModeOrdinalMethodID = env.GetMethodID(runtimeClass, "getMarkingModeOrdinal",
    //  "()I");
    // jint markingMode = env.CallIntMethod(m_javaRuntimeObject, getMarkingModeOrdinalMethodID);
    // m_markingMode = static_cast<JavaScriptMarkingMode>(markingMode);
}

void ObjectManager::SetInstanceEnv(napi_env env) {
    m_env = env;
}


void ObjectManager::Init(napi_env env) {
    napi_value jsWrapper;
    napi_create_function(env, "JSWrapper", NAPI_AUTO_LENGTH, JSWrapperConstructorCallback, nullptr,
                         &jsWrapper);

    napi_value hint;
    napi_get_boolean(env, true, &hint);

    napi_value proto;
    napi_get_prototype(env, jsWrapper, &proto);
    napi_set_named_property(env, proto, PRIVATE_IS_NAPI, hint);
    m_poJsWrapperFunc = napi_util::make_ref(env, jsWrapper, 1);

    static const char *proxyFunctionScript = R"((function () {
  return function (object, objectId, isArray) {
    const arrayHandler = (target, prop) => {
      if (typeof prop === "string" && !isNaN(prop)) {
        return target.getValueAtIndex(Number(prop));
      }

      if (prop === Symbol.iterator) {
        let index = 0;
        const l = target.length;
        return function () {
          return {
            next: function () {
              if (index < l) {
                return {
                  value: target.getValueAtIndex(index++),
                  done: false,
                };
              } else {
                return { done: true };
              }
            },
          };
        };
      }
      if (prop === "map") {
        return function (callback) {
          const values = target.getAllValues();
          const result = [];
          const l = target.length;
          for (let i = 0; i < l; i++) {
            result.push(callback(values[i], i, target));
          }
          return result;
        };
      }

      if (prop === "toString") {
        return function () {
          const result = target.getAllValues();
          return result.join(",");
        };
      }

      if (prop === "forEach") {
        return function (callback) {
          const values = target.getAllValues();
          const l = values.length;
          for (let i = 0; i < l; i++) {
            callback(values[i], i, target);
          }
        };
      }
      return target[prop];
    };
    let javaObjectId = objectId;
    const proxy = new Proxy(object, {
      get: (target, prop, receiver) => {
        if (prop === "javaObjectId") return javaObjectId;
        if (isArray) {
          return arrayHandler(target, prop);
        }
        return target[prop];
      },
      set: (target, prop, value) => {
        if (isArray && typeof prop === "string" && !isNaN(prop)) {
          return target.setValueAtIndex(Number(prop), value);
        }

        target[prop] = value;
        return value;
      },
    });
    object["__proxy__"] = new WeakRef(proxy);
    return proxy;
  };
})();)";
    napi_value proxyFunction;
    napi_value scriptValue;
    napi_create_string_utf8(env, proxyFunctionScript, strlen(proxyFunctionScript), &scriptValue);
    napi_run_script(env, scriptValue, &proxyFunction);
    napi_ref ref = napi_util::make_ref(env, proxyFunction);
    this->m_poJsProxyFunction = ref;
}

napi_value ObjectManager::GetOrCreateProxy(jint javaObjectID, napi_value instance, bool isArray) {
    auto itFoundProxy = m_idToProxy.find(javaObjectID);
    if (itFoundProxy != m_idToProxy.end()) {
        napi_value proxy = napi_util::get_ref_value(m_env, itFoundProxy->second);
        if (!napi_util::is_null_or_undefined(m_env, proxy)) {
            DEBUG_WRITE("Returned existing proxy for object: %d", javaObjectID);
            return proxy;
        }
    }

    auto itFound = m_idToObject.find(javaObjectID);
    if (itFound != m_idToObject.end()) {
        napi_value object = napi_util::get_ref_value(m_env, itFound->second);
        napi_value weak_ref;
        napi_get_named_property(m_env, object, "__proxy__", &weak_ref);
        if (!napi_util::is_null_or_undefined(m_env, weak_ref)) {
            napi_value deref;
            napi_get_named_property(m_env, weak_ref, "deref", &deref);
            napi_value proxy;
            napi_call_function(m_env, weak_ref, deref, 0, nullptr, &proxy);
            if (!napi_util::is_null_or_undefined(m_env, proxy)) {
                DEBUG_WRITE("Returned existing proxy for object: %d", javaObjectID);
                return proxy;
            }
        }
    }

    napi_value proxy;
    napi_value argv[3];
    argv[0] = instance;
    napi_create_int32(m_env, javaObjectID, &argv[1]);
    napi_get_boolean(m_env, isArray, &argv[2]);
    napi_call_function(m_env, nullptr, napi_util::get_ref_value(m_env, this->m_poJsProxyFunction),
                       3, argv, &proxy);
    napi_ref proxy_ref = napi_util::make_ref(m_env, proxy, 0);
    m_idToProxy.emplace(javaObjectID, proxy_ref);

    auto javaObjectIdFound = m_weakObjectIds.find(javaObjectID);
    if (javaObjectIdFound != m_weakObjectIds.end()) {
        m_weakObjectIds.erase(javaObjectID);
        JEnv jenv;
        jenv.CallVoidMethod(m_javaRuntimeObject,
                            MAKE_INSTANCE_STRONG_METHOD_ID,
                            javaObjectID);
        DEBUG_WRITE("Making instance strong: %d", javaObjectID);
    }

    auto hint = new ProxyFinalizerHint(this, javaObjectID);

    napi_add_finalizer(m_env, proxy, hint, JSProxyWrapperFinalizer, nullptr, nullptr);

    return proxy;
}

JniLocalRef ObjectManager::GetJavaObjectByJsObject(napi_env env, napi_value object) {
    int32_t javaObjectId;
    napi_value javaObjectIdValue;
    napi_get_named_property(m_env, object, "javaObjectid", &javaObjectIdValue);

    if (!napi_util::is_null_or_undefined(m_env, javaObjectIdValue)) {
        napi_get_value_int32(m_env, javaObjectIdValue, &javaObjectId);
    } else {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo == nullptr)  return JniLocalRef();
        javaObjectId = jsInstanceInfo->JavaObjectID;
    }

    if (m_useGlobalRefs) {
        JniLocalRef javaObject(GetJavaObjectByID(javaObjectId), true);
        return javaObject;
    }

    JEnv jEnv;
    JniLocalRef javaObject(
            jEnv.NewLocalRef(GetJavaObjectByID(javaObjectId)));
    return javaObject;

}

ObjectManager::JSInstanceInfo *ObjectManager::GetJSInstanceInfo(napi_value object) {
    JSInstanceInfo *jsInstanceInfo = nullptr;
    if (IsJsRuntimeObject(m_env, object)) {
        return GetJSInstanceInfoFromRuntimeObject(object);
    }

    return nullptr;
}

ObjectManager::JSInstanceInfo *
ObjectManager::GetJSInstanceInfoFromRuntimeObject(napi_value object) {
    napi_value jsInfo;

    napi_handle_scope scope;
    napi_open_handle_scope(m_env, &scope);

    napi_get_named_property(m_env, object, PRIVATE_JSINFO, &jsInfo);

    if (jsInfo == nullptr) {
        napi_value proto;
        napi_get_prototype(m_env, object, &proto);
        //Typescript object layout has an object instance as child of the actual registered instance. checking for that
        if (proto != nullptr) {
            if (IsJsRuntimeObject(m_env, proto)) {
                napi_get_named_property(m_env, proto, PRIVATE_JSINFO, &jsInfo);
            }
        }
    }

    if (jsInfo != nullptr) {
        void *externalValue;
        napi_get_value_external(m_env, jsInfo, &externalValue);
        napi_close_handle_scope(m_env, scope);
        return static_cast<JSInstanceInfo *>(externalValue);
    }
    napi_close_handle_scope(m_env, scope);
    return nullptr;
}

bool ObjectManager::IsJsRuntimeObject(napi_env env, napi_value object) {
    bool result;

    napi_has_named_property(env, object, PRIVATE_IS_NAPI, &result);

    return result;
}

jweak ObjectManager::GetJavaObjectByID(uint32_t javaObjectID) {
    jweak obj = m_cache(javaObjectID);

    return obj;
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
    DEBUG_WRITE("GetClass called");
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
    auto itProxyFound = m_idToProxy.find(javaObjectID);
    if (itProxyFound != m_idToProxy.end()) {
        napi_value proxy = napi_util::get_ref_value(m_env, itProxyFound->second);
        if (napi_util::is_null_or_undefined(m_env, proxy)) {
            return proxy;
        }
    }

    auto it = m_idToObject.find(javaObjectID);
    if (it == m_idToObject.end()) {
        return nullptr;
    }

    napi_value instance = napi_util::get_ref_value(m_env, it->second);
    bool isArray;
    napi_has_named_property(m_env, instance, "getAllValues", &isArray);
    return GetOrCreateProxy(javaObjectID, instance, isArray);
}

napi_value
ObjectManager::CreateJSWrapper(jint javaObjectID, const std::string &typeName, bool isArray) {
    return CreateJSWrapperHelper(javaObjectID, typeName, nullptr, isArray);
}

napi_value
ObjectManager::CreateJSWrapper(jint javaObjectID, const std::string &typeName, jobject instance,
                               bool isArray) {
    JEnv jenv;
    JniLocalRef clazz(jenv.GetObjectClass(instance));

    return CreateJSWrapperHelper(javaObjectID, typeName, clazz, isArray);
}

napi_value
ObjectManager::CreateJSWrapperHelper(jint javaObjectID, const std::string &typeName, jclass clazz,
                                     bool isArray) {
    auto className = (clazz != nullptr) ? GetClassName(clazz) : typeName;

    auto node = MetadataNode::GetOrCreate(className);
    napi_value proxy = nullptr;
    napi_value jsWrapper = node->CreateJSWrapper(m_env, this);
    if (jsWrapper != nullptr) {
        JEnv jenv;
        auto claz = jenv.FindClass(className);
        Link(jsWrapper, javaObjectID, claz);
        proxy = GetOrCreateProxy(javaObjectID, jsWrapper, isArray);
    }

    return proxy;
}

void ObjectManager::Link(napi_value object, uint32_t javaObjectID, jclass clazz) {
    if (!IsJsRuntimeObject(m_env, object)) {
        std::string errMsg("Trying to link invalid 'this' to a Java object");
        throw NativeScriptException(errMsg);
    }

    DEBUG_WRITE("Linking js object and java instance id: %d", javaObjectID);

    auto jsInstanceInfo = new JSInstanceInfo( javaObjectID, clazz);

    napi_ref objectHandle = napi_util::make_ref(m_env, object, 1);

    auto state = new JSObjectFinalizerHint(this, jsInstanceInfo, objectHandle);

    napi_value jsInfo;
    napi_create_external(m_env, jsInstanceInfo, JSObjectFinalizer, state, &jsInfo);
    napi_set_named_property(m_env, object, PRIVATE_JSINFO, jsInfo);
    m_idToObject.emplace(javaObjectID, objectHandle);
}

bool ObjectManager::CloneLink(napi_value src, napi_value dest) {
    auto jsInfo = GetJSInstanceInfo(src);

    auto success = jsInfo != nullptr;

    if (success) {
        napi_value external;
        napi_get_named_property(m_env, src, PRIVATE_JSINFO, &external);
        napi_set_named_property(m_env, dest, PRIVATE_JSINFO, external);
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

void ObjectManager::JSObjectFinalizer(napi_env env, void *finalizeData, void *finalizeHint) {
    auto state = reinterpret_cast<JSObjectFinalizerHint *>(finalizeHint);
    DEBUG_WRITE("JS Object finalizer called for object id: %d",state->jsInfo->JavaObjectID);
    delete state->jsInfo;
    delete state;

}

void ObjectManager::JSProxyWrapperFinalizer(napi_env env, void *finalizeData, void *finalizeHint) {
    auto state = reinterpret_cast<ProxyFinalizerHint *>(finalizeData);
    DEBUG_WRITE("JS Proxy finalizer called for object id: %d", state->javaObjectId);
    JEnv jEnv;
    ObjectManager *objectManager = state->thisPtr;
//    objectManager->m_buff.Write(state->javaObjectId);
// TODO find a way to do this in a batch, maybe some GC callback but NAPI doesn't have it although an extension can be added to support this so we have to go to JNI only once after a single gc pass
    jEnv.CallVoidMethod(objectManager->m_javaRuntimeObject,
                        objectManager->MAKE_INSTANCE_WEAK_METHOD_ID, state->javaObjectId);
    objectManager->m_weakObjectIds.emplace(state->javaObjectId);

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
    auto objManager = reinterpret_cast<ObjectManager *>(state);
    JEnv jEnv;
    jEnv.DeleteWeakGlobalRef(object);
}

napi_value ObjectManager::GetEmptyObject(napi_env env) {
    napi_value emptyObjCtorFunc = napi_util::get_ref_value(env, m_poJsWrapperFunc);

    napi_value jsWrapper;
    napi_new_instance(env, emptyObjCtorFunc, 0, nullptr, &jsWrapper);

    if (jsWrapper == nullptr) {
        return nullptr;
    }

    return jsWrapper;
}

napi_value ObjectManager::JSWrapperConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    napi_value newTarget;
    napi_get_new_target(env, info, &newTarget);
    assert(newTarget != NULL);
    return jsThis;
}

void ObjectManager::ReleaseNativeCounterpart(napi_value object) {
}

ObjectManager::JavaScriptMarkingMode ObjectManager::GetMarkingMode() {
    return this->m_markingMode;
}

void ObjectManager::OnGarbageCollected(JNIEnv *jEnv, jintArray object_ids) {
    JEnv jenv(jEnv);
    jsize length = jenv.GetArrayLength(object_ids);
    int *cppArray = jenv.GetIntArrayElements(object_ids, nullptr);
    for (jsize i = 0; i < length; i++) {
        int objectId = cppArray[i];
        auto itFound = this->m_idToObject.find(objectId);
        if (itFound != this->m_idToObject.end()) {
            napi_delete_reference(m_env, itFound->second);
            this->m_idToObject.erase(objectId);
            this->m_weakObjectIds.erase(objectId);
            DEBUG_WRITE("JS Object released for object id: %d", objectId);

        }
    }
}












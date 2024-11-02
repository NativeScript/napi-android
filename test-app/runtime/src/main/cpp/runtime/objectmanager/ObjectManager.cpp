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
        m_env(nullptr)
        {

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

    auto useGlobalRefsMethodID = env.GetStaticMethodID(runtimeClass, "useGlobalRefs", "()Z");
    assert(useGlobalRefsMethodID != nullptr);

    auto useGlobalRefs = env.CallStaticBooleanMethod(runtimeClass, useGlobalRefsMethodID);
    m_useGlobalRefs = useGlobalRefs == JNI_TRUE;
}

void ObjectManager::SetInstanceEnv(napi_env env) {
    m_env = env;
}


void ObjectManager::Init(napi_env env) {
    napi_value jsObjectCtor;
    napi_define_class(env, "JSObject", NAPI_AUTO_LENGTH, JSObjectConstructorCallback, nullptr,
                      0,
                      nullptr, &jsObjectCtor);

    napi_set_named_property(env, napi_util::get_proto(env, jsObjectCtor), PRIVATE_IS_NAPI, napi_util::get_true(env));
    m_jsObjectCtor = napi_util::make_ref(env, jsObjectCtor, 1);

    napi_set_gc_finish_callback(env, [](napi_env env, void *data, void* hint) {
        auto* objectManager = reinterpret_cast<ObjectManager*>(data);
        if (!objectManager->m_weakObjectIds.empty()) {
            JEnv jEnv;
            std::for_each(objectManager->m_weakObjectIds.begin(),
                          objectManager->m_weakObjectIds.end(), [&](int id) {
                    auto itFound = objectManager->m_markedAsWeakIds.find(id);
                    if (itFound == objectManager->m_markedAsWeakIds.end()) {
                        objectManager->m_buff.Write(id);
                        objectManager->m_markedAsWeakIds.emplace(id);
                    }
            });
            if (objectManager->m_buff.Size()) {
                int length = objectManager->m_buff.Size();
                jboolean keepAsWeak = JNI_TRUE;
                jEnv.CallVoidMethod(objectManager->m_javaRuntimeObject, objectManager->MAKE_INSTANCE_WEAK_BATCH_METHOD_ID,
                                    (jobject) objectManager->m_buff, length, keepAsWeak);
                objectManager->m_buff.Reset();
            }
        }
        DEBUG_WRITE("%s", "GC completed");
        }, this);

    static const char *proxyFunctionScript = R"((function () {

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
	  const EXTERNAL_PROP = "[[external]]";
	  return function (object, objectId, isArray) {
		const proxy = new Proxy(object, {
		  get: function(target, prop) {
			if (prop === EXTERNAL_PROP) return this[EXTERNAL_PROP];
			if (prop === "javaObjectId") return objectId;
			if (isArray) {
			  return arrayHandler(target, prop);
			}

			return target[prop];
		  },
		  set: function(target, prop, value) {
			if (prop === EXTERNAL_PROP) {
			  this[EXTERNAL_PROP] = value;
			  return true;
			};
			if (prop === "javaObjectId") return true;
			if (isArray && typeof prop === "string" && !isNaN(prop)) {
			  target.setValueAtIndex(Number(prop), value);
			  return true;
			}

			target[prop] = value;
			return true;
		  },
		});
		return proxy;
	  };
	})();)";

    napi_value jsObjectProxyCreator;
    napi_value scriptValue;
    napi_create_string_utf8(env, proxyFunctionScript, strlen(proxyFunctionScript), &scriptValue);
    napi_run_script(env, scriptValue, "<ns_proxy_script>", &jsObjectProxyCreator);
    napi_ref ref = napi_util::make_ref(env, jsObjectProxyCreator);
    this->m_jsObjectProxyCreator = ref;
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

    DEBUG_WRITE("%s %d", "Creating a new proxy for for java object with id:", javaObjectID);

    napi_value proxy;
    napi_value argv[3];
    argv[0] = instance;
    napi_create_int32(m_env, javaObjectID, &argv[1]);
    napi_get_boolean(m_env, isArray, &argv[2]);
    napi_call_function(m_env, nullptr, napi_util::get_ref_value(m_env, this->m_jsObjectProxyCreator),
                       3, argv, &proxy);

    napi_ref proxy_ref = napi_util::make_ref(m_env, proxy, 0);
    m_idToProxy.emplace(javaObjectID, proxy_ref);

    auto javaObjectIdFound = m_weakObjectIds.find(javaObjectID);
    if (javaObjectIdFound != m_weakObjectIds.end()) {
        m_weakObjectIds.erase(javaObjectID);
        m_markedAsWeakIds.erase(javaObjectID);
        JEnv jenv;
        jenv.CallVoidMethod(m_javaRuntimeObject,
                            MAKE_INSTANCE_STRONG_METHOD_ID,
                            javaObjectID);
        DEBUG_WRITE("Making instance strong: %d", javaObjectID);
    }

    auto data = new JSObjectProxyData(this, javaObjectID);

    napi_value external;
    napi_create_external(m_env, data, JSObjectProxyFinalizerCallback, nullptr, &external);
    // Set external property on proxy's handler object.
    napi_set_named_property(m_env, proxy, "[[external]]", external);

    return proxy;
}

JniLocalRef ObjectManager::GetJavaObjectByJsObject(napi_env env, napi_value object) {
    int32_t javaObjectId;
    napi_value javaObjectIdValue;
    napi_get_named_property(m_env, object, "javaObjectId", &javaObjectIdValue);
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
    if (IsRuntimeJsObject(m_env, object)) {
        return GetJSInstanceInfoFromRuntimeObject(object);
    }

    return nullptr;
}

ObjectManager::JSInstanceInfo *
ObjectManager::GetJSInstanceInfoFromRuntimeObject(napi_value object) {
    napi_value jsInfo;
    napi_get_named_property(m_env, object, PRIVATE_JSINFO, &jsInfo);

    if (napi_util::is_null_or_undefined(m_env, jsInfo)) {
        napi_value proto;
        napi_get_prototype(m_env, object, &proto);
        //Typescript object layout has an object instance as child of the actual registered instance. checking for that
        if (!napi_util::is_null_or_undefined(m_env, proto)) {
            if (IsRuntimeJsObject(m_env, proto)) {
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

bool ObjectManager::IsRuntimeJsObject(napi_env env, napi_value object) {
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
    if (!IsRuntimeJsObject(m_env, object)) {
        std::string errMsg("Trying to link invalid 'this' to a Java object");
        throw NativeScriptException(errMsg);
    }

    DEBUG_WRITE("Linking js object and java instance id: %d", javaObjectID);

    auto jsInstanceInfo = new JSInstanceInfo( javaObjectID, clazz);

    napi_ref objectHandle = napi_util::make_ref(m_env, object, 1);

    auto state = new JSObjectFinalizerHint(this, jsInstanceInfo, objectHandle);

    napi_value jsInfo;
    napi_create_external(m_env, jsInstanceInfo, JSObjectFinalizerCallback, state, &jsInfo);
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

void ObjectManager::JSObjectFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint) {
    auto state = reinterpret_cast<JSObjectFinalizerHint *>(finalizeHint);
    DEBUG_WRITE("JS Object finalizer called for object id: %d",state->jsInfo->JavaObjectID);
    delete state->jsInfo;
    delete state;
}

void ObjectManager::JSObjectProxyFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint) {
    auto state = reinterpret_cast<JSObjectProxyData *>(finalizeData);
    ObjectManager *objectManager = state->thisPtr;
    objectManager->m_weakObjectIds.emplace(state->javaObjectId);
//    JEnv jEnv;
//    jEnv.CallVoidMethod(objectManager->m_javaRuntimeObject, objectManager->MAKE_INSTANCE_WEAK_METHOD_ID, state->javaObjectId);
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
    napi_value emptyObjCtorFunc = napi_util::get_ref_value(m_env, m_jsObjectCtor);

    auto isDefined = napi_util::is_of_type(m_env, emptyObjCtorFunc, napi_function);

    napi_value jsWrapper;
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
            this->m_weakObjectIds.erase(javaObjectId);
            this->m_markedAsWeakIds.erase(javaObjectId);
            DEBUG_WRITE("JS Object released for object id: %d", javaObjectId);
        }
    }
}












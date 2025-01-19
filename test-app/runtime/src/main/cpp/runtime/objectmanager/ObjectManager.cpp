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
      const REFERENCE_PROP_JSC = "[[jsc_reference_info]]";
	  return function (object, objectId, isArray) {
        const proxy = new Proxy(object, {
		  get: function(target, prop) {
			if (prop === "javaObjectId") return objectId;
			if (prop === EXTERNAL_PROP) return this[EXTERNAL_PROP];
            if (prop === REFERENCE_PROP_JSC) return this[REFERENCE_PROP_JSC];
			if (target.__is__javaArray) {
			  return arrayHandler(target, prop);
			}
			return target[prop];
		  },
		  set: function(target, prop, value) {
			if (prop === EXTERNAL_PROP) {
			  this[EXTERNAL_PROP] = value;
			  return true;
			};

            if (prop === REFERENCE_PROP_JSC) {
                this[REFERENCE_PROP_JSC] = value;
            }

			if (prop === "javaObjectId") return true;
			if (target.__is__javaArray && !isNaN(prop)) {
			  target.setValueAtIndex(Number(prop), value);
			  return true;
			}

			target[prop] = value;
			return true;
		  },
		});

        object.__proxy__ = new WeakRef(proxy);
		return proxy;
	  };
	})();)";

    napi_value jsObjectProxyCreator;
    napi_value scriptValue;
    napi_create_string_utf8(env, proxyFunctionScript, strlen(proxyFunctionScript), &scriptValue);
    js_execute_script(env, scriptValue, "<ns_proxy_script>", &jsObjectProxyCreator);
    napi_ref ref = napi_util::make_ref(env, jsObjectProxyCreator);
    this->m_jsObjectProxyCreator = ref;
}

napi_value ObjectManager::GetOrCreateProxy(jint javaObjectID, napi_value instance) {
    napi_value weakProxy, proxy;
    napi_get_named_property(m_env, instance, "__proxy__", &weakProxy);
    if (!napi_util::is_null_or_undefined(m_env, weakProxy)) {
        napi_value deref;
        napi_get_named_property(m_env, weakProxy, "deref", &deref);
        napi_call_function(m_env, weakProxy, deref, 0, nullptr, &proxy);
        if (!napi_util::is_null_or_undefined(m_env, proxy)) {
            return proxy;
        }
    }

    DEBUG_WRITE("%s %d", "Creating a new proxy for java object with id:", javaObjectID);

    napi_value argv[2];
    argv[0] = instance;
    napi_create_int32(m_env, javaObjectID, &argv[1]);

    napi_call_function(m_env, napi_util::global(m_env),
                       napi_util::get_ref_value(m_env, this->m_jsObjectProxyCreator),
                       2, argv, &proxy);

    auto javaObjectIdFound = m_weakObjectIds.find(javaObjectID);
    if (javaObjectIdFound != m_weakObjectIds.end()) {
        m_weakObjectIds.erase(javaObjectID);
//        m_markedAsWeakIds.erase(javaObjectID);
        JEnv jenv;
        jenv.CallVoidMethod(m_javaRuntimeObject,
                            MAKE_INSTANCE_STRONG_METHOD_ID,
                            javaObjectID);
        DEBUG_WRITE("Making instance strong: %d", javaObjectID);
    }

    auto data = new JSObjectProxyData(this, javaObjectID);

    napi_value obj;
    napi_create_object(m_env, &obj);

    napi_value external;
    napi_create_external(m_env, data, JSObjectProxyFinalizerCallback, nullptr, &external);
    napi_set_named_property(m_env, proxy, "[[external]]", external);

    return proxy;
}

JniLocalRef ObjectManager::GetJavaObjectByJsObject(napi_value object) {
    int32_t javaObjectId = -1;
    napi_value javaObjectIdValue;
    napi_get_named_property(m_env, object, "javaObjectId", &javaObjectIdValue);
    if (!napi_util::is_null_or_undefined(m_env, javaObjectIdValue)) {
        napi_get_value_int32(m_env, javaObjectIdValue, &javaObjectId);
    } else {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo != nullptr) javaObjectId = jsInstanceInfo->JavaObjectID;
    }

    if (javaObjectId != -1) return {GetJavaObjectByID(javaObjectId), true};

    return {};
}

ObjectManager::JSInstanceInfo *ObjectManager::GetJSInstanceInfo(napi_value object) {
    if (IsRuntimeJsObject(object)) {
        return GetJSInstanceInfoFromRuntimeObject(object);
    }
    return nullptr;
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
    return GetOrCreateProxy(javaObjectID, instance);
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
        if (node->isArray()) {
            napi_set_named_property(m_env, jsWrapper, "__is__javaArray", napi_util::get_true(m_env));
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

void
ObjectManager::JSObjectFinalizerCallback(napi_env env, void *finalizeData, void *finalizeHint) {
    auto state = reinterpret_cast<JSObjectFinalizerHint *>(finalizeHint);
    DEBUG_WRITE("JS Object finalizer called for object id: %d", state->jsInfo->JavaObjectID);
    delete state->jsInfo;
    delete state;
}

void ObjectManager::JSObjectProxyFinalizerCallback(napi_env env, void *finalizeData,
                                                   void *finalizeHint) {
    auto state = reinterpret_cast<JSObjectProxyData *>(finalizeData);
    if (state == nullptr) return;
    DEBUG_WRITE("JS Proxy finalizer called for object id: %d", state->javaObjectId);

//#ifdef __HERMES__
    ReleaseObjectNow(env, state->javaObjectId);
//#endif
//#ifdef __QJS__
//    objectManager->m_weakObjectIds.emplace(state->javaObjectId);
//#endif

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
    if (javaObjectId < 0) return;
    ObjectManager* objMgr = Runtime::GetRuntime(env)->GetObjectManager();
//    auto itFound = this->m_markedAsWeakIds.find(javaObjectId);
//    if (itFound == this->m_markedAsWeakIds.end()) {
        auto itFound = objMgr->m_weakObjectIds.find(javaObjectId);
        if (itFound == objMgr->m_weakObjectIds.end()) {
            objMgr->m_weakObjectIds.emplace(javaObjectId);
            JEnv jEnv;
            jEnv.CallVoidMethod(objMgr->m_javaRuntimeObject, objMgr->MAKE_INSTANCE_WEAK_METHOD_ID,
                                javaObjectId);
        }
//        this->m_markedAsWeakIds.emplace(javaObjectId);

//    }
}

void ObjectManager::ReleaseNativeObject(napi_env env, napi_value object) {
    int32_t javaObjectId = -1;
    napi_value javaObjectIdValue;
    napi_get_named_property(m_env, object, "javaObjectId", &javaObjectIdValue);
    if (!napi_util::is_null_or_undefined(m_env, javaObjectIdValue)) {
        napi_get_value_int32(m_env, javaObjectIdValue, &javaObjectId);
    } else {
        JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);
        if (jsInstanceInfo) {
            javaObjectId = jsInstanceInfo->JavaObjectID;
        }
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
            this->m_weakObjectIds.erase(javaObjectId);
//            this->m_markedAsWeakIds.erase(javaObjectId);
            DEBUG_WRITE("JS Object released for object id: %d", javaObjectId);
        }
    }
}












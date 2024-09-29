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
        m_currentObjectId(0),
        m_cache(NewWeakGlobalRefCallback, DeleteWeakGlobalRefCallback, 1000, this) {

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

    MAKE_INSTANCE_WEAK_BATCH_METHOD_ID = env.GetMethodID(runtimeClass, "makeInstanceWeak",
                                                         "(Ljava/nio/ByteBuffer;IZ)V");
    assert(MAKE_INSTANCE_WEAK_BATCH_METHOD_ID != nullptr);

    MAKE_INSTANCE_WEAK_AND_CHECK_IF_ALIVE_METHOD_ID = env.GetMethodID(runtimeClass,
                                                                      "makeInstanceWeakAndCheckIfAlive",
                                                                      "(I)Z");
    assert(MAKE_INSTANCE_WEAK_AND_CHECK_IF_ALIVE_METHOD_ID != nullptr);

    RELEASE_NATIVE_INSTANCE_METHOD_ID = env.GetMethodID(runtimeClass, "releaseNativeCounterpart",
                                                        "(I)V");
    assert(RELEASE_NATIVE_INSTANCE_METHOD_ID != nullptr);

    CHECK_WEAK_OBJECTS_ARE_ALIVE_METHOD_ID = env.GetMethodID(runtimeClass,
                                                             "checkWeakObjectAreAlive",
                                                             "(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;I)V");
    assert(CHECK_WEAK_OBJECTS_ARE_ALIVE_METHOD_ID != nullptr);

    JAVA_LANG_CLASS = env.FindClass("java/lang/Class");
    assert(JAVA_LANG_CLASS != nullptr);

    GET_NAME_METHOD_ID = env.GetMethodID(JAVA_LANG_CLASS, "getName", "()Ljava/lang/String;");
    assert(GET_NAME_METHOD_ID != nullptr);

    auto useGlobalRefsMethodID = env.GetStaticMethodID(runtimeClass, "useGlobalRefs", "()Z");
    assert(useGlobalRefsMethodID != nullptr);

    auto useGlobalRefs = env.CallStaticBooleanMethod(runtimeClass, useGlobalRefsMethodID);
    m_useGlobalRefs = useGlobalRefs == JNI_TRUE;

    auto getMarkingModeOrdinalMethodID = env.GetMethodID(runtimeClass, "getMarkingModeOrdinal",
                                                         "()I");
    jint markingMode = env.CallIntMethod(m_javaRuntimeObject, getMarkingModeOrdinalMethodID);
    m_markingMode = static_cast<JavaScriptMarkingMode>(markingMode);


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
}

JniLocalRef ObjectManager::GetJavaObjectByJsObject(napi_env env, napi_value object) {
    JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);

    if (jsInstanceInfo == nullptr) return JniLocalRef();

    if (m_useGlobalRefs) {
        JniLocalRef javaObject(GetJavaObjectByID(jsInstanceInfo->JavaObjectID), true);
        return javaObject;
    }

    JEnv jEnv;
    JniLocalRef javaObject(
            jEnv.NewLocalRef(GetJavaObjectByID(jsInstanceInfo->JavaObjectID)));
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

    napi_has_own_named_property(env, object, PRIVATE_IS_NAPI, &result);

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
    napi_escapable_handle_scope escapableScope;
    napi_open_escapable_handle_scope(m_env, &escapableScope);

    auto it = m_idToObject.find(javaObjectID);
    if (it == m_idToObject.end()) {
        return nullptr;
    }

    return napi_util::get_ref_value(m_env, it->second);
}

napi_value ObjectManager::CreateJSWrapper(jint javaObjectID, const std::string &typeName) {
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

    napi_value jsWrapper = node->CreateJSWrapper(m_env, this);

    if (jsWrapper != nullptr) {
        JEnv jenv;
        auto claz = jenv.FindClass(className);
        Link(jsWrapper, javaObjectID, claz);
    }
    return jsWrapper;
}

void ObjectManager::Link(napi_value object, uint32_t javaObjectID, jclass clazz) {
    if (!IsJsRuntimeObject(m_env, object)) {
        std::string errMsg("Trying to link invalid 'this' to a Java object");
        throw NativeScriptException(errMsg);
    }

    DEBUG_WRITE("Linking js object and java instance id: %d", javaObjectID);

    auto jsInstanceInfo = new JSInstanceInfo(false /*isJavaObjWeak*/, javaObjectID, clazz);

    napi_ref objectHandle = napi_util::make_ref(m_env, object, 1);

    auto state = new ObjectWeakCallbackState(this, jsInstanceInfo, objectHandle);
    // subscribe for JS GC event

    napi_value jsInfo;
    if (m_markingMode == JavaScriptMarkingMode::None) {
        napi_create_external(m_env, jsInstanceInfo, JSObjectFinalizerStatic, state, &jsInfo);
    } else {
        napi_create_external(m_env, jsInstanceInfo, JSObjectWeakCallbackStatic, state, &jsInfo);
    }

    napi_set_named_property(m_env, object, PRIVATE_JSINFO, jsInfo);
    m_idToObject.emplace(javaObjectID, objectHandle);
}

bool ObjectManager::CloneLink(napi_value src, napi_value dest) {
    auto jsInfo = GetJSInstanceInfo(src);

    auto success = jsInfo != nullptr;

    if (success) {
        void * jsInfo;
        napi_value external;
        napi_get_named_property(m_env, src, PRIVATE_JSINFO, &external);
        napi_get_value_external(m_env, external, &jsInfo);

        napi_value jsInfoClone;
        napi_create_external(m_env, jsInfo, nullptr, nullptr, &jsInfoClone);
        napi_set_named_property(m_env, dest, PRIVATE_JSINFO, jsInfoClone);
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
ObjectManager::JSObjectWeakCallbackStatic(napi_env env, void *finalizeData, void *finalizeHint) {
    ObjectWeakCallbackState * state = reinterpret_cast<ObjectWeakCallbackState *>(finalizeHint);
    // TODO
}

void ObjectManager::JSObjectFinalizerStatic(napi_env env, void *finalizeData, void *finalizeHint) {
    ObjectWeakCallbackState * state = reinterpret_cast<ObjectWeakCallbackState *>(finalizeHint);
    // TODO
}

int ObjectManager::GenerateNewObjectID() {
    const int one = 1;
    int oldValue = __sync_fetch_and_add(&m_currentObjectId, one);

    return oldValue;
}

void ObjectManager::ReleaseJSInstance(napi_ref object, JSInstanceInfo *jsInstanceInfo) {
    int javaObjectID = jsInstanceInfo->JavaObjectID;

    auto it = m_idToObject.find(javaObjectID);

    if (it == m_idToObject.end()) {
        stringstream ss;
        ss << "(InternalError): Js object with id: " << javaObjectID << " not found";
        throw NativeScriptException(ss.str());
    }

    assert(object == it->second);

    m_idToObject.erase(it);
    m_released.insert(object, javaObjectID);
    napi_delete_reference(m_env, object);

    delete object;
    delete jsInstanceInfo;

    DEBUG_WRITE("ReleaseJSObject instance disposed. id:%d", javaObjectID);
}

void ObjectManager::ReleaseRegularObjects() {
    // TODO
}

bool ObjectManager::HasImplObject(napi_env env, napi_value obj) {
    // TODO   auto implObject = MetadataNode::GetImplementationObject(env, obj);
    //
    //    bool hasImplObj = implObject != nullptr;

    return false;
}

jweak ObjectManager::NewWeakGlobalRefCallback(const int &javaObjectID, void *state) {
    auto objManager = reinterpret_cast<ObjectManager *>(state);

    JniLocalRef obj(objManager->GetJavaObjectByIDImpl(javaObjectID));

    JEnv env;
    jweak weakRef = env.NewWeakGlobalRef(obj);

    return weakRef;
}

void ObjectManager::DeleteWeakGlobalRefCallback(const jweak &object, void *state) {
    auto objManager = reinterpret_cast<ObjectManager *>(state);
    JEnv env;
    env.DeleteWeakGlobalRef(object);
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
    napi_valuetype type;
    napi_typeof(m_env, object, &type);

    if (type != napi_object) {
        throw NativeScriptException("Argument is not an object!");
    }

    JSInstanceInfo *jsInstanceInfo = GetJSInstanceInfo(object);

    if (jsInstanceInfo == nullptr) {
        throw NativeScriptException("Trying to release a non-native object!");
    }

    JEnv jenv;
    jenv.CallVoidMethod(m_javaRuntimeObject, RELEASE_NATIVE_INSTANCE_METHOD_ID, jsInstanceInfo->JavaObjectID);

    delete jsInstanceInfo;

    napi_value undefined;
    napi_get_undefined(m_env, &undefined);
    napi_set_named_property(m_env, object, PRIVATE_JSINFO, undefined);
}

ObjectManager::JavaScriptMarkingMode ObjectManager::GetMarkingMode() {
    return this->m_markingMode;
}












//
// Created by Ammar Ahmed on 20/09/2024.
//
#include "JEnv.h"
#include <assert.h>
#include <string>
#include "CallbackHandlers.h"
#include "Util.h"
#include "JniLocalRef.h"

using namespace std;
using namespace ns;

void CallbackHandlers::Init() {
    JEnv env;

    JAVA_LANG_STRING = env.FindClass("java/lang/String");
    assert(JAVA_LANG_STRING != nullptr);

    RUNTIME_CLASS = env.FindClass("org/nativescript/runtime/napi/Runtime");
    assert(RUNTIME_CLASS != nullptr);

//    RESOLVE_CLASS_METHOD_ID = env.GetMethodID(RUNTIME_CLASS, "resolveClass",
//                                              "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;Z)Ljava/lang/Class;");
//    assert(RESOLVE_CLASS_METHOD_ID != nullptr);
//
//    CURRENT_OBJECTID_FIELD_ID = env.GetFieldID(RUNTIME_CLASS, "currentObjectId", "I");
//    assert(CURRENT_OBJECTID_FIELD_ID != nullptr);
//
//    MAKE_INSTANCE_STRONG_ID = env.GetMethodID(RUNTIME_CLASS, "makeInstanceStrong",
//                                              "(Ljava/lang/Object;I)V");
//    assert(MAKE_INSTANCE_STRONG_ID != nullptr);

    GET_TYPE_METADATA = env.GetStaticMethodID(RUNTIME_CLASS, "getTypeMetadata",
                                              "(Ljava/lang/String;I)[Ljava/lang/String;");
    assert(GET_TYPE_METADATA != nullptr);

//    ENABLE_VERBOSE_LOGGING_METHOD_ID = env.GetMethodID(RUNTIME_CLASS, "enableVerboseLogging",
//                                                       "()V");
//    assert(ENABLE_VERBOSE_LOGGING_METHOD_ID != nullptr);
//
//    DISABLE_VERBOSE_LOGGING_METHOD_ID = env.GetMethodID(RUNTIME_CLASS, "disableVerboseLogging",
//                                                        "()V");
//    assert(ENABLE_VERBOSE_LOGGING_METHOD_ID != nullptr);
//
//    INIT_WORKER_METHOD_ID = env.GetStaticMethodID(RUNTIME_CLASS, "initWorker",
//                                                  "(Ljava/lang/String;Ljava/lang/String;I)V");
//
//    assert(INIT_WORKER_METHOD_ID != nullptr);

//    MetadataNode::Init(isolate);

//    MethodCache::Init();
}

napi_value CallbackHandlers::FindClass(napi_env env, const char *name) {
    napi_value clazz = nullptr;
    JEnv jEnv;
    jclass javaClass = jEnv.FindClass(name);
    if (jEnv.ExceptionCheck() == JNI_FALSE) {
        auto runtime = Runtime::GetRuntime(env);
        auto objectManager = runtime->GetObjectManager();

        jint javaObjectID = objectManager->GetOrCreateObjectId(javaClass);
        clazz = objectManager->GetJsObjectByJavaObject(javaObjectID);

        if (clazz == nullptr) {
            clazz = objectManager->CreateJSWrapper(javaObjectID, "Ljava/lang/Class;", javaClass);
        }
    }
    return clazz;
}

vector<string> CallbackHandlers::GetTypeMetadata(const string &name, int index) {
    JEnv env;

    string canonicalName = Util::ConvertFromJniToCanonicalName(name);

    JniLocalRef className(env.NewStringUTF(canonicalName.c_str()));
    jint idx = index;

    JniLocalRef pubApi(
            env.CallStaticObjectMethod(RUNTIME_CLASS, GET_TYPE_METADATA, (jstring) className, idx));

    jsize length = env.GetArrayLength(pubApi);

    assert(length > 0);

    vector<string> result;

    for (jsize i = 0; i < length; i++) {
        JniLocalRef s(env.GetObjectArrayElement(pubApi, i));
        const char *pc = env.GetStringUTFChars(s, nullptr);
        result.push_back(string(pc));
        env.ReleaseStringUTFChars(s, pc);
    }

    return result;
}

short CallbackHandlers::MAX_JAVA_STRING_ARRAY_LENGTH = 100;
jclass CallbackHandlers::RUNTIME_CLASS = nullptr;
jclass CallbackHandlers::JAVA_LANG_STRING = nullptr;
jfieldID CallbackHandlers::CURRENT_OBJECTID_FIELD_ID = nullptr;
jmethodID CallbackHandlers::RESOLVE_CLASS_METHOD_ID = nullptr;
jmethodID CallbackHandlers::MAKE_INSTANCE_STRONG_ID = nullptr;
jmethodID CallbackHandlers::GET_TYPE_METADATA = nullptr;
jmethodID CallbackHandlers::ENABLE_VERBOSE_LOGGING_METHOD_ID = nullptr;
jmethodID CallbackHandlers::DISABLE_VERBOSE_LOGGING_METHOD_ID = nullptr;
jmethodID CallbackHandlers::INIT_WORKER_METHOD_ID = nullptr;
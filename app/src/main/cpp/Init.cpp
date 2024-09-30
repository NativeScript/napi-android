#include <android/log.h>
#include "Runtime.h"
#include "NativeScriptException.h"
#include "CallbackHandlers.h"
#include <sstream>

using namespace std;
using namespace ns;

extern "C" JNIEXPORT void JNICALL
Java_org_nativescript_runtime_napi_MainActivity_startNAPIRuntime(JNIEnv* env, jobject obj, jstring filesPath) {
    int runtime_id = 1;
    Runtime::Init(env, obj, runtime_id, filesPath);
    auto *rt = Runtime::GetRuntime(runtime_id);

    rt->RunScript(env, obj, filesPath);
}

jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    Runtime::Init(vm);
    return JNI_VERSION_1_6;
}

Runtime* TryGetRuntime(int runtimeId) {
    Runtime* runtime = nullptr;
    try {
        runtime = Runtime::GetRuntime(runtimeId);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(runtime->GetNapiEnv());
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(runtime->GetNapiEnv());
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(runtime->GetNapiEnv());
    }
    return runtime;
}

extern "C" JNIEXPORT jobject Java_org_nativescript_runtime_napi_Runtime_callJSMethodNative(JNIEnv* _env, jobject obj, jint runtimeId, jint javaObjectID, jstring methodName, jint retType, jboolean isConstructor, jobjectArray packagedArgs) {
    jobject result = nullptr;

    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return result;
    }

    try {
        result = runtime->CallJSMethodNative(_env, obj, javaObjectID, methodName, retType, isConstructor, packagedArgs);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava( runtime->GetNapiEnv());
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava( runtime->GetNapiEnv());
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava( runtime->GetNapiEnv());
    }
    return result;
}

extern "C" JNIEXPORT void Java_org_nativescript_runtime_napi_Runtime_createJSInstanceNative(JNIEnv* _env, jobject obj, jint runtimeId, jobject javaObject, jint javaObjectID, jstring className) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }

    try {
        runtime->CreateJSInstanceNative(_env, obj, javaObject, javaObjectID, className);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava( runtime->GetNapiEnv());
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava( runtime->GetNapiEnv());
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava( runtime->GetNapiEnv());
    }
}

extern "C" JNIEXPORT jint Java_org_nativescript_runtime_napi_Runtime_generateNewObjectId(JNIEnv* env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return 0;
    }
    try {

        return runtime->GenerateNewObjectId(env, obj);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava( runtime->GetNapiEnv());
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava( runtime->GetNapiEnv());
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava( runtime->GetNapiEnv());
    }
    // this is only to avoid warnings, we should never come here
    return 0;
}

extern "C" JNIEXPORT jboolean Java_org_nativescript_runtime_napi_Runtime_notifyGc(JNIEnv* env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return JNI_FALSE;
    }

//    jboolean success = runtime->NotifyGC(env, obj) ? JNI_TRUE : JNI_FALSE;
    return success;
}

extern "C" JNIEXPORT void Java_org_nativescript_runtime_napi_Runtime_lock(JNIEnv* env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime != nullptr) {
//        runtime->Lock();
    }
}

extern "C" JNIEXPORT void Java_org_nativescript_runtime_napi_Runtime_unlock(JNIEnv* env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime != nullptr) {
//        runtime->Unlock();
    }
}

extern "C" JNIEXPORT void Java_org_nativescript_runtime_napi_Runtime_passExceptionToJsNative(JNIEnv* env, jobject obj, jint runtimeId, jthrowable exception, jstring message, jstring fullStackTrace, jstring jsStackTrace, jboolean isDiscarded) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }


    try {
        runtime->PassExceptionToJsNative(env, obj, exception, message, fullStackTrace, jsStackTrace, isDiscarded);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(runtime->GetNapiEnv());
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(runtime->GetNapiEnv());
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(runtime->GetNapiEnv());
    }
}

extern "C" JNIEXPORT jint Java_org_nativescript_runtime_napi_Runtime_getPointerSize(JNIEnv* env, jobject obj) {
    return sizeof(void*);
}


extern "C" JNIEXPORT jint Java_org_nativescript_runtime_napi_Runtime_getCurrentRuntimeId(JNIEnv* _env, jobject obj) {
//    Isolate* isolate = Isolate::TryGetCurrent();
//    if (isolate == nullptr) {
//        return -1;
//    }
//
//    Runtime* runtime = Runtime::GetRuntime(isolate);
//    int id = runtime->GetId();

    return 0;
}


extern "C" JNIEXPORT void Java_org_nativescript_runtime_napi_Runtime_ResetDateTimeConfigurationCache(JNIEnv* _env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }

//    auto isolate = runtime->GetIsolate();
//    isolate->DateTimeConfigurationChangeNotification(Isolate::TimeZoneDetection::kRedetect);
}
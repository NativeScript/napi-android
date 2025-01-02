#include "Runtime.h"
#include "NativeScriptException.h"
#include "CallbackHandlers.h"
#include <sstream>

#ifdef __HERMES__
#include <fbjni/fbjni.h>
#endif

using namespace std;
using namespace tns;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    try {
#ifdef __HERMES__
        facebook::jni::initialize(vm, [] {
            DEBUG_WRITE("fbjni::loaded");
        });
#endif
        Runtime::Init(vm);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(nullptr);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(nullptr);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(nullptr);
    }
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_SetManualInstrumentationMode(JNIEnv* _env, jclass obj, jstring mode) {
    try {
        Runtime::SetManualInstrumentationMode(mode);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(nullptr);
    }
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_initNativeScript(JNIEnv* _env, jobject obj, jint runtimeId, jstring filesPath, jstring nativeLibDir, jboolean verboseLoggingEnabled, jboolean isDebuggable, jstring packageName, jobjectArray args, jstring callingDir, jint maxLogcatObjectSize, jboolean forceLog) {
    try {
        DEBUG_WRITE("NativeScript Initializing!");
        Runtime::Init(_env, obj, runtimeId, filesPath, nativeLibDir, verboseLoggingEnabled, isDebuggable, packageName, args, callingDir, maxLogcatObjectSize, forceLog);
        DEBUG_WRITE("NativeScript Initialized!");
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(nullptr);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(nullptr);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(nullptr);
    }
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

extern "C" JNIEXPORT void Java_com_tns_Runtime_runModule(JNIEnv* _env, jobject obj, jint runtimeId, jstring scriptFile) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }

    try {
        runtime->RunModule(_env, obj, scriptFile);
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

extern "C" JNIEXPORT void Java_com_tns_Runtime_runWorker(JNIEnv* _env, jobject obj, jint runtimeId, jstring scriptFile) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }

    try {
        runtime->RunWorker(scriptFile);
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

extern "C" JNIEXPORT jobject Java_com_tns_Runtime_runScript(JNIEnv* _env, jobject obj, jint runtimeId, jstring scriptFile) {
    jobject result = nullptr;

    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return result;
    }

    napi_env napiEnv = runtime->GetNapiEnv();
    try {
        result = runtime->RunScript(_env, obj, scriptFile);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(napiEnv);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(napiEnv);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(napiEnv);
    }

    return result;
}

extern "C" JNIEXPORT jobject Java_com_tns_Runtime_callJSMethodNative(JNIEnv* _env, jobject obj, jint runtimeId, jint javaObjectID, jstring methodName, jint retType, jboolean isConstructor, jobjectArray packagedArgs) {
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


extern "C" JNIEXPORT void Java_com_tns_Runtime_createJSInstanceNative(JNIEnv* _env, jobject obj, jint runtimeId, jobject javaObject, jint javaObjectID, jstring className) {
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

extern "C" JNIEXPORT jint Java_com_tns_Runtime_generateNewObjectId(JNIEnv* env, jobject obj, jint runtimeId) {
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

extern "C" JNIEXPORT jboolean Java_com_tns_Runtime_notifyGc(JNIEnv* jEnv, jobject obj, jint runtimeId, jintArray object_ids) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return JNI_FALSE;
    }
    auto env = runtime->GetNapiEnv();

    runtime->NotifyGC(jEnv, obj, object_ids);

    return true;
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_lock(JNIEnv* env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime != nullptr) {
       runtime->Lock();
    }
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_unlock(JNIEnv* env, jobject obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime != nullptr) {
        runtime->Unlock();
    }
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_passExceptionToJsNative(JNIEnv* jEnv, jobject obj, jint runtimeId, jthrowable exception, jstring message, jstring fullStackTrace, jstring jsStackTrace, jboolean isDiscarded) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }

    auto env = runtime->GetNapiEnv();

    try {
        runtime->PassExceptionToJsNative(jEnv, obj, exception, message, fullStackTrace, jsStackTrace, isDiscarded);
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

extern "C" JNIEXPORT jint Java_com_tns_Runtime_getPointerSize(JNIEnv* env, jclass obj) {
    return sizeof(void*);
}

extern "C" JNIEXPORT jint Java_com_tns_Runtime_getCurrentRuntimeId(JNIEnv* _env, jclass obj) {
    auto rt = Runtime::Current();
    if (rt == nullptr) {
        return -1;
    }
    return rt->GetId();
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_WorkerGlobalOnMessageCallback(JNIEnv* env, jclass obj, jint runtimeId, jstring msg) {
    // Worker Thread runtime
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        // TODO: Pete: Log message informing the developer of the failure
        DEBUG_WRITE("WorkerGlobalOnMessageCallback: worker runtime not loaded.");
        return;
    }

    CallbackHandlers::WorkerGlobalOnMessageCallback(runtime->GetNapiEnv(), msg);
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_WorkerObjectOnMessageCallback(JNIEnv* env, jclass obj, jint runtimeId, jint workerId, jstring msg) {
    // Main Thread runtime
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
         DEBUG_WRITE("WorkerObjectOnMessageCallback: worker runtime not loaded.");
        return;
    }

    CallbackHandlers::WorkerObjectOnMessageCallback(runtime->GetNapiEnv(), workerId, msg);
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_TerminateWorkerCallback(JNIEnv* env, jclass obj, jint runtimeId) {
    // Worker Thread runtime
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        DEBUG_WRITE("TerminateWorkerCallback: trying to call terminate before worker is loaded.");
        return;
    }
    auto napiEnv = runtime->GetNapiEnv();
    CallbackHandlers::TerminateWorkerThread(napiEnv);
    delete runtime;
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_ClearWorkerPersistent(JNIEnv* env, jclass obj, jint runtimeId, jint workerId) {
    // Worker Thread runtime
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
         DEBUG_WRITE("ClearWorkerPersistent: trying to call before worker is loaded.");
         return;
    }

    CallbackHandlers::ClearWorkerPersistent(runtime->GetNapiEnv(), workerId);
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_CallWorkerObjectOnErrorHandleMain(JNIEnv* env, jclass obj, jint runtimeId, jint workerId, jstring message, jstring stackTrace, jstring filename, jint lineno, jstring threadName) {
    // Main Thread runtime
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
         DEBUG_WRITE("CallWorkerObjectOnErrorHandleMain: trying to call before worker is loaded.");
         return;
    }

    try {
        CallbackHandlers::CallWorkerObjectOnErrorHandle(runtime->GetNapiEnv(), workerId, message, stackTrace, filename, lineno, threadName);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(runtime->GetNapiEnv());
    }
}

extern "C" JNIEXPORT void Java_com_tns_Runtime_ResetDateTimeConfigurationCache(JNIEnv* _env, jclass obj, jint runtimeId) {
    auto runtime = TryGetRuntime(runtimeId);
    if (runtime == nullptr) {
        return;
    }
}
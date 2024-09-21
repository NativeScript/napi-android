#include <android/log.h>
#include "Runtime.h"

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


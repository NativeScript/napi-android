#include <android/log.h>
#include <jni.h>
#include <string>
#include "quickjs/quickjs.h"
#include "js_native_api.h"

#define print(fmt, expr) \
__android_log_print(ANDROID_LOG_INFO, "NAPI", fmt, expr);

#define GUARD(expr) \
status = expr; \
if (status != 0) { \
__android_log_print(ANDROID_LOG_INFO, "NAPI", "Call Returned Error %d", status); \
}

JavaVM* g_vm = nullptr;
jobject g_obj = nullptr;

extern "C" JNIEXPORT jstring JNICALL
Java_org_nativescript_runtime_napi_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

napi_value CallJavaMethod(napi_env env, napi_callback_info info) {
    JNIEnv* jni_env;
    g_vm->AttachCurrentThread(&jni_env, nullptr);
    jclass cls = jni_env->GetObjectClass(g_obj);
    jmethodID mid = jni_env->GetMethodID(cls, "javaMethod", "()V");

    if (mid != nullptr) {
        jni_env->CallVoidMethod(g_obj, mid);
    }

    return nullptr;
}

napi_value Log(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc > 0) {
        napi_value str;
        napi_coerce_to_string(env, args[0], &str);

        size_t str_size;
        napi_get_value_string_utf8(env, str, nullptr, 0, &str_size);

        char *buffer = new char[str_size + 1];
        napi_get_value_string_utf8(env, str, buffer, str_size + 1, &str_size);
        print("%s", buffer)
        delete[] buffer;
    }

    return nullptr;
}

napi_value Now(napi_env env, napi_callback_info info) {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    napi_value result;
    napi_create_double(env, static_cast<double>(ms), &result);
    return result;
}



void StartNAPIRuntime() {

    napi_runtime rt;
    napi_env env;
    napi_handle_scope globalScope;

    int status;

    NAPICreateJSRuntime(&rt);
    NAPICreateEnv(&env, rt);
    napi_open_handle_scope(env, &globalScope);

    napi_value global;
    napi_get_global(env, &global);

    napi_value console;
    napi_create_object(env, &console);

    napi_value log_function;
    napi_create_function(env, "log", NAPI_AUTO_LENGTH, Log, nullptr, &log_function);

    napi_set_named_property(env, console, "log", log_function);
    napi_set_named_property(env, global, "console", console);

    napi_value call_java_method;

    napi_create_function(env, "callJavaMethod", NAPI_AUTO_LENGTH,
                         &CallJavaMethod, nullptr, &call_java_method);

    napi_set_named_property(env, global, "callJavaMethod", call_java_method);

    napi_value performance;
    napi_create_object(env, &performance);

    napi_value now_function;
    napi_create_function(env, "now", NAPI_AUTO_LENGTH, Now, nullptr, &now_function);

    napi_set_named_property(env, performance, "now", now_function);
    napi_set_named_property(env, global, "performance", performance);


    napi_value napi_source;

    auto code = ""
                "console.log(\"Hello World from QuickJS\");"
                "for (let i=0;i<2;i++) {"
                "const now = performance.now();"
                "callJavaMethod();"
                "console.log(performance.now() - now);}";

    GUARD(napi_create_string_utf8(env, code, strlen(code), &napi_source))

    napi_value result;
    status = napi_run_script(env, napi_source, &result);

    if (status != napi_ok) {
        print("%s", "Script has thrown an exception")
        const napi_extended_error_info *info;
        napi_get_last_error_info(env, &info);
        print("%s", info->error_message)
    }


}

extern "C" JNIEXPORT void JNICALL
Java_org_nativescript_runtime_napi_MainActivity_callFromJS(JNIEnv* env, jobject obj) {
    // Store the Java VM and object reference
    env->GetJavaVM(&g_vm);
    g_obj = env->NewGlobalRef(obj);

    StartNAPIRuntime();
}

jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    // Run QuickJS and print to the console
    g_vm = vm;

    // Return the JNI version
    return JNI_VERSION_1_6;
}
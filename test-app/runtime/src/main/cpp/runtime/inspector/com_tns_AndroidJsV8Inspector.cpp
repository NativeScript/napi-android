#include "JEnv.h"
#ifdef APPLICATION_IN_DEBUG
#include "JsV8InspectorClient.h"
#endif

#include <sstream>

using namespace tns;
using namespace std;

JNIEXPORT extern "C" void Java_com_tns_AndroidJsV8Inspector_init(JNIEnv* env, jobject object) {
#ifdef APPLICATION_IN_DEBUG
    JsV8InspectorClient::GetInstance()->init();
#endif
}

JNIEXPORT extern "C" void Java_com_tns_AndroidJsV8Inspector_connect(JNIEnv* env, jobject instance, jobject connection) {
#ifdef APPLICATION_IN_DEBUG
    JsV8InspectorClient::GetInstance()->disconnect();
    JsV8InspectorClient::GetInstance()->connect(connection);
#endif
}

JNIEXPORT extern "C" void Java_com_tns_AndroidJsV8Inspector_scheduleBreak(JNIEnv* env, jobject instance) {
#ifdef APPLICATION_IN_DEBUG
    JsV8InspectorClient::GetInstance()->scheduleBreak();
#endif
}

JNIEXPORT extern "C" void Java_com_tns_AndroidJsV8Inspector_disconnect(JNIEnv* env, jobject instance) {
#ifdef APPLICATION_IN_DEBUG
    JsV8InspectorClient::GetInstance()->disconnect();
#endif
}

JNIEXPORT extern "C" void Java_com_tns_AndroidJsV8Inspector_dispatchMessage(JNIEnv* env, jobject instance, jstring jMessage) {
#ifdef APPLICATION_IN_DEBUG
    std::string message = tns::jstringToString(jMessage);
    JsV8InspectorClient::GetInstance()->dispatchMessage(message);
#endif
}

#include "JEnv.h"
#include <sstream>

using namespace tns;
using namespace std;

JNIEXPORT extern "C" void Java_com_tns_AndroidJsV8Inspector_init(JNIEnv *env, jobject object) {
}

JNIEXPORT extern "C" void
Java_com_tns_AndroidJsV8Inspector_connect(JNIEnv *env, jobject instance, jobject connection) {
}

JNIEXPORT extern "C" void
Java_com_tns_AndroidJsV8Inspector_scheduleBreak(JNIEnv *env, jobject instance) {
}

JNIEXPORT extern "C" void
Java_com_tns_AndroidJsV8Inspector_disconnect(JNIEnv *env, jclass instance) {
}

JNIEXPORT extern "C" void
Java_com_tns_AndroidJsV8Inspector_dispatchMessage(JNIEnv *env, jobject instance, jstring jMessage) {

}

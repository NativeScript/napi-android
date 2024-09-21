#include "jni/Util.h"
#include "NativeScriptException.h"
#include "NativeScriptAssert.h"
#include <sstream>

using namespace std;
using namespace ns;

NativeScriptException::NativeScriptException(JEnv& env)
    :
    m_javascriptException(nullptr) {
    m_javaException = JniLocalRef(env.ExceptionOccurred());
    env.ExceptionClear();
}

NativeScriptException::NativeScriptException(const string& message)
    :
    m_javascriptException(nullptr), m_javaException(JniLocalRef()), m_message(message) {
}

NativeScriptException::NativeScriptException(const string& message, const string& stackTrace)
    :
    m_javascriptException(nullptr), m_javaException(JniLocalRef()), m_message(message), m_stackTrace(stackTrace) {
}


string NativeScriptException::GetExceptionMessage(JEnv& env, jthrowable exception) {
    string errMsg;
    JniLocalRef msg(env.CallStaticObjectMethod(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_GET_MESSAGE_METHOD_ID, exception));

    const char* msgStr = env.GetStringUTFChars(msg, nullptr);

    errMsg.append(msgStr);

    env.ReleaseStringUTFChars(msg, msgStr);

    return errMsg;
}

string NativeScriptException::GetExceptionStackTrace(JEnv& env, jthrowable exception) {
    string errStackTrace;
    JniLocalRef msg(env.CallStaticObjectMethod(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_GET_STACK_TRACE_AS_STRING_METHOD_ID, exception));

    const char* msgStr = env.GetStringUTFChars(msg, nullptr);

    errStackTrace.append(msgStr);

    env.ReleaseStringUTFChars(msg, msgStr);

    return errStackTrace;
}

jclass NativeScriptException::RUNTIME_CLASS = nullptr;
jclass NativeScriptException::THROWABLE_CLASS = nullptr;
jclass NativeScriptException::NATIVESCRIPTEXCEPTION_CLASS = nullptr;
jmethodID NativeScriptException::NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID = nullptr;
jmethodID NativeScriptException::NATIVESCRIPTEXCEPTION_THROWABLE_CTOR_ID = nullptr;
jmethodID NativeScriptException::NATIVESCRIPTEXCEPTION_GET_MESSAGE_METHOD_ID = nullptr;
jmethodID NativeScriptException::NATIVESCRIPTEXCEPTION_GET_STACK_TRACE_AS_STRING_METHOD_ID = nullptr;
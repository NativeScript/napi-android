#include "Util.h"
#include "NativeScriptException.h"
#include "ArgConverter.h"
#include "NativeScriptAssert.h"
#include "Runtime.h"
#include <sstream>

using namespace std;
using namespace tns;

NativeScriptException::NativeScriptException(JEnv& env)
    : m_javascriptException(nullptr) {
    jthrowable  thrw = env.ExceptionOccurred();
    m_javaException = JniLocalRef(thrw);
    env.ExceptionClear();
    DEBUG_WRITE("%s, %s", GetExceptionMessage(env, m_javaException).c_str(), GetExceptionStackTrace(env, m_javaException).c_str());
}

NativeScriptException::NativeScriptException(const string& message)
    : m_javascriptException(nullptr), m_javaException(JniLocalRef()), m_message(message) {

    DEBUG_WRITE("%s", m_message.c_str());
}

NativeScriptException::NativeScriptException(const string& message, const string& stackTrace)
    : m_javascriptException(nullptr), m_javaException(JniLocalRef()), m_message(message), m_stackTrace(stackTrace) {

    DEBUG_WRITE("%s, %s ", m_message.c_str(), m_stackTrace.c_str());
}

NativeScriptException::NativeScriptException(napi_env env, napi_value error, const string& message)
    : m_javaException(JniLocalRef()) {
    m_javascriptException = nullptr;
    napi_create_reference(env, error, 1, &m_javascriptException);
    m_message = GetErrorMessage(env, error, message);
    m_stackTrace = GetErrorStackTrace(env, error);
    m_fullMessage = GetFullMessage(env, error, m_message);
}

void NativeScriptException::ReThrowToNapi(napi_env env) {
    napi_value errObj;

    if (m_javascriptException != nullptr) {
        napi_get_reference_value(env, m_javascriptException, &errObj);
        if (napi_util::is_of_type(env, errObj, napi_object)) {
            if (!m_fullMessage.empty()) {
                napi_set_named_property(env, errObj, "fullMessage", ArgConverter::convertToJsString(env, m_fullMessage));
            } else if (!m_message.empty()) {
                 napi_set_named_property(env, errObj, "fullMessage", ArgConverter::convertToJsString(env, m_message));
            }
        }
    } else if (!m_fullMessage.empty()) {
        napi_create_error(env, nullptr, ArgConverter::convertToJsString(env, m_fullMessage), &errObj);
    } else if (!m_message.empty()) {
        napi_create_error(env, nullptr, ArgConverter::convertToJsString(env, m_message), &errObj);
    } else if (!m_javaException.IsNull()) {
        errObj = WrapJavaToJsException(env);
    } else {
        napi_create_error(env, nullptr, ArgConverter::convertToJsString(env, "No javascript exception or message provided."), &errObj);
    }

    napi_throw(env, errObj);

//    JSLeave
}

void NativeScriptException::ReThrowToJava(napi_env env) {
    if (env) {
        NapiScope scope(env);
    }
    jthrowable ex = nullptr;
    JEnv jEnv;

    if (!m_javaException.IsNull()) {
        std::string excClassName;
        if (env) {
            auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
            excClassName = objectManager->GetClassName((jobject)m_javaException);
        }

        if (excClassName == "com/tns/NativeScriptException") {
            ex = m_javaException;
        } else {
            JniLocalRef msg(jEnv.NewStringUTF("Java Error!"));
            JniLocalRef stack(jEnv.NewStringUTF(""));
            ex = static_cast<jthrowable>(jEnv.NewObject(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_THROWABLE_CTOR_ID, (jstring)msg, (jstring)stack, (jobject)m_javaException));
        }
    } else if (m_javascriptException != nullptr && env != nullptr) {
        napi_value errObj;
        napi_get_reference_value(env, m_javascriptException, &errObj);
        if (napi_util::is_of_type(env, errObj, napi_object)) {
            auto exObj = TryGetJavaThrowableObject(jEnv, env, errObj);
            ex = (jthrowable)exObj.Move();
        }

        JniLocalRef msg(jEnv.NewStringUTF(m_message.c_str()));
        JniLocalRef stackTrace(jEnv.NewStringUTF(m_stackTrace.c_str()));

        if (ex == nullptr) {
            ex = static_cast<jthrowable>(jEnv.NewObject(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID, (jstring)msg, (jstring)stackTrace, reinterpret_cast<jlong>(m_javascriptException)));
        } else {
            auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
            auto excClassName = objectManager->GetClassName(ex);
            if (excClassName != "com/tns/NativeScriptException") {
                ex = static_cast<jthrowable>(jEnv.NewObject(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_THROWABLE_CTOR_ID, (jstring)msg, (jstring)stackTrace, ex));
            }
        }
    } else if (!m_message.empty()) {
        JniLocalRef msg(jEnv.NewStringUTF(m_message.c_str()));
        JniLocalRef stackTrace(jEnv.NewStringUTF(m_stackTrace.c_str()));
        ex = static_cast<jthrowable>(jEnv.NewObject(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID, (jstring)msg, (jstring)stackTrace, (jlong)0));
    } else {
        JniLocalRef msg(jEnv.NewStringUTF("No java exception or message provided."));
         ex = static_cast<jthrowable>(jEnv.NewObject(NATIVESCRIPTEXCEPTION_CLASS, NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID, (jstring)msg, (jstring)nullptr, (jlong)0));
    }
    jEnv.Throw(ex);
}

void NativeScriptException::Init() {
    JEnv jenv;

    RUNTIME_CLASS = jenv.FindClass("com/tns/Runtime");
    assert(RUNTIME_CLASS != nullptr);

    THROWABLE_CLASS = jenv.FindClass("java/lang/Throwable");
    assert(THROWABLE_CLASS != nullptr);

    NATIVESCRIPTEXCEPTION_CLASS = jenv.FindClass("com/tns/NativeScriptException");
    assert(NATIVESCRIPTEXCEPTION_CLASS != nullptr);

    NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID = jenv.GetMethodID(NATIVESCRIPTEXCEPTION_CLASS, "<init>", "(Ljava/lang/String;Ljava/lang/String;J)V");
    assert(NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID != nullptr);

    NATIVESCRIPTEXCEPTION_THROWABLE_CTOR_ID = jenv.GetMethodID(NATIVESCRIPTEXCEPTION_CLASS, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)V");
    assert(NATIVESCRIPTEXCEPTION_THROWABLE_CTOR_ID != nullptr);

    NATIVESCRIPTEXCEPTION_GET_STACK_TRACE_AS_STRING_METHOD_ID = jenv.GetStaticMethodID(NATIVESCRIPTEXCEPTION_CLASS, "getStackTraceAsString", "(Ljava/lang/Throwable;)Ljava/lang/String;");
    assert(NATIVESCRIPTEXCEPTION_GET_STACK_TRACE_AS_STRING_METHOD_ID != nullptr);

    NATIVESCRIPTEXCEPTION_GET_MESSAGE_METHOD_ID = jenv.GetStaticMethodID(NATIVESCRIPTEXCEPTION_CLASS, "getMessage", "(Ljava/lang/Throwable;)Ljava/lang/String;");
    assert(NATIVESCRIPTEXCEPTION_GET_MESSAGE_METHOD_ID != nullptr);
}

// ON N-API UNCAUGHT EXCEPTION
void NativeScriptException::OnUncaughtError(napi_env env, napi_value error) {
    string errorMessage = GetErrorMessage(env, error);
    string stackTrace = GetErrorStackTrace(env, error);

    NativeScriptException e(errorMessage, stackTrace);
    e.ReThrowToJava(env);
}

void NativeScriptException::CallJsFuncWithErr(napi_env env, napi_value errObj, bool isDiscarded) {
    napi_value global;
    napi_get_global(env, &global);

    napi_value handler = nullptr;
    if (isDiscarded) {
        napi_get_named_property(env, global, "__onDiscardedError", &handler);
    } else {
        napi_get_named_property(env, global, "__onUncaughtError", &handler);
    }

    if (napi_util::is_of_type(env, handler, napi_function)) {
        napi_value result;
        napi_call_function(env, global, handler, 1, &errObj, &result);
    }
}

napi_value NativeScriptException::WrapJavaToJsException(napi_env env) {
    napi_value errObj;

    JEnv jenv;

    auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();

    string excClassName = objectManager->GetClassName((jobject)m_javaException);
    if (excClassName == "com/tns/NativeScriptException") {
        jfieldID fieldID = jenv.GetFieldID(jenv.GetObjectClass(m_javaException), "jsValueAddress", "J");
        jlong addr = jenv.GetLongField(m_javaException, fieldID);

        if (addr != 0) {
            auto pv = reinterpret_cast<napi_ref>(addr);
            napi_get_reference_value(env, pv, &errObj);
            napi_delete_reference(env, pv);
        } else {
            errObj = GetJavaExceptionFromEnv(env, m_javaException, jenv);
        }
    } else {
        errObj = GetJavaExceptionFromEnv(env, m_javaException, jenv);
    }

    return errObj;
}

napi_value NativeScriptException::GetJavaExceptionFromEnv(napi_env env, const JniLocalRef& exc, JEnv& jenv) {
    auto errMsg = GetExceptionMessage(jenv, exc);
    auto stackTrace = GetExceptionStackTrace(jenv, exc);
    DEBUG_WRITE("Error during java interop errorMessage: %s\n stackTrace:\n %s", errMsg.c_str(), stackTrace.c_str());

    auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();

    napi_value msg = ArgConverter::convertToJsString(env, errMsg);
    napi_value errObj;
    napi_value code = ArgConverter::convertToJsString(env, "0", 1);
    napi_create_error(env, code, msg, &errObj);

    jint javaObjectID = objectManager->GetOrCreateObjectId((jobject)exc);
    auto nativeExceptionObject = objectManager->GetJsObjectByJavaObject(javaObjectID);

    if (napi_util::is_null_or_undefined(env, nativeExceptionObject)) {
        string className = objectManager->GetClassName((jobject)exc);
        nativeExceptionObject = objectManager->CreateJSWrapper(javaObjectID, className);
    }

    napi_set_named_property(env, errObj, "nativeException", nativeExceptionObject);

    string jsStackTraceMessage = GetErrorStackTrace(env, errObj);
    napi_set_named_property(env, errObj, "stack", ArgConverter::convertToJsString(env, jsStackTraceMessage));
    napi_set_named_property(env, errObj, "stackTrace", ArgConverter::convertToJsString(env, jsStackTraceMessage + stackTrace) );

    return errObj;
}

string NativeScriptException::GetFullMessage(napi_env env, napi_value error, const string& jsExceptionMessage) {
    bool isError;
    napi_is_error(env, error, &isError);
    if (!isError) {
        return jsExceptionMessage;
    }

    stringstream ss;
    ss << jsExceptionMessage;

    string stackTraceMessage = GetErrorStackTrace(env, error);

    ss << endl << "StackTrace: " << endl << stackTraceMessage << endl;

    string loggedMessage = ss.str();

    PrintErrorMessage(loggedMessage);

    return loggedMessage;
}

JniLocalRef NativeScriptException::TryGetJavaThrowableObject(JEnv& env, napi_env napiEnv, napi_value jsObj) {
    JniLocalRef javaThrowableObject;

    auto objectManager = Runtime::GetRuntime(napiEnv)->GetObjectManager();

    auto javaObj = objectManager->GetJavaObjectByJsObject(jsObj);
    JniLocalRef objClass;

    if (!javaObj.IsNull()) {
        objClass = JniLocalRef(env.GetObjectClass(javaObj));
    } else {
        napi_value nativeEx;
        napi_get_named_property(napiEnv, jsObj, "nativeException", &nativeEx);
        if (napi_util::is_object(napiEnv, nativeEx)) {
            javaObj = objectManager->GetJavaObjectByJsObject(nativeEx);
            objClass = JniLocalRef(env.GetObjectClass(javaObj));
        }
    }

    auto isThrowable = !objClass.IsNull() ? env.IsAssignableFrom(objClass, THROWABLE_CLASS) : JNI_FALSE;

    if (isThrowable == JNI_TRUE) {
        javaThrowableObject = JniLocalRef(env.NewLocalRef(javaObj));
    }

    return javaThrowableObject;
}

void NativeScriptException::PrintErrorMessage(const string& errorMessage) {
    stringstream ss(errorMessage);
    string line;
    while (getline(ss, line, '\n')) {
        DEBUG_WRITE("%s", line.c_str());
    }
}

string NativeScriptException::GetErrorMessage(napi_env env, napi_value error, const string& prependMessage) {
    bool isError;
    napi_is_error(env, error, &isError);

    if (!isError) {
        napi_value err;
        napi_coerce_to_string(env, error, &err);
        return napi_util::get_string_value(env, err);
    }

    napi_value message;
    napi_get_named_property(env, error, "message", &message);

    string mes = ArgConverter::ConvertToString(env, message);

    stringstream ss;

    if (!prependMessage.empty()) {
        ss << prependMessage << endl;
    }

    string errMessage;
    bool hasFullErrorMessage = false;
    napi_value fullMessage;
    napi_get_named_property(env, error, "fullMessage", &fullMessage);
    if (napi_util::is_of_type(env, fullMessage, napi_string)) {
        hasFullErrorMessage = true;
        errMessage = ArgConverter::ConvertToString(env, fullMessage);
        ss << errMessage;
    }

    if (!mes.empty()) {
        if (hasFullErrorMessage) {
            ss << endl;
        }
        ss << mes;
    }

    return ss.str();
}

string NativeScriptException::GetErrorStackTrace(napi_env env, napi_value error) {
    stringstream ss;

    bool isError;
    napi_is_error(env, error, &isError);
    if (!isError) return "";

    napi_value stack;
    napi_get_named_property(env, error, "stack", &stack);


    string stackStr = ArgConverter::ConvertToString(env, stack);
    ss << stackStr;

    return ss.str();
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
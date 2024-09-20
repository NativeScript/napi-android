#ifndef NATIVESCRIPTEXCEPTION_H_
#define NATIVESCRIPTEXCEPTION_H_

#include "jni/JEnv.h"
#include "jni/JniLocalRef.h"
#include "js_native_api.h"

namespace tns {
class NativeScriptException {
    public:
        /*
         * Generates a NativeScriptException with java error from environment
         */
        NativeScriptException(JEnv& env);

        /*
         * Generates a NativeScriptException with given message
         */
        NativeScriptException(const std::string& message);

        /*
         * Generates a NativeScriptException with given message and stackTrace
         */
        NativeScriptException(const std::string& message, const std::string& stackTrace);



    private:

        /*
         * Gets java exception message from jthrowable
         */
        std::string GetExceptionMessage(JEnv& env, jthrowable exception);

        /*
         * Gets java exception stack trace from jthrowable
         */
        std::string GetExceptionStackTrace(JEnv& env, jthrowable exception);


        napi_value m_javascriptException;
        JniLocalRef m_javaException;
        std::string m_message;
        std::string m_stackTrace;
        std::string m_fullMessage;

        static jclass RUNTIME_CLASS;
        static jclass THROWABLE_CLASS;
        static jclass NATIVESCRIPTEXCEPTION_CLASS;
        static jmethodID NATIVESCRIPTEXCEPTION_JSVALUE_CTOR_ID;
        static jmethodID NATIVESCRIPTEXCEPTION_THROWABLE_CTOR_ID;
        static jmethodID NATIVESCRIPTEXCEPTION_GET_MESSAGE_METHOD_ID;
        static jmethodID NATIVESCRIPTEXCEPTION_GET_STACK_TRACE_AS_STRING_METHOD_ID;

        static void PrintErrorMessage(const std::string& errorMessage);
};
}

#endif /* NATIVESCRIPTEXCEPTION_H_ */

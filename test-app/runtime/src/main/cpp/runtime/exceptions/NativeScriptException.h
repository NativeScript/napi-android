#ifndef NATIVESCRIPTEXCEPTION_H_
#define NATIVESCRIPTEXCEPTION_H_

#include "js_native_api.h"
#include "JEnv.h"
#include "JniLocalRef.h"
#include "ObjectManager.h"

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

        /*
         * Generates a NativeScriptException with javascript error from napi_env and a prepend message if any
         */
        NativeScriptException(napi_env env, napi_value error, const std::string& message = "");

        void ReThrowToNapi(napi_env env);
        void ReThrowToJava(napi_env env);

        static void Init();

        /*
         * This handler is attached to Node-API to handle uncaught javascript exceptions.
         */
        static void OnUncaughtError(napi_env env, napi_value error);

        /*
         * Calls the global "__onUncaughtError" or "__onDiscardedError" if such is provided
         */
        static void CallJsFuncWithErr(napi_env env, napi_value errObj, bool isDiscarded);

    private:
        /*
         * Try to get native exception or NativeScriptException from js object
         */
        JniLocalRef TryGetJavaThrowableObject(JEnv& env, napi_env napiEnv, napi_value jsObj);

        /*
         * Gets java exception message from jthrowable
         */
        std::string GetExceptionMessage(JEnv& env, jthrowable exception);

        /*
         * Gets java exception stack trace from jthrowable
         */
        std::string GetExceptionStackTrace(JEnv& env, jthrowable exception);

        /*
         * Gets the member m_javaException, wraps it and creates a javascript error object from it
         */
        napi_value WrapJavaToJsException(napi_env env);

        /*
         * Gets all the information from a java exception and puts it in a javascript error object
         */
        napi_value GetJavaExceptionFromEnv(napi_env env, const JniLocalRef& exc, JEnv& jenv);

        /*
         * Gets all the information from a js message and an js error object and puts it in a string
         */
        static std::string GetErrorMessage(napi_env env, napi_value error, const std::string& prependMessage = "");

        /*
         * Generates string stack trace from js StackTrace
         */
        static std::string GetErrorStackTrace(napi_env env, napi_value stackTrace);

        /*
         *	Adds a prepend message to the normal message process
         */
        std::string GetFullMessage(napi_env env, napi_value error, const std::string& jsExceptionMessage);

        napi_ref m_javascriptException;
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
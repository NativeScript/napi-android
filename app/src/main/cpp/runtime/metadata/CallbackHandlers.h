//
// Created by Ammar Ahmed on 20/09/2024.
//

#ifndef TESTAPPNAPI_CALLBACKHANDLERS_H
#define TESTAPPNAPI_CALLBACKHANDLERS_H
#include "JEnv.h"
#include "Runtime.h"
#include <string>;


class CallbackHandlers {

public:
    static void Init();
    static std::vector<std::string> GetTypeMetadata(const std::string &name, int index);
    static napi_value FindClass(napi_env env, const char * name);

private:
    static short MAX_JAVA_STRING_ARRAY_LENGTH;

    static jclass RUNTIME_CLASS;

    static jclass JAVA_LANG_STRING;

    static jmethodID RESOLVE_CLASS_METHOD_ID;

    static jfieldID CURRENT_OBJECTID_FIELD_ID;

    static jmethodID MAKE_INSTANCE_STRONG_ID;

    static jmethodID GET_TYPE_METADATA;

    static jmethodID ENABLE_VERBOSE_LOGGING_METHOD_ID;

    static jmethodID DISABLE_VERBOSE_LOGGING_METHOD_ID;

    static jmethodID INIT_WORKER_METHOD_ID;
};


#endif //TESTAPPNAPI_CALLBACKHANDLERS_H

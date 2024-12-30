#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#include <string>
#include <unordered_map>
#include <js_native_api.h>

class Constants {
public:
    const static char CLASS_NAME_LOCATION_SEPARATOR = '_';
    static std::string APP_ROOT_FOLDER_PATH;
    static std::string V8_STARTUP_FLAGS;

    static napi_status Init(napi_env env);

    static Constants *Get(napi_env env);

    static void DeInit(napi_env env);

    napi_value extendValue;
    napi_value nullObjectValue;
    napi_value nullNodeNameValue;
    napi_value valueOfValue;
    napi_value classValue;
    napi_value privateTypeNameValue;
    napi_value classImplementationObjectValue;
    napi_value superValue;
    napi_value superValueValue;
    napi_value privateJsInfoValue;
    napi_value privateCallSuperValue;
    napi_value privateIsNapiValue;
    napi_value toStringValue;
    napi_value isPrototypeImplementationObjectValue;
    napi_value prototypeValue;
    napi_value constructorValue;
    napi_value nameValue;
    napi_value objectValue;
    napi_value numberValue;
    napi_value isIntegerValue;
    napi_value setPrototypeOf;
    napi_value stringValue;
    napi_value booleanValue;
    napi_value protoValue;
    napi_value valueValue;


private:
    Constants();

    static std::unordered_map<napi_env, Constants *> s_constantsMap;
};

#endif /* CONSTANTS_H_ */
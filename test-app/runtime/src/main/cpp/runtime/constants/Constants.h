#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#include <string>
#include <unordered_map>
#include <js_native_api.h>

#define PROP(name) \
    napi_ref name##ValueRef; \
    napi_value name##Value(napi_env env);

class Constants {
public:
    const static char CLASS_NAME_LOCATION_SEPARATOR = '_';
    static std::string APP_ROOT_FOLDER_PATH;
    static std::string V8_STARTUP_FLAGS;

    static napi_status Init(napi_env env);
    static Constants *Get(napi_env env);
    static void DeInit(napi_env env);

    PROP(extend)
    PROP(nullObject)
    PROP(nullNodeName)
    PROP(valueOf)
    PROP(cls)
    PROP(privateTypeName)
    PROP(classImplementationObject)
    PROP(super)
    PROP(superValue)
    PROP(privateJsInfo)
    PROP(privateCallSuper)
    PROP(privateIsNapi)
    PROP(toString)
    PROP(isPrototypeImplementationObject)
    PROP(prototype)
    PROP(constructor)
    PROP(name)
    PROP(object)
    PROP(number)
    PROP(isInteger)
    PROP(setPrototypeOf)
    PROP(string)
    PROP(boolean)
    PROP(proto)
    PROP(value)

private:
    Constants();

    static std::unordered_map<napi_env, Constants *> s_constantsMap;
};

#endif /* CONSTANTS_H_ */
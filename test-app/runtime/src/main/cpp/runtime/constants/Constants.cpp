#include "Constants.h"

#define  PROP_KEY_EXTEND "extend"
#define  PROP_KEY_NULLOBJECT "null"
#define  PROP_KEY_NULL_NODE_NAME "nullNode"
#define  PROP_KEY_VALUEOF "valueOf"
#define  PROP_KEY_CLASS "class"
#define  PRIVATE_TYPE_NAME "#typename"
#define  CLASS_IMPLEMENTATION_OBJECT "t::ClassImplementationObject"
#define  PROP_KEY_SUPER "super"
#define  PROP_KEY_SUPERVALUE "supervalue"
#define  PRIVATE_JSINFO "#js_info"
#define  PRIVATE_CALLSUPER "#supercall"
#define  PRIVATE_IS_NAPI "#is_napi"
#define  PROP_KEY_TOSTRING "toString"
#define  PROP_KEY_IS_PROTOTYPE_IMPLEMENTATION_OBJECT "__isPrototypeImplementationObject"
#define  PROP_KEY_PROTOTYPE "prototype"
#define  PROP_KEY_CONSTRUCTOR "constructor"

std::unordered_map<napi_env, Constants*> Constants::s_constantsMap;
std::string Constants::APP_ROOT_FOLDER_PATH;
std::string Constants::V8_STARTUP_FLAGS;

Constants::Constants()
    : extendValue(nullptr), nullObjectValue(nullptr), nullNodeNameValue(nullptr),
      valueOfValue(nullptr), classValue(nullptr), privateTypeNameValue(nullptr),
      classImplementationObjectValue(nullptr), superValue(nullptr), superValueValue(nullptr),
      privateJsInfoValue(nullptr), privateCallSuperValue(nullptr), privateIsNapiValue(nullptr),
      toStringValue(nullptr), isPrototypeImplementationObjectValue(nullptr) {}

napi_status Constants::Init(napi_env env) {
    if (s_constantsMap.find(env) == s_constantsMap.end()) {
        auto* instance = new Constants();

        napi_create_string_utf8(env, PROP_KEY_EXTEND, NAPI_AUTO_LENGTH, &instance->extendValue);
        napi_create_string_utf8(env, PROP_KEY_NULLOBJECT, NAPI_AUTO_LENGTH, &instance->nullObjectValue);
        napi_create_string_utf8(env, PROP_KEY_NULL_NODE_NAME, NAPI_AUTO_LENGTH, &instance->nullNodeNameValue);
        napi_create_string_utf8(env, PROP_KEY_VALUEOF, NAPI_AUTO_LENGTH, &instance->valueOfValue);
        napi_create_string_utf8(env, PROP_KEY_CLASS, NAPI_AUTO_LENGTH, &instance->classValue);
        napi_create_string_utf8(env, PRIVATE_TYPE_NAME, NAPI_AUTO_LENGTH, &instance->privateTypeNameValue);
        napi_create_string_utf8(env, CLASS_IMPLEMENTATION_OBJECT, NAPI_AUTO_LENGTH, &instance->classImplementationObjectValue);
        napi_create_string_utf8(env, PROP_KEY_SUPER, NAPI_AUTO_LENGTH, &instance->superValue);
        napi_create_string_utf8(env, PROP_KEY_SUPERVALUE, NAPI_AUTO_LENGTH, &instance->superValueValue);
        napi_create_string_utf8(env, PRIVATE_JSINFO, NAPI_AUTO_LENGTH, &instance->privateJsInfoValue);
        napi_create_string_utf8(env, PRIVATE_CALLSUPER, NAPI_AUTO_LENGTH, &instance->privateCallSuperValue);
        napi_create_string_utf8(env, PRIVATE_IS_NAPI, NAPI_AUTO_LENGTH, &instance->privateIsNapiValue);
        napi_create_string_utf8(env, PROP_KEY_TOSTRING, NAPI_AUTO_LENGTH, &instance->toStringValue);
        napi_create_string_utf8(env, PROP_KEY_IS_PROTOTYPE_IMPLEMENTATION_OBJECT, NAPI_AUTO_LENGTH, &instance->isPrototypeImplementationObjectValue);
        napi_create_string_utf8(env, PROP_KEY_PROTOTYPE, NAPI_AUTO_LENGTH, &instance->prototypeValue);
        napi_create_string_utf8(env, PROP_KEY_CONSTRUCTOR, NAPI_AUTO_LENGTH, &instance->constructorValue);
        napi_create_string_utf8(env, "name", NAPI_AUTO_LENGTH, &instance->nameValue);

        napi_create_string_utf8(env, "Object", NAPI_AUTO_LENGTH, &instance->objectValue);
        napi_create_string_utf8(env, "Number", NAPI_AUTO_LENGTH, &instance->numberValue);
        napi_create_string_utf8(env, "isInteger", NAPI_AUTO_LENGTH, &instance->isIntegerValue);
        napi_create_string_utf8(env, "String", NAPI_AUTO_LENGTH, &instance->stringValue);
        napi_create_string_utf8(env, "Boolean", NAPI_AUTO_LENGTH, &instance->booleanValue);
        napi_create_string_utf8(env, "setPrototypeOf", NAPI_AUTO_LENGTH, &instance->setPrototypeOf);
        napi_create_string_utf8(env, "__proto__", NAPI_AUTO_LENGTH, &instance->protoValue);
        napi_create_string_utf8(env, "value", NAPI_AUTO_LENGTH, &instance->valueValue);

        s_constantsMap[env] = instance;
    }
    return napi_ok;
}

Constants* Constants::Get(napi_env env) {
    auto it = s_constantsMap.find(env);
    if (it != s_constantsMap.end()) {
        return it->second;
    }
    return nullptr;
}

void Constants::DeInit(napi_env env) {
    auto it = s_constantsMap.find(env);
    if (it != s_constantsMap.end()) {
        delete it->second;
        s_constantsMap.erase(it);
    }
}
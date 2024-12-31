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

#define DEFINE_PROP(env, propStr, fieldName)            \
do {                                                   \
    napi_value localVal = nullptr;                     \
    napi_create_string_utf8(env, propStr, NAPI_AUTO_LENGTH, &localVal); \
    napi_create_reference(env, localVal, 1, &fieldName); \
} while(0)

#define PROP_GETTER(propName)                                      \
napi_value Constants::propName##Value(napi_env env) {                     \
    napi_value val = nullptr;                                      \
    napi_get_reference_value(env, propName##ValueRef, &val);            \
    return val;                                                    \
}

std::unordered_map<napi_env, Constants*> Constants::s_constantsMap;
std::string Constants::APP_ROOT_FOLDER_PATH;
std::string Constants::V8_STARTUP_FLAGS;

Constants::Constants()
    : extendValueRef(nullptr),
      nullObjectValueRef(nullptr),
      nullNodeNameValueRef(nullptr),
      valueOfValueRef(nullptr),
      clsValueRef(nullptr),
      privateTypeNameValueRef(nullptr),
      classImplementationObjectValueRef(nullptr),
      superValueRef(nullptr),
      superValueValueRef(nullptr),
      privateJsInfoValueRef(nullptr),
      privateCallSuperValueRef(nullptr),
      privateIsNapiValueRef(nullptr),
      toStringValueRef(nullptr),
      isPrototypeImplementationObjectValueRef(nullptr),
      prototypeValueRef(nullptr),
      constructorValueRef(nullptr),
      nameValueRef(nullptr),
      objectValueRef(nullptr),
      numberValueRef(nullptr),
      isIntegerValueRef(nullptr),
      setPrototypeOfValueRef(nullptr),
      stringValueRef(nullptr),
      booleanValueRef(nullptr),
      protoValueRef(nullptr),
      valueValueRef(nullptr) {}

napi_status Constants::Init(napi_env env) {
    if (s_constantsMap.find(env) == s_constantsMap.end()) {
        auto* instance = new Constants();

        DEFINE_PROP(env, PROP_KEY_EXTEND, instance->extendValueRef);
        DEFINE_PROP(env, PROP_KEY_NULLOBJECT, instance->nullObjectValueRef);
        DEFINE_PROP(env, PROP_KEY_NULL_NODE_NAME, instance->nullNodeNameValueRef);
        DEFINE_PROP(env, PROP_KEY_VALUEOF, instance->valueOfValueRef);
        DEFINE_PROP(env, PROP_KEY_CLASS, instance->clsValueRef);
        DEFINE_PROP(env, PRIVATE_TYPE_NAME, instance->privateTypeNameValueRef);
        DEFINE_PROP(env, CLASS_IMPLEMENTATION_OBJECT, instance->classImplementationObjectValueRef);
        DEFINE_PROP(env, PROP_KEY_SUPER, instance->superValueRef);
        DEFINE_PROP(env, PROP_KEY_SUPERVALUE, instance->superValueValueRef);
        DEFINE_PROP(env, PRIVATE_JSINFO, instance->privateJsInfoValueRef);
        DEFINE_PROP(env, PRIVATE_CALLSUPER, instance->privateCallSuperValueRef);
        DEFINE_PROP(env, PRIVATE_IS_NAPI, instance->privateIsNapiValueRef);
        DEFINE_PROP(env, PROP_KEY_TOSTRING, instance->toStringValueRef);
        DEFINE_PROP(env, PROP_KEY_IS_PROTOTYPE_IMPLEMENTATION_OBJECT, instance->isPrototypeImplementationObjectValueRef);
        DEFINE_PROP(env, PROP_KEY_PROTOTYPE, instance->prototypeValueRef);
        DEFINE_PROP(env, PROP_KEY_CONSTRUCTOR, instance->constructorValueRef);
        DEFINE_PROP(env, "name", instance->nameValueRef);
        DEFINE_PROP(env, "Object", instance->objectValueRef);
        DEFINE_PROP(env, "Number", instance->numberValueRef);
        DEFINE_PROP(env, "isInteger", instance->isIntegerValueRef);
        DEFINE_PROP(env, "setPrototypeOf", instance->setPrototypeOfValueRef);
        DEFINE_PROP(env, "String", instance->stringValueRef);
        DEFINE_PROP(env, "Boolean", instance->booleanValueRef);
        DEFINE_PROP(env, "__proto__", instance->protoValueRef);
        DEFINE_PROP(env, "value", instance->valueValueRef);

        s_constantsMap[env] = instance;
    }
    return napi_ok;
}

PROP_GETTER(extend)
PROP_GETTER(nullObject)
PROP_GETTER(nullNodeName)
PROP_GETTER(valueOf)
PROP_GETTER(cls)
PROP_GETTER(privateTypeName)
PROP_GETTER(classImplementationObject)
PROP_GETTER(super)
PROP_GETTER(superValue)
PROP_GETTER(privateJsInfo)
PROP_GETTER(privateCallSuper)
PROP_GETTER(privateIsNapi)
PROP_GETTER(toString)
PROP_GETTER(isPrototypeImplementationObject)
PROP_GETTER(prototype)
PROP_GETTER(constructor)
PROP_GETTER(name)
PROP_GETTER(object)
PROP_GETTER(number)
PROP_GETTER(isInteger)
PROP_GETTER(setPrototypeOf)
PROP_GETTER(string)
PROP_GETTER(boolean)
PROP_GETTER(proto)
PROP_GETTER(value)

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
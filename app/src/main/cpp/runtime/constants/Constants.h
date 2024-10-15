#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#include <string>

#define  PROP_KEY_EXTEND "extend"
#define PROP_KEY_NULLOBJECT "null"
#define PROP_KEY_NULL_NODE_NAME "nullNode"
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

class Constants {
    public:
        const static char CLASS_NAME_LOCATION_SEPARATOR = '_';

        static std::string APP_ROOT_FOLDER_PATH;
        static std::string V8_STARTUP_FLAGS;
        static bool V8_CACHE_COMPILED_CODE;

    private:
        Constants() {
        }
};

#endif /* CONSTANTS_H_ */

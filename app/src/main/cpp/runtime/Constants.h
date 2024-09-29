#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#include <string>

const char * PROP_KEY_EXTEND = "extend";
const char * PROP_KEY_NULLOBJECT = "null";
const char * PROP_KEY_NULL_NODE_NAME = "nullNode";
const char * PROP_KEY_VALUEOF = "valueOf";
const char * PROP_KEY_CLASS = "class";
const char * PRIVATE_TYPE_NAME = "#typename";
const char * CLASS_IMPLEMENTATION_OBJECT = "t::ClassImplementationObject";
const char * PROP_KEY_SUPER = "super";
const char * PROP_KEY_SUPERVALUE = "supervalue";

const char * PRIVATE_JSINFO = "#js_info";
const char * PRIVATE_CALLSUPER = "#supercall";
const char * PRIVATE_IS_NAPI = "#is_napi";
const char * PROP_KEY_TOSTRING = "toString";

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

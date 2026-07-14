#include "NumericCasts.h"
#include "NativeScriptAssert.h"
#include "Util.h"
#include "ArgConverter.h"
#include "NativeScriptException.h"
#include <sstream>

using namespace std;
using namespace tns;

void NumericCasts::CreateGlobalCastFunctions(napi_env env, napi_value globalObject) {

    napi_status status;

    napi_value longFunc, byteFunc, shortFunc, doubleFunc, floatFunc, charFunc;

    NAPI_GUARD(napi_create_function(env, "long", NAPI_AUTO_LENGTH, NumericCasts::MarkAsLongCallback, nullptr,
                         &longFunc)) {
        return;
    }
    NAPI_GUARD(napi_create_function(env, "byte", NAPI_AUTO_LENGTH, NumericCasts::MarkAsByteCallback, nullptr,
                         &byteFunc)) {
        return;
    }
    NAPI_GUARD(napi_create_function(env, "short", NAPI_AUTO_LENGTH, NumericCasts::MarkAsShortCallback, nullptr,
                         &shortFunc)) {
        return;
    }
    NAPI_GUARD(napi_create_function(env, "double", NAPI_AUTO_LENGTH, NumericCasts::MarkAsDoubleCallback,
                         nullptr,
                         &doubleFunc)) {
        return;
    }
    NAPI_GUARD(napi_create_function(env, "float", NAPI_AUTO_LENGTH, NumericCasts::MarkAsFloatCallback, nullptr,
                         &floatFunc)) {
        return;
    }
    NAPI_GUARD(napi_create_function(env, "char", NAPI_AUTO_LENGTH, NumericCasts::MarkAsCharCallback, nullptr,
                         &charFunc)) {
        return;
    }

    NAPI_GUARD(napi_set_named_property(env, globalObject, "long", longFunc)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, globalObject, "byte", byteFunc)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, globalObject, "short", shortFunc)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, globalObject, "double", doubleFunc)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, globalObject, "float", floatFunc)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, globalObject, "char", charFunc)) {
        return;
    }
}

void NumericCasts::MarkAsLong(napi_env env, napi_value object, napi_value value) {
    MarkJsObject(env, object, CastType::Long, value);
}


napi_value NumericCasts::MarkAsLongCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    if (argc != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "long(x) should be called with single parameter")) {}
        return nullptr;
    }

    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, argv[0], &type)) {
        return nullptr;
    }

    if (type != napi_string && type != napi_number) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "long(x) should be called with single parameter containing a long number representation")) {}
        return nullptr;
    }

    napi_value value = argv[0];

    napi_value cast;
    NAPI_GUARD(napi_create_object(env, &cast)) {
        return nullptr;
    }
    MarkJsObject(env, cast, CastType::Long, value);
    return cast;
}

napi_value NumericCasts::MarkAsByteCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    if (argc != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "byte(x) should be called with single parameter")) {}
        return nullptr;
    }

    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, argv[0], &type)) {
        return nullptr;
    }

    if (type != napi_string && type != napi_number && !napi_util::is_number_object(env, argv[0]) && !napi_util::is_string_object(env, argv[0])) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "byte(x) should be called with single parameter containing a byte number representation")) {}
        return nullptr;
    }
    napi_value value;
    if (type == napi_number) {
         value = argv[0];
    } else {
        NAPI_GUARD(napi_coerce_to_string(env, argv[0], &value)) {
            return nullptr;
        }
    }

    napi_value cast;
    NAPI_GUARD(napi_create_object(env, &cast)) {
        return nullptr;
    }
    MarkJsObject(env, cast, CastType::Byte, value);
    return cast;
}

napi_value NumericCasts::MarkAsShortCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    if (argc != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "short(x) should be called with single parameter")) {}
        return nullptr;
    }

    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, argv[0], &type)) {
        return nullptr;
    }

    if (type != napi_string && type != napi_number && !napi_util::is_number_object(env, argv[0]) && !napi_util::is_string_object(env, argv[0])) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "short(x) should be called with single parameter containing a byte number representation")) {}
        return nullptr;
    }
    napi_value value;
    if (type == napi_number) {
        value = argv[0];
    } else {
        NAPI_GUARD(napi_coerce_to_string(env, argv[0], &value)) {
            return nullptr;
        }
    }

    napi_value cast;
    NAPI_GUARD(napi_create_object(env, &cast)) {
        return nullptr;
    }
    MarkJsObject(env, cast, CastType::Short, value);
    return cast;
}

napi_value NumericCasts::MarkAsCharCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    if (argc != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "char(x) should be called with single parameter")) {}
        return nullptr;
    }

    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, argv[0], &type)) {
        return nullptr;
    }

    if (type != napi_string) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "char(x) should be called with single parameter containing a char representation")) {}
        return nullptr;
    }

    size_t str_len;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len)) {
        return nullptr;
    }
    if (str_len != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "char(x) should be called with single parameter containing a single char")) {}
        return nullptr;
    }


    napi_value cast;
    NAPI_GUARD(napi_create_object(env, &cast)) {
        return nullptr;
    }
    MarkJsObject(env, cast, CastType::Char, argv[0]);
    return cast;
}

napi_value NumericCasts::MarkAsFloatCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    if (argc != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "float(x) should be called with single parameter")) {}
        return nullptr;
    }

    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, argv[0], &type)) {
        return nullptr;
    }

    if (type != napi_number) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "float(x) should be called with single parameter containing a float number representation")) {}
        return nullptr;
    }

    napi_value value = argv[0];

    napi_value cast;
    NAPI_GUARD(napi_create_object(env, &cast)) {
        return nullptr;
    }
    MarkJsObject(env, cast, CastType::Float, value);
    return cast;
}

napi_value NumericCasts::MarkAsDoubleCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    if (argc != 1) {
        NAPI_GUARD(napi_throw_error(env, nullptr, "double(x) should be called with single parameter")) {}
        return nullptr;
    }

    napi_valuetype type;
    NAPI_GUARD(napi_typeof(env, argv[0], &type)) {
        return nullptr;
    }

    if (type != napi_number) {
        NAPI_GUARD(napi_throw_error(env, nullptr,
                         "double(x) should be called with single parameter containing a double number representation")) {}
        return nullptr;
    }

    napi_value value = argv[0];

    napi_value cast;
    NAPI_GUARD(napi_create_object(env, &cast)) {
        return nullptr;
    }
    MarkJsObject(env, cast, CastType::Double, value);
    return cast;
}

void
NumericCasts::MarkJsObject(napi_env env, napi_value object, CastType castType, napi_value value) {
    napi_status status;

    napi_value type;
    NAPI_GUARD(napi_create_int32(env, static_cast<int>(castType), &type)) {
        return;
    }

    NAPI_GUARD(napi_set_named_property(env, object, s_castMarker, type)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, object, "value", value)) {
        return;
    }

//    DEBUG_WRITE("MarkJsObject: Marking js object with cast type: %d", castType);
}

const char *NumericCasts::s_castMarker = "t::cast";
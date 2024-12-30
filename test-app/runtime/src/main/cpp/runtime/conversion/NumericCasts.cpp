#include "NumericCasts.h"
#include "NativeScriptAssert.h"
#include "Util.h"
#include "ArgConverter.h"
#include "NativeScriptException.h"
#include <sstream>

using namespace std;
using namespace tns;

void NumericCasts::CreateGlobalCastFunctions(napi_env env, napi_value globalObject) {

    napi_value longFunc, byteFunc, shortFunc, doubleFunc, floatFunc, charFunc;

    napi_create_function(env, "long", NAPI_AUTO_LENGTH, NumericCasts::MarkAsLongCallback, nullptr,
                         &longFunc);
    napi_create_function(env, "byte", NAPI_AUTO_LENGTH, NumericCasts::MarkAsByteCallback, nullptr,
                         &byteFunc);
    napi_create_function(env, "short", NAPI_AUTO_LENGTH, NumericCasts::MarkAsShortCallback, nullptr,
                         &shortFunc);
    napi_create_function(env, "double", NAPI_AUTO_LENGTH, NumericCasts::MarkAsDoubleCallback,
                         nullptr,
                         &doubleFunc);
    napi_create_function(env, "float", NAPI_AUTO_LENGTH, NumericCasts::MarkAsFloatCallback, nullptr,
                         &floatFunc);
    napi_create_function(env, "char", NAPI_AUTO_LENGTH, NumericCasts::MarkAsCharCallback, nullptr,
                         &charFunc);

    napi_set_named_property(env, globalObject, "long", longFunc);
    napi_set_named_property(env, globalObject, "byte", byteFunc);
    napi_set_named_property(env, globalObject, "short", shortFunc);
    napi_set_named_property(env, globalObject, "double", doubleFunc);
    napi_set_named_property(env, globalObject, "float", floatFunc);
    napi_set_named_property(env, globalObject, "char", charFunc);
}

CastType NumericCasts::GetCastType(napi_env env, napi_value object) {
    CastType ret = CastType::None;

    napi_value hidden;

    napi_get_named_property(env, object, s_castMarker, &hidden);

    if (!napi_util::is_null_or_undefined(env, hidden)) {
        int32_t castType;
        napi_get_value_int32(env, hidden, &castType);
        ret = static_cast<CastType>(castType);
    }

    return ret;
}

napi_value NumericCasts::GetCastValue(napi_env env, napi_value object) {
    napi_value value;
    napi_get_property(env, object, Constants::Get(env)->valueValue, &value);
    return value;
}

void NumericCasts::MarkAsLong(napi_env env, napi_value object, napi_value value) {
    MarkJsObject(env, object, CastType::Long, value);
}


napi_value NumericCasts::MarkAsLongCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    if (argc != 1) {
        napi_throw_error(env, nullptr, "long(x) should be called with single parameter");
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, argv[0], &type);

    if (type != napi_string && type != napi_number) {
        napi_throw_error(env, nullptr,
                         "long(x) should be called with single parameter containing a long number representation");
        return nullptr;
    }

    napi_value value = argv[0];

    napi_value cast;
    napi_create_object(env, &cast);
    MarkJsObject(env, cast, CastType::Long, value);
    return cast;
}

napi_value NumericCasts::MarkAsByteCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    if (argc != 1) {
        napi_throw_error(env, nullptr, "byte(x) should be called with single parameter");
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, argv[0], &type);

    if (type != napi_string && type != napi_number && !napi_util::is_number_object(env, argv[0]) && !napi_util::is_string_object(env, argv[0])) {
        napi_throw_error(env, nullptr,
                         "byte(x) should be called with single parameter containing a byte number representation");
        return nullptr;
    }
    napi_value value;
    if (type == napi_number) {
         value = argv[0];
    } else {
        napi_coerce_to_string(env, argv[0], &value);
    }

    napi_value cast;
    napi_create_object(env, &cast);
    MarkJsObject(env, cast, CastType::Byte, value);
    return cast;
}

napi_value NumericCasts::MarkAsShortCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    if (argc != 1) {
        napi_throw_error(env, nullptr, "short(x) should be called with single parameter");
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, argv[0], &type);

    if (type != napi_string && type != napi_number && !napi_util::is_number_object(env, argv[0]) && !napi_util::is_string_object(env, argv[0])) {
        napi_throw_error(env, nullptr,
                         "short(x) should be called with single parameter containing a byte number representation");
        return nullptr;
    }
    napi_value value;
    if (type == napi_number) {
        value = argv[0];
    } else {
        napi_coerce_to_string(env, argv[0], &value);
    }

    napi_value cast;
    napi_create_object(env, &cast);
    MarkJsObject(env, cast, CastType::Short, value);
    return cast;
}

napi_value NumericCasts::MarkAsCharCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    if (argc != 1) {
        napi_throw_error(env, nullptr, "char(x) should be called with single parameter");
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, argv[0], &type);

    if (type != napi_string) {
        napi_throw_error(env, nullptr,
                         "char(x) should be called with single parameter containing a char representation");
        return nullptr;
    }

    size_t str_len;
    napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
    if (str_len != 1) {
        napi_throw_error(env, nullptr,
                         "char(x) should be called with single parameter containing a single char");
        return nullptr;
    }


    napi_value cast;
    napi_create_object(env, &cast);
    MarkJsObject(env, cast, CastType::Char, argv[0]);
    return cast;
}

napi_value NumericCasts::MarkAsFloatCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    if (argc != 1) {
        napi_throw_error(env, nullptr, "float(x) should be called with single parameter");
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, argv[0], &type);

    if (type != napi_number) {
        napi_throw_error(env, nullptr,
                         "float(x) should be called with single parameter containing a float number representation");
        return nullptr;
    }

    napi_value value = argv[0];

    napi_value cast;
    napi_create_object(env, &cast);
    MarkJsObject(env, cast, CastType::Float, value);
    return cast;
}

napi_value NumericCasts::MarkAsDoubleCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    if (argc != 1) {
        napi_throw_error(env, nullptr, "double(x) should be called with single parameter");
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, argv[0], &type);

    if (type != napi_number) {
        napi_throw_error(env, nullptr,
                         "double(x) should be called with single parameter containing a double number representation");
        return nullptr;
    }

    napi_value value = argv[0];

    napi_value cast;
    napi_create_object(env, &cast);
    MarkJsObject(env, cast, CastType::Double, value);
    return cast;
}

void
NumericCasts::MarkJsObject(napi_env env, napi_value object, CastType castType, napi_value value) {
    napi_value type;
    napi_create_int32(env, static_cast<int>(castType), &type);

    napi_set_named_property(env, object, s_castMarker, type);
    napi_set_property(env, object, Constants::Get(env)->valueValue, value);

//    DEBUG_WRITE("MarkJsObject: Marking js object with cast type: %d", castType);
}

const char *NumericCasts::s_castMarker = "t::cast";
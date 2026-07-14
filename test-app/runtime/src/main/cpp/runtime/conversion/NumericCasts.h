#ifndef NUMERICCASTS_H_
#define NUMERICCASTS_H_

#include "js_native_api.h"
#include "Runtime.h"
#include <string>

namespace tns {
    enum class CastType {
        None,
        Char,
        Byte,
        Short,
        Long,
        Float,
        Double
    };

    class NumericCasts {
    public:
        void CreateGlobalCastFunctions(napi_env env, napi_value globalObject);

        inline static CastType GetCastType(napi_env env, napi_value object) {
            CastType ret = CastType::None;

#ifdef USE_HOST_OBJECT
            bool is_host_object = false;
            napi_is_host_object(env, object, &is_host_object);
            if (is_host_object) return ret;
#endif

            napi_value hidden;
            napi_get_named_property(env, object, s_castMarker, &hidden);
            napi_valuetype type;
            napi_typeof(env, hidden, &type);
            if (type == napi_number) {
                napi_get_named_property(env, object, s_castMarker, &hidden);
                int32_t castType;
                napi_get_value_int32(env, hidden, &castType);
                ret = static_cast<CastType>(castType);
            }

            return ret;
        }

        inline static napi_value GetCastValue(napi_env env, napi_value object) {
            napi_value value;
            napi_get_named_property(env, object, "value", &value);
            return value;
        }

        static void MarkAsLong(napi_env env, napi_value object, napi_value value);

    private:
        static napi_value MarkAsLongCallback(napi_env env, napi_callback_info info);

        static napi_value MarkAsByteCallback(napi_env env, napi_callback_info info);

        static napi_value MarkAsShortCallback(napi_env env, napi_callback_info info);

        static napi_value MarkAsCharCallback(napi_env env, napi_callback_info info);

        static napi_value MarkAsFloatCallback(napi_env env, napi_callback_info info);

        static napi_value MarkAsDoubleCallback(napi_env env, napi_callback_info info);

        static void
        MarkJsObject(napi_env env, napi_value object, CastType castType, napi_value value);

        static const char *s_castMarker;
    };
}

#endif /* NUMERICCASTS_H_ */
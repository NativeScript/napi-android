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
            void *data;
            napi_get_host_object_data(env, object, &data);
            if (data != nullptr) return ret;
#endif

            napi_value hidden;

            bool hasProperty;
            napi_has_named_property(env, object, s_castMarker, &hasProperty);

            if (hasProperty) {
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
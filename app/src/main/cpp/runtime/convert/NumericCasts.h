#ifndef NUMERICCASTS_H_
#define NUMERICCASTS_H_

#include "js_native_api.h"
#include <string>

namespace ns {
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

        static CastType GetCastType(napi_env env, napi_value object);

        static napi_value GetCastValue(napi_env env, napi_value object);

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

        static const char * s_castMarker;
    };
}

#endif /* NUMERICCASTS_H_ */
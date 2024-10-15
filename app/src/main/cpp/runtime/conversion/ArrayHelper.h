#ifndef ARRAYHELPER_H_
#define ARRAYHELPER_H_

#include <js_native_api.h>
#include "ObjectManager.h"
#include <string.h>

namespace ns {
class ArrayHelper {
    public:
        static void Init(napi_env env);

    private:
        ArrayHelper();

        static napi_value CreateJavaArrayCallback(napi_env env, napi_callback_info info);

        static napi_value CreateJavaArray(napi_env env, napi_callback_info info);

        static void Throw(napi_env env, const std::string& errorMessage);

        static jobject CreateArrayByClassName(const std::string& typeName, int length);

        static jclass RUNTIME_CLASS;

        static jmethodID CREATE_ARRAY_HELPER;
};
}

#endif /* ARRAYHELPER_H_ */
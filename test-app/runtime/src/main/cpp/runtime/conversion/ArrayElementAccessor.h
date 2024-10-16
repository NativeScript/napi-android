#ifndef ARRAYELEMENTACCESSOR_H_
#define ARRAYELEMENTACCESSOR_H_

#include "JEnv.h"
#include "JniLocalRef.h"
#include "js_native_api.h"
#include <string>
#include "ObjectManager.h"


namespace tns {
    class ArrayElementAccessor {
    public:
        napi_value GetArrayElement(napi_env env, napi_value array, uint32_t index, const std::string& arraySignature);

        void SetArrayElement(napi_env env, napi_value array, uint32_t index, const std::string& arraySignature, napi_value value);

    private:
        napi_value ConvertToJsValue(napi_env env, ObjectManager* objectManager, JEnv& jEnv, const std::string& elementSignature, const void* value);
        void assertNonNullNativeArray(tns::JniLocalRef& arrayReference);
    };
}

#endif /* ARRAYELEMENTACCESSOR_H_ */
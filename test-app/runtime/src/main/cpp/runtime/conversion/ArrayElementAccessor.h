#ifndef ARRAYELEMENTACCESSOR_H_
#define ARRAYELEMENTACCESSOR_H_

#include "JEnv.h"
#include "JniLocalRef.h"
#include "js_native_api.h"
#include <string>
#include <string_view>
#include "ObjectManager.h"


namespace tns {
    class ArrayElementAccessor {
    public:
        // `objectManager` and `arrayObject` may be supplied pre-resolved by the
        // caller (e.g. the host indexed interceptor or an array-loop helper) to
        // avoid a locked env->runtime lookup and re-resolving the Java array on
        // every element. Both fall back to resolving internally when omitted.
        napi_value GetArrayElement(napi_env env, napi_value array, uint32_t index,
                                   const std::string& arraySignature,
                                   ObjectManager* objectManager = nullptr,
                                   jobject arrayObject = nullptr);

        void SetArrayElement(napi_env env, napi_value array, uint32_t index,
                             const std::string& arraySignature, napi_value value,
                             ObjectManager* objectManager = nullptr,
                             jobject arrayObject = nullptr);

    private:
        napi_value ConvertToJsValue(napi_env env, ObjectManager* objectManager, JEnv& jEnv, std::string_view elementSignature, const void* value);
        void assertNonNullNativeArray(tns::JniLocalRef& arrayReference);
    };
}

#endif /* ARRAYELEMENTACCESSOR_H_ */
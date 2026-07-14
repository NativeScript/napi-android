#ifndef FIELDACCESSOR_H_
#define FIELDACCESSOR_H_

#include "JEnv.h"
#include <string>
#include "ObjectManager.h"
#include "FieldCallbackData.h"

namespace tns {
class FieldAccessor {
    public:
        // `objectManager` and `targetJavaObject` may be supplied pre-resolved by
        // the caller (the accessor callback) so this avoids a locked env->runtime
        // lookup and a second host-object probe. Both fall back to resolving
        // internally when omitted.
        napi_value GetJavaField(napi_env env, napi_value target, FieldCallbackData* fieldData,
                                ObjectManager* objectManager = nullptr,
                                JniLocalRef targetJavaObject = JniLocalRef());

        void SetJavaField(napi_env env, napi_value target, napi_value value, FieldCallbackData* fieldData,
                          ObjectManager* objectManager = nullptr,
                          JniLocalRef targetJavaObject = JniLocalRef());
};
}

#endif /* FIELDACCESSOR_H_ */

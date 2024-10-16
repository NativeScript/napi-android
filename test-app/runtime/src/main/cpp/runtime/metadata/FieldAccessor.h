#ifndef FIELDACCESSOR_H_
#define FIELDACCESSOR_H_

#include "JEnv.h"
#include <string>
#include "ObjectManager.h"
#include "FieldCallbackData.h"

namespace tns {
class FieldAccessor {
    public:
        napi_value GetJavaField(napi_env env, napi_value target, FieldCallbackData* fieldData);

        void SetJavaField(napi_env env, napi_value target, napi_value value, FieldCallbackData* fieldData);
};
}

#endif /* FIELDACCESSOR_H_ */

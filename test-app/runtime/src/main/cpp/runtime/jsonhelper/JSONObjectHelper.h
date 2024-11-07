#ifndef JSONOBJECTHELPER_H_
#define JSONOBJECTHELPER_H_

#include "js_native_api.h"

namespace tns {

    class JSONObjectHelper {
    public:
        static void RegisterFromFunction(napi_env env, napi_value value);
    private:
        static napi_value CreateFromFunction(napi_env env);
    };

}

#endif //JSONOBJECTHELPER_H_
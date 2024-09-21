//
// Created by Ammar Ahmed on 21/09/2024.
//

#ifndef TESTAPPNAPI_CONSOLE_H
#define TESTAPPNAPI_CONSOLE_H
#include "js_native_api.h"


namespace ns {
    class Console {
    public:
        static napi_status createConsole(napi_env env, napi_value global);
    };

}

#endif //TESTAPPNAPI_CONSOLE_H

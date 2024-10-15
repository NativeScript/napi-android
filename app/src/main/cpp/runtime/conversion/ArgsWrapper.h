/*
 * ArgsWrapper.h
 *
 *  Created on: Dec 20, 2013
 *      Author: slavchev
 */

#ifndef ARGSWRAPPER_H_
#define ARGSWRAPPER_H_
#include "js_native_api.h"

namespace ns {
enum class ArgType {
    Class,
    Interface
};

struct ArgsWrapper {
    public:
        ArgsWrapper(napi_callback_info a, ArgType t)
            :
            args(a), type(t) {
        }
        napi_callback_info args;
        ArgType type;
};
}

#endif /* ARGSWRAPPER_H_ */

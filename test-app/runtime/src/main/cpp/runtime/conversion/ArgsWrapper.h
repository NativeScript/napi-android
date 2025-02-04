/*
 * ArgsWrapper.h
 *
 *  Created on: Dec 20, 2013
 *      Author: slavchev
 */

#ifndef ARGSWRAPPER_H_
#define ARGSWRAPPER_H_
#include "js_native_api.h"

namespace tns {
enum class ArgType {
    Class,
    Interface
};

struct ArgsWrapper {
    public:
        ArgsWrapper(napi_value* argv_, size_t argc_, ArgType t)
            :
            argv(argv_), argc(argc_), type(t) {
        }
        napi_value* argv;
        size_t argc;
        ArgType type;
};
}

#endif /* ARGSWRAPPER_H_ */

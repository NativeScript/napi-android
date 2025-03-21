#ifndef SIGHANDLER_H
#define SIGHANDLER_H
#include <sstream>
#include "NativeScriptException.h"
using namespace tns;

void SIGHandler(int sigNumber) {
    std::stringstream msg;
    msg << "JNI Exception occurred (";
    switch (sigNumber) {
        case SIGABRT:
            msg << "SIGABRT";
            break;
        case SIGSEGV:
            msg << "SIGSEGV";
            break;
        default:
            // Shouldn't happen, but for completeness
            msg << "Signal #" << sigNumber;
            break;
    }
    msg << ").\n=======\nCheck the 'adb logcat' for additional information about the error.\n=======\n";
    throw NativeScriptException(msg.str());
}

#endif //SIGHANDLER_H
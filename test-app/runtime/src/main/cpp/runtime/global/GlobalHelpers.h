#ifndef NAPI_GLOBALHELPERS_H_
#define NAPI_GLOBALHELPERS_H_

#include "jni.h"
#include "js_native_api.h"
#include <string>
#include <map>
#include <utility>

namespace tns {
std::string JsonStringifyObject(napi_env env, napi_value value, bool handleCircularReferences = true);

napi_value JsonParseString(napi_env env, const std::string& value);

struct JsStacktraceFrame {
    JsStacktraceFrame(): line(0), col(0) {}
    JsStacktraceFrame(
            int _line, int _col, std::string _filename, std::string _text
            ): line(_line), col(_col), filename(std::move(_filename)), text(std::move(_text)) {}

    int line;
    int col;
    std::string filename;
    std::string text;
};

std::vector<JsStacktraceFrame> BuildStacktraceFrames(napi_env env, napi_value error, int size);

namespace GlobalHelpers {
    void onDisposeEnv(napi_env env);
}
}

#endif /* NAPI_GLOBALHELPERS_H_ */
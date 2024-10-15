#ifndef NAPI_GLOBALHELPERS_H_
#define NAPI_GLOBALHELPERS_H_

#include "jni.h"
#include "js_native_api.h"
#include <string>
#include <map>

namespace ns {
std::string JsonStringifyObject(napi_env env, napi_value value, bool handleCircularReferences = true);

napi_value JsonParseString(napi_env env, const std::string& value);

struct JsStacktraceFrame {
    JsStacktraceFrame(): line(nullptr), col(nullptr), filename(nullptr), text(nullptr) {}
    JsStacktraceFrame(
            int _line, int _col, std::string _filename, std::string _text
            ): line(_line), col(_col), filename(_filename), text(_text) {}

    int line;
    int col;
    string filename;
    string text;
};

std::vector<JsStacktraceFrame> BuildStacktraceFrames(napi_env env, napi_value error, int size);

namespace GlobalHelpers {
    void onDisposeEnv(napi_env env);
}
}

#endif /* NAPI_GLOBALHELPERS_H_ */
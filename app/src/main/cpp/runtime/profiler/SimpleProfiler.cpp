#include "SimpleProfiler.h"
#include "NativeScriptException.h"
#include "NativeScriptAssert.h"
#include <algorithm>
#include <sstream>

using namespace ns;
using namespace std;

SimpleProfiler::SimpleProfiler(char* fileName, int lineNumber)
    :
    m_frame(nullptr), m_time(0) {
    for (auto& f : s_frames) {
        if ((f.fileName == fileName) && (f.lineNumber == lineNumber)) {
            m_frame = &f;
            break;
        }
    }
    if (m_frame == nullptr) {
        FrameEntry entry(fileName, lineNumber);
        s_frames.push_back(entry);
        m_frame = &s_frames.back();
    }
    ++m_frame->stackCount;
    if (m_frame->stackCount == 1) {
        struct timespec nowt;
        clock_gettime(CLOCK_MONOTONIC, &nowt);
        m_time = (int64_t) nowt.tv_sec * 1000000000LL + nowt.tv_nsec;
    }
}

SimpleProfiler::~SimpleProfiler() {
    --m_frame->stackCount;
    if (m_frame->stackCount == 0) {
        struct timespec nowt;
        clock_gettime(CLOCK_MONOTONIC, &nowt);
        auto time = (int64_t) nowt.tv_sec * 1000000000LL + nowt.tv_nsec;
        m_frame->time += (time - m_time) / 1000000;
    }
}

void SimpleProfiler::Init(napi_env env, napi_value global) {
    s_frames.reserve(10000);
    napi_util::napi_set_function(env, global, "__printProfilerData", PrintProfilerDataCallback, nullptr);
}

napi_value SimpleProfiler::PrintProfilerDataCallback(napi_env env, napi_callback_info info) {
    try {
        PrintProfilerData();
    } catch (NativeScriptException& e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
}

void SimpleProfiler::PrintProfilerData() {
    std::sort(s_frames.begin(), s_frames.end());
    for (auto& f : s_frames) {
        __android_log_print(ANDROID_LOG_DEBUG, "TNS.Native.Profiler", "Time: %lld, File: %s, Line: %d", (long long)f.time, f.fileName, f.lineNumber);
    }
}

std::vector<SimpleProfiler::FrameEntry> SimpleProfiler::s_frames;

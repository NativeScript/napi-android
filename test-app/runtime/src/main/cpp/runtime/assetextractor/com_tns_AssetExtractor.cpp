#include "jni.h"
#include <sstream>
#include "AssetExtractor.h"
#include "NativeScriptException.h"

using namespace tns;

extern "C" JNIEXPORT void Java_com_tns_AssetExtractor_extractAssets(JNIEnv* env, jobject obj, jstring apk, jstring inputDir, jstring outputDir, jboolean _forceOverwrite) {
    try {
        AssetExtractor::ExtractAssets(env, obj, apk, inputDir, outputDir, _forceOverwrite);
    } catch (NativeScriptException& e) {
        e.ReThrowToJava(nullptr);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(nullptr);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(nullptr);
    }
}
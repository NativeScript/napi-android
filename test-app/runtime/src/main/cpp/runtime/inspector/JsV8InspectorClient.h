#ifndef JSV8INSPECTORCLIENT_H_
#define JSV8INSPECTORCLIENT_H_

#ifdef __V8__

#include <string>
#include <vector>
#include <src/inspector/v8-console-message.h>
#include "v8.h"
#include "JEnv.h"
#include "Runtime.h"
#include "v8-inspector.h"
#include "v8_inspector/ns-v8-tracing-agent-impl.h"

using namespace v8_inspector;

namespace tns {

    inline static v8::Local<v8::String> ConvertToV8String(v8::Isolate* isolate, const jchar* data, int length) {
        return v8::String::NewFromTwoByte(isolate, (const uint16_t*) data, v8::NewStringType::kNormal, length).ToLocalChecked();
    }

    inline static v8::Local<v8::String> ConvertToV8String(v8::Isolate* isolate, const std::string& s) {
        return v8::String::NewFromUtf8(isolate, s.c_str(), v8::NewStringType::kNormal, s.length()).ToLocalChecked();
    }

    inline static v8::Local<v8::String> ConvertToV8String(v8::Isolate* isolate, const char* data, int length)  {
        return v8::String::NewFromUtf8(isolate, (const char*) data, v8::NewStringType::kNormal, length).ToLocalChecked();
    }

    inline static v8::Local<v8::String> ConvertToV8UTF16String(v8::Isolate* isolate, const std::u16string& utf16string) {
        return v8::String::NewFromTwoByte(isolate, ((const uint16_t*) utf16string.data())).ToLocalChecked();
    }

    inline static v8::Local<v8::String> ToV8String(v8::Isolate *isolate, const std::string &value) {
        return v8::String::NewFromUtf8(isolate, value.c_str(), v8::NewStringType::kNormal,
                                       (int) value.length()).ToLocalChecked();
    }

    inline static std::string ConvertToString(const v8::Local<v8::String>& s) {
        if (s.IsEmpty()) {
            return {};
        } else {
            auto isolate = v8::Isolate::GetCurrent();
            v8::String::Utf8Value str(isolate, s);
            return {*str};
        }
    }


    static v8::Local<v8::Value> jstringToV8String(v8::Isolate* isolate, jstring value) {
        if (value == nullptr) {
            return Null(isolate);
        }

        JEnv env;
        auto chars = env.GetStringChars(value, NULL);
        auto length = env.GetStringLength(value);
        auto v8String = tns::ConvertToV8String(isolate, chars, length);
        env.ReleaseStringChars(value, chars);

        return v8String;
    }

    inline static std::string ToString(v8::Isolate *isolate, const v8::Local<v8::Value> &value) {
        if (value.IsEmpty()) {
            return std::string();
        }

        if (value->IsStringObject()) {
            v8::Local<v8::String> obj = value.As<v8::StringObject>()->ValueOf();
            return ToString(isolate, obj);
        }

        v8::String::Utf8Value result(isolate, value);

        const char *val = *result;
        if (val == nullptr) {
            return std::string();
        }

        return std::string(*result, result.length());
    }

    static std::string jstringToString(jstring value) {
        if (value == nullptr) {
            return {};
        }

        JEnv env;

        jboolean f = JNI_FALSE;
        auto chars = env.GetStringUTFChars(value, &f);
        std::string s(chars);
        env.ReleaseStringUTFChars(value, chars);

        return s;
    }

class JsV8InspectorClient : V8InspectorClient, v8_inspector::V8Inspector::Channel {
    public:
        static JsV8InspectorClient* GetInstance();

        void init();
        void connect(jobject connection);
        void scheduleBreak();
        void disconnect();
        void dispatchMessage(const std::string& message);

        void registerModules();

        // Overrides of V8Inspector::Channel
        void sendResponse(int callId, std::unique_ptr<StringBuffer> message) override;
        void sendNotification(const std::unique_ptr<StringBuffer> message) override;
        void flushProtocolNotifications() override;

        static void sendToFrontEndCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
        static void consoleLogCallback(napi_env env, ConsoleAPIType method, const std::vector<v8::Local<v8::Value>>& args);
        static void inspectorSendEventCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
        static void registerDomainDispatcherCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
        static void inspectorTimestampCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

        // Overrides of V8InspectorClient
        void runMessageLoopOnPause(int context_group_id) override;
        void quitMessageLoopOnPause() override;

        static bool inspectorIsConnected() {
            return JsV8InspectorClient::GetInstance()->isConnected_;
        }

        static std::map<std::string, v8::Persistent<v8::Object>*> Domains;

    private:
        JsV8InspectorClient(napi_env env);

        // Override of V8InspectorClient
        v8::Local<v8::Context> ensureDefaultContextInGroup(int contextGroupId) override;

        void createInspectorSession();
        void doDispatchMessage(const std::string& message);

        static void InspectorIsConnectedGetterCallback(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info);

        static JsV8InspectorClient* instance;
        static constexpr int contextGroupId = 1;

        std::unique_ptr<tns::inspector::TracingAgentImpl> tracing_agent_;
        napi_env env;
        std::unique_ptr<V8Inspector> inspector_;
        v8::Persistent<v8::Context> context_;
        std::unique_ptr<V8InspectorSession> session_;
        jclass inspectorClass_;
        jmethodID sendMethod_;
        jmethodID getInspectorMessageMethod_;
        jmethodID sendToDevToolsConsoleMethod_;
        jobject connection_;
        bool running_nested_loop_ : 1;
        bool terminated_ : 1;
        bool isConnected_ : 1;


    // {N} specific helpers
    bool CallDomainHandlerFunction(v8::Local<v8::Context> context,
                                   v8::Local<v8::Function> domainMethodFunc,
                                   const v8::Local<v8::Object>& arg,
                                   v8::Local<v8::Object>& domainDebugger,
                                   v8::Local<v8::Value>& result);
    std::string GetReturnMessageFromDomainHandlerResult(const v8::Local<v8::Value>& result, const v8::Local<v8::Value>& requestId);
};
}


#endif

#endif /* JSV8INSPECTORCLIENT_H_ */

#ifndef UTIL_H_
#define UTIL_H_

#include <string>
#include <vector>
#ifdef  __V8__
#include <v8.h>
#endif

namespace tns {
class Util {
    public:
        static std::string JniClassPathToCanonicalName(const std::string& jniClassPath);

        static void SplitString(const std::string& str, const std::string& delimiters, std::vector<std::string>& tokens);

        static void JoinString(const std::vector<std::string>& list, const std::string& delimiter, std::string& out);

        static bool EndsWith(const std::string& str, const std::string& suffix);
        static bool Contains(const std::string &str, const std::string &sequence);

        static std::string ConvertFromJniToCanonicalName(const std::string& name);

        static std::string ConvertFromCanonicalToJniName(const std::string& name);

        static std::string ReplaceAll(std::string& str, const std::string& from, const std::string& to);

        static std::u16string ConvertFromUtf8ToUtf16(const std::string& str);

       // static std::uint16_t* ConvertFromUtf8ToProtocolUtf16(const std::string& str);
       static std::vector<uint16_t> ToVector(const std::string &value);

#ifdef __V8__
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
#endif
};

}

#endif /* UTIL_H_ */
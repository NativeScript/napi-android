#ifndef JSARGCONVERTER_H_
#define JSARGCONVERTER_H_

#include <string>
#include <vector>
#include "JEnv.h"
#include "Runtime.h"
#include "MetadataEntry.h"

namespace tns {

    enum class BufferCastType {
        Byte,
        Short,
        Int,
        Long,
        Float,
        Double
    };

    class JsArgConverter {
    public:

        JsArgConverter(napi_env env, napi_value caller, napi_value* args, size_t argc, const std::string& methodSignature, MetadataEntry* entry);

        JsArgConverter(napi_env env, napi_value* args, size_t argc, bool hasImplementationObject, const std::string& methodSignature, MetadataEntry* entry);

        JsArgConverter(napi_env env, napi_value* args, size_t argc, const std::string& methodSignature);

        ~JsArgConverter();

        jvalue* ToArgs();

        int Length() const;

        bool IsValid() const;

        struct Error;

        Error GetError() const;

        struct Error {
            Error() :
                    index(-1), msg(std::string()) {
            }

            int index;
            std::string msg;
        };

        static BufferCastType GetCastType(napi_typedarray_type type);

        static JniLocalRef GetByteBuffer(napi_env env, napi_value object, bool isArrayBuffer, bool isTypedArray, bool isDataView);



        static jmethodID BYTE_ORDER_METHOD_ID;
        static jmethodID BYTE_ORDER_ENUM_ID;
        static jmethodID AS_SHORT_BUFFER;
        static jmethodID AS_INT_BUFFER;
        static jmethodID AS_LONG_BUFFER;
        static jmethodID AS_FLOAT_BUFFER;
        static jmethodID AS_DOUBLE_BUFFER;
    private:

        bool ConvertArg(napi_env env, napi_value arg, int index);

        bool ConvertJavaScriptArray(napi_env env, napi_value jsArr, int index);

        bool ConvertJavaScriptNumber(napi_env env, napi_value jsValue, int index, bool isNumberObject);

        bool ConvertJavaScriptBoolean(napi_env env, napi_value jsValue, int index);

        bool ConvertJavaScriptString(napi_env env, napi_value jsValue, int index);

        void SetConvertedObject(int index, jobject obj, bool isGlobal = false);


        template<typename T>
        bool ConvertFromCastFunctionObject(T value, int index);

        napi_env m_env;

        int m_argsLen;

        bool m_isValid;

        jvalue m_args[255];
        int m_args_refs[255];
        int m_args_refs_size = 0;

        std::string m_methodSignature;

        std::vector<std::string> m_tokens;

        Error m_error;
    };
}

#endif /* JSARGCONVERTER_H_ */
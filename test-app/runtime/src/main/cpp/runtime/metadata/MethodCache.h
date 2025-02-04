#ifndef METHODCACHE_H_
#define METHODCACHE_H_

#include <string>
#include <map>
#include "JEnv.h"
#include "MetadataEntry.h"
#include "ArgsWrapper.h"
#include "NativeScriptAssert.h"
#include "MetadataReader.h"
#include "Runtime.h"
#include "MetadataNode.h"
#include "NumericCasts.h"
#include "NativeScriptException.h"
#include "JsArgToArrayConverter.h"
#include "Util.h"

namespace tns {
/*
 * MethodCache: class dealing with method/constructor resolution.
 */
class MethodCache {
    public:
        /*
         * CacheMethodInfo: struct holding resolved methods/constructor resolution
         */
        struct CacheMethodInfo {
            CacheMethodInfo()
                :
                retType(MethodReturnType::Unknown), mid(nullptr), clazz(nullptr), isStatic(false) {
            }
            std::string signature;
            std::string returnType;
            MethodReturnType retType;
            jmethodID mid;
            jclass clazz;
            bool isStatic;
        };

        static void Init();

    inline static MethodCache::CacheMethodInfo ResolveMethodSignature(napi_env env, const string &className, const string &methodName, size_t argc, napi_value* argv, bool isStatic)
    {
        CacheMethodInfo method_info;

        auto encoded_method_signature = EncodeSignature(env, className, methodName,argc, argv, isStatic);
        auto it = s_method_ctor_signature_cache.find(encoded_method_signature);

        if (it == s_method_ctor_signature_cache.end())
        {
            auto signature = ResolveJavaMethod(env, argc, argv, className, methodName);

            DEBUG_WRITE("ResolveMethodSignature %s='%s'", encoded_method_signature.c_str(), signature.c_str());

            if (!signature.empty())
            {
                JEnv jEnv;
                auto clazz = jEnv.FindClass(className);
                assert(clazz != nullptr);
                method_info.clazz = clazz;
                method_info.signature = signature;
                method_info.returnType = MetadataReader::ParseReturnType(method_info.signature);
                method_info.retType = MetadataReader::GetReturnType(method_info.returnType);
                method_info.isStatic = isStatic;
                method_info.mid = isStatic
                                  ? jEnv.GetStaticMethodID(clazz, methodName, signature)
                                  : jEnv.GetMethodID(clazz, methodName, signature);

                s_method_ctor_signature_cache.emplace(encoded_method_signature, method_info);
            }
        }
        else
        {
            method_info = (*it).second;
        }

        return method_info;
    }

    inline static MethodCache::CacheMethodInfo ResolveConstructorSignature(napi_env env, const ArgsWrapper &argWrapper, const string &fullClassName, jclass javaClass, bool isInterface)
    {
        CacheMethodInfo constructor_info;

        auto encoded_ctor_signature = EncodeSignature(env, fullClassName, "<init>", argWrapper.argc, argWrapper.argv, false);
        auto it = s_method_ctor_signature_cache.find(encoded_ctor_signature);

        if (it == s_method_ctor_signature_cache.end())
        {
            auto signature = ResolveConstructor(env, argWrapper.argc, argWrapper.argv, javaClass, isInterface);

            DEBUG_WRITE("ResolveConstructorSignature %s='%s'", encoded_ctor_signature.c_str(), signature.c_str());

            if (!signature.empty())
            {
                JEnv jEnv;
                constructor_info.clazz = javaClass;
                constructor_info.signature = signature;
                constructor_info.mid = jEnv.GetMethodID(javaClass, "<init>", signature);

                s_method_ctor_signature_cache.emplace(encoded_ctor_signature, constructor_info);
            }
        }
        else
        {
            constructor_info = (*it).second;
        }

        return constructor_info;
    }

private:
        MethodCache() {
        }

    // Encoded signature <className>.S/I.<methodName>.<argsCount>.<arg1class>.<...>
    inline static string EncodeSignature(napi_env env, const string &className, const string &methodName, size_t argc, napi_value* argv, bool isStatic)
    {
        string sig(className);
        sig.append(".");
        if (isStatic)
        {
            sig.append("S.");
        }
        else
        {
            sig.append("I.");
        }
        sig.append(methodName);
        sig.append(".");

        stringstream s;
        s << argc;
        sig.append(s.str());

        for (int i = 0; i < argc; i++)
        {
            sig.append(".");
            sig.append(GetType(env, argv[i]));
        }

        return sig;
    }

    inline static string GetType(napi_env env, napi_value value)
    {
        napi_valuetype valueType;
        napi_typeof(env, value, &valueType);
        string type = "";

        if (valueType == napi_object || valueType == napi_function)
        {

            napi_value nullNode;
            napi_get_named_property(env, value, PROP_KEY_NULL_NODE_NAME, &nullNode);

            if (!napi_util::is_null_or_undefined(env, nullNode))
            {
                void *data;
                napi_get_value_external(env, nullNode, &data);
                auto treeNode = reinterpret_cast<MetadataNode *>(data);

                type = (treeNode != nullptr) ? treeNode->GetName() : "<unknown>";

                DEBUG_WRITE("Parameter of type %s with NULL value is passed to the method.", type.c_str());
                return type;
            }
        }


        if (valueType == napi_string) {
            type = "string";
        } else if (valueType == napi_null) {
            type = "null";
        } else if (valueType == napi_undefined) {
            type = "undefined";
        } else if (valueType == napi_number) {
            type = "number";
        } else if (valueType == napi_object) {
            type = "object";
        } else if (napi_util::is_array(env, value)) {
            type = "array";
        } else if (valueType == napi_function) {
            type = "function";
        } else if (napi_util::is_typedarray(env, value)) {
            type = "typedarray";
        } else if (valueType == napi_boolean) {
            type = "bool";
        } else if (napi_util::is_dataview(env, value)) {
            type = "view";
        } else if (napi_util::is_date(env, value)) {
            type = "date";
        }

        // Handle special cases for typed arrays
        if (type == "typedarray")
        {
            napi_typedarray_type arrayType;
            napi_get_typedarray_info(env, value, &arrayType, nullptr, nullptr, nullptr, nullptr);
            switch (arrayType)
            {
                case napi_int8_array:
                case napi_uint8_array:
                case napi_uint8_clamped_array:
                    type = "bytebuffer";
                    break;
                case napi_int16_array:
                case napi_uint16_array:
                    type = "shortbuffer";
                    break;
                case napi_int32_array:
                case napi_uint32_array:
                    type = "intbuffer";
                    break;
                case napi_bigint64_array:
                case napi_biguint64_array:
                    type = "longbuffer";
                    break;
                case napi_float32_array:
                    type = "floatbuffer";
                    break;
                case napi_float64_array:
                    type = "doublebuffer";
                    break;
                default:
                    type = "<unknown>";
            }
        }

        // Handle special cases for numbers
        if (type == "number")
        {
            double d;
            napi_get_value_double(env, value, &d);
            int64_t i = (int64_t)d;
            bool isInteger = d == i;
            type = isInteger ? "intnumber" : "doublenumber";
        }

        // Handle special cases for objects
        if (type == "object" || type == "function")
        {
            auto castType = NumericCasts::GetCastType(env, value);
            MetadataNode *node;

            switch (castType)
            {
                case CastType::Char:
                    type = "char";
                    break;
                case CastType::Byte:
                    type = "byte";
                    break;
                case CastType::Short:
                    type = "short";
                    break;
                case CastType::Long:
                    type = "long";
                    break;
                case CastType::Float:
                    type = "float";
                    break;
                case CastType::Double:
                    type = "double";
                    break;
                case CastType::None:
                    node = MetadataNode::GetNodeFromHandle(env, value);
                    type = (node != nullptr) ? node->GetName() : "<unknown>";

                    if (type == "<unknown>") {
                        if (napi_util::is_number_object(env, value)) {
                            napi_value numValue = napi_util::valueOf(env, value);
                            bool isFloat = napi_util::is_float(env, numValue);
                            if (isFloat) {
                                type = "float";
                            } else {
                                type = "int";
                            }
                        } else if (napi_util::is_string_object(env, value)) {
                            type = "string";
                        } else if (napi_util::is_number_object(env, value)) {
                            type = "bool";
                        }
                    }

                    break;
                default:
                    throw NativeScriptException("Unsupported cast type");
            }
        }

        if (type == "undefined") {
            type = "null";
        }

        return type;
    }

    inline static string ResolveJavaMethod(napi_env env , size_t argc, napi_value* argv, const string &className, const string &methodName)
    {
        JEnv jEnv;

        JsArgToArrayConverter argConverter(env, argc, argv, false);

        auto canonicalClassName = Util::ConvertFromJniToCanonicalName(className);
        JniLocalRef jsClassName(jEnv.NewStringUTF(canonicalClassName.c_str()));
        JniLocalRef jsMethodName(jEnv.NewStringUTF(methodName.c_str()));

        jobjectArray arrArgs = argConverter.ToJavaArray();

        auto runtime = Runtime::GetRuntime(env);

        jstring signature = (jstring)jEnv.CallObjectMethod(runtime->GetJavaRuntime(), RESOLVE_METHOD_OVERLOAD_METHOD_ID, (jstring)jsClassName, (jstring)jsMethodName, arrArgs);

        string resolvedSignature;

        const char *str = jEnv.GetStringUTFChars(signature, nullptr);
        resolvedSignature = string(str);
        jEnv.ReleaseStringUTFChars(signature, str);

        jEnv.DeleteLocalRef(signature);

        return resolvedSignature;
    }

    inline static string ResolveConstructor(napi_env env, size_t argc, napi_value* argv, jclass javaClass, bool isInterface)
    {
        JEnv jEnv;
        string resolvedSignature;

        JsArgToArrayConverter argConverter(env, argc, argv, isInterface);
        if (argConverter.IsValid())
        {
            jobjectArray javaArgs = argConverter.ToJavaArray();

            auto runtime = Runtime::GetRuntime(env);

            jstring signature = (jstring)jEnv.CallObjectMethod(runtime->GetJavaRuntime(), RESOLVE_CONSTRUCTOR_SIGNATURE_ID, javaClass, javaArgs);

            const char *str = jEnv.GetStringUTFChars(signature, nullptr);
            resolvedSignature = string(str);
            jEnv.ReleaseStringUTFChars(signature, str);
            jEnv.DeleteLocalRef(signature);
        }
        else
        {
            JsArgToArrayConverter::Error err = argConverter.GetError();
            throw NativeScriptException(err.msg);
        }

        return resolvedSignature;
    }

    static jclass RUNTIME_CLASS;

        static jmethodID RESOLVE_METHOD_OVERLOAD_METHOD_ID;

        static jmethodID RESOLVE_CONSTRUCTOR_SIGNATURE_ID;

        /*
         * "s_method_ctor_signature_cache" holding all resolved CacheMethodInfo against an encoded_signature string.
         *  Used for caching the resolved constructor or method signature.
         * The encoded signature has template: <className>.S/I.<methodName>.<argsCount>.<arg1class>.<...>
         */
        static robin_hood::unordered_map<std::string, CacheMethodInfo> s_method_ctor_signature_cache;
};
}

#endif /* METHODCACHE_H_ */


#include "MethodCache.h"
#include "JniLocalRef.h"
#include "JsArgToArrayConverter.h"
#include "MetadataNode.h"
#include "NativeScriptAssert.h"
#include "Util.h"
#include "ArgConverter.h"
#include "NumericCasts.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include <sstream>

using namespace std;
using namespace ns;

void MethodCache::Init()
{
    JEnv jEnv;

    RUNTIME_CLASS = jEnv.FindClass("org/nativescript/runtime/napi/Runtime");
    assert(RUNTIME_CLASS != nullptr);

    RESOLVE_METHOD_OVERLOAD_METHOD_ID = jEnv.GetMethodID(RUNTIME_CLASS, "resolveMethodOverload", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;");
    assert(RESOLVE_METHOD_OVERLOAD_METHOD_ID != nullptr);

    RESOLVE_CONSTRUCTOR_SIGNATURE_ID = jEnv.GetMethodID(RUNTIME_CLASS, "resolveConstructorSignature", "(Ljava/lang/Class;[Ljava/lang/Object;)Ljava/lang/String;");
    assert(RESOLVE_CONSTRUCTOR_SIGNATURE_ID != nullptr);
}

MethodCache::CacheMethodInfo MethodCache::ResolveMethodSignature(napi_env env, const string &className, const string &methodName, napi_callback_info info, bool isStatic)
{
    CacheMethodInfo method_info;

    auto encoded_method_signature = EncodeSignature(env, className, methodName, info, isStatic);
    auto it = s_method_ctor_signature_cache.find(encoded_method_signature);

    if (it == s_method_ctor_signature_cache.end())
    {
        auto signature = ResolveJavaMethod(env, info, className, methodName);

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

MethodCache::CacheMethodInfo MethodCache::ResolveConstructorSignature(napi_env env, const ArgsWrapper &argWrapper, const string &fullClassName, jclass javaClass, bool isInterface)
{
    CacheMethodInfo constructor_info;

    auto &args = argWrapper.args;
    auto encoded_ctor_signature = EncodeSignature(env, fullClassName, "<init>", args, false);
    auto it = s_method_ctor_signature_cache.find(encoded_ctor_signature);

    if (it == s_method_ctor_signature_cache.end())
    {
        auto signature = ResolveConstructor(env, args, javaClass, isInterface);

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

// Encoded signature <className>.S/I.<methodName>.<argsCount>.<arg1class>.<...>
string MethodCache::EncodeSignature(napi_env env, const string &className, const string &methodName, napi_callback_info info, bool isStatic)
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

    size_t argc;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr);
    std::vector<napi_value> argv(argc);
    if (argc > 0) {
        napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr);
    }

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

string MethodCache::GetType(napi_env env, napi_value value)
{
    napi_valuetype valueType;
    napi_typeof(env, value, &valueType);
    string type = "";

    if (napi_util::is_of_type(env, value, napi_object))
    {

        napi_value nullNode;
        napi_get_named_property(env, value, PROP_KEY_NULL_NODE_NAME, &nullNode);

        if (nullNode != nullptr && !napi_util::is_undefined(env, nullNode))
        {
            void *data;
            napi_get_value_external(env, nullNode, &data);
            auto treeNode = reinterpret_cast<MetadataNode *>(data);

            type = (treeNode != nullptr) ? treeNode->GetName() : "<unknown>";

            DEBUG_WRITE("Parameter of type %s with NULL value is passed to the method.", type.c_str());
            return type;
        }
    }

    

    // Helper function to check if a value is of a specific type
    static auto is_type = [&](const std::function<napi_status(napi_env, napi_value, bool *)>& check_func) -> bool
    {
        bool result;
        check_func(env, value, &result);
        return result;
    };

    // Map of type checks to type strings
    static std::unordered_map<std::string, std::function<bool()>> type_map = {
        {"array", [&]() { return is_type(napi_is_array); }},
        {"typedarray", [&]() { return is_type(napi_is_typedarray); }},
        {"bool", [&]() { return napi_util::is_of_type(env, value, napi_boolean); }},
        {"view", [&]() { return is_type(napi_is_dataview); }},
        {"date", [&]() { return is_type(napi_is_date); }},
        {"function", [&]() { return napi_util::is_of_type(env, value, napi_function); }},
        {"null", [&]() { return napi_util::is_of_type(env, value, napi_null); }},
        {"undefined", [&]() { return napi_util::is_of_type(env, value, napi_undefined); }},
        {"string", [&]() { return napi_util::is_of_type(env, value, napi_string); }},
        {"number", [&]() { return napi_util::is_of_type(env, value, napi_number); }},
        {"object", [&]() { return napi_util::is_of_type(env, value, napi_object); }}
    };

    // Iterate through the map to determine the type
    for (const auto &entry : type_map)
    {
        if (entry.second())
        {
            type = entry.first;
            break;
        }
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
    if (type == "object")
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

string MethodCache::ResolveJavaMethod(napi_env env, napi_callback_info info, const string &className, const string &methodName)
{
    JEnv jEnv;

    JsArgToArrayConverter argConverter(env, info, false);

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

string MethodCache::ResolveConstructor(napi_env env, napi_callback_info info, jclass javaClass, bool isInterface)
{
    JEnv jEnv;
    string resolvedSignature;

    JsArgToArrayConverter argConverter(env, info, isInterface);
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

robin_hood::unordered_map<string, MethodCache::CacheMethodInfo> MethodCache::s_method_ctor_signature_cache;
jclass MethodCache::RUNTIME_CLASS = nullptr;
jmethodID MethodCache::RESOLVE_METHOD_OVERLOAD_METHOD_ID = nullptr;
jmethodID MethodCache::RESOLVE_CONSTRUCTOR_SIGNATURE_ID = nullptr;

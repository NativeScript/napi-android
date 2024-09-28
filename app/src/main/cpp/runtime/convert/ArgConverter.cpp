#include "ArgConverter.h"
// TODO #include "ObjectManager.h"
#include "Util.h"
#include "NativeScriptException.h"
#include "NumericCasts.h"
#include "NativeScriptAssert.h"
#include <sstream>

using namespace std;
using namespace ns;

void ArgConverter::Init(napi_env env, napi_value context) {
    auto cache = GetTypeLongCache(env);

    napi_value longNumberCtorFunc;
    napi_value longNumberPrototype;

    napi_value valueOfFunc;
    napi_value toStringFunc;

    napi_create_function(env, "NativeScriptLongFunctionCallback", NAPI_AUTO_LENGTH, ArgConverter::NativeScriptLongFunctionCallback, nullptr, &longNumberCtorFunc);
    napi_create_object(env, &longNumberPrototype);
    napi_create_function(env, "NativeScriptLongValueOfFunctionCallback", NAPI_AUTO_LENGTH, ArgConverter::NativeScriptLongValueOfFunctionCallback, nullptr, &valueOfFunc);
    napi_create_function(env, "NativeScriptLongToStringFunctionCallback", NAPI_AUTO_LENGTH, ArgConverter::NativeScriptLongToStringFunctionCallback, nullptr, &toStringFunc);

    napi_set_named_property(env, longNumberPrototype, "valueOf", valueOfFunc);
    napi_set_named_property(env, longNumberPrototype, "toString", toStringFunc);
    napi_set_named_property(env, longNumberCtorFunc, "prototype", longNumberPrototype);


    cache->LongNumberCtorFunc = napi_util::make_ref(env, longNumberCtorFunc, 1);
    napi_value nanValue;
    napi_create_double(env, numeric_limits<double>::quiet_NaN(), &nanValue);
    cache->NanNumberObject = napi_util::make_ref(env, nanValue, 1);
}

napi_value ArgConverter::NativeScriptLongValueOfFunctionCallback(napi_env env, napi_callback_info info) {
    try {
        napi_value result;
        napi_create_double(env, numeric_limits<double>::quiet_NaN(), &result);
        return result;
    } catch (NativeScriptException& e) {
//        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
//        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
//        nsEx.ReThrowToNapi(env);
    }
}

napi_value ArgConverter::NativeScriptLongToStringFunctionCallback(napi_env env, napi_callback_info info) {
    try {
        napi_value thisArg;
        napi_get_cb_info(env, info, nullptr, nullptr, &thisArg, nullptr);

        napi_value value;
        napi_get_named_property(env, thisArg, "value", &value);

        return value;
    } catch (NativeScriptException& e) {
//        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
//        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
//        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

napi_value ArgConverter::NativeScriptLongFunctionCallback(napi_env env, napi_callback_info info) {
    try {
        NAPI_CALLBACK_BEGIN(1);

        auto cache = GetTypeLongCache(env);
        napi_value javaLong;
        napi_get_boolean(env, true, &javaLong);
        napi_set_named_property(env, jsThis, "javaLong", javaLong);

        NumericCasts::MarkAsLong(env, jsThis, argv[0]);

        napi_util::napi_inherits(env, jsThis, napi_util::get_ref_value(env, cache->NanNumberObject));

        return jsThis;

    } catch (NativeScriptException& e) {
//        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
//        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
//        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

napi_value ArgConverter::ConvertJavaArgsToJsArgs(napi_env env, jobjectArray args) {
    JEnv jenv;

    int argc = jenv.GetArrayLength(args) / 3;
    napi_value arr;
    napi_create_array_with_length(env, argc, &arr);

    auto runtime = Runtime::GetRuntime(env);
//    auto objectManager = runtime->GetObjectManager();

    int jArrayIndex = 0;
    for (int i = 0; i < argc; i++) {
        JniLocalRef argTypeIDObj(jenv.GetObjectArrayElement(args, jArrayIndex++));
        JniLocalRef arg(jenv.GetObjectArrayElement(args, jArrayIndex++));
        JniLocalRef argJavaClassPath(jenv.GetObjectArrayElement(args, jArrayIndex++));

        Type argTypeID = (Type) JType::IntValue(jenv, argTypeIDObj);

        napi_value jsArg;
        switch (argTypeID) {
            case Type::Boolean:
                napi_get_boolean(env, JType::BooleanValue(jenv, arg), &jsArg);
                break;
            case Type::Char:
                jsArg = jcharToJsString(env, JType::CharValue(jenv, arg));
                break;
            case Type::Byte:
                napi_create_int32(env, JType::ByteValue(jenv, arg), &jsArg);
                break;
            case Type::Short:
                napi_create_int32(env, JType::ShortValue(jenv, arg), &jsArg);
                break;
            case Type::Int:
                napi_create_int32(env, JType::IntValue(jenv, arg), &jsArg);
                break;
            case Type::Long:
                napi_create_int64(env, JType::LongValue(jenv, arg), &jsArg);
                break;
            case Type::Float:
                napi_create_double(env, JType::FloatValue(jenv, arg), &jsArg);
                break;
            case Type::Double:
                napi_create_double(env, JType::DoubleValue(jenv, arg), &jsArg);
                break;
            case Type::String:
                jsArg = jstringToJsString(env, (jstring) arg);
                break;
            case Type::JsObject: {
//                jint javaObjectID = JType::IntValue(jenv, arg);
//                jsArg = objectManager->GetJsObjectByJavaObject(javaObjectID);
//
//                if (jsArg == nullptr) {
//                    string argClassName = jstringToString(ObjectToString(argJavaClassPath));
//                    argClassName = Util::ConvertFromCanonicalToJniName(argClassName);
//                    jsArg = objectManager->CreateJSWrapper(javaObjectID, argClassName);
//                }
                break;
            }
            case Type::Null:
                napi_get_null(env, &jsArg);
                break;
        }

        napi_set_element(env, arr, i, jsArg);
    }

    return arr;
}

napi_value ArgConverter::ConvertFromJavaLong(napi_env env, jlong value) {
    napi_value convertedValue;
    long long longValue = value;

    if ((-JS_LONG_LIMIT < longValue) && (longValue < JS_LONG_LIMIT)) {
        napi_create_double(env, longValue, &convertedValue);
    } else {
        auto cache = GetTypeLongCache(env);
        char strNumber[24];
        sprintf(strNumber, "%lld", longValue);
        napi_value strValue;
        napi_create_string_utf8(env, strNumber, NAPI_AUTO_LENGTH, &strValue);
        napi_value args[1] = { strValue };

        napi_new_instance(env, napi_util::get_ref_value(env, cache->LongNumberCtorFunc), 1, args, &convertedValue);
    }

    return convertedValue;
}

int64_t ArgConverter::ConvertToJavaLong(napi_env env, napi_value value) {
    napi_value valueProp;
    napi_get_named_property(env, value, "value", &valueProp);

    size_t str_len;
    napi_get_value_string_utf8(env, valueProp, nullptr, 0, &str_len);
    string num(str_len, '\0');
    napi_get_value_string_utf8(env, valueProp, &num[0], str_len + 1, &str_len);

    int64_t longValue = atoll(num.c_str());

    return longValue;
}

ArgConverter::TypeLongOperationsCache* ArgConverter::GetTypeLongCache(napi_env env) {
    TypeLongOperationsCache* cache;
    auto itFound = s_type_long_operations_cache.find(env);
    if (itFound == s_type_long_operations_cache.end()) {
        cache = new TypeLongOperationsCache;
        s_type_long_operations_cache.emplace(env, cache);
    } else {
        cache = itFound->second;
    }

    return cache;
}

u16string ArgConverter::ConvertToUtf16String(napi_env env, napi_value s) {
    if (s == nullptr) {
        return {};
    } else {
        size_t str_len;
        napi_get_value_string_utf8(env, s, nullptr, 0, &str_len);
        string str(str_len, '\0');
        napi_get_value_string_utf8(env, s, &str[0], str_len + 1, &str_len);
        auto utf16str = Util::ConvertFromUtf8ToUtf16(str);

        return utf16str;
    }
}

void ArgConverter::onDisposeIsolate(napi_env env) {
    auto itFound = s_type_long_operations_cache.find(env);
    if (itFound != s_type_long_operations_cache.end()) {
        delete itFound->second;
        s_type_long_operations_cache.erase(itFound);
    }
}

robin_hood::unordered_map<napi_env, ArgConverter::TypeLongOperationsCache*> ArgConverter::s_type_long_operations_cache;
#include "ArgConverter.h"
#include "ObjectManager.h"
#include "Util.h"
#include "NativeScriptException.h"
#include "NumericCasts.h"
#include "NativeScriptAssert.h"
#include <sstream>
#ifdef USE_MIMALLOC
#include "mimalloc.h"
#endif


using namespace std;
using namespace tns;

void ArgConverter::Init(napi_env env) {
    napi_status status;
    auto cache = GetTypeLongCache(env);

    napi_value longNumberCtorFunc;
    napi_value valueOfFunc;
    napi_value toStringFunc;

    NAPI_GUARD(napi_define_class(env, "NativeScriptLongNumber", NAPI_AUTO_LENGTH, ArgConverter::NativeScriptLongFunctionCallback,nullptr, 0, nullptr, &longNumberCtorFunc)) {
        return;
    }

    napi_value longNumberPrototype = napi_util::get_prototype(env, longNumberCtorFunc);

    NAPI_GUARD(napi_create_function(env, "valueOf", strlen("valueOf"),
                         ArgConverter::NativeScriptLongValueOfFunctionCallback, nullptr,
                         &valueOfFunc)) {
        return;
    }

    NAPI_GUARD(napi_create_function(env, "toString", strlen("toString"),
                         ArgConverter::NativeScriptLongToStringFunctionCallback, nullptr,
                         &toStringFunc)) {
        return;
    }


    NAPI_GUARD(napi_set_named_property(env, longNumberPrototype, "valueOf", valueOfFunc)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, longNumberPrototype, "toString", toStringFunc)) {
        return;
    }

    cache->LongNumberCtorFunc = napi_util::make_ref(env, longNumberCtorFunc, 1);
    napi_value nanValue;
    NAPI_GUARD(napi_create_double(env, numeric_limits<double>::quiet_NaN(), &nanValue)) {
        return;
    }


    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    napi_value numCtor;
    NAPI_GUARD(napi_get_named_property(env, global, "Number", &numCtor)) {
        return;
    }

    napi_value nanObject;
    NAPI_GUARD(napi_new_instance(env, numCtor, 1, &nanValue, &nanObject)) {
        return;
    }

    cache->NanNumberObject = napi_util::make_ref(env, nanObject, 1);
}

napi_value ArgConverter::NativeScriptLongValueOfFunctionCallback(napi_env env, napi_callback_info info) {
    try {
        napi_status status;
        napi_value result;
        NAPI_GUARD(napi_create_double(env, numeric_limits<double>::quiet_NaN(), &result)) {
            return nullptr;
        }
        return result;
    } catch (NativeScriptException &e) {
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
    return nullptr;
}

napi_value ArgConverter::NativeScriptLongToStringFunctionCallback(napi_env env, napi_callback_info info) {
    try {
        napi_status status;
        napi_value thisArg;
        NAPI_GUARD(napi_get_cb_info(env, info, nullptr, nullptr, &thisArg, nullptr)) {
            return nullptr;
        }

        napi_value value;
        NAPI_GUARD(napi_get_named_property(env, thisArg, "value", &value)) {
            return nullptr;
        }

        return value;
    } catch (NativeScriptException &e) {
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
    return nullptr;
}

napi_value ArgConverter::NativeScriptLongFunctionCallback(napi_env env, napi_callback_info info) {
    try {
        NAPI_CALLBACK_BEGIN(1);
        auto cache = GetTypeLongCache(env);
        napi_value javaLong;
        NAPI_GUARD(napi_get_boolean(env, true, &javaLong)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_named_property(env, jsThis, "javaLong", javaLong)) {
            return nullptr;
        }

        NumericCasts::MarkAsLong(env, jsThis, argv[0]);

        NAPI_GUARD(napi_set_named_property(env, jsThis, "prototype", napi_util::get_ref_value(env, cache->NanNumberObject))) {
            return nullptr;
        }
        return jsThis;

    } catch (NativeScriptException &e) {
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
    return nullptr;
}

void ArgConverter::ConvertJavaArgsToJsArgs(napi_env env, jobjectArray args, size_t argc, napi_value* arr) {
    napi_status status;
    JEnv jenv;

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    int jArrayIndex = 0;
    for (int i = 0; i < argc; i++) {
        JniLocalRef argTypeIDObj(jenv.GetObjectArrayElement(args, jArrayIndex++));
        JniLocalRef arg(jenv.GetObjectArrayElement(args, jArrayIndex++));
        JniLocalRef argJavaClassPath(jenv.GetObjectArrayElement(args, jArrayIndex++));

        Type argTypeID = (Type) JType::IntValue(jenv, argTypeIDObj);

        napi_value jsArg;
        switch (argTypeID) {
            case Type::Boolean:
                NAPI_GUARD(napi_get_boolean(env, JType::BooleanValue(jenv, arg), &jsArg)) {}
                break;
            case Type::Char:
                jsArg = jcharToJsString(env, JType::CharValue(jenv, arg));
                break;
            case Type::Byte:
                NAPI_GUARD(napi_create_int32(env, JType::ByteValue(jenv, arg), &jsArg)) {}
                break;
            case Type::Short:
                NAPI_GUARD(napi_create_int32(env, JType::ShortValue(jenv, arg), &jsArg)) {}
                break;
            case Type::Int:
                NAPI_GUARD(napi_create_int32(env, JType::IntValue(jenv, arg), &jsArg)) {}
                break;
            case Type::Long:
                NAPI_GUARD(napi_create_int64(env, JType::LongValue(jenv, arg), &jsArg)) {}
                break;
            case Type::Float:
                NAPI_GUARD(napi_create_double(env, JType::FloatValue(jenv, arg), &jsArg)) {}
                break;
            case Type::Double:
                NAPI_GUARD(napi_create_double(env, JType::DoubleValue(jenv, arg), &jsArg)) {}
                break;
            case Type::String:
                jsArg = jstringToJsString(env, (jstring) arg);
                break;
            case Type::JsObject: {
                jint javaObjectID = JType::IntValue(jenv, arg);
                jsArg = objectManager->GetJsObjectByJavaObject(javaObjectID);

                if (napi_util::is_null_or_undefined(env, jsArg)) {
                    string argClassName = jstringToString(ObjectToString(argJavaClassPath));
                    argClassName = Util::ConvertFromCanonicalToJniName(argClassName);
                    jsArg = objectManager->CreateJSWrapper(javaObjectID, argClassName);
                }
                break;
            }
            case Type::Null:
                NAPI_GUARD(napi_get_null(env, &jsArg)) {}
                break;
        }

        arr[i] = jsArg;
    }

}

napi_value ArgConverter::ConvertFromJavaLong(napi_env env, jlong value) {
    napi_status status;
    napi_value convertedValue;
    long long longValue = value;

    if ((-JS_LONG_LIMIT < longValue) && (longValue < JS_LONG_LIMIT)) {
        NAPI_GUARD(napi_create_double(env, longValue, &convertedValue)) {
            return nullptr;
        }
    } else {
        auto cache = GetTypeLongCache(env);
        char strNumber[24];
        sprintf(strNumber, "%lld", longValue);
        napi_value strValue;
        NAPI_GUARD(napi_create_string_utf8(env, strNumber, NAPI_AUTO_LENGTH, &strValue)) {
            return nullptr;
        }
        napi_value args[1] = {strValue};

        NAPI_GUARD(napi_new_instance(env, napi_util::get_ref_value(env, cache->LongNumberCtorFunc), 1, args,
                          &convertedValue)) {
            return nullptr;
        }
    }

    return convertedValue;
}

int64_t ArgConverter::ConvertToJavaLong(napi_env env, napi_value value) {
    napi_status status;
    napi_value valueProp;
    NAPI_GUARD(napi_get_named_property(env, value, "value", &valueProp)) {
        return 0;
    }

    size_t str_len;
    NAPI_GUARD(napi_get_value_string_utf8(env, valueProp, nullptr, 0, &str_len)) {
        return 0;
    }
    string num(str_len, '\0');
    NAPI_GUARD(napi_get_value_string_utf8(env, valueProp, &num[0], str_len + 1, &str_len)) {
        return 0;
    }

    int64_t longValue = atoll(num.c_str());

    return longValue;
}

ArgConverter::TypeLongOperationsCache *ArgConverter::GetTypeLongCache(napi_env env) {
    TypeLongOperationsCache *cache;
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
        napi_status status;
        size_t str_len;
        NAPI_GUARD(napi_get_value_string_utf8(env, s, nullptr, 0, &str_len)) {
            return {};
        }
        string str(str_len, '\0');
        NAPI_GUARD(napi_get_value_string_utf8(env, s, &str[0], str_len + 1, &str_len)) {
            return {};
        }
        auto utf16str = Util::ConvertFromUtf8ToUtf16(str);

        return utf16str;
    }
}

void ArgConverter::onDisposeEnv(napi_env env) {
    napi_status status;
    auto itFound = s_type_long_operations_cache.find(env);
    if (itFound != s_type_long_operations_cache.end()) {
        if (itFound->second->LongNumberCtorFunc) {
            NAPI_GUARD(napi_delete_reference(env, itFound->second->LongNumberCtorFunc)) {}
        }
        if (itFound->second->NanNumberObject) {
            NAPI_GUARD(napi_delete_reference(env, itFound->second->NanNumberObject)) {}
        }
        delete itFound->second;
        s_type_long_operations_cache.erase(itFound);
    }
}

robin_hood::unordered_map<napi_env, ArgConverter::TypeLongOperationsCache *> ArgConverter::s_type_long_operations_cache;
#include "ArrayHelper.h"
#include "ArgConverter.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include <sstream>

using namespace std;
using namespace tns;

ArrayHelper::ArrayHelper() {
}

void ArrayHelper::Init(napi_env env) {
    JEnv jenv;

    RUNTIME_CLASS = jenv.FindClass("com/tns/Runtime");
    assert(RUNTIME_CLASS != nullptr);

    CREATE_ARRAY_HELPER = jenv.GetStaticMethodID(RUNTIME_CLASS, "createArrayHelper", "(Ljava/lang/String;I)Ljava/lang/Object;");
    assert(CREATE_ARRAY_HELPER != nullptr);

    napi_value global;
    napi_get_global(env, &global);

    napi_value arrayConstructor;
    napi_get_named_property(env, global, "Array", &arrayConstructor);

    napi_util::napi_set_function(env, arrayConstructor, "create", CreateJavaArrayCallback, nullptr);

}

napi_value ArrayHelper::CreateJavaArrayCallback(napi_env env, napi_callback_info info) {
    try {
       napi_value array = CreateJavaArray(env, info);
       return array;
    } catch (NativeScriptException& e) {
        e.ReThrowToNapi(env);
    } catch (std::exception& e) {
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

napi_value ArrayHelper::CreateJavaArray(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS()

    if (argc != 2) {
        Throw(env, "Expect two parameters.");
        return nullptr;
    }

    napi_value type = argv[0];
    napi_value length = argv[1];

    JniLocalRef array;

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    napi_valuetype typeType;
    napi_typeof(env, type, &typeType);

    napi_valuetype lengthType;
    napi_typeof(env, length, &lengthType);

    if (typeType == napi_string) {
        if (lengthType != napi_number) {
            Throw(env, "Expect integer value as a second argument.");
            return nullptr;
        }

        bool isFloat = napi_util::is_float(env, length);

        if (isFloat) {
            Throw(env, "Expect integer value as a second argument. It is a float");
            return nullptr;
        }

        int32_t len;
        napi_get_value_int32(env, length, &len);
        if (len < 0) {
            Throw(env, "Expect non-negative integer value as a second argument.");
            return nullptr;
        }

        string typeName = ArgConverter::ConvertToString(env, type);
        array = JniLocalRef(CreateArrayByClassName(typeName, len));
    } else if (typeType == napi_function || typeType == napi_object) {
        if (lengthType != napi_number) {
            Throw(env, "Expect integer value as a second argument.");
            return nullptr;
        }

        bool isFloat = napi_util::is_float(env, length);

        if (isFloat) {
            Throw(env, "Expect integer value as a second argument.");
            return nullptr;
        }

        int32_t len;
        napi_get_value_int32(env, length, &len);
        if (len < 0) {
            Throw(env, "Expect non-negative integer value as a second argument.");
            return nullptr;
        }

        napi_value classVal;
        napi_get_named_property(env, type, "class", &classVal);

        napi_valuetype classValType;
        napi_typeof(env, classVal, &classValType);

        if (classValType == napi_undefined) {
            Throw(env, "Expect known class as a second argument.");
            return nullptr;
        }

        auto c = objectManager->GetJavaObjectByJsObject(classVal);

        JEnv jenv;
        array = jenv.NewObjectArray(len, static_cast<jclass>(c), nullptr);
    } else {
        Throw(env, "Expect primitive type name or class function as a first argument");
        return nullptr;
    }

    jint javaObjectID = objectManager->GetOrCreateObjectId(array);
    return objectManager->CreateJSWrapper(javaObjectID, "" /* ignored */, array);
}

void ArrayHelper::Throw(napi_env env, const std::string& errorMessage) {
    napi_value errMsg;
    napi_create_string_utf8(env, errorMessage.c_str(), NAPI_AUTO_LENGTH, &errMsg);

    napi_value err;
    napi_create_error(env, nullptr, errMsg, &err);

    napi_throw(env, err);
}

jobject ArrayHelper::CreateArrayByClassName(const string& typeName, int length) {
    JEnv jEnv;
    jobject array;

    if (typeName == "char") {
        array = jEnv.NewCharArray(length);
    } else if (typeName == "boolean") {
        array = jEnv.NewBooleanArray(length);
    } else if (typeName == "byte") {
        array = jEnv.NewByteArray(length);
    } else if (typeName == "short") {
        array = jEnv.NewShortArray(length);
    } else if (typeName == "int") {
        array = jEnv.NewIntArray(length);
    } else if (typeName == "long") {
        array = jEnv.NewLongArray(length);
    } else if (typeName == "float") {
        array = jEnv.NewFloatArray(length);
    } else if (typeName == "double") {
        array = jEnv.NewDoubleArray(length);
    } else {
        JniLocalRef s(jEnv.NewStringUTF(typeName.c_str()));
        array = jEnv.CallStaticObjectMethod(RUNTIME_CLASS, CREATE_ARRAY_HELPER, (jstring)s, length);
    }

    return array;
}

jclass ArrayHelper::RUNTIME_CLASS = nullptr;
jmethodID ArrayHelper::CREATE_ARRAY_HELPER = nullptr;
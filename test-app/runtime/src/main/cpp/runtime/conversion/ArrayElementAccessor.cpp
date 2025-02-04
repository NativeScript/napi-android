#include "ArrayElementAccessor.h"
#include "JsArgToArrayConverter.h"
#include "ArgConverter.h"
#include "Util.h"
#include "NativeScriptException.h"
#include "Runtime.h"

using namespace std;
using namespace tns;

napi_value ArrayElementAccessor::GetArrayElement(napi_env env, napi_value array, uint32_t index, const string& arraySignature) {
    JEnv jenv;

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();
    auto arr = objectManager->GetJavaObjectByJsObject(array);

    assertNonNullNativeArray(arr);

    napi_value value;
    jsize startIndex = index;
    const jsize length = 1;

    const string elementSignature = arraySignature.substr(1);
    jboolean isCopy = false;

    if (elementSignature == "Z") {
        jbooleanArray boolArr = static_cast<jbooleanArray>(arr);
        jboolean boolArrValue;
        jenv.GetBooleanArrayRegion(boolArr, startIndex, length, &boolArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &boolArrValue);
    } else if (elementSignature == "B") {
        jbyteArray byteArr = static_cast<jbyteArray>(arr);
        jbyte byteArrValue;
        jenv.GetByteArrayRegion(byteArr, startIndex, length, &byteArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &byteArrValue);
    } else if (elementSignature == "C") {
        jcharArray charArr = static_cast<jcharArray>(arr);
        jchar charArrValue;
        jenv.GetCharArrayRegion(charArr, startIndex, length, &charArrValue);
        JniLocalRef s(jenv.NewString(&charArrValue, 1));
        const char* singleChar = jenv.GetStringUTFChars(s, &isCopy);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, singleChar);
        jenv.ReleaseStringUTFChars(s, singleChar);
    } else if (elementSignature == "S") {
        jshortArray shortArr = static_cast<jshortArray>(arr);
        jshort shortArrValue;
        jenv.GetShortArrayRegion(shortArr, startIndex, length, &shortArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &shortArrValue);
    } else if (elementSignature == "I") {
        jintArray intArr = static_cast<jintArray>(arr);
        jint intArrValue;
        jenv.GetIntArrayRegion(intArr, startIndex, length, &intArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &intArrValue);
    } else if (elementSignature == "J") {
        jlongArray longArr = static_cast<jlongArray>(arr);
        jlong longArrValue;
        jenv.GetLongArrayRegion(longArr, startIndex, length, &longArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &longArrValue);
    } else if (elementSignature == "F") {
        jfloatArray floatArr = static_cast<jfloatArray>(arr);
        jfloat floatArrValue;
        jenv.GetFloatArrayRegion(floatArr, startIndex, length, &floatArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &floatArrValue);
    } else if (elementSignature == "D") {
        jdoubleArray doubleArr = static_cast<jdoubleArray>(arr);
        jdouble doubleArrValue;
        jenv.GetDoubleArrayRegion(doubleArr, startIndex, length, &doubleArrValue);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &doubleArrValue);
    } else {
        jobject result = jenv.GetObjectArrayElement(static_cast<jobjectArray>(arr), index);
        value = ConvertToJsValue(env, objectManager, jenv, elementSignature, &result);
        jenv.DeleteLocalRef(result);
    }

    return value;
}

void ArrayElementAccessor::SetArrayElement(napi_env env, napi_value array, uint32_t index, const string& arraySignature, napi_value value) {
    JEnv jenv;


    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    tns::JniLocalRef arr = objectManager->GetJavaObjectByJsObject(array);

    assertNonNullNativeArray(arr);

    const string elementSignature = arraySignature.substr(1);
    jboolean isCopy = false;

    if (elementSignature == "Z") { //bool
        bool boolElementValue;
        napi_get_value_bool(env, value, &boolElementValue);
        jboolean jboolElementValue = static_cast<jboolean>(boolElementValue);
        jbooleanArray boolArr = static_cast<jbooleanArray>(arr);
        jenv.SetBooleanArrayRegion(boolArr, index, 1, &jboolElementValue);
    } else if (elementSignature == "B") { //byte
        int32_t byteElementValue;
        napi_get_value_int32(env, value, &byteElementValue);
        jbyte jbyteElementValue = static_cast<jbyte>(byteElementValue);
        jbyteArray byteArr = static_cast<jbyteArray>(arr);
        jenv.SetByteArrayRegion(byteArr, index, 1, &jbyteElementValue);
    } else if (elementSignature == "C") { //char
        size_t str_len;
        napi_get_value_string_utf8(env, value, nullptr, 0, &str_len);
        string str(str_len, '\0');
        napi_get_value_string_utf8(env, value, &str[0], str_len + 1, &str_len);
        JniLocalRef s(jenv.NewString(reinterpret_cast<const jchar*>(str.c_str()), 1));
        const char* singleChar = jenv.GetStringUTFChars(s, &isCopy);
        jchar charElementValue = *singleChar;
        jenv.ReleaseStringUTFChars(s, singleChar);
        jcharArray charArr = static_cast<jcharArray>(arr);
        jenv.SetCharArrayRegion(charArr, index, 1, &charElementValue);
    } else if (elementSignature == "S") { //short
        int32_t shortElementValue;
        napi_get_value_int32(env, value, &shortElementValue);
        jshort jshortElementValue = static_cast<jshort>(shortElementValue);
        jshortArray shortArr = static_cast<jshortArray>(arr);
        jenv.SetShortArrayRegion(shortArr, index, 1, &jshortElementValue);
    } else if (elementSignature == "I") { //int
        int32_t intElementValue;
        napi_get_value_int32(env, value, &intElementValue);
        jint jintElementValue = static_cast<jint>(intElementValue);
        jintArray intArr = static_cast<jintArray>(arr);
        jenv.SetIntArrayRegion(intArr, index, 1, &jintElementValue);
    } else if (elementSignature == "J") { //long
        int64_t longElementValue;
        napi_get_value_int64(env, value, &longElementValue);
        jlong jlongElementValue = static_cast<jlong>(longElementValue);
        jlongArray longArr = static_cast<jlongArray>(arr);
        jenv.SetLongArrayRegion(longArr, index, 1, &jlongElementValue);
    } else if (elementSignature == "F") { //float
        double floatElementValue;
        napi_get_value_double(env, value, &floatElementValue);
        jfloat jfloatElementValue = static_cast<jfloat>(floatElementValue);
        jfloatArray floatArr = static_cast<jfloatArray>(arr);
        jenv.SetFloatArrayRegion(floatArr, index, 1, &jfloatElementValue);
    } else if (elementSignature == "D") { //double
        double doubleElementValue;
        napi_get_value_double(env, value, &doubleElementValue);
        jdouble jdoubleElementValue = static_cast<jdouble>(doubleElementValue);
        jdoubleArray doubleArr = static_cast<jdoubleArray>(arr);
        jenv.SetDoubleArrayRegion(doubleArr, index, 1, &jdoubleElementValue);
    } else { //string or object
        napi_valuetype ref_type;

        napi_typeof(env, value, &ref_type);

        if (ref_type == napi_object || ref_type == napi_function || ref_type == napi_string) {
            auto object = value;

            JsArgToArrayConverter argConverter(env, value, false, (int) Type::Null);
            if (argConverter.IsValid()) {
                jobjectArray objArr = static_cast<jobjectArray>(arr);
                jobject objectElementValue = argConverter.GetConvertedArg();
                jenv.SetObjectArrayElement(objArr, index, objectElementValue);
            } else {
                JsArgToArrayConverter::Error err = argConverter.GetError();
                throw NativeScriptException(string(err.msg));
            }
        } else {
            throw NativeScriptException(string("Cannot assign primitive value to array of objects."));
        }
    }

}

napi_value ArrayElementAccessor::ConvertToJsValue(napi_env env, ObjectManager* objectManager, JEnv& jenv, const string& elementSignature, const void* value) {
    napi_value jsValue;

    jint val = *(jint*) value;

    if (elementSignature == "Z") {
        napi_get_boolean(env, *(jboolean*) value, &jsValue);
    } else if (elementSignature == "B") {
        napi_create_int32(env, *(jbyte*) value, &jsValue);
    } else if (elementSignature == "C") {
        napi_create_string_utf8(env, (const char*) value, 1, &jsValue);
    } else if (elementSignature == "S") {
        napi_create_int32(env, *(jshort*) value, &jsValue);
    } else if (elementSignature == "I") {
        napi_create_int32(env, *(jint*) value, &jsValue);
    } else if (elementSignature == "J") {
        napi_create_int64(env, *(jlong*) value, &jsValue);
    } else if (elementSignature == "F") {
        napi_create_double(env, *(jfloat*) value, &jsValue);
    } else if (elementSignature == "D") {
        napi_create_double(env, *(jdouble*) value, &jsValue);
    } else {
        if (nullptr != (*(jobject*) value)) {
            bool isString = elementSignature == "Ljava/lang/String;";

            if (isString) {
                jsValue = ArgConverter::jstringToJsString(env, *(jstring *) value);
            } else {
                jint javaObjectID = objectManager->GetOrCreateObjectId(*(jobject*) value);
                jsValue = objectManager->GetJsObjectByJavaObject(javaObjectID);

                if (napi_util::is_null_or_undefined(env, jsValue)) {
                    string className;
                    if (elementSignature[0] == '[') {
                        className = Util::JniClassPathToCanonicalName(elementSignature);
                    } else {
                        className = objectManager->GetClassName(*(jobject*) value);
                    }

                    jsValue = objectManager->CreateJSWrapper(javaObjectID, className);
                }
            }
        } else {
            napi_get_null(env, &jsValue);
        }
    }

    return jsValue;
}

void ArrayElementAccessor::assertNonNullNativeArray(tns::JniLocalRef& arrayReference) {
    if(arrayReference.IsNull()){
        throw NativeScriptException("Failed calling indexer operator on native array. The JavaScript instance no longer has available Java instance counterpart.");
    }
}
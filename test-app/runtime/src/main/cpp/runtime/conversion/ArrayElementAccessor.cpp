#include "ArrayElementAccessor.h"
#include "JsArgToArrayConverter.h"
#include "ArgConverter.h"
#include "Util.h"
#include "NativeScriptException.h"
#include "Runtime.h"

using namespace std;
using namespace tns;

napi_value ArrayElementAccessor::GetArrayElement(napi_env env, napi_value array, uint32_t index,
                                                 const string& arraySignature,
                                                 ObjectManager* objectManager, jobject arrayObject) {
    JEnv jenv;

    if (objectManager == nullptr) {
        objectManager = Runtime::GetRuntime(env)->GetObjectManager();
    }

    // The caller may hand us the already-resolved Java array (single probe per
    // loop instead of per element); otherwise resolve it here.
    JniLocalRef localArr;
    jobject arr;
    if (arrayObject != nullptr) {
        arr = arrayObject;
    } else {
        localArr = objectManager->GetJavaObjectByJsObject(array);
        assertNonNullNativeArray(localArr);
        arr = localArr;
    }

    napi_value value;
    const jsize startIndex = index;
    const jsize length = 1;

    // Dispatch on the element-type char (no substr allocation, no string-compare
    // chain). Primitive element values are created inline.
    switch (arraySignature[1]) {
        case 'Z': {
            jboolean v;
            jenv.GetBooleanArrayRegion((jbooleanArray) arr, startIndex, length, &v);
            napi_get_boolean(env, v, &value);
            break;
        }
        case 'B': {
            jbyte v;
            jenv.GetByteArrayRegion((jbyteArray) arr, startIndex, length, &v);
            napi_create_int32(env, v, &value);
            break;
        }
        case 'C': {
            jchar v;
            jenv.GetCharArrayRegion((jcharArray) arr, startIndex, length, &v);
            JniLocalRef s(jenv.NewString(&v, 1));
            jboolean isCopy = false;
            const char* singleChar = jenv.GetStringUTFChars(s, &isCopy);
            napi_create_string_utf8(env, singleChar, 1, &value);
            jenv.ReleaseStringUTFChars(s, singleChar);
            break;
        }
        case 'S': {
            jshort v;
            jenv.GetShortArrayRegion((jshortArray) arr, startIndex, length, &v);
            napi_create_int32(env, v, &value);
            break;
        }
        case 'I': {
            jint v;
            jenv.GetIntArrayRegion((jintArray) arr, startIndex, length, &v);
            napi_create_int32(env, v, &value);
            break;
        }
        case 'J': {
            jlong v;
            jenv.GetLongArrayRegion((jlongArray) arr, startIndex, length, &v);
            napi_create_int64(env, v, &value);
            break;
        }
        case 'F': {
            jfloat v;
            jenv.GetFloatArrayRegion((jfloatArray) arr, startIndex, length, &v);
            napi_create_double(env, v, &value);
            break;
        }
        case 'D': {
            jdouble v;
            jenv.GetDoubleArrayRegion((jdoubleArray) arr, startIndex, length, &v);
            napi_create_double(env, v, &value);
            break;
        }
        default: {  // 'L' object or '[' nested array
            jobject result = jenv.GetObjectArrayElement((jobjectArray) arr, index);
            value = ConvertToJsValue(env, objectManager, jenv, arraySignature.substr(1), &result);
            jenv.DeleteLocalRef(result);
            break;
        }
    }

    return value;
}

void ArrayElementAccessor::SetArrayElement(napi_env env, napi_value array, uint32_t index,
                                           const string& arraySignature, napi_value value,
                                           ObjectManager* objectManager, jobject arrayObject) {
    JEnv jenv;

    if (objectManager == nullptr) {
        objectManager = Runtime::GetRuntime(env)->GetObjectManager();
    }

    JniLocalRef localArr;
    jobject arr;
    if (arrayObject != nullptr) {
        arr = arrayObject;
    } else {
        localArr = objectManager->GetJavaObjectByJsObject(array);
        assertNonNullNativeArray(localArr);
        arr = localArr;
    }

    // Dispatch on the element-type char (no substr allocation, no string-compare
    // chain).
    switch (arraySignature[1]) {
        case 'Z': { //bool
            bool b;
            napi_get_value_bool(env, value, &b);
            jboolean v = static_cast<jboolean>(b);
            jenv.SetBooleanArrayRegion((jbooleanArray) arr, index, 1, &v);
            break;
        }
        case 'B': { //byte
            int32_t i;
            napi_get_value_int32(env, value, &i);
            jbyte v = static_cast<jbyte>(i);
            jenv.SetByteArrayRegion((jbyteArray) arr, index, 1, &v);
            break;
        }
        case 'C': { //char
            size_t str_len;
            napi_get_value_string_utf8(env, value, nullptr, 0, &str_len);
            string str(str_len, '\0');
            napi_get_value_string_utf8(env, value, &str[0], str_len + 1, &str_len);
            JniLocalRef s(jenv.NewString(reinterpret_cast<const jchar*>(str.c_str()), 1));
            jboolean isCopy = false;
            const char* singleChar = jenv.GetStringUTFChars(s, &isCopy);
            jchar v = *singleChar;
            jenv.ReleaseStringUTFChars(s, singleChar);
            jenv.SetCharArrayRegion((jcharArray) arr, index, 1, &v);
            break;
        }
        case 'S': { //short
            int32_t i;
            napi_get_value_int32(env, value, &i);
            jshort v = static_cast<jshort>(i);
            jenv.SetShortArrayRegion((jshortArray) arr, index, 1, &v);
            break;
        }
        case 'I': { //int
            int32_t i;
            napi_get_value_int32(env, value, &i);
            jint v = static_cast<jint>(i);
            jenv.SetIntArrayRegion((jintArray) arr, index, 1, &v);
            break;
        }
        case 'J': { //long
            int64_t l;
            napi_get_value_int64(env, value, &l);
            jlong v = static_cast<jlong>(l);
            jenv.SetLongArrayRegion((jlongArray) arr, index, 1, &v);
            break;
        }
        case 'F': { //float
            double d;
            napi_get_value_double(env, value, &d);
            jfloat v = static_cast<jfloat>(d);
            jenv.SetFloatArrayRegion((jfloatArray) arr, index, 1, &v);
            break;
        }
        case 'D': { //double
            double d;
            napi_get_value_double(env, value, &d);
            jdouble v = static_cast<jdouble>(d);
            jenv.SetDoubleArrayRegion((jdoubleArray) arr, index, 1, &v);
            break;
        }
        default: { //string or object
            napi_valuetype ref_type;
            napi_typeof(env, value, &ref_type);

            if (ref_type == napi_object || ref_type == napi_function || ref_type == napi_string) {
                JsArgToArrayConverter argConverter(env, value, false, (int) Type::Null, objectManager);
                if (argConverter.IsValid()) {
                    jobject objectElementValue = argConverter.GetConvertedArg();
                    jenv.SetObjectArrayElement((jobjectArray) arr, index, objectElementValue);
                } else {
                    JsArgToArrayConverter::Error err = argConverter.GetError();
                    throw NativeScriptException(string(err.msg));
                }
            } else {
                throw NativeScriptException(string("Cannot assign primitive value to array of objects."));
            }
            break;
        }
    }
}

napi_value ArrayElementAccessor::ConvertToJsValue(napi_env env, ObjectManager* objectManager, JEnv& jenv, const string& elementSignature, const void* value) {
    napi_value jsValue;

    switch (elementSignature[0]) {
        case 'Z':
            napi_get_boolean(env, *(jboolean*) value, &jsValue);
            break;
        case 'B':
            napi_create_int32(env, *(jbyte*) value, &jsValue);
            break;
        case 'C':
            napi_create_string_utf8(env, (const char*) value, 1, &jsValue);
            break;
        case 'S':
            napi_create_int32(env, *(jshort*) value, &jsValue);
            break;
        case 'I':
            napi_create_int32(env, *(jint*) value, &jsValue);
            break;
        case 'J':
            napi_create_int64(env, *(jlong*) value, &jsValue);
            break;
        case 'F':
            napi_create_double(env, *(jfloat*) value, &jsValue);
            break;
        case 'D':
            napi_create_double(env, *(jdouble*) value, &jsValue);
            break;
        default: {
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
            break;
        }
    }

    return jsValue;
}

void ArrayElementAccessor::assertNonNullNativeArray(tns::JniLocalRef& arrayReference) {
    if(arrayReference.IsNull()){
        throw NativeScriptException("Failed calling indexer operator on native array. The JavaScript instance no longer has available Java instance counterpart.");
    }
}
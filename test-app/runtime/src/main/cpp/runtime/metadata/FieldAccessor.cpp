#include "FieldAccessor.h"
#include "ArgConverter.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include <sstream>

using namespace std;
using namespace tns;

napi_value
FieldAccessor::GetJavaField(napi_env env, napi_value target, FieldCallbackData *fieldData) {
    JEnv jEnv;

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    napi_value fieldResult;

    JniLocalRef targetJavaObject;

    auto &fieldMetadata = fieldData->metadata;

    const auto &fieldTypeName = fieldMetadata.getSig();
    auto isStatic = fieldMetadata.isStatic;

    auto isPrimitiveType = fieldTypeName.size() == 1;
    if (fieldData->fid == nullptr) {
        auto isFieldArray = fieldTypeName[0] == '[';
        auto fieldJniSig = isPrimitiveType
                           ? fieldTypeName
                           : (isFieldArray
                              ? fieldTypeName
                              : ("L" + fieldTypeName + ";"));

        if (isStatic) {
            fieldData->clazz = jEnv.FindClass(fieldMetadata.getDeclaringType());
            fieldData->fid = jEnv.GetStaticFieldID(fieldData->clazz, fieldMetadata.getName(),
                                                   fieldJniSig);
        } else {
            fieldData->clazz = jEnv.FindClass(fieldMetadata.getDeclaringType());
            fieldData->fid = jEnv.GetFieldID(fieldData->clazz, fieldMetadata.getName(),
                                             fieldJniSig);
        }
    }

    if (!isStatic) {
        // Using fast, target is always the original *this*
        targetJavaObject = objectManager->GetJavaObjectByJsObjectFast(target);

        if (targetJavaObject.IsNull()) {
            stringstream ss;
            ss << "Cannot access property '" << fieldMetadata.getName().c_str()
               << "' because there is no corresponding Java object";
            throw NativeScriptException(ss.str());
        }

    }


    auto fieldId = fieldData->fid;
    auto clazz = fieldData->clazz;

    if (isPrimitiveType) {
        switch (fieldTypeName[0]) {
            case 'Z': { // bool
                jboolean result;
                if (isStatic) {
                    result = jEnv.GetStaticBooleanField(clazz, fieldId);
                } else {
                    result = jEnv.GetBooleanField(targetJavaObject, fieldId);
                }
                fieldResult =
                        result == JNI_TRUE ? napi_util::get_true(env) : napi_util::get_false(env);
                break;
            }
            case 'B': { // byte
                jbyte result;
                if (isStatic) {
                    result = jEnv.GetStaticByteField(clazz, fieldId);
                } else {
                    result = jEnv.GetByteField(targetJavaObject, fieldId);
                }
                napi_create_int32(env, result, &fieldResult);
                break;
            }
            case 'C': { // char
                jchar result;
                if (isStatic) {
                    result = jEnv.GetStaticCharField(clazz, fieldId);
                } else {
                    result = jEnv.GetCharField(targetJavaObject, fieldId);
                }

                JniLocalRef str(jEnv.NewString(&result, 1));
                jboolean bol = true;
                const char *resP = jEnv.GetStringUTFChars(str, &bol);
                fieldResult = ArgConverter::convertToJsString(env, resP, 1);
                jEnv.ReleaseStringUTFChars(str, resP);
                break;
            }
            case 'S': { // short
                jshort result;
                if (isStatic) {
                    result = jEnv.GetStaticShortField(clazz, fieldId);
                } else {
                    result = jEnv.GetShortField(targetJavaObject, fieldId);
                }
                napi_create_int32(env, result, &fieldResult);
                break;
            }
            case 'I': { // int
                jint result;
                if (isStatic) {
                    result = jEnv.GetStaticIntField(clazz, fieldId);
                } else {
                    result = jEnv.GetIntField(targetJavaObject, fieldId);
                }

                napi_create_int32(env, result, &fieldResult);
                break;
            }
            case 'J': { // long
                jlong result;
                if (isStatic) {
                    result = jEnv.GetStaticLongField(clazz, fieldId);
                } else {
                    result = jEnv.GetLongField(targetJavaObject, fieldId);
                }

                fieldResult = ArgConverter::ConvertFromJavaLong(env, result);
                break;
            }
            case 'F': { // float
                jfloat result;
                if (isStatic) {
                    result = jEnv.GetStaticFloatField(clazz, fieldId);
                } else {
                    result = jEnv.GetFloatField(targetJavaObject, fieldId);
                }
                napi_create_double(env, (double) result, &fieldResult);
                break;
            }
            case 'D': { // double
                jdouble result;
                if (isStatic) {
                    result = jEnv.GetStaticDoubleField(clazz, fieldId);
                } else {
                    result = jEnv.GetDoubleField(targetJavaObject, fieldId);
                }
                napi_create_double(env, (double) result, &fieldResult);
                break;
            }
            default: {
                stringstream ss;
                ss << "(InternalError): in FieldAccessor::GetJavaField: Unknown field type: '"
                   << fieldTypeName[0] << "'";
                throw NativeScriptException(ss.str());
            }
        }
    } else {
        jobject result;

        if (isStatic) {
            result = jEnv.GetStaticObjectField(clazz, fieldId);
        } else {
            result = jEnv.GetObjectField(targetJavaObject, fieldId);
        }

        if (result != nullptr) {

            bool isString = fieldTypeName == "java/lang/String";
            if (isString) {
                fieldResult = ArgConverter::jstringToJsString(env, (jstring) result);
            } else {
                int javaObjectID = objectManager->GetOrCreateObjectId(result);
                auto objectResult = objectManager->GetJsObjectByJavaObject(javaObjectID);

                if (napi_util::is_null_or_undefined(env, objectResult)) {
                    objectResult = objectManager->CreateJSWrapper(javaObjectID, fieldTypeName,
                                                                  result);
                }

                fieldResult = objectResult;
            }
            jEnv.DeleteLocalRef(result);
        } else {
            napi_get_null(env, &fieldResult);
        }
    }
    return fieldResult;
}

void FieldAccessor::SetJavaField(napi_env env, napi_value target, napi_value value,
                                 FieldCallbackData *fieldData) {
    JEnv jEnv;

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    JniLocalRef targetJavaObject;

    auto &fieldMetadata = fieldData->metadata;

    const auto &fieldTypeName = fieldMetadata.getSig();
    auto isStatic = fieldMetadata.isStatic;

    auto isPrimitiveType = fieldTypeName.size() == 1;
    auto isFieldArray = fieldTypeName[0] == '[';

    if (fieldData->fid == nullptr) {
        auto fieldJniSig = isPrimitiveType
                           ? fieldTypeName
                           : (isFieldArray
                              ? fieldTypeName
                              : ("L" + fieldTypeName + ";"));

        if (isStatic) {
            fieldData->clazz = jEnv.FindClass(fieldMetadata.getDeclaringType());
            assert(fieldData->clazz != nullptr);
            fieldData->fid = jEnv.GetStaticFieldID(fieldData->clazz, fieldMetadata.getName(),
                                                   fieldJniSig);
            assert(fieldData->fid != nullptr);
        } else {
            fieldData->clazz = jEnv.FindClass(fieldMetadata.getDeclaringType());
            assert(fieldData->clazz != nullptr);
            fieldData->fid = jEnv.GetFieldID(fieldData->clazz, fieldMetadata.getName(),
                                             fieldJniSig);
            assert(fieldData->fid != nullptr);
        }
    }

    if (!isStatic) {
        // Using fast, target is always the original *this*
        targetJavaObject = objectManager->GetJavaObjectByJsObjectFast(target);

        if (targetJavaObject.IsNull()) {
            stringstream ss;
            ss << "Cannot access property '" << fieldMetadata.getName().c_str()
               << "' because there is no corresponding Java object";
            throw NativeScriptException(ss.str());
        }
    }

    auto fieldId = fieldData->fid;
    auto clazz = fieldData->clazz;

    if (isPrimitiveType) {
        switch (fieldTypeName[0]) {
            case 'Z': { // bool
                // TODO: validate value is a boolean before calling
                bool boolValue = napi_util::is_of_type(env, value, napi_boolean)
                                 ? napi_util::get_bool(env, value) : false;
                if (isStatic) {
                    jEnv.SetStaticBooleanField(clazz, fieldId, boolValue);
                } else {
                    jEnv.SetBooleanField(targetJavaObject, fieldId,
                                         boolValue);
                }
                break;
            }
            case 'B': { // byte
                // TODO: validate value is a byte before calling
                jbyte intValue = !napi_util::is_of_type(env, value, napi_number)
                                 ? napi_util::get_int32(env, value) : 0;
                if (isStatic) {
                    jEnv.SetStaticByteField(clazz, fieldId, intValue);
                } else {
                    jEnv.SetByteField(targetJavaObject, fieldId, intValue);
                }
                break;
            }
            case 'C': { // char
                const char *stringValue = napi_util::get_string_value(env, value, 1);
                JniLocalRef strValue(jEnv.NewStringUTF(stringValue));
                const char *chars = jEnv.GetStringUTFChars(strValue, 0);

                if (isStatic) {
                    jEnv.SetStaticCharField(clazz, fieldId, chars[0]);
                } else {
                    jEnv.SetCharField(targetJavaObject, fieldId, chars[0]);
                }
                jEnv.ReleaseStringUTFChars(strValue, chars);
                break;
            }
            case 'S': { // short
                // TODO: validate value is a short before calling
                short shortValue = !napi_util::is_of_type(env, value, napi_number)
                                   ? napi_util::get_int32(env, value) : 0;
                if (isStatic) {
                    jEnv.SetStaticShortField(clazz, fieldId, shortValue);
                } else {
                    jEnv.SetShortField(targetJavaObject, fieldId, shortValue);
                }
                break;
            }
            case 'I': { // int
                // TODO: validate value is a int before calling
                int intValue = napi_util::is_of_type(env, value, napi_number)
                               ? napi_util::get_int32(env, value) : 0;
                if (isStatic) {
                    jEnv.SetStaticIntField(clazz, fieldId, intValue);
                } else {
                    jEnv.SetIntField(targetJavaObject, fieldId, intValue);
                }
                break;
            }
            case 'J': { // long
                jlong longValue = static_cast<jlong>(ArgConverter::ConvertToJavaLong(env, value));
                if (isStatic) {
                    jEnv.SetStaticLongField(clazz, fieldId, longValue);
                } else {
                    jEnv.SetLongField(targetJavaObject, fieldId, longValue);
                }
                break;
            }
            case 'F': { // float
                float floatValue = napi_util::is_of_type(env, value, napi_number)
                                   ? napi_util::get_number(env,
                                                           value) : 0.0;
                if (isStatic) {
                    jEnv.SetStaticFloatField(clazz, fieldId,
                                             static_cast<jfloat>(floatValue));
                } else {
                    jEnv.SetFloatField(targetJavaObject, fieldId,
                                       static_cast<jfloat>(floatValue));
                }
                break;
            }
            case 'D': { // double
                double doubleValue = napi_util::is_of_type(env, value, napi_number)
                                     ? napi_util::get_number(env,
                                                             value) : 0.0;
                if (isStatic) {
                    jEnv.SetStaticDoubleField(clazz, fieldId, doubleValue);
                } else {
                    jEnv.SetDoubleField(targetJavaObject, fieldId, doubleValue);
                }
                break;
            }
            default: {
                stringstream ss;
                ss << "(InternalError): in FieldAccessor::SetJavaField: Unknown field type: '"
                   << fieldTypeName[0] << "'";
                throw NativeScriptException(ss.str());
            }
        }
    } else {
        bool isString = fieldTypeName == "java/lang/String";
        JniLocalRef result;

        if (!napi_util::is_null(env, value) && !napi_util::is_undefined(env, value)) {
            if (isString) {
                // TODO: validate valie is a string;
                result = ArgConverter::ConvertToJavaString(env, value);
            } else {
                result = objectManager->GetJavaObjectByJsObject(value);
            }
        }

        if (isStatic) {
            jEnv.SetStaticObjectField(clazz, fieldId, result);
        } else {
            jEnv.SetObjectField(targetJavaObject, fieldId, result);
        }
    }
}

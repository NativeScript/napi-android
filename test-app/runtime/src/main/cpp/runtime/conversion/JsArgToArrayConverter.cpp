#include "JsArgToArrayConverter.h"
#include <sstream>
#include "ObjectManager.h"
#include "ArgConverter.h"
#include "NumericCasts.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include "MetadataNode.h"
#include "JsArgConverter.h"

using namespace std;
using namespace tns;

JsArgToArrayConverter::JsArgToArrayConverter(napi_env env, napi_value arg,
                                             bool isImplementationObject, int classReturnType,
                                             ObjectManager* objectManager)
        : m_arr(nullptr), m_argsAsObject(nullptr), m_argsLen(0), m_isValid(false), m_error(Error()),
          m_return_type(classReturnType) {
    m_objectManager = objectManager;
    if (!isImplementationObject) {
        m_argsLen = 1;
        m_argsAsObject = (m_argsLen <= INLINE_CAPACITY) ? m_inlineArgs : new jobject[m_argsLen];
        memset(m_argsAsObject, 0, m_argsLen * sizeof(jobject));

        m_isValid = ConvertArg(env, arg, 0);
    }
}

JsArgToArrayConverter::JsArgToArrayConverter(napi_env env, size_t argc, napi_value *argv,
                                             bool hasImplementationObject)
        : m_arr(nullptr), m_argsAsObject(nullptr), m_argsLen(0), m_isValid(false), m_error(Error()),
          m_return_type(static_cast<int>(Type::Null)) {
    m_argsLen = !hasImplementationObject ? argc : argc - 2;

    bool success = true;

    if (m_argsLen > 0) {
        m_argsAsObject = (m_argsLen <= INLINE_CAPACITY) ? m_inlineArgs : new jobject[m_argsLen];
        memset(m_argsAsObject, 0, m_argsLen * sizeof(jobject));

        for (int i = 0; i < m_argsLen; i++) {
            success = ConvertArg(env, argv[i], i);

            if (!success) {
                break;
            }
        }
    }

    m_isValid = success;
}

bool JsArgToArrayConverter::ConvertArg(napi_env env, napi_value arg, int index) {
    bool success = false;
    napi_status status;
    // Error text is built only on failure (avoids a per-call stringstream).
    std::string errMsg;

    // Seed a default diagnostic: the NAPI_GUARD bails below `return false`
    // without reaching the error-population tail, and the caller loop stops at
    // the first failing argument, so GetError() always carries a non-empty,
    // indexed message even on those early-exit paths.
    m_error.index = index;
    m_error.msg = "Cannot marshal JavaScript argument at index " +
                  std::to_string(index) + " to Java type.";

    JEnv jEnv;

    Type returnType = JType::getClassType(m_return_type);

    napi_valuetype argType;
    NAPI_GUARD(napi_typeof(env, arg, &argType)) {
        return false;
    }

    if (argType == napi_undefined || argType == napi_null) {
        SetConvertedObject(jEnv, index, nullptr);
        success = true;
    } else if (argType == napi_number) {
        double d;
        NAPI_GUARD(napi_get_value_double(env, arg, &d)) {
            return false;
        }
        int64_t i = (int64_t) d;

        bool isWholeNumber = d == i;

        if (isWholeNumber) {
            jobject obj;

            if ((INT_MIN <= i) && (i <= INT_MAX) &&
                (returnType == Type::Int || returnType == Type::Null)) {
                obj = JType::NewInt(jEnv, (jint) i);
            } else {
                obj = JType::NewLong(jEnv, (jlong) d);
            }

            SetConvertedObject(jEnv, index, obj);
            success = true;
        } else {
            jobject obj;

            if ((FLT_MIN <= d) && (d <= FLT_MAX) &&
                (returnType == Type::Float || returnType == Type::Null)) {
                obj = JType::NewFloat(jEnv, (jfloat) d);
            } else {
                obj = JType::NewDouble(jEnv, (jdouble) d);
            }

            SetConvertedObject(jEnv, index, obj);
            success = true;
        }
    } else if (argType == napi_boolean) {
        bool value;
        NAPI_GUARD(napi_get_value_bool(env, arg, &value)) {
            return false;
        }
        auto javaObject = JType::NewBoolean(jEnv, value);
        SetConvertedObject(jEnv, index, javaObject);
        success = true;
    } else if (argType == napi_string) {
        auto stringObject = ArgConverter::ConvertToJavaString(env, arg);
        SetConvertedObject(jEnv, index, stringObject);
        success = true;
    } else if (argType == napi_object || argType == napi_function) {
        napi_value jsObj = arg;


        CastType castType = CastType::None;
#ifdef USE_HOST_OBJECT
        // A non-ok status here just means "not a host object" (some engines,
        // e.g. PrimJS, return an error rather than data=NULL for plain objects);
        // treat it as no host data and continue.
        void *data = nullptr;
        napi_get_host_object_data(env, jsObj, &data);
        if (data) {
            castType = CastType::None;
        } else {
            castType = NumericCasts::GetCastType(env, jsObj);
        }
#else
        castType = NumericCasts::GetCastType(env, jsObj);
#endif

        napi_value castValue;
        jchar charValue;
        jbyte byteValue;
        jshort shortValue;
        jlong longValue;
        jfloat floatValue;
        jdouble doubleValue;
        jobject javaObject;
        JniLocalRef obj;

        auto objectManager = m_objectManager != nullptr
                             ? m_objectManager
                             : Runtime::GetRuntime(env)->GetObjectManager();

        switch (castType) {
            case CastType::Char:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                charValue = '\0';
                if (castValue != nullptr) {
                    string str = ArgConverter::ConvertToString(env, castValue);
                    charValue = (jchar) str[0];
                }
                javaObject = JType::NewChar(jEnv, charValue);
                SetConvertedObject(jEnv, index, javaObject);
                success = true;
                break;

            case CastType::Byte:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                byteValue = 0;

                if (castValue != nullptr) {
                    if (napi_util::is_of_type(env, castValue, napi_string)) {
                        string value = ArgConverter::ConvertToString(env, castValue);
                        int byteArg = atoi(value.c_str());
                        byteValue = (jbyte) byteArg;
                    } else {
                        int byteArg = napi_util::get_int32(env, castValue);
                        byteValue = (jbyte) byteArg;
                    }
                }

                javaObject = JType::NewByte(jEnv, byteValue);
                SetConvertedObject(jEnv, index, javaObject);
                success = true;
                break;

            case CastType::Short:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                shortValue = 0;
                if (castValue != nullptr) {
                    if (napi_util::is_of_type(env, castValue, napi_string)) {
                        string value = ArgConverter::ConvertToString(env, castValue);
                        int shortArg = atoi(value.c_str());
                        shortValue = (jshort) shortArg;
                    } else {
                        int shortArg = napi_util::get_int32(env, castValue);
                        shortValue = (jshort) shortArg;
                    }
                }

                javaObject = JType::NewShort(jEnv, shortValue);

                SetConvertedObject(jEnv, index, javaObject);
                success = true;
                break;

            case CastType::Long:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                longValue = 0;
                if (castValue != nullptr) {
                    if (napi_util::is_of_type(env, castValue, napi_string)) {
                        auto strValue = ArgConverter::ConvertToString(env, castValue);
                        longValue = atoll(strValue.c_str());
                    } else {
                        int64_t longArg;
                        NAPI_GUARD(napi_get_value_int64(env, castValue, &longArg)) {
                            return false;
                        }
                        longValue = (jlong) longArg;
                    }
                }
                javaObject = JType::NewLong(jEnv, longValue);
                SetConvertedObject(jEnv, index, javaObject);
                success = true;
                break;

            case CastType::Float:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                floatValue = 0;
                if (castValue != nullptr) {
                    double floatArg;
                    NAPI_GUARD(napi_get_value_double(env, castValue, &floatArg)) {
                        return false;
                    }
                    floatValue = (jfloat) floatArg;
                }
                javaObject = JType::NewFloat(jEnv, floatValue);
                SetConvertedObject(jEnv, index, javaObject);
                success = true;
                break;

            case CastType::Double:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                doubleValue = 0;
                if (castValue != nullptr) {
                    double doubleArg;
                    NAPI_GUARD(napi_get_value_double(env, castValue, &doubleArg)) {
                        return false;
                    }
                    doubleValue = (jdouble) doubleArg;
                }
                javaObject = JType::NewDouble(jEnv, doubleValue);
                SetConvertedObject(jEnv, index, javaObject);
                success = true;
                break;

            case CastType::None:

                obj = objectManager->GetJavaObjectByJsObject(jsObj);

                if (obj.IsNull()) {
                    bool isArrayBuffer = false;
                    bool isDataView = false;
                    bool isTypedArray = false;

                    NAPI_GUARD(napi_is_arraybuffer(env, jsObj, &isArrayBuffer)) {
                        return false;
                    }
                    if (!isArrayBuffer) {
                        NAPI_GUARD(napi_is_typedarray(env, jsObj, &isTypedArray)) {
                            return false;
                        }
                        if (!isTypedArray) {
                            NAPI_GUARD(napi_is_dataview(env, jsObj, &isDataView)) {
                                return false;
                            }
                        }
                    }

                    if (isArrayBuffer || isDataView || isTypedArray) {
                        obj = JsArgConverter::GetByteBuffer(env, jsObj, isArrayBuffer, isTypedArray,
                                                            isDataView);
                    }
                }


#ifdef USE_HOST_OBJECT
                if (!data) {
#endif
                    napi_value privateValue;
                    NAPI_GUARD(napi_get_named_property(env, jsObj, PROP_KEY_NULL_NODE_NAME, &privateValue)) {
                        return false;
                    }
                    if (!napi_util::is_null_or_undefined(env, privateValue)) {
                        void *data = nullptr;
                        NAPI_GUARD(napi_get_value_external(env, privateValue, &data)) {
                            return false;
                        }
                        auto node = reinterpret_cast<MetadataNode *>(data);
                        if (node == nullptr) {
                            errMsg = "Cannot get type of the null argument at index " +
                                     std::to_string(index);
                            success = false;
                            break;
                        }

                        auto type = node->GetName();
                        auto nullObjName = "com/tns/NullObject";
                        auto nullObjCtorSig = "(Ljava/lang/Class;)V";

                        jclass nullClazz = jEnv.FindClass(nullObjName);
                        jmethodID ctor = jEnv.GetMethodID(nullClazz, "<init>", nullObjCtorSig);
                        jclass clazzToNull = jEnv.FindClass(type);
                        jobject nullObjType = jEnv.NewObject(nullClazz, ctor, clazzToNull);

                        if (nullObjType != nullptr) {
                            SetConvertedObject(jEnv, index, nullObjType, false);
                        } else {
                            SetConvertedObject(jEnv, index, nullptr);
                        }

                        success = true;
                        return success;
                    }

#ifdef USE_HOST_OBJECT
                }
#endif


                success = !obj.IsNull();
                if (success) {
                    SetConvertedObject(jEnv, index, obj.Move(), obj.IsGlobal());
                } else {
                    if (napi_util::is_number_object(env, arg)) {
                        napi_value numValue = napi_util::valueOf(env, arg);
                        bool isFloat = napi_util::is_float(env, numValue);
                        if (isFloat) {
                            double floatArg;
                            NAPI_GUARD(napi_get_value_double(env, numValue, &floatArg)) {
                                return false;
                            }
                            jfloat value = (jfloat) floatArg;
                            javaObject = JType::NewFloat(jEnv, value);
                            SetConvertedObject(jEnv, index, javaObject);
                            success = true;
                        } else {
                            int intArg;
                            NAPI_GUARD(napi_get_value_int32(env, numValue, &intArg)) {
                                return false;
                            }
                            jint value = (jint) intArg;
                            javaObject = JType::NewInt(jEnv, value);
                            SetConvertedObject(jEnv, index, javaObject);
                            success = true;
                        }
                        break;
                    } else if (napi_util::is_string_object(env, arg)) {
                        napi_value stringValue = napi_util::valueOf(env, arg);
                        javaObject = ArgConverter::ConvertToJavaString(env, stringValue);
                        SetConvertedObject(jEnv, index, javaObject);
                        success = true;
                        break;
                    } else if (napi_util::is_boolean_object(env, arg)) {
                        napi_value boolValue = napi_util::valueOf(env, arg);
                        bool value = napi_util::get_bool(env, boolValue);
                        javaObject = JType::NewBoolean(jEnv, value);
                        SetConvertedObject(jEnv, index, javaObject);
                        success = true;
                        break;
                    }

                    if (!success) {
                        napi_value objStr;
                        NAPI_GUARD(napi_coerce_to_string(env, jsObj, &objStr)) {
                            return false;
                        }
                        const char *objStrValue = napi_util::get_string_value(env, objStr);
                        stringstream s;
                        s << "Cannot marshal JavaScript argument " << objStrValue << " at index "
                          << index
                          << " to Java type.";
                        errMsg = s.str();
                    }
                }
                break;

            default:
                throw NativeScriptException("Unsupported cast type");
        }
    } else {
        errMsg = "Cannot marshal JavaScript argument at index " + std::to_string(index) +
                 " to Java type.";
        success = false;
    }

    if (!success) {
        m_error.index = index;
        // Keep the seeded default when no specific message was built.
        if (!errMsg.empty()) {
            m_error.msg = std::move(errMsg);
        }
    }

    return success;
}

jobject JsArgToArrayConverter::GetConvertedArg() {
    return (m_argsLen > 0) ? m_argsAsObject[0] : nullptr;
}

void JsArgToArrayConverter::SetConvertedObject(JEnv &env, int index, jobject obj, bool isGlobal) {
    m_argsAsObject[index] = obj;
    if ((obj != nullptr) && !isGlobal) {
        m_storedIndexes.push_back(index);
    }
}

int JsArgToArrayConverter::Length() const {
    return m_argsLen;
}

bool JsArgToArrayConverter::IsValid() const {
    return m_isValid;
}

JsArgToArrayConverter::Error JsArgToArrayConverter::GetError() const {
    return m_error;
}

jobjectArray JsArgToArrayConverter::ToJavaArray() {
    if ((m_arr == nullptr) && (m_argsLen > 0)) {
        if (m_argsLen >= JsArgToArrayConverter::MAX_JAVA_PARAMS_COUNT) {
            stringstream ss;
            ss << "You are trying to override more than the MAX_JAVA_PARAMS_COUNT: "
               << JsArgToArrayConverter::MAX_JAVA_PARAMS_COUNT;
            throw NativeScriptException(ss.str());
        }

        JEnv jEnv;

        if (JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS == nullptr) {
            JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS = jEnv.FindClass("java/lang/Object");
        }

        JniLocalRef tmpArr(
                jEnv.NewObjectArray(m_argsLen, JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS,
                                    nullptr));
        m_arr = (jobjectArray) jEnv.NewGlobalRef(tmpArr);

        for (int i = 0; i < m_argsLen; i++) {
            jEnv.SetObjectArrayElement(m_arr, i, m_argsAsObject[i]);
        }
    }

    return m_arr;
}

JsArgToArrayConverter::~JsArgToArrayConverter() {
    if (m_argsLen > 0) {
        JEnv env;

        env.DeleteGlobalRef(m_arr);

        int length = m_storedIndexes.size();
        for (int i = 0; i < length; i++) {
            int index = m_storedIndexes[i];
            env.DeleteLocalRef(m_argsAsObject[index]);
        }

        if (m_argsAsObject != m_inlineArgs) {
            delete[] m_argsAsObject;
        }
    }
}

jclass JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS = nullptr;
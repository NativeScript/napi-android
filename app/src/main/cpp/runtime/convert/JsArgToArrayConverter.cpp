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
using namespace ns;

JsArgToArrayConverter::JsArgToArrayConverter(napi_env env, napi_value arg,
                                             bool isImplementationObject, int classReturnType)
        : m_arr(nullptr), m_argsAsObject(nullptr), m_argsLen(0), m_isValid(false), m_error(Error()),
          m_return_type(classReturnType) {
    if (!isImplementationObject) {
        m_argsLen = 1;
        m_argsAsObject = new jobject[m_argsLen];
        memset(m_argsAsObject, 0, m_argsLen * sizeof(jobject));

        m_isValid = ConvertArg(env, arg, 0);
    }
}

JsArgToArrayConverter::JsArgToArrayConverter(napi_env env, napi_callback_info info,
                                             bool hasImplementationObject)
        : m_arr(nullptr), m_argsAsObject(nullptr), m_argsLen(0), m_isValid(false), m_error(Error()),
          m_return_type(static_cast<int>(Type::Null)) {
    size_t argc;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr);
    m_argsLen = !hasImplementationObject ? argc : argc - 2;

    bool success = true;

    if (m_argsLen > 0) {
        m_argsAsObject = new jobject[m_argsLen];
        memset(m_argsAsObject, 0, m_argsLen * sizeof(jobject));

        std::vector<napi_value> argv(argc);
        napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr);

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
    stringstream s;

    JEnv jenv;

    Type returnType = JType::getClassType(m_return_type);

    napi_valuetype argType;
    napi_typeof(env, arg, &argType);

    if (argType == napi_undefined || argType == napi_null) {
        SetConvertedObject(jenv, index, nullptr);
        success = true;
    } else if (argType == napi_number) {
        double d;
        napi_get_value_double(env, arg, &d);
        int64_t i = (int64_t) d;

        bool isWholeNumber = d == i;

        if (isWholeNumber) {
            jobject obj;

            if ((INT_MIN <= i) && (i <= INT_MAX) &&
                (returnType == Type::Int || returnType == Type::Null)) {
                obj = JType::NewInt(jenv, (jint) d);
            } else {
                obj = JType::NewLong(jenv, (jlong) d);
            }

            SetConvertedObject(jenv, index, obj);
            success = true;
        } else {
            jobject obj;

            if ((FLT_MIN <= d) && (d <= FLT_MAX) &&
                (returnType == Type::Float || returnType == Type::Null)) {
                obj = JType::NewFloat(jenv, (jfloat) d);
            } else {
                obj = JType::NewDouble(jenv, (jdouble) d);
            }

            SetConvertedObject(jenv, index, obj);
            success = true;
        }
    } else if (argType == napi_boolean) {
        bool value;
        napi_get_value_bool(env, arg, &value);
        auto javaObject = JType::NewBoolean(jenv, value);
        SetConvertedObject(jenv, index, javaObject);
        success = true;
    } else if (argType == napi_string) {
        auto stringObject = ArgConverter::ConvertToJavaString(env, arg);
        SetConvertedObject(jenv, index, stringObject);
        success = true;
    } else if (argType == napi_object) {
        napi_value jsObj = arg;

        auto castType = NumericCasts::GetCastType(env, jsObj);

        napi_value castValue;
        jchar charValue;
        jbyte byteValue;
        jshort shortValue;
        jlong longValue;
        jfloat floatValue;
        jdouble doubleValue;
        jobject javaObject;
        JniLocalRef obj;

        auto runtime = Runtime::GetRuntime(env);
        auto objectManager = runtime->GetObjectManager();

        switch (castType) {
            case CastType::Char:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                charValue = '\0';
                if (castValue != nullptr) {
                    string str = ArgConverter::ConvertToString(env, castValue);
                    charValue = (jchar) str[0];
                }
                javaObject = JType::NewChar(jenv, charValue);
                SetConvertedObject(jenv, index, javaObject);
                success = true;
                break;

            case CastType::Byte:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                byteValue = 0;
                if (castValue != nullptr) {
                    string value = ArgConverter::ConvertToString(env, castValue);
                    int byteArg = atoi(value.c_str());
                    byteValue = (jbyte) byteArg;
                }
                javaObject = JType::NewByte(jenv, byteValue);
                SetConvertedObject(jenv, index, javaObject);
                success = true;
                break;

            case CastType::Short:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                shortValue = 0;
                if (castValue != nullptr) {
                    string value = ArgConverter::ConvertToString(env, castValue);
                    int shortArg = atoi(value.c_str());
                    shortValue = (jshort) shortArg;
                }
                javaObject = JType::NewShort(jenv, shortValue);
                SetConvertedObject(jenv, index, javaObject);
                success = true;
                break;

            case CastType::Long:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                longValue = 0;
                if (castValue != nullptr) {
                    auto strValue = ArgConverter::ConvertToString(env, castValue);
                    longValue = atoll(strValue.c_str());
                }
                javaObject = JType::NewLong(jenv, longValue);
                SetConvertedObject(jenv, index, javaObject);
                success = true;
                break;

            case CastType::Float:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                floatValue = 0;
                if (castValue != nullptr) {
                    double floatArg;
                    napi_get_value_double(env, castValue, &floatArg);
                    floatValue = (jfloat) floatArg;
                }
                javaObject = JType::NewFloat(jenv, floatValue);
                SetConvertedObject(jenv, index, javaObject);
                success = true;
                break;

            case CastType::Double:
                castValue = NumericCasts::GetCastValue(env, jsObj);
                doubleValue = 0;
                if (castValue != nullptr) {
                    double doubleArg;
                    napi_get_value_double(env, castValue, &doubleArg);
                    doubleValue = (jdouble) doubleArg;
                }
                javaObject = JType::NewDouble(jenv, doubleValue);
                SetConvertedObject(jenv, index, javaObject);
                success = true;
                break;

            case CastType::None:

                obj = objectManager->GetJavaObjectByJsObject(env, jsObj);

                bool bufferOrTypedArrayOrDataView;

                napi_is_arraybuffer(env, jsObj, &bufferOrTypedArrayOrDataView);

                if (!bufferOrTypedArrayOrDataView) {
                    napi_is_typedarray(env, jsObj, &bufferOrTypedArrayOrDataView);
                }

                if (!bufferOrTypedArrayOrDataView) {
                    napi_is_dataview(env, jsObj, &bufferOrTypedArrayOrDataView);
                }


                if (obj.IsNull() && bufferOrTypedArrayOrDataView) {
                    BufferCastType bufferCastType = ns::BufferCastType::Byte;
                    size_t offset = 0;
                    size_t length;
                    uint8_t *data = nullptr;

                    bool isTypedArray;
                    napi_is_typedarray(env, jsObj, &isTypedArray);
                    if (isTypedArray) {
                        napi_typedarray_type type;
                        napi_value arrayBuffer;
                        size_t byteOffset;
                        napi_get_typedarray_info(env, jsObj, &type, &length, (void **) &data,
                                                 &arrayBuffer, &byteOffset);
                        offset = byteOffset;
                        bufferCastType = JsArgConverter::GetCastType(type);
                    } else {
                        bool isArrayBuffer;
                        napi_is_arraybuffer(env, jsObj, &isArrayBuffer);
                        if (isArrayBuffer) {
                            napi_get_arraybuffer_info(env, jsObj, (void **) &data, &length);
                        } else {
                            napi_get_dataview_info(env, jsObj, &length, (void **) &data, nullptr,
                                                   &offset);
                        }
                    }

                    auto directBuffer = jenv.NewDirectByteBuffer(data + offset, length);

                    auto directBufferClazz = jenv.GetObjectClass(directBuffer);

                    auto byteOrderId = jenv.GetMethodID(directBufferClazz, "order",
                                                        "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;");

                    auto byteOrderClazz = jenv.FindClass("java/nio/ByteOrder");

                    auto byteOrderEnumId = jenv.GetStaticMethodID(byteOrderClazz, "nativeOrder",
                                                                  "()Ljava/nio/ByteOrder;");

                    auto nativeByteOrder = jenv.CallStaticObjectMethodA(byteOrderClazz,
                                                                        byteOrderEnumId, nullptr);

                    directBuffer = jenv.CallObjectMethod(directBuffer, byteOrderId,
                                                         nativeByteOrder);

                    jobject buffer;

                    if (bufferCastType == BufferCastType::Short) {
                        auto id = jenv.GetMethodID(directBufferClazz, "asShortBuffer",
                                                   "()Ljava/nio/ShortBuffer;");
                        buffer = jenv.CallObjectMethodA(directBuffer, id, nullptr);
                    } else if (bufferCastType == BufferCastType::Int) {
                        auto id = jenv.GetMethodID(directBufferClazz, "asIntBuffer",
                                                   "()Ljava/nio/IntBuffer;");
                        buffer = jenv.CallObjectMethodA(directBuffer, id, nullptr);
                    } else if (bufferCastType == BufferCastType::Long) {
                        auto id = jenv.GetMethodID(directBufferClazz, "asLongBuffer",
                                                   "()Ljava/nio/LongBuffer;");
                        buffer = jenv.CallObjectMethodA(directBuffer, id, nullptr);
                    } else if (bufferCastType == BufferCastType::Float) {
                        auto id = jenv.GetMethodID(directBufferClazz, "asFloatBuffer",
                                                   "()Ljava/nio/FloatBuffer;");
                        buffer = jenv.CallObjectMethodA(directBuffer, id, nullptr);
                    } else if (bufferCastType == BufferCastType::Double) {
                        auto id = jenv.GetMethodID(directBufferClazz, "asDoubleBuffer",
                                                   "()Ljava/nio/DoubleBuffer;");
                        buffer = jenv.CallObjectMethodA(directBuffer, id, nullptr);
                    } else {
                        buffer = directBuffer;
                    }

                    buffer = jenv.NewGlobalRef(buffer);

                    int id = objectManager->GetOrCreateObjectId(buffer);
                    auto clazz = jenv.GetObjectClass(buffer);
                    objectManager->Link(jsObj, id, clazz);

                    obj = objectManager->GetJavaObjectByJsObject(env, jsObj);
                }

                napi_value privateValue;
                napi_get_named_property(env, jsObj, "nullNode", &privateValue);

                if (privateValue != nullptr) {
                    auto node = reinterpret_cast<MetadataNode *>(privateValue);

                    if (node == nullptr) {
                        s << "Cannot get type of the null argument at index " << index;
                        success = false;
                        break;
                    }

                    auto type = node->GetName();
                    auto nullObjName = "org/nativescript/runtime/napi/NullObject";
                    auto nullObjCtorSig = "(Ljava/lang/Class;)V";

                    jclass nullClazz = jenv.FindClass(nullObjName);
                    jmethodID ctor = jenv.GetMethodID(nullClazz, "<init>", nullObjCtorSig);
                    jclass clazzToNull = jenv.FindClass(type.c_str());
                    jobject nullObjType = jenv.NewObject(nullClazz, ctor, clazzToNull);

                    if (nullObjType != nullptr) {
                        SetConvertedObject(jenv, index, nullObjType, false);
                    } else {
                        SetConvertedObject(jenv, index, nullptr);
                    }

                    success = true;
                    return success;
                }

                success = !obj.IsNull();
                if (success) {
                    SetConvertedObject(jenv, index, obj.Move(), obj.IsGlobal());
                } else {
                    size_t str_len;
                    napi_get_value_string_utf8(env, jsObj, nullptr, 0, &str_len);
                    string jsObjStr(str_len, '\0');
                    napi_get_value_string_utf8(env, jsObj, &jsObjStr[0], str_len + 1, &str_len);
                    s << "Cannot marshal JavaScript argument " << jsObjStr << " at index " << index
                      << " to Java type.";
                }
                break;

            default:
                throw NativeScriptException("Unsupported cast type");
        }
    } else {
        s << "Cannot marshal JavaScript argument at index " << index << " to Java type.";
        success = false;
    }

    if (!success) {
        m_error.index = index;
        m_error.msg = s.str();
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

        JEnv env;

        if (JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS == nullptr) {
            JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS = env.FindClass("java/lang/Object");
        }

        JniLocalRef tmpArr(
                env.NewObjectArray(m_argsLen, JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS,
                                   nullptr));
        m_arr = (jobjectArray) env.NewGlobalRef(tmpArr);

        for (int i = 0; i < m_argsLen; i++) {
            env.SetObjectArrayElement(m_arr, i, m_argsAsObject[i]);
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

        delete[] m_argsAsObject;
    }
}

jclass JsArgToArrayConverter::JAVA_LANG_OBJECT_CLASS = nullptr;
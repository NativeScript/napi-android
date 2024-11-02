#include "JsArgConverter.h"
#include "ObjectManager.h"
#include "JniSignatureParser.h"
#include "JsArgToArrayConverter.h"
#include "ArgConverter.h"
#include "NumericCasts.h"
#include "NativeScriptException.h"
#include <cstdlib>

using namespace std;
using namespace tns;

JsArgConverter::JsArgConverter(napi_env env, napi_value caller, napi_value *args, size_t argc,
                               const std::string &methodSignature, MetadataEntry *entry)
        : m_env(env), m_isValid(true), m_methodSignature(methodSignature), m_error(Error()) {
    int napiProvidedArgumentsLength = argc;
    m_argsLen = 1 + napiProvidedArgumentsLength;

    if (m_argsLen > 0) {
        if ((entry != nullptr) && (entry->getIsResolved())) {
            if (entry->parsedSig.empty()) {
                JniSignatureParser parser(m_methodSignature);
                entry->parsedSig = parser.Parse();
            }
            m_tokens = entry->parsedSig;
        } else {
            JniSignatureParser parser(m_methodSignature);
            m_tokens = parser.Parse();
        }

        m_isValid = ConvertArg(env, caller, 0);

        if (!m_isValid) {
            throw NativeScriptException("Error while converting argument!");
        }

        for (size_t i = 0; i < napiProvidedArgumentsLength; i++) {
            m_isValid = ConvertArg(env, args[i], i + 1);

            if (!m_isValid) {
                break;
            }
        }
    }
}

JsArgConverter::JsArgConverter(napi_env env, napi_value *args, size_t argc,
                               bool hasImplementationObject, const std::string &methodSignature,
                               MetadataEntry *entry)
        : m_env(env), m_isValid(true), m_methodSignature(methodSignature), m_error(Error()) {
    m_argsLen = !hasImplementationObject ? argc : argc - 1;

    if (m_argsLen > 0) {
        if ((entry != nullptr) && (entry->getIsResolved())) {
            if (entry->parsedSig.empty()) {
                JniSignatureParser parser(m_methodSignature);
                entry->parsedSig = parser.Parse();
            }
            m_tokens = entry->parsedSig;
        } else {
            JniSignatureParser parser(m_methodSignature);
            m_tokens = parser.Parse();
        }

        for (size_t i = 0; i < m_argsLen; i++) {
            m_isValid = ConvertArg(env, args[i], i);

            if (!m_isValid) {
                break;
            }
        }
    }
}

JsArgConverter::JsArgConverter(napi_env env, napi_value *args, size_t argc,
                               const std::string &methodSignature)
        : m_env(env), m_isValid(true), m_methodSignature(methodSignature), m_error(Error()) {
    m_argsLen = argc;

    JniSignatureParser parser(m_methodSignature);
    m_tokens = parser.Parse();

    for (size_t i = 0; i < m_argsLen; i++) {
        m_isValid = ConvertArg(env, args[i], i);

        if (!m_isValid) {
            break;
        }
    }
}

tns::BufferCastType JsArgConverter::GetCastType(napi_typedarray_type type) {
    switch (type) {
        case napi_uint16_array:
        case napi_int16_array:
            return tns::BufferCastType::Short;
        case napi_uint32_array:
        case napi_int32_array:
            return tns::BufferCastType::Int;
        case napi_float32_array:
            return tns::BufferCastType::Float;
        case napi_float64_array:
            return tns::BufferCastType::Double;
        case napi_bigint64_array:
        case napi_biguint64_array:
            return tns::BufferCastType::Long;
        default:
            return tns::BufferCastType::Byte;
    }
}

bool JsArgConverter::ConvertArg(napi_env env, napi_value arg, int index) {
    bool success = false;

    char buff[1024];

    const auto &typeSignature = m_tokens.at(index);

    if (arg == nullptr) {
        SetConvertedObject(index, nullptr);
        success = false;
    } else {
        napi_valuetype argType;
        napi_typeof(m_env, arg, &argType);

        if (argType == napi_object || argType == napi_function) {
            bool isArray;
            napi_is_array(m_env, arg, &isArray);

            if (isArray) {
                success = typeSignature[0] == '[';

                if (success) {
                    success = ConvertJavaScriptArray(env, arg, index);
                }

                if (!success) {
                    sprintf(buff, "Cannot convert array to %s at index %d", typeSignature.c_str(),
                            index);
                }
            } else {
                auto castType = NumericCasts::GetCastType(m_env, arg);

                napi_value castValue = NumericCasts::GetCastValue(m_env, arg);
                JniLocalRef obj;

                auto runtime = Runtime::GetRuntime(m_env);
                auto objectManager = runtime->GetObjectManager();

                JEnv jEnv;

                napi_valuetype valueType = napi_undefined;
                if (castValue != nullptr) {
                    napi_typeof(env, castValue, &valueType);
                }

                switch (castType) {
                    case CastType::Char:
                        if (valueType == napi_string) {
                            string value = ArgConverter::ConvertToString(m_env, castValue);
                            m_args[index].c = (jchar) value[0];
                            success = true;
                        }
                        break;

                    case CastType::Byte:
                        if (valueType == napi_string) {
                            string strValue = ArgConverter::ConvertToString(m_env, castValue);
                            int byteArg = atoi(strValue.c_str());
                            jbyte value = (jbyte) byteArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        } else if (valueType == napi_number) {
                            int byteArg = napi_util::get_int32(env, castValue);
                            jbyte value = (jbyte) byteArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }

                        break;

                    case CastType::Short:
                        if (valueType == napi_string) {
                            string strValue = ArgConverter::ConvertToString(m_env, castValue);
                            int shortArg = atoi(strValue.c_str());
                            jshort value = (jshort) shortArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        } else if (valueType == napi_number) {
                            int shortArg;
                            napi_get_value_int32(m_env, castValue, &shortArg);
                            jshort value = (jshort) shortArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::Long:
                        if (valueType == napi_string) {
                            string strValue = ArgConverter::ConvertToString(m_env, castValue);
                            int64_t longArg = atoll(strValue.c_str());
                            jlong value = (jlong) longArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        } else if (valueType == napi_number) {
                            int64_t longArg;
                            napi_get_value_int64(m_env, castValue, &longArg);
                            jlong value = (jlong) longArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::Float:
                        if (valueType == napi_number) {
                            double floatArg;
                            napi_get_value_double(m_env, castValue, &floatArg);
                            jfloat value = (jfloat) floatArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::Double:
                        if (valueType == napi_number) {
                            double doubleArg;
                            napi_get_value_double(m_env, castValue, &doubleArg);
                            jdouble value = (jdouble) doubleArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::None:
                        obj = objectManager->GetJavaObjectByJsObject(env, arg);

                        if (obj.IsNull()) {
                            bool isTypedArray;
                            napi_is_typedarray(m_env, arg, &isTypedArray);

                            if (isTypedArray) {
                                napi_typedarray_type type;
                                size_t length;
                                void *data;
                                napi_value arrayBuffer;
                                size_t byteOffset;

                                napi_get_typedarray_info(m_env, arg, &type, &length, &data,
                                                         &arrayBuffer, &byteOffset);

                                BufferCastType bufferCastType = JsArgConverter::GetCastType(type);

                                auto directBuffer = jEnv.NewDirectByteBuffer(
                                        static_cast<uint8_t *>(data) + byteOffset, length);

                                auto directBufferClazz = jEnv.GetObjectClass(directBuffer);

                                auto byteOrderId = jEnv.GetMethodID(directBufferClazz, "order",
                                                                    "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;");

                                auto byteOrderClazz = jEnv.FindClass("java/nio/ByteOrder");

                                auto byteOrderEnumId = jEnv.GetStaticMethodID(byteOrderClazz,
                                                                              "nativeOrder",
                                                                              "()Ljava/nio/ByteOrder;");

                                auto nativeByteOrder = jEnv.CallStaticObjectMethodA(byteOrderClazz,
                                                                                    byteOrderEnumId,
                                                                                    nullptr);

                                directBuffer = jEnv.CallObjectMethod(directBuffer, byteOrderId,
                                                                     nativeByteOrder);

                                jobject buffer;

                                if (bufferCastType == BufferCastType::Short) {
                                    auto id = jEnv.GetMethodID(directBufferClazz, "asShortBuffer",
                                                               "()Ljava/nio/ShortBuffer;");
                                    buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
                                } else if (bufferCastType == BufferCastType::Int) {
                                    auto id = jEnv.GetMethodID(directBufferClazz, "asIntBuffer",
                                                               "()Ljava/nio/IntBuffer;");
                                    buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
                                } else if (bufferCastType == BufferCastType::Long) {
                                    auto id = jEnv.GetMethodID(directBufferClazz, "asLongBuffer",
                                                               "()Ljava/nio/LongBuffer;");
                                    buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
                                } else if (bufferCastType == BufferCastType::Float) {
                                    auto id = jEnv.GetMethodID(directBufferClazz, "asFloatBuffer",
                                                               "()Ljava/nio/FloatBuffer;");
                                    buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
                                } else if (bufferCastType == BufferCastType::Double) {
                                    auto id = jEnv.GetMethodID(directBufferClazz, "asDoubleBuffer",
                                                               "()Ljava/nio/DoubleBuffer;");
                                    buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
                                } else {
                                    buffer = directBuffer;
                                }

                                buffer = jEnv.NewGlobalRef(buffer);

                                int id = objectManager->GetOrCreateObjectId(buffer);
                                auto clazz = jEnv.GetObjectClass(buffer);

                                objectManager->Link(arg, id, clazz);

                                obj = objectManager->GetJavaObjectByJsObject(env, arg);
                            }
                        }

                        napi_value nullNode;
                        napi_get_named_property(env, arg, PROP_KEY_NULL_NODE_NAME, &nullNode);
                        if (!napi_util::is_null_or_undefined(env, nullNode)) {
                            SetConvertedObject(index, nullptr);
                            success = true;
                            break;
                        }

                        success = !obj.IsNull();

                        if (success) {
                            SetConvertedObject(index, obj.Move(), obj.IsGlobal());
                        } else {
                            if (napi_util::is_number_object(env, arg)) {
                                napi_value numValue = napi_util::valueOf(env, arg);

                                bool isFloat;
                                napi_is_float(env, numValue, &isFloat);
                                if (isFloat) {
                                    double floatArg;
                                    napi_get_value_double(m_env, numValue, &floatArg);
                                    jfloat value = (jfloat) floatArg;
                                    success = ConvertFromCastFunctionObject(value, index);
                                } else {
                                    int intArg;
                                    napi_get_value_int32(m_env, numValue, &intArg);
                                    jint value = (jint) intArg;
                                    success = ConvertFromCastFunctionObject(value, index);
                                }
                                break;
                            } else if (napi_util::is_string_object(env, arg)) {
                                napi_value stringValue = napi_util::valueOf(env, arg);
                                success = ConvertJavaScriptString(env, stringValue, index);
                                break;
                            } else if (napi_util::is_boolean_object(env, arg)) {
                                napi_value boolValue = napi_util::valueOf(env, arg);
                                success = ConvertJavaScriptBoolean(env, boolValue, index);
                                break;
                            }

                            sprintf(buff, "Cannot convert object to %s at index %d",
                                    typeSignature.c_str(), index);
                        }
                        break;

                    default:
                        throw NativeScriptException("Unsupported cast type");
                }
            }
        } else if (argType == napi_number) {
            success = ConvertJavaScriptNumber(env, arg, index);

            if (!success) {
                sprintf(buff, "Cannot convert number to %s at index %d", typeSignature.c_str(),
                        index);
            }
        } else if (argType == napi_boolean) {
            success = ConvertJavaScriptBoolean(env, arg, index);

            if (!success) {
                sprintf(buff, "Cannot convert boolean to %s at index %d", typeSignature.c_str(),
                        index);
            }
        } else if (argType == napi_string) {
            success = ConvertJavaScriptString(env, arg, index);

            if (!success) {
                sprintf(buff, "Cannot convert string to %s at index %d", typeSignature.c_str(),
                        index);
            }
        } else if (argType == napi_undefined || argType == napi_null) {
            SetConvertedObject(index, nullptr);
            success = true;
        } else {
            SetConvertedObject(index, nullptr);
            success = false;
        }
    }

    if (!success) {
        m_error.index = index;
        m_error.msg = string(buff);
    }

    return success;
}


void JsArgConverter::SetConvertedObject(int index, jobject obj, bool isGlobal) {
    m_args[index].l = obj;
    if ((obj != nullptr) && !isGlobal) {
        m_args_refs[m_args_refs_size++] = index;
    }
}

bool JsArgConverter::ConvertJavaScriptNumber(napi_env env, napi_value jsValue, int index) {
    bool success = true;

    jvalue value = {0};

    const auto &typeSignature = m_tokens.at(index);

    const char typePrefix = typeSignature[0];

    switch (typePrefix) {
        case 'B': { // byte
            int32_t intValue;
            napi_get_value_int32(env, jsValue, &intValue);
            value.b = (jbyte) intValue;
            break;
        }
        case 'S': { // short
            int32_t intValue;
            napi_get_value_int32(env, jsValue, &intValue);
            value.s = (jshort) intValue;
            break;
        }
        case 'I': { // int
            int32_t intValue;
            napi_get_value_int32(env, jsValue, &intValue);
            value.i = (jint) intValue;
            break;
        }
        case 'J': { // long
            int64_t intValue;
            napi_get_value_int64(env, jsValue, &intValue);
            value.j = (jlong) intValue;
            break;
        }
        case 'F': { // float
            double doubleValue;
            napi_get_value_double(env, jsValue, &doubleValue);
            value.f = (jfloat) doubleValue;
            break;
        }
        case 'D': { // double
            double doubleValue;
            napi_get_value_double(env, jsValue, &doubleValue);
            value.d = (jdouble) doubleValue;
            break;
        }
        default:
            success = false;
            break;
    }

    if (success) {
        m_args[index] = value;
    }

    return success;
}

bool JsArgConverter::ConvertJavaScriptBoolean(napi_env env, napi_value jsValue, int index) {
    bool success;

    const auto &typeSignature = m_tokens.at(index);

    if (typeSignature == "Z") {
        bool argValue;
        napi_get_value_bool(env, jsValue, &argValue);

        jboolean value = argValue ? JNI_TRUE : JNI_FALSE;
        m_args[index].z = value;
        success = true;
    } else {
        success = false;
    }

    return success;
}

bool JsArgConverter::ConvertJavaScriptString(napi_env env, napi_value jsValue, int index) {
    jstring stringObject = ArgConverter::ConvertToJavaString(env, jsValue);
    SetConvertedObject(index, stringObject);

    return true;
}

bool JsArgConverter::ConvertJavaScriptArray(napi_env env, napi_value jsArr, int index) {
    bool success = true;

    jarray arr = nullptr;

    uint32_t jsLen;
    napi_get_array_length(env, jsArr, &jsLen);

    const jsize arrLength = jsLen;

    const auto &arraySignature = m_tokens.at(index);

    std::string elementType = arraySignature.substr(1);

    const char elementTypePrefix = elementType[0];

    jclass elementClass;
    std::string strippedClassName;

    JEnv jenv;
    switch (elementTypePrefix) {
        case 'Z': {
            arr = jenv.NewBooleanArray(arrLength);
            std::vector<jboolean> bools(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);

                bool boolValue;
                napi_get_value_bool(env, element, &boolValue);
                bools[i] = (jboolean) boolValue;
            }
            jenv.SetBooleanArrayRegion((jbooleanArray) arr, 0, arrLength, bools.data());
            break;
        }
        case 'B': {
            arr = jenv.NewByteArray(arrLength);
            std::vector<jbyte> bytes(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                int32_t intValue;
                napi_get_value_int32(env, element, &intValue);
                bytes[i] = (jbyte) intValue;
            }
            jenv.SetByteArrayRegion((jbyteArray) arr, 0, arrLength, bytes.data());
            break;
        }
        case 'C': {
            arr = jenv.NewCharArray(arrLength);
            std::vector<jchar> chars(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                size_t str_len;
                napi_get_value_string_utf8(env, element, nullptr, 0, &str_len);
                std::string str(str_len, '\0');
                napi_get_value_string_utf8(env, element, &str[0], str_len + 1, &str_len);
                chars[i] = (jchar) str[0];
            }
            jenv.SetCharArrayRegion((jcharArray) arr, 0, arrLength, chars.data());
            break;
        }
        case 'S': {
            arr = jenv.NewShortArray(arrLength);
            std::vector<jshort> shorts(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                int32_t intValue;
                napi_get_value_int32(env, element, &intValue);
                shorts[i] = (jshort) intValue;
            }
            jenv.SetShortArrayRegion((jshortArray) arr, 0, arrLength, shorts.data());
            break;
        }
        case 'I': {
            arr = jenv.NewIntArray(arrLength);
            std::vector<jint> ints(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                int32_t intValue;
                napi_get_value_int32(env, element, &intValue);
                ints[i] = (jint) intValue;
            }
            jenv.SetIntArrayRegion((jintArray) arr, 0, arrLength, ints.data());
            break;
        }
        case 'J': {
            arr = jenv.NewLongArray(arrLength);
            std::vector<jlong> longs(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                int64_t intValue;
                napi_get_value_int64(env, element, &intValue);
                longs[i] = (jlong) intValue;
            }
            jenv.SetLongArrayRegion((jlongArray) arr, 0, arrLength, longs.data());
            break;
        }
        case 'F': {
            arr = jenv.NewFloatArray(arrLength);
            std::vector<jfloat> floats(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                double doubleValue;
                napi_get_value_double(env, element, &doubleValue);
                floats[i] = (jfloat) doubleValue;
            }
            jenv.SetFloatArrayRegion((jfloatArray) arr, 0, arrLength, floats.data());
            break;
        }
        case 'D': {
            arr = jenv.NewDoubleArray(arrLength);
            std::vector<jdouble> doubles(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                double doubleValue;
                napi_get_value_double(env, element, &doubleValue);
                doubles[i] = (jdouble) doubleValue;
            }
            jenv.SetDoubleArrayRegion((jdoubleArray) arr, 0, arrLength, doubles.data());
            break;
        }
        case 'L':
            strippedClassName = elementType.substr(1, elementType.length() - 2);
            elementClass = jenv.FindClass(strippedClassName);
            arr = jenv.NewObjectArray(arrLength, elementClass, nullptr);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                napi_get_element(env, jsArr, i, &element);
                JsArgToArrayConverter c(env, element, false, (int) Type::Null);
                jobject o = c.GetConvertedArg();
                jenv.SetObjectArrayElement((jobjectArray) arr, (int) i, o);
            }
            break;
        default:
            success = false;
            break;
    }

    if (success) {
        SetConvertedObject(index, arr);
    }

    return success;
}


template<typename T>
bool JsArgConverter::ConvertFromCastFunctionObject(T value, int index) {
    bool success = false;

    const auto &typeSignature = m_tokens.at(index);

    const char typeSignaturePrefix = typeSignature[0];

    switch (typeSignaturePrefix) {
        case 'B':
            m_args[index].b = (jbyte) value;
            success = true;
            break;

        case 'S':
            m_args[index].s = (jshort) value;
            success = true;
            break;

        case 'I':
            m_args[index].i = (jint) value;
            success = true;
            break;

        case 'J':
            m_args[index].j = (jlong) value;
            success = true;
            break;

        case 'F':
            m_args[index].f = (jfloat) value;
            success = true;
            break;

        case 'D':
            m_args[index].d = (jdouble) value;
            success = true;
            break;

        default:
            success = false;
            break;
    }

    return success;
}

int JsArgConverter::Length() const {
    return m_argsLen;
}

bool JsArgConverter::IsValid() const {
    return m_isValid;
}

jvalue *JsArgConverter::ToArgs() {
    return m_args;
}

JsArgConverter::Error JsArgConverter::GetError() const {
    return m_error;
}

JsArgConverter::~JsArgConverter() {
    if (m_argsLen > 0) {
        JEnv env;
        for (int i = 0; i < m_args_refs_size; i++) {
            int index = m_args_refs[i];
            if (index != -1) {
                env.DeleteLocalRef(m_args[index].l);
            }
        }
    }
}
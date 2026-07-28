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
                               const std::string &methodSignature, MetadataEntry *entry, JNIEnv *jniEnv,
                               ObjectManager *objectManager)
        : m_env(env), m_jniEnv(jniEnv), m_objectManager(objectManager), m_isValid(true),
          m_error(Error()) {
    int napiProvidedArgumentsLength = argc;
    m_argsLen = 1 + napiProvidedArgumentsLength;

    if (m_argsLen > 0) {
        if ((entry != nullptr) && (entry->getIsResolved())) {
            if (entry->parsedSig.empty()) {
                JniSignatureParser parser(methodSignature);
                entry->parsedSig = parser.Parse();
            }
            m_tokens = &entry->parsedSig;
        } else {
            JniSignatureParser parser(methodSignature);
            m_ownedTokens = parser.Parse();
            m_tokens = &m_ownedTokens;
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
                               MetadataEntry *entry, JNIEnv *jniEnv, ObjectManager *objectManager)
        : m_env(env), m_jniEnv(jniEnv), m_objectManager(objectManager), m_isValid(true),
          m_error(Error()) {
    m_argsLen = !hasImplementationObject ? argc : argc - 1;

    if (m_argsLen > 0) {
        if ((entry != nullptr) && (entry->getIsResolved())) {
            if (entry->parsedSig.empty()) {
                JniSignatureParser parser(methodSignature);
                entry->parsedSig = parser.Parse();
            }
            m_tokens = &entry->parsedSig;
        } else {
            JniSignatureParser parser(methodSignature);
            m_ownedTokens = parser.Parse();
            m_tokens = &m_ownedTokens;
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
        : m_env(env), m_isValid(true), m_error(Error()) {
    m_argsLen = argc;

    JniSignatureParser parser(methodSignature);
    m_ownedTokens = parser.Parse();
    m_tokens = &m_ownedTokens;

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
    napi_status status;
    bool success = false;

    char buff[1024];
    buff[0] = '\0';

    const auto &typeSignature = (*m_tokens)[index];

    // Record only the failing index up front (cheap). The default diagnostic
    // string is built lazily in GetError() from m_tokens[index], so the common
    // success path pays no per-argument string allocation. A NAPI_GUARD early
    // `return false` below still leaves m_error.index set for GetError().
    m_error.index = index;

    if (arg == nullptr) {
        SetConvertedObject(index, nullptr);
        success = false;
    } else {
        napi_valuetype argType;
        NAPI_GUARD(napi_typeof(m_env, arg, &argType)) {
            return false;
        }

        if (argType == napi_object || argType == napi_function) {
            bool isArray;
            NAPI_GUARD(napi_is_array(m_env, arg, &isArray)) {
                return false;
            }

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

                CastType castType = CastType::None;

#ifdef USE_HOST_OBJECT
                // A non-ok status here just means "not a host object" (some
                // engines, e.g. PrimJS, return an error rather than data=NULL for
                // plain objects); treat it as no host data and continue.
                void *data = nullptr;
                napi_get_host_object_data(env, arg, &data);
                if (data) {
                    castType = CastType::None;
                } else {
                    castType = NumericCasts::GetCastType(env, arg);
                }
#else
                castType = NumericCasts::GetCastType(m_env, arg);
#endif

                CastType castTypeCheck = NumericCasts::GetCastType(env, arg);
                if (castTypeCheck != CastType::None) {
                    castType = castTypeCheck;
                }
                napi_value castValue;
                napi_valuetype valueType = napi_undefined;
                if (castType != CastType::None) {
                    castValue = NumericCasts::GetCastValue(m_env, arg);
                    if (castValue != nullptr) {
                        NAPI_GUARD(napi_typeof(env, castValue, &valueType)) {
                            return false;
                        }
                    }
                }

                JniLocalRef obj;

                auto objectManager = m_objectManager != nullptr
                                     ? m_objectManager
                                     : Runtime::GetRuntime(m_env)->GetObjectManager();

                JEnv jEnv = GetJEnv();

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
                            NAPI_GUARD(napi_get_value_int32(m_env, castValue, &shortArg)) {
                                return false;
                            }
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
                            NAPI_GUARD(napi_get_value_int64(m_env, castValue, &longArg)) {
                                return false;
                            }
                            jlong value = (jlong) longArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::Float:
                        if (valueType == napi_number) {
                            double floatArg;
                            NAPI_GUARD(napi_get_value_double(m_env, castValue, &floatArg)) {
                                return false;
                            }
                            jfloat value = (jfloat) floatArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::Double:
                        if (valueType == napi_number) {
                            double doubleArg;
                            NAPI_GUARD(napi_get_value_double(m_env, castValue, &doubleArg)) {
                                return false;
                            }
                            jdouble value = (jdouble) doubleArg;
                            success = ConvertFromCastFunctionObject(value, index);
                        }
                        break;

                    case CastType::None:
                        obj = objectManager->GetJavaObjectByJsObject(arg);

                        if (obj.IsNull()) {
                            bool isArrayBuffer = false;
                            bool isDataView = false;
                            bool isTypedArray = false;

                            NAPI_GUARD(napi_is_arraybuffer(env, arg, &isArrayBuffer)) {
                                return false;
                            }
                            if (!isArrayBuffer) {
                                NAPI_GUARD(napi_is_typedarray(env, arg, &isTypedArray)) {
                                    return false;
                                }
                                if (!isTypedArray) {
                                    NAPI_GUARD(napi_is_dataview(env, arg, &isDataView)) {
                                        return false;
                                    }
                                }
                            }

                            if (isArrayBuffer || isDataView || isTypedArray) {
                                obj = JsArgConverter::GetByteBuffer(env, arg, isArrayBuffer,
                                                                    isTypedArray, isDataView);
                            }
                        }

#ifdef USE_HOST_OBJECT
                        if (!data) {
#endif
                            napi_value nullNode;
                            NAPI_GUARD(napi_get_named_property(env, arg, PROP_KEY_NULL_NODE_NAME, &nullNode)) {
                                return false;
                            }
                            if (!napi_util::is_null_or_undefined(env, nullNode)) {
                                SetConvertedObject(index, nullptr);
                                success = true;
                                break;
                            }
#ifdef USE_HOST_OBJECT
                        }
#endif

                        success = !obj.IsNull();

                        if (success) {
                            SetConvertedObject(index, obj.Move(), obj.IsGlobal());
                        } else {
                            if (napi_util::is_number_object(env, arg)) {
                                success = ConvertJavaScriptNumber(env, arg, index, true);
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

                            if (!success) {
                                sprintf(buff, "Cannot convert object to %s at index %d",
                                        typeSignature.c_str(), index);
                            }
                        }
                        break;

                    default:
                        throw NativeScriptException("Unsupported cast type");
                }
            }
        } else if (argType == napi_number) {
            success = ConvertJavaScriptNumber(env, arg, index, false);

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
        // Keep the seeded default when no specific message was formatted (buff
        // untouched), avoiding a garbage/empty message.
        if (buff[0] != '\0') {
            m_error.msg = string(buff);
        }
    }

    return success;
}


void JsArgConverter::SetConvertedObject(int index, jobject obj, bool isGlobal) {
    m_args[index].l = obj;
    if ((obj != nullptr) && !isGlobal) {
        m_args_refs[m_args_refs_size++] = index;
    }
}

bool JsArgConverter::ConvertJavaScriptNumber(napi_env env, napi_value jsValue, int index,
                                             bool isNumberObject = false) {
    napi_status status;
    bool success = true;

    jvalue value = {0};

    const auto &typeSignature = (*m_tokens)[index];

    const char typePrefix = typeSignature[0];

    switch (typePrefix) {
        case 'B': { // byte
            int32_t intValue;
            if (isNumberObject) {
                NAPI_GUARD(napi_get_value_int32(env, napi_util::valueOf(env, jsValue), &intValue)) {
                    return false;
                }
            } else {
                NAPI_GUARD(napi_get_value_int32(env, jsValue, &intValue)) {
                    return false;
                }
            }
            value.b = (jbyte) intValue;
            break;
        }
        case 'S': { // short
            int intValue;
            if (isNumberObject) {
                NAPI_GUARD(napi_get_value_int32(env, napi_util::valueOf(env, jsValue), &intValue)) {
                    return false;
                }
            } else {
                NAPI_GUARD(napi_get_value_int32(env, jsValue, &intValue)) {
                    return false;
                }
            }
            value.s = (jshort) intValue;
            break;
        }
        case 'I': { // int
            int intValue;
            if (isNumberObject) {
                NAPI_GUARD(napi_get_value_int32(env, napi_util::valueOf(env, jsValue), &intValue)) {
                    return false;
                }
            } else {
                NAPI_GUARD(napi_get_value_int32(env, jsValue, &intValue)) {
                    return false;
                }
            }
            value.i = (jint) intValue;
            break;
        }
        case 'J': { // long
            int64_t intValue;
            if (isNumberObject) {
                NAPI_GUARD(napi_get_value_int64(env, napi_util::valueOf(env, jsValue), &intValue)) {
                    return false;
                }
            } else {
                NAPI_GUARD(napi_get_value_int64(env, jsValue, &intValue)) {
                    return false;
                }
            }
            value.j = (jlong) intValue;
            break;
        }
        case 'F': { // float
            double doubleValue;
            if (isNumberObject) {
                NAPI_GUARD(napi_get_value_double(env, napi_util::valueOf(env, jsValue), &doubleValue)) {
                    return false;
                }
            } else {
                NAPI_GUARD(napi_get_value_double(env, jsValue, &doubleValue)) {
                    return false;
                }
            }
            value.f = (jfloat) doubleValue;
            break;
        }
        case 'D': { // double
            double doubleValue;
            if (isNumberObject) {
                NAPI_GUARD(napi_get_value_double(env, napi_util::valueOf(env, jsValue), &doubleValue)) {
                    return false;
                }
            } else {
                NAPI_GUARD(napi_get_value_double(env, jsValue, &doubleValue)) {
                    return false;
                }
            }
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
    napi_status status;
    bool success;

    const auto &typeSignature = (*m_tokens)[index];

    if (typeSignature == "Z") {
        bool argValue;
        NAPI_GUARD(napi_get_value_bool(env, jsValue, &argValue)) {
            return false;
        }

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
    napi_status status;
    bool success = true;

    jarray arr = nullptr;

    uint32_t jsLen;
    NAPI_GUARD(napi_get_array_length(env, jsArr, &jsLen)) {
        return false;
    }

    const jsize arrLength = jsLen;

    const auto &arraySignature = (*m_tokens)[index];

    std::string elementType = arraySignature.substr(1);

    const char elementTypePrefix = elementType[0];

    jclass elementClass;
    std::string strippedClassName;

    JEnv jenv = GetJEnv();
    switch (elementTypePrefix) {
        case 'Z': {
            arr = jenv.NewBooleanArray(arrLength);
            std::vector<jboolean> bools(arrLength);
            for (uint32_t i = 0; i < arrLength; i++) {
                napi_value element;
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}

                bool boolValue;
                NAPI_GUARD(napi_get_value_bool(env, element, &boolValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                int32_t intValue;
                NAPI_GUARD(napi_get_value_int32(env, element, &intValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                size_t str_len;
                NAPI_GUARD(napi_get_value_string_utf8(env, element, nullptr, 0, &str_len)) {}
                std::string str(str_len, '\0');
                NAPI_GUARD(napi_get_value_string_utf8(env, element, &str[0], str_len + 1, &str_len)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                int32_t intValue;
                NAPI_GUARD(napi_get_value_int32(env, element, &intValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                int32_t intValue;
                NAPI_GUARD(napi_get_value_int32(env, element, &intValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                int64_t intValue;
                NAPI_GUARD(napi_get_value_int64(env, element, &intValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                double doubleValue;
                NAPI_GUARD(napi_get_value_double(env, element, &doubleValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                double doubleValue;
                NAPI_GUARD(napi_get_value_double(env, element, &doubleValue)) {}
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
                NAPI_GUARD(napi_get_element(env, jsArr, i, &element)) {}
                JsArgToArrayConverter c(env, element, false, (int) Type::Null,
                                        m_objectManager != nullptr
                                        ? m_objectManager
                                        : Runtime::GetRuntime(env)->GetObjectManager());
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

    const auto &typeSignature = (*m_tokens)[index];

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
    Error e = m_error;
    // Build the default diagnostic lazily (only when an error is actually
    // queried and no specific message was already formatted on the failure path).
    if (e.index >= 0 && e.msg.empty() && m_tokens != nullptr &&
        e.index < (int) m_tokens->size()) {
        e.msg = "Cannot convert argument at index " + std::to_string(e.index) +
                " to " + (*m_tokens)[e.index];
    }
    return e;
}

JsArgConverter::~JsArgConverter() {
    if (m_argsLen > 0) {
        JEnv env = GetJEnv();
        for (int i = 0; i < m_args_refs_size; i++) {
            int index = m_args_refs[i];
            if (index != -1) {
                env.DeleteLocalRef(m_args[index].l);
            }
        }
    }
}

JniLocalRef JsArgConverter::GetByteBuffer(napi_env env, napi_value object, bool isArrayBuffer,
                                          bool isTypedArray, bool isDataView) {
    napi_status status;
    JEnv jEnv;

    BufferCastType bufferCastType = tns::BufferCastType::Byte;
    size_t offset = 0;
    size_t length = 0;
    void *data = nullptr;

    if (isTypedArray) {
        napi_typedarray_type type;
        napi_value arrayBuffer = nullptr;
        size_t byteOffset = 0;
        // Bail on failure: continuing would feed uninitialized arrayBuffer/offset
        // (and a possibly-null data pointer) into NewDirectByteBuffer below.
        NAPI_GUARD(napi_get_typedarray_info(env, object, &type, nullptr, &data,
                                 &arrayBuffer, &byteOffset)) {
            return JniLocalRef();
        }
        NAPI_GUARD(napi_get_arraybuffer_info(env, arrayBuffer, nullptr, &length)) {
            return JniLocalRef();
        }

        offset = byteOffset;
        bufferCastType = JsArgConverter::GetCastType(type);
    } else if (isArrayBuffer) {
        NAPI_GUARD(napi_get_arraybuffer_info(env, object, &data, &length)) {
            return JniLocalRef();
        }
    } else if (isDataView) {
        NAPI_GUARD(napi_get_dataview_info(env, object, &length, &data, nullptr,
                               &offset)) {
            return JniLocalRef();
        }
    }

    jobject directBuffer;

    if (isDataView || isTypedArray) {
        directBuffer =jEnv.NewDirectByteBuffer(static_cast<uint8_t *>(data) + offset, length);
    } else {
        directBuffer = jEnv.NewDirectByteBuffer(static_cast<uint8_t *>(data), length);
    }


    auto directBufferClazz = jEnv.GetObjectClass(directBuffer);

    auto byteOrderId = BYTE_ORDER_METHOD_ID;

    if (!BYTE_ORDER_METHOD_ID) {
        byteOrderId = jEnv.GetMethodID(directBufferClazz, "order",
                                       "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;");
        BYTE_ORDER_METHOD_ID = byteOrderId;
    }

    auto byteOrderClazz = jEnv.FindClass("java/nio/ByteOrder");

    auto byteOrderEnumId = BYTE_ORDER_ENUM_ID;

    if (!byteOrderEnumId) {
        byteOrderEnumId = jEnv.GetStaticMethodID(byteOrderClazz,
                                                 "nativeOrder",
                                                 "()Ljava/nio/ByteOrder;");
        BYTE_ORDER_ENUM_ID = byteOrderEnumId;
    }

    auto nativeByteOrder = jEnv.CallStaticObjectMethodA(byteOrderClazz,
                                                        byteOrderEnumId,
                                                        nullptr);

    directBuffer = jEnv.CallObjectMethod(directBuffer, byteOrderId,
                                         nativeByteOrder);

    jobject buffer;

    if (bufferCastType == BufferCastType::Short) {
        auto id = AS_SHORT_BUFFER;
        if (!id) {
            id = jEnv.GetMethodID(directBufferClazz, "asShortBuffer",
                                  "()Ljava/nio/ShortBuffer;");
            AS_SHORT_BUFFER = id;
        }

        buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
    } else if (bufferCastType == BufferCastType::Int) {
        auto id = AS_INT_BUFFER;

        if (!id) {
            id = jEnv.GetMethodID(directBufferClazz, "asIntBuffer",
                                  "()Ljava/nio/IntBuffer;");
            AS_INT_BUFFER = id;
        }
        buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
    } else if (bufferCastType == BufferCastType::Long) {
        auto id = AS_LONG_BUFFER;

        if (!id) {
            id = jEnv.GetMethodID(directBufferClazz, "asLongBuffer",
                                  "()Ljava/nio/LongBuffer;");
            AS_LONG_BUFFER = id;
        }

        buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
    } else if (bufferCastType == BufferCastType::Float) {

        auto id = AS_FLOAT_BUFFER;
        if (!id) {
            id = jEnv.GetMethodID(directBufferClazz, "asFloatBuffer",
                                  "()Ljava/nio/FloatBuffer;");
            AS_FLOAT_BUFFER = id;
        }

        buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
    } else if (bufferCastType == BufferCastType::Double) {

        auto id = AS_DOUBLE_BUFFER;
        if (!id) {
            id = jEnv.GetMethodID(directBufferClazz, "asDoubleBuffer",
                                  "()Ljava/nio/DoubleBuffer;");
            AS_DOUBLE_BUFFER = id;
        }
        buffer = jEnv.CallObjectMethodA(directBuffer, id, nullptr);
    } else {
        buffer = directBuffer;
    }

    buffer = jEnv.NewGlobalRef(buffer);

    ObjectManager *objectManager = Runtime::GetRuntime(env)->GetObjectManager();

    int id = objectManager->GetOrCreateObjectId(buffer);
    auto clazz = jEnv.GetObjectClass(buffer);

    ObjectManager::MarkObject(env, object);

    objectManager->Link(object, id, clazz);

    return objectManager->GetJavaObjectByJsObject(object);
}

jmethodID JsArgConverter::BYTE_ORDER_METHOD_ID = nullptr;
jmethodID JsArgConverter::BYTE_ORDER_ENUM_ID = nullptr;
jmethodID JsArgConverter::AS_SHORT_BUFFER = nullptr;
jmethodID JsArgConverter::AS_LONG_BUFFER = nullptr;
jmethodID JsArgConverter::AS_FLOAT_BUFFER = nullptr;
jmethodID JsArgConverter::AS_INT_BUFFER = nullptr;
jmethodID JsArgConverter::AS_DOUBLE_BUFFER = nullptr;
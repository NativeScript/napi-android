#include "ArrayBufferHelper.h"
#include "ArgConverter.h"
#include "NativeScriptException.h"
#include <sstream>

using namespace tns;

ArrayBufferHelper::ArrayBufferHelper()
        : m_objectManager(nullptr), m_ByteBufferClass(nullptr), m_isDirectMethodID(nullptr),
          m_remainingMethodID(nullptr), m_getMethodID(nullptr) {
}

void ArrayBufferHelper::CreateConvertFunctions(napi_env env, napi_value global, ObjectManager* objectManager) {
    m_objectManager = objectManager;
    napi_value extData;
    napi_create_external(env, this, nullptr, nullptr, &extData);
    napi_value fromFunc;
    napi_create_function(env, "from", NAPI_AUTO_LENGTH, CreateFromCallbackStatic, extData, &fromFunc);

    napi_value arrBufferCtorFunc;
    napi_get_named_property(env, global, "ArrayBuffer", &arrBufferCtorFunc);
    napi_set_named_property(env, arrBufferCtorFunc, "from", fromFunc);
}

napi_value ArrayBufferHelper::CreateFromCallbackStatic(napi_env env, napi_callback_info info) {
    napi_value thisArg;
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, &thisArg, nullptr);

    try {
        void* data;
        napi_get_value_external(env, thisArg, &data);
        auto thiz = reinterpret_cast<ArrayBufferHelper*>(data);
        return thiz->CreateFromCallbackImpl(env, argc, args);
    } catch (NativeScriptException& e) {
        e.ReThrowToNapi(env);
    } catch (std::exception& e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value ArrayBufferHelper::CreateFromCallbackImpl(napi_env env, size_t argc, napi_value* args) {
    if (argc != 1) {
        throw NativeScriptException("Wrong number of arguments (1 expected)");
    }

    napi_value arg = args[0];

    bool isObject = napi_util::is_of_type(env, arg, napi_object);

    if (!isObject) {
        throw NativeScriptException("Wrong type of argument (object expected)");
    }

    auto argObj = arg;

    auto obj = m_objectManager->GetJavaObjectByJsObject(env, argObj);

    if (obj.IsNull()) {
        throw NativeScriptException("Wrong type of argument (object expected)");
    }

    JEnv jEnv;

    if (m_ByteBufferClass == nullptr) {
        m_ByteBufferClass = jEnv.FindClass("java/nio/ByteBuffer");
        assert(m_ByteBufferClass != nullptr);
    }

    auto isByteBuffer = jEnv.IsInstanceOf(obj, m_ByteBufferClass);

    if (!isByteBuffer) {
        throw NativeScriptException("Wrong type of argument (ByteBuffer expected)");
    }

    if (m_isDirectMethodID == nullptr) {
        m_isDirectMethodID = jEnv.GetMethodID(m_ByteBufferClass, "isDirect", "()Z");
        assert(m_isDirectMethodID != nullptr);
    }

    auto ret = jEnv.CallBooleanMethod(obj, m_isDirectMethodID);

    auto isDirectBuffer = ret == JNI_TRUE;

    napi_value arrayBuffer;

    if (isDirectBuffer) {
        auto data = jEnv.GetDirectBufferAddress(obj);
        auto size = jEnv.GetDirectBufferCapacity(obj);

        void* externalData = data;
        napi_create_external_arraybuffer(env, externalData, size, nullptr, nullptr, &arrayBuffer);
    } else {
        if (m_remainingMethodID == nullptr) {
            m_remainingMethodID = jEnv.GetMethodID(m_ByteBufferClass, "remaining", "()I");
            assert(m_remainingMethodID != nullptr);
        }

        int bufferRemainingSize = jEnv.CallIntMethod(obj, m_remainingMethodID);

        if (m_getMethodID == nullptr) {
            m_getMethodID = jEnv.GetMethodID(m_ByteBufferClass, "get", "([BII)Ljava/nio/ByteBuffer;");
            assert(m_getMethodID != nullptr);
        }

        jbyteArray byteArray = jEnv.NewByteArray(bufferRemainingSize);
        jEnv.CallObjectMethod(obj, m_getMethodID, byteArray, 0, bufferRemainingSize);

        auto byteArrayElements = jEnv.GetByteArrayElements(byteArray, 0);

        jbyte* data = new jbyte[bufferRemainingSize];
        memcpy(data, byteArrayElements, bufferRemainingSize);

        napi_create_external_arraybuffer(env, data, bufferRemainingSize, [](napi_env env, void* finalize_data, void* finalize_hint) {
            delete[] static_cast<jbyte*>(finalize_data);
        }, nullptr, &arrayBuffer);

        jEnv.ReleaseByteArrayElements(byteArray, byteArrayElements, 0);
    }

    napi_value nativeObjectKey;
    napi_create_string_utf8(env, "nativeObject", NAPI_AUTO_LENGTH, &nativeObjectKey);
    napi_set_property(env, arrayBuffer, nativeObjectKey, argObj);

    return arrayBuffer;
}
#ifndef ARRAYBUFFERHELPER_H_
#define ARRAYBUFFERHELPER_H_

#include "ObjectManager.h"

namespace tns {
    class ArrayBufferHelper {
        public:
            ArrayBufferHelper();

            void CreateConvertFunctions(napi_env env, napi_value global, ObjectManager* objectManager);

        private:

            static napi_value CreateFromCallbackStatic(napi_env env, napi_callback_info info);

            napi_value CreateFromCallbackImpl(napi_env env, size_t argc, napi_value* args);

            ObjectManager* m_objectManager;

            jclass m_ByteBufferClass;
            jmethodID m_isDirectMethodID;
            jmethodID m_remainingMethodID;
            jmethodID m_getMethodID;
    };
}


#endif /* ARRAYBUFFERHELPER_H_ */

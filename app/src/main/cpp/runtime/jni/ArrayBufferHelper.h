#ifndef ARRAYBUFFERHELPER_H_
#define ARRAYBUFFERHELPER_H_

#include "v8.h"
#include "ObjectManager.h"

namespace tns {
    class ArrayBufferHelper {
        public:
            ArrayBufferHelper();

            void CreateConvertFunctions(napi_env env, napi_value global, ObjectManager* objectManager);

        private:

            static void CreateFromCallbackStatic(napi_env env, napi_callback_info info);

            void CreateFromCallbackImpl(napi_env env, napi_callback_info info);

            ObjectManager* m_objectManager;

            jclass m_ByteBufferClass;
            jmethodID m_isDirectMethodID;
            jmethodID m_remainingMethodID;
            jmethodID m_getMethodID;
    };
}


#endif /* ARRAYBUFFERHELPER_H_ */

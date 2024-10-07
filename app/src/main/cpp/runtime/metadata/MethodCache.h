#ifndef METHODCACHE_H_
#define METHODCACHE_H_

#include <string>
#include <map>
#include "JEnv.h"
#include "MetadataEntry.h"
#include "ArgsWrapper.h"

namespace ns {
/*
 * MethodCache: class dealing with method/constructor resolution.
 */
class MethodCache {
    public:
        /*
         * CacheMethodInfo: struct holding resolved methods/constructor resolution
         */
        struct CacheMethodInfo {
            CacheMethodInfo()
                :
                retType(MethodReturnType::Unknown), mid(nullptr), clazz(nullptr), isStatic(false) {
            }
            std::string signature;
            std::string returnType;
            MethodReturnType retType;
            jmethodID mid;
            jclass clazz;
            bool isStatic;
        };

        static void Init();

        static CacheMethodInfo ResolveMethodSignature(napi_env env, const std::string& className, const std::string& methodName, napi_callback_info info, bool isStatic);

        static CacheMethodInfo ResolveConstructorSignature(napi_env env, const ArgsWrapper& argWrapper, const std::string& fullClassName, jclass javaClass, bool isInterface);

    private:
        MethodCache() {
        }

        static std::string EncodeSignature(napi_env env, const std::string& className, const std::string& methodName, napi_callback_info info, bool isStatic);

        static std::string GetType(napi_env env, napi_value value);

        static std::string ResolveJavaMethod(napi_env env, napi_callback_info info, const std::string& className, const std::string& methodName);

        static std::string ResolveConstructor(napi_env env, napi_callback_info info, jclass javaClass, bool isInterface);

        static jclass RUNTIME_CLASS;

        static jmethodID RESOLVE_METHOD_OVERLOAD_METHOD_ID;

        static jmethodID RESOLVE_CONSTRUCTOR_SIGNATURE_ID;

        /*
         * "s_method_ctor_signature_cache" holding all resolved CacheMethodInfo against an encoded_signature string.
         *  Used for caching the resolved constructor or method signature.
         * The encoded signature has template: <className>.S/I.<methodName>.<argsCount>.<arg1class>.<...>
         */
        static robin_hood::unordered_map<std::string, CacheMethodInfo> s_method_ctor_signature_cache;
};
}

#endif /* METHODCACHE_H_ */


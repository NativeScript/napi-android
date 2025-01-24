#include "MethodCache.h"
#include "JniLocalRef.h"
#include "JsArgToArrayConverter.h"
#include "MetadataNode.h"
#include "NativeScriptAssert.h"
#include "Util.h"
#include "ArgConverter.h"
#include "NumericCasts.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include <sstream>

using namespace std;
using namespace tns;

void MethodCache::Init()
{
    JEnv jEnv;

    RUNTIME_CLASS = jEnv.FindClass("com/tns/Runtime");
    assert(RUNTIME_CLASS != nullptr);

    RESOLVE_METHOD_OVERLOAD_METHOD_ID = jEnv.GetMethodID(RUNTIME_CLASS, "resolveMethodOverload", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;");
    assert(RESOLVE_METHOD_OVERLOAD_METHOD_ID != nullptr);

    RESOLVE_CONSTRUCTOR_SIGNATURE_ID = jEnv.GetMethodID(RUNTIME_CLASS, "resolveConstructorSignature", "(Ljava/lang/Class;[Ljava/lang/Object;)Ljava/lang/String;");
    assert(RESOLVE_CONSTRUCTOR_SIGNATURE_ID != nullptr);
}


robin_hood::unordered_map<string, MethodCache::CacheMethodInfo> MethodCache::s_method_ctor_signature_cache;
jclass MethodCache::RUNTIME_CLASS = nullptr;
jmethodID MethodCache::RESOLVE_METHOD_OVERLOAD_METHOD_ID = nullptr;
jmethodID MethodCache::RESOLVE_CONSTRUCTOR_SIGNATURE_ID = nullptr;

#include <string>
#include <sstream>
#include <cctype>
#include <regex>
#include <dirent.h>
#include <set>
#include <cerrno>
#include <unistd.h>
#include "NativeScriptException.h"
#include "MetadataNode.h"
#include "CallbackHandlers.h"
#include "NativeScriptAssert.h"
#include "File.h"
#include "Runtime.h"
#include "ArgConverter.h"
#include "FieldCallbackData.h"
#include "MetadataBuilder.h"
#include "ArgsWrapper.h"
#include "Util.h"
#include "GlobalHelpers.h"
#include "JSONObjectHelper.h"

using namespace std;

void MetadataNode::Init(napi_env env) {
    auto cache = GetMetadataNodeCache(env);
}

napi_value MetadataNode::CreateArrayObjectConstructor(napi_env env) {
    auto it = s_arrayObjects.find(env);
    if (it != s_arrayObjects.end()) {
        auto value = napi_util::get_ref_value(env, it->second);
        if (!napi_util::is_null_or_undefined(env, value)) return value;
    }

    auto node = GetOrCreate("java/lang/Object");
    auto objectConstructor = node->GetConstructorFunction(env);

    napi_status status;
    napi_value arrayConstructor;
    const char *name = "ArrayObjectWrapper";
    NAPI_GUARD(napi_define_class(env, name, strlen(name),
                      [](napi_env env, napi_callback_info info) -> napi_value {
                          NAPI_CALLBACK_BEGIN(0)
                          return jsThis;
                      }, nullptr, 0, nullptr, &arrayConstructor)) {
        return nullptr;
    }
    napi_value proto = napi_util::get_prototype(env, arrayConstructor);
    ObjectManager::MarkObject(env, proto);

    napi_util::napi_set_function(env, proto, "setValueAtIndex", ArraySetterCallback, nullptr);
    napi_util::napi_set_function(env, proto, "getValueAtIndex", ArrayGetterCallback, nullptr);
    napi_util::napi_set_function(env, proto, "getAllValues", ArrayGetAllValuesCallback, nullptr);
    napi_util::define_property(env, proto, "length", nullptr, ArrayLengthCallback);

    // Native helpers (previously synthesized by the JS getNativeArrayProp).
    napi_util::napi_set_function(env, proto, "map", ArrayMapCallback, nullptr);
    napi_util::napi_set_function(env, proto, "forEach", ArrayForEachCallback, nullptr);
    napi_util::napi_set_function(env, proto, "toString", ArrayToStringCallback, nullptr);
    {
        napi_value globalObj, symbolCtor, symbolIterator, iteratorFn;
        NAPI_GUARD(napi_get_global(env, &globalObj)) {}
        NAPI_GUARD(napi_get_named_property(env, globalObj, "Symbol", &symbolCtor)) {}
        NAPI_GUARD(napi_get_named_property(env, symbolCtor, "iterator", &symbolIterator)) {}
        NAPI_GUARD(napi_create_function(env, "[Symbol.iterator]", NAPI_AUTO_LENGTH,
                             ArraySymbolIteratorCallback, nullptr, &iteratorFn)) {}
        NAPI_GUARD(napi_set_property(env, proto, symbolIterator, iteratorFn)) {}
    }

    napi_util::napi_inherits(env, arrayConstructor, objectConstructor);

    s_arrayObjects.emplace(env, napi_util::make_ref(env, arrayConstructor));

    return arrayConstructor;
}

napi_value MetadataNode::CreateExtendedJSWrapper(napi_env env, ObjectManager *objectManager,
                                                 const std::string &proxyClassName,
                                                 int javaObjectID, MetadataNode **outNode) {
    napi_value extInstance = nullptr;

    auto cacheData = GetCachedExtendedClassData(env, proxyClassName);

    if (cacheData.node != nullptr) {
        
        extInstance = objectManager->GetEmptyObject();
        ObjectManager::MarkSuperCall(env, extInstance);
        napi_value extendedCtorFunc = napi_util::get_ref_value(env,
                                                               cacheData.extendedCtorFunction);
        napi_util::setPrototypeOf(env, extInstance,
                                  napi_util::get_prototype(env, extendedCtorFunc));

        napi_status status;
        NAPI_GUARD(napi_set_named_property(env, extInstance, CONSTRUCTOR, extendedCtorFunc)) {}

        SetInstanceMetadata(env, extInstance, cacheData.node);
        *outNode = cacheData.node;
    }

    return extInstance;
}

string MetadataNode::GetTypeMetadataName(napi_env env, napi_value value) {
    napi_status status;
    napi_value typeMetadataName;
    NAPI_GUARD(napi_get_named_property(env, value, PRIVATE_TYPE_NAME, &typeMetadataName)) {
        return "";
    }

    return napi_util::get_string_value(env, typeMetadataName);
}


bool MetadataNode::isArray() {
    return m_isArray;
}

napi_value MetadataNode::CreateJSWrapper(napi_env env, ObjectManager *objectManager) {
    napi_status status;
    napi_value obj;

    if (m_isArray) {
        obj = CreateArrayWrapper(env);
    } else {
        obj = objectManager->GetEmptyObject();
        napi_value ctorFunc = GetConstructorFunction(env);
        NAPI_GUARD(napi_set_named_property(env, obj, CONSTRUCTOR, ctorFunc)) {}
        napi_util::setPrototypeOf(env, obj, napi_util::get_prototype(env, ctorFunc));
        SetInstanceMetadata(env, obj, this);
    }

    return obj;
}

napi_value MetadataNode::ArrayGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    try {

        napi_value index = argv[0];
        int32_t indexValue;
        NAPI_GUARD(napi_get_value_int32(env, index, &indexValue)) {
            return nullptr;
        }
        auto node = GetInstanceMetadata(env, jsThis);

        return CallbackHandlers::GetArrayElement(env, jsThis, indexValue, node->m_name);

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ArrayGetAllValuesCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    try {
        auto node = GetInstanceMetadata(env, jsThis);
        auto length = CallbackHandlers::GetArrayLength(env, jsThis);
        napi_value arr;
        NAPI_GUARD(napi_create_array(env, &arr)) {
            return nullptr;
        }

        // Resolve the manager + backing array once for the whole loop.
        auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
        JniLocalRef javaArr = objectManager->GetJavaObjectByJsObjectFast(jsThis);
        jobject javaArrObj = javaArr;

        for (int i = 0; i < length; i++) {
            napi_value element = CallbackHandlers::GetArrayElement(env, jsThis, i, node->m_name,
                                                                  objectManager, javaArrObj);
            NAPI_GUARD(napi_set_element(env, arr, i, element)) {}
        }

        return arr;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ArraySetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2);

    try {

        napi_value index = argv[0];
        napi_value value = argv[1];

        int32_t indexValue;
        NAPI_GUARD(napi_get_value_int32(env, index, &indexValue)) {
            return nullptr;
        }
        auto node = GetInstanceMetadata(env, jsThis);

        CallbackHandlers::SetArrayElement(env, jsThis, indexValue, node->m_name, value);
        return value;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ArrayLengthCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    try {
        int length = CallbackHandlers::GetArrayLength(env, jsThis);

        napi_value len;
        NAPI_GUARD(napi_create_int32(env, length, &len)) {
            return nullptr;
        }
        return len;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ArrayMapCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    try {
        napi_value callback = argv[0];
        auto node = GetInstanceMetadata(env, jsThis);
        int length = CallbackHandlers::GetArrayLength(env, jsThis);

        napi_value result;
        NAPI_GUARD(napi_create_array_with_length(env, length, &result)) {
            return nullptr;
        }

        napi_value undefined;
        NAPI_GUARD(napi_get_undefined(env, &undefined)) {
            return nullptr;
        }

        // Resolve the manager + backing array once for the whole loop.
        auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
        JniLocalRef javaArr = objectManager->GetJavaObjectByJsObjectFast(jsThis);
        jobject javaArrObj = javaArr;

        for (int i = 0; i < length; i++) {
            napi_value element =
                    CallbackHandlers::GetArrayElement(env, jsThis, i, node->m_name,
                                                      objectManager, javaArrObj);
            napi_value index;
            NAPI_GUARD(napi_create_int32(env, i, &index)) {
                return nullptr;
            }
            napi_value cbArgs[3] = {element, index, jsThis};
            napi_value mapped;
            NAPI_GUARD(napi_call_function(env, undefined, callback, 3, cbArgs, &mapped)) {
                return nullptr;
            }
            NAPI_GUARD(napi_set_element(env, result, i, mapped)) {}
        }

        return result;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ArrayForEachCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    try {
        napi_value callback = argv[0];
        auto node = GetInstanceMetadata(env, jsThis);
        int length = CallbackHandlers::GetArrayLength(env, jsThis);

        napi_value undefined;
        NAPI_GUARD(napi_get_undefined(env, &undefined)) {
            return nullptr;
        }

        // Resolve the manager + backing array once for the whole loop.
        auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
        JniLocalRef javaArr = objectManager->GetJavaObjectByJsObjectFast(jsThis);
        jobject javaArrObj = javaArr;

        for (int i = 0; i < length; i++) {
            napi_value element =
                    CallbackHandlers::GetArrayElement(env, jsThis, i, node->m_name,
                                                      objectManager, javaArrObj);
            napi_value index;
            NAPI_GUARD(napi_create_int32(env, i, &index)) {
                return nullptr;
            }
            napi_value cbArgs[3] = {element, index, jsThis};
            napi_value ignored;
            NAPI_GUARD(napi_call_function(env, undefined, callback, 3, cbArgs, &ignored)) {
                return nullptr;
            }
        }

        return undefined;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

// Builds a real JS array snapshot of all elements (native get loop).
static napi_value BuildArraySnapshot(napi_env env, napi_value jsThis,
                                     const std::string &signature) {
    napi_status status;
    int length = CallbackHandlers::GetArrayLength(env, jsThis);
    napi_value values;
    NAPI_GUARD(napi_create_array_with_length(env, length, &values)) {
        return nullptr;
    }

    // Resolve the manager + backing array once for the whole loop.
    auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
    JniLocalRef javaArr = objectManager->GetJavaObjectByJsObjectFast(jsThis);
    jobject javaArrObj = javaArr;

    for (int i = 0; i < length; i++) {
        napi_value element =
                CallbackHandlers::GetArrayElement(env, jsThis, i, signature,
                                                  objectManager, javaArrObj);
        NAPI_GUARD(napi_set_element(env, values, i, element)) {}
    }
    return values;
}

napi_value MetadataNode::ArrayToStringCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    try {
        auto node = GetInstanceMetadata(env, jsThis);
        napi_value values = BuildArraySnapshot(env, jsThis, node->m_name);

        // values.join(",")
        napi_value joinFn;
        NAPI_GUARD(napi_get_named_property(env, values, "join", &joinFn)) {
            return nullptr;
        }
        napi_value comma;
        NAPI_GUARD(napi_create_string_utf8(env, ",", 1, &comma)) {
            return nullptr;
        }
        napi_value result;
        NAPI_GUARD(napi_call_function(env, values, joinFn, 1, &comma, &result)) {
            return nullptr;
        }
        return result;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value
MetadataNode::ArraySymbolIteratorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    try {
        auto node = GetInstanceMetadata(env, jsThis);
        napi_value values = BuildArraySnapshot(env, jsThis, node->m_name);

        // return values[Symbol.iterator]()  -> delegate to the real array iterator
        napi_value globalObj, symbolCtor, symbolIterator, iterMethod, iterator;
        NAPI_GUARD(napi_get_global(env, &globalObj)) {
            return nullptr;
        }
        NAPI_GUARD(napi_get_named_property(env, globalObj, "Symbol", &symbolCtor)) {
            return nullptr;
        }
        NAPI_GUARD(napi_get_named_property(env, symbolCtor, "iterator", &symbolIterator)) {
            return nullptr;
        }
        NAPI_GUARD(napi_get_property(env, values, symbolIterator, &iterMethod)) {
            return nullptr;
        }
        NAPI_GUARD(napi_call_function(env, values, iterMethod, 0, nullptr, &iterator)) {
            return nullptr;
        }
        return iterator;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::CreateArrayWrapper(napi_env env) {
    napi_status status;
    napi_value constructor = CreateArrayObjectConstructor(env);
    napi_value instance;
    NAPI_GUARD(napi_new_instance(env, constructor, 0, nullptr, &instance)) {
        return nullptr;
    }
    SetInstanceMetadata(env, instance, this);
    return instance;
}

napi_value MetadataNode::GetImplementationObject(napi_env env, napi_value object) {
    napi_status status;
    auto target = object;
    napi_value currentPrototype = target;

    napi_value implementationObject;

    NAPI_GUARD(napi_get_named_property(env, currentPrototype, CLASS_IMPLEMENTATION_OBJECT,
                            &implementationObject)) {}

    if (implementationObject != nullptr && !napi_util::is_undefined(env, implementationObject)) {
        return implementationObject;
    }

    bool hasProperty;

    napi_value prototypeImplObjectKey;
    NAPI_GUARD(napi_create_string_utf8(env, PROP_KEY_IS_PROTOTYPE_IMPLEMENTATION_OBJECT, NAPI_AUTO_LENGTH,
                            &prototypeImplObjectKey)) {}
    NAPI_GUARD(napi_has_own_property(env, object, prototypeImplObjectKey, &hasProperty)) {}

    if (hasProperty) {
        bool maybeHasOwnProperty;
        napi_value prototypeKey;
        NAPI_GUARD(napi_create_string_utf8(env, PROTOTYPE, NAPI_AUTO_LENGTH, &prototypeKey)) {}
        NAPI_GUARD(napi_has_own_property(env, object, prototypeKey, &maybeHasOwnProperty)) {}

        if (!maybeHasOwnProperty) {
            return nullptr;
        }

        return napi_util::get_prototype(env, object);
    }

    napi_value activityImplementationObject;
    NAPI_GUARD(napi_get_named_property(env, object, "t::ActivityImplementationObject",
                            &activityImplementationObject)) {}

    if (activityImplementationObject != nullptr &&
        !napi_util::is_undefined(env, activityImplementationObject)) {
        return activityImplementationObject;
    }

    napi_value lastPrototype;

    bool prototypeCycleDetected = false;

    bool foundImplementationObject = false;

    while (!foundImplementationObject) {
        currentPrototype = napi_util::get_prototype(env, currentPrototype);

        if (napi_util::is_null(env, currentPrototype)) {
            break;
        }

        if (lastPrototype == currentPrototype) {
            auto abovePrototype = napi_util::get_prototype(env, currentPrototype);
            prototypeCycleDetected = abovePrototype == currentPrototype;
            break;
        }

        if (currentPrototype == nullptr || napi_util::is_null(env, currentPrototype) ||
            prototypeCycleDetected) {
            return nullptr;
        } else {
            napi_value implObject;
            NAPI_GUARD(napi_get_named_property(env, currentPrototype, CLASS_IMPLEMENTATION_OBJECT,
                                    &implObject)) {}

            if (implObject != nullptr && !napi_util::is_undefined(env, implObject)) {
                foundImplementationObject = true;
                return currentPrototype;
            }
        }
        lastPrototype = currentPrototype;
    }

    return implementationObject;
}

void MetadataNode::SetInstanceMetadata(napi_env env, napi_value object, MetadataNode *node) {
#ifdef USE_HOST_OBJECT
    // node now lives on the per-instance JSInstanceInfo (set in
    // ObjectManager::Link / GetOrCreateProxy); the "#instance_metadata" property
    // is no longer used on the host path.
    (void) env;
    (void) object;
    (void) node;
#else
    napi_status status;
    napi_value external;
    NAPI_GUARD(napi_create_external(env, node, [](napi_env env, void *d1, void *d2) {}, node, &external)) {
        return;
    }
    NAPI_GUARD(napi_set_named_property(env, object, "#instance_metadata", external)) {}
#endif
//    napi_wrap(env, object, node, nullptr, nullptr, nullptr);
}


napi_value MetadataNode::ExtendedClassConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(8)

    try {
        napi_value newTarget;
        // Throw (not return nullptr) so a JS exception is left pending; otherwise
        // the constructor trampoline maps a null result to `undefined` and
        // `new X()` silently yields undefined.
        NAPI_GUARD(napi_get_new_target(env, info, &newTarget)) {
            throw NativeScriptException("Failed to read new.target in constructor call.");
        }
        if (napi_util::is_null_or_undefined(env, newTarget)) return nullptr;

        auto extData = reinterpret_cast<ExtendedClassCallbackData *>(data);
        SetInstanceMetadata(env, jsThis, extData->node);

        napi_value implementationObject = napi_util::get_ref_value(env,
                                                                   extData->implementationObject);
        ObjectManager::MarkSuperCall(env, jsThis);

        string fullClassName = extData->fullClassName;

        ArgsWrapper argWrapper(argv, argc, ArgType::Class);
        napi_value jsThisProxy;
        bool success = CallbackHandlers::RegisterInstance(env, jsThis, fullClassName, argWrapper,
                                                          implementationObject, false,
                                                          &jsThisProxy, extData->node->m_name,
                                                          extData->node);

        return jsThisProxy;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::InterfaceConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(8)

    try {

        napi_valuetype arg1Type;
        napi_valuetype arg2Type;

        // Throw so an exception is left pending (a null result would otherwise
        // surface as `undefined` from `new`).
        NAPI_GUARD(napi_typeof(env, argv[0], &arg1Type)) {
            throw NativeScriptException("Failed to read constructor argument type.");
        }

        if (argc == 2) {
            NAPI_GUARD(napi_typeof(env, argv[1], &arg2Type)) {
                throw NativeScriptException("Failed to read constructor argument type.");
            }
        }

        napi_value implementationObject;
        napi_value interfaceName;

        if (argc == 1) {
            if (arg1Type != napi_object) {
                throw NativeScriptException(
                        string("Invalid arguments provided, first argument must be an object if only one argument is provided"));
                return nullptr;
            }
            implementationObject = argv[0];
        } else if (argc == 2) {
            if (arg1Type != napi_string) {
                throw NativeScriptException(
                        string("Invalid arguments provided, first argument must be a string if only two argument is provided"));
                return nullptr;
            }

            if (arg2Type != napi_object) {
                throw NativeScriptException(
                        string("Invalid arguments provided, second argument must be an object if only one argument is provided"));
                return nullptr;
            }

            interfaceName = argv[0];
            implementationObject = argv[1];
        } else {
            throw NativeScriptException(
                    string("Invalid arguments provided, first argument must be a string and second argument must be an object"));
        }

        auto node = reinterpret_cast<MetadataNode *>(data);

        auto className = node->m_implType;

        SetInstanceMetadata(env, jsThis, node);

        ObjectManager::MarkSuperCall(env, jsThis);


        napi_util::setPrototypeOf(env, implementationObject,
                                  napi_util::getPrototypeOf(env, jsThis));

        napi_util::setPrototypeOf(env, jsThis, implementationObject);

        NAPI_GUARD(napi_set_named_property(env, jsThis, CLASS_IMPLEMENTATION_OBJECT, implementationObject)) {}

        ArgsWrapper argsWrapper(argv, argc, ArgType::Interface);

        napi_value jsThisProxy;
        auto success = CallbackHandlers::RegisterInstance(env, jsThis, className, argsWrapper,
                                                          implementationObject, true, &jsThisProxy,
                                                          std::string(), node);
        return jsThisProxy;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ClassConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(8)

    try {

        auto node = reinterpret_cast<MetadataNode *>(data);

        SetInstanceMetadata(env, jsThis, node);

        // Plain construction has no extend name, so the full class name equals the
        // base class name; skip CreateFullClassName (a string copy) and use the
        // node's name directly for both.
        const string &className = node->m_name;

        ArgsWrapper argsWrapper(argv, argc, ArgType::Class);
        napi_value jsThisProxy;
        bool success = CallbackHandlers::RegisterInstance(env, jsThis, className, argsWrapper,
                                                          nullptr, false, &jsThisProxy, className,
                                                          node);

        return jsThisProxy;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

string MetadataNode::CreateFullClassName(const std::string &className,
                                         const std::string &extendNameAndLocation = "") {
    string fullClassName = className;

    // create a class name consisting only of the base class name + last file name part + line + column + variable identifier
    if (!extendNameAndLocation.empty()) {
        string tempClassName = className;
        fullClassName = Util::ReplaceAll(tempClassName, "$", "_");
        fullClassName += "_" + extendNameAndLocation;
    }

    return fullClassName;
}

bool MetadataNode::IsValidExtendName(napi_env env, napi_value name) {
    string extendName = ArgConverter::ConvertToString(env, name);

    for (char currentSymbol: extendName) {
        bool isValidExtendNameSymbol = isalpha(currentSymbol) ||
                                       isdigit(currentSymbol) ||
                                       currentSymbol == '_';
        if (!isValidExtendNameSymbol) {
            return false;
        }
    }

    return true;
}


bool
MetadataNode::GetExtendLocation(napi_env env, string &extendLocation, bool isTypeScriptExtend) {
    stringstream extendLocationStream;

    auto frames = tns::BuildStacktraceFrames(env, nullptr, 4);
    tns::JsStacktraceFrame *frame;
    if (isTypeScriptExtend) {
        if (Util::Contains(frames[2].text, "call_super")) {
            frame = &frames[3];
        } else {
            frame = &frames[2]; // the _super.apply call to ts_helpers will always be the third call frame
        }
    } else {
        frame = &frames[0];
    }

    if (frame == NULL) {
        DEBUG_WRITE("%s", "FRAME IS NULL!");
        return true;
    }

    string srcFileName = Util::ReplaceAll(frame->filename, "file://", "");

    string fullPathToFile;
    if (srcFileName == "<embedded>" || srcFileName == "<input>" || srcFileName == "JavaScript") {
        fullPathToFile = "script";
    } else {
        string hardcodedPathToSkip = Constants::APP_ROOT_FOLDER_PATH;
        int startIndex = hardcodedPathToSkip.length();
        int strToTakeLen = srcFileName.length() - startIndex - 3;
        fullPathToFile = srcFileName.substr(startIndex, strToTakeLen);
        fullPathToFile = srcFileName;
        replace(fullPathToFile.begin(), fullPathToFile.end(), '/', '_');
        replace(fullPathToFile.begin(), fullPathToFile.end(), '.', '_');
        replace(fullPathToFile.begin(), fullPathToFile.end(), '-', '_');
        replace(fullPathToFile.begin(), fullPathToFile.end(), ' ', '_');

        vector<string> pathParts;
        Util::SplitString(fullPathToFile, "_", pathParts);
        fullPathToFile =
                pathParts.back() == "js" ? pathParts[pathParts.size() - 2] : pathParts.back();
    }

    if (frame->line < 0) {
        extendLocationStream << fullPathToFile << " unknown line number";
        extendLocation = extendLocationStream.str();
        return false;
    }

    if (frame->col < 0) {
        extendLocationStream << fullPathToFile << " line:" << frame->line
                             << " unknown column number";
        extendLocation = extendLocationStream.str();
        return false;
    }
    int column = frame->col;
    if (frame->line == 1) {
        column -= ModuleInternal::MODULE_PROLOGUE_LENGTH;
    }

#ifdef __HERMES__
    column = column - 6;
#endif

    extendLocationStream << fullPathToFile << "_" << frame->line << "_" << column << "_";
    extendLocation = extendLocationStream.str();
    return true;
}


bool MetadataNode::ValidateExtendArguments(napi_env env, size_t argc, napi_value *argv,
                                           bool extendLocationFound, string &extendLocation,
                                           napi_value *extendName, napi_value *implementationObject,
                                           bool isTypeScriptExtend) {

    if (argc == 1) {
        if (!extendLocationFound) {
            stringstream ss;
            ss << "Invalid extend() call. No name specified for extend at location: "
               << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        if (!napi_util::is_object(env, argv[0])) {
            stringstream ss;
            ss << "Invalid extend() call. No implementation object specified at location: "
               << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        *implementationObject = argv[0];
    } else if (argc == 2 || isTypeScriptExtend) {
        if (!napi_util::is_of_type(env, argv[0], napi_string)) {
            stringstream ss;
            ss << "Invalid extend() call. No name for extend specified at location: "
               << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        if (!napi_util::is_object(env, argv[1])) {
            stringstream ss;
            ss
                    << "Invalid extend() call. Named extend should be called with second object parameter containing overridden methods at location: "
                    << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        DEBUG_WRITE("ExtendsCallMethodHandler: getting extend name");

        *extendName = argv[0];
        bool isValidExtendName = IsValidExtendName(env, *extendName);
        if (!isValidExtendName) {
            stringstream ss;
            ss << "The extend name \"" << ArgConverter::ConvertToString(env, *extendName)
               << "\" you provided contains invalid symbols. Try using the symbols [a-z, A-Z, 0-9, _]."
               << endl;
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }
        *implementationObject = argv[1];
    } else {
        stringstream ss;
        ss << "Invalid extend() call at location: " << extendLocation.c_str();
        string exceptionMessage = ss.str();
        throw NativeScriptException(exceptionMessage);
    }

    return true;
}

MetadataNode::ExtendedClassCacheData
MetadataNode::GetCachedExtendedClassData(napi_env env, const string &proxyClassName) {
    auto cache = GetMetadataNodeCache(env);
    ExtendedClassCacheData cacheData;
    auto itFound = cache->ExtendedCtorFuncCache.find(proxyClassName);
    if (itFound != cache->ExtendedCtorFuncCache.end()) {
        cacheData = itFound->second;
    }

    return cacheData;
}

MetadataNode::MetadataNodeCache *MetadataNode::GetMetadataNodeCache(napi_env env) {
    auto cache = s_metadata_node_cache.Get(env);
    if (cache) return cache;
    cache = new MetadataNodeCache;
    s_metadata_node_cache.Insert(env, cache);
    return cache;
}

MetadataNode::MetadataNode(MetadataTreeNode *treeNode) : m_treeNode(treeNode) {
    uint8_t nodeType = s_metadataReader.GetNodeType(treeNode);

    m_name = s_metadataReader.ReadTypeName(m_treeNode);

    uint8_t parentNodeType = s_metadataReader.GetNodeType(treeNode->parent);

    m_isArray = s_metadataReader.IsNodeTypeArray(parentNodeType);

    bool isInterface = s_metadataReader.IsNodeTypeInterface(nodeType);

    if (!m_isArray && isInterface) {
        bool isPrefix;
        auto impTypeName = s_metadataReader.ReadInterfaceImplementationTypeName(m_treeNode,
                                                                                isPrefix);
        m_implType = isPrefix
                     ? (impTypeName + m_name)
                     : impTypeName;
    }
}

void MetadataNode::CreateTopLevelNamespaces(napi_env env) {
    napi_status status;
    napi_value global;

    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    auto root = s_metadataReader.GetRoot();

    const auto &children = *root->children;

    for (auto treeNode: children) {
        uint8_t nodeType = s_metadataReader.GetNodeType(treeNode);

        if (nodeType == MetadataTreeNode::PACKAGE) {
            auto node = GetOrCreateInternal(treeNode);

            napi_value packageObj = node->CreateWrapper(env);

            string nameSpace = node->m_treeNode->name;
            // if the namespaces matches a javascript keyword, prefix it with $ to avoid TypeScript and JavaScript errors
            if (IsJavascriptKeyword(nameSpace)) {
                nameSpace = "$" + nameSpace;
            }
            NAPI_GUARD(napi_set_named_property(env, global, nameSpace.c_str(), packageObj)) {}
        }
    }
}

MetadataTreeNode *MetadataNode::GetOrCreateTreeNodeByName(const string &className) {
    MetadataTreeNode *result = nullptr;

    auto itFound = s_name2TreeNodeCache.find(className);

    if (itFound != s_name2TreeNodeCache.end()) {
        result = itFound->second;
    } else {
        result = s_metadataReader.GetOrCreateTreeNodeByName(className);

        s_name2TreeNodeCache.emplace(className, result);
    }

    return result;
}

string MetadataNode::GetName() {
    return m_name;
}

MetadataNode *MetadataNode::GetOrCreate(const string &className) {
    MetadataNode *node = nullptr;
    
    auto it = s_name2NodeCache.find(className);

    if (it == s_name2NodeCache.end()) {
        MetadataTreeNode *treeNode = GetOrCreateTreeNodeByName(className);

        node = GetOrCreateInternal(treeNode);

        s_name2NodeCache.emplace(className, node);
    } else {
        node = it->second;
    }

    return node;
}

MetadataNode *MetadataNode::GetOrCreateInternal(MetadataTreeNode *treeNode) {
    MetadataNode *result = nullptr;

    auto it = s_treeNode2NodeCache.find(treeNode);

    if (it != s_treeNode2NodeCache.end()) {
        result = it->second;
    } else {
            auto name = GetJniClassName(treeNode);
            if (!name.empty()) {
                auto it2 = s_name2NodeCache.find(name);
                if ( it2 != s_name2NodeCache.end()) {
                    result = it2->second;
                }
            }

            if (!result) {
                result = new MetadataNode(treeNode);
                s_treeNode2NodeCache.emplace(treeNode, result);
                if (!result->m_name.empty()) {
                    s_name2NodeCache.emplace(result->m_name, result);
                }
            }
    }

    auto found = s_treeNode2NodeCache.find(treeNode);
    if (found == s_treeNode2NodeCache.end()) {
        s_treeNode2NodeCache.emplace(treeNode, result);
    }

    return result;
}

MetadataEntry MetadataNode::GetChildMetadataForPackage(MetadataNode *node, const char *propName) {
    assert(node->m_treeNode->children != nullptr);

    MetadataEntry child(nullptr, NodeType::Class);

    const auto &children = *node->m_treeNode->children;

    for (auto treeNodeChild: children) {
        if (strcmp(treeNodeChild->name.c_str(), propName) == 0) {
            child.name = propName;
            child.treeNode = treeNodeChild;
            child.type = static_cast<NodeType>(s_metadataReader.GetNodeType(treeNodeChild));

            if (s_metadataReader.IsNodeTypeInterface((uint8_t) child.type)) {
                bool isPrefix;
                string declaringType = s_metadataReader.ReadInterfaceImplementationTypeName(
                        treeNodeChild, isPrefix);
                child.declaringType = isPrefix
                                      ? (declaringType +
                                         s_metadataReader.ReadTypeName(child.treeNode))
                                      : declaringType;
            }
        }
    }

    return child;
}

bool MetadataNode::IsJavascriptKeyword(const std::string &word) {
    static set<string> keywords;

    if (keywords.empty()) {
        string kw[]{"abstract", "arguments", "boolean", "break", "byte", "case", "catch", "char",
                    "class", "const", "continue", "debugger", "default", "delete", "do",
                    "double", "else", "enum", "eval", "export", "extends", "false", "final",
                    "finally", "float", "for", "function", "goto", "if", "implements",
                    "import", "in", "instanceof", "int", "interface", "let", "long", "native",
                    "new", "null", "package", "private", "protected", "public", "return",
                    "short", "static", "super", "switch", "synchronized", "this", "throw", "throws",
                    "transient", "true", "try", "typeof", "var", "void", "volatile", "while",
                    "with", "yield"};

        keywords = set<string>(kw, kw + sizeof(kw) / sizeof(kw[0]));
    }

    return keywords.find(word) != keywords.end();
}

napi_value MetadataNode::CreateWrapper(napi_env env) {
    napi_value result;
    uint8_t nodeType = s_metadataReader.GetNodeType(m_treeNode);
    bool isClass = s_metadataReader.IsNodeTypeClass(nodeType),
            isInterface = s_metadataReader.IsNodeTypeInterface(nodeType);
    napi_status status;

    if (isClass || isInterface) {
        result = GetConstructorFunction(env);
    } else if (s_metadataReader.IsNodeTypePackage(nodeType)) {
        result = CreatePackageObject(env);
    } else {
        std::stringstream ss;
        ss << "(InternalError): Can't create proxy for this type=" << static_cast<int>(nodeType);
        throw NativeScriptException(ss.str());
    }

    return result;
}

napi_value MetadataNode::PackageGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    try {
        auto childTreeNode = static_cast<MetadataTreeNode *>(data);
        DEBUG_WRITE("Get package item: %s", childTreeNode->name.c_str());

        auto childNode = MetadataNode::GetOrCreateInternal(childTreeNode);
        napi_value value = childNode->CreateWrapper(env);

        uint8_t childNodeType = s_metadataReader.GetNodeType(childTreeNode);
        if (s_metadataReader.IsNodeTypeInterface(childNodeType)) {
            // For all java interfaces we register the special Symbol.hasInstance property
            // which is invoked by the instanceof operator (https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Symbol/hasInstance).
            // For example:
            //
            // Object.defineProperty(android.view.animation.Interpolator, Symbol.hasInstance, {
            //    value: function(obj) {
            //        return true;
            //    }
            // });
            RegisterSymbolHasInstanceCallback(env, childTreeNode, value);
        }

        // org.json.JSONObject special-case. Cheap name check first so the parent
        // lookup only happens for the one class that needs it.
        if (childTreeNode->name == "JSONObject") {
            auto parentNode = GetOrCreateInternal(childTreeNode->parent);
            if (parentNode->m_name == "org/json") {
                JSONObjectHelper::RegisterFromFunction(env, value);
            }
        }

        // Replace this accessor on the receiver with the resolved value as a plain
        // (configurable) data property, so every subsequent `pkg.Child` access is a
        // direct, inline-cacheable property load instead of re-invoking this getter.
        napi_property_descriptor dataProp = {
                childTreeNode->name.c_str(), nullptr, nullptr, nullptr, nullptr,
                value, napi_default_jsproperty, nullptr};
        NAPI_GUARD(napi_define_properties(env, jsThis, 1, &dataProp)) {}

        return value;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
    return nullptr;
}

void MetadataNode::RegisterSymbolHasInstanceCallback(napi_env env, const MetadataTreeNode *treeNode,
                                                     napi_value interface) {
    if (napi_util::is_undefined(env, interface) || napi_util::is_null(env, interface)) {
        return;
    }

    JEnv jEnv;

    auto className = GetJniClassName(treeNode);
    auto clazz = jEnv.FindClass(className);
    if (clazz == nullptr) {
        return;
    }

    napi_status status;
    napi_value hasInstance;
    napi_value symbol;
    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }
    NAPI_GUARD(napi_get_named_property(env, global, "Symbol", &symbol)) {
        return;
    }
    NAPI_GUARD(napi_get_named_property(env, symbol, "hasInstance", &hasInstance)) {
        return;
    }
    napi_value method;
    NAPI_GUARD(napi_create_function(env, "hasInstance", NAPI_AUTO_LENGTH, SymbolHasInstanceCallback, clazz,
                         &method)) {
        return;
    }
   
    napi_property_descriptor desc = {
            nullptr, // utf8name
            hasInstance,      // name
            nullptr,      // method
            nullptr,       // getter
            nullptr,       // setter
            method,        // value
            napi_default, // attributes
            nullptr          // data
    };
    NAPI_GUARD(napi_define_properties(env, interface, 1, &desc)) {}
}

napi_value MetadataNode::SymbolHasInstanceCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(2);
    if (argc != 1) {
        throw NativeScriptException(string("Symbol.hasInstance must take exactly 1 argument"));
        return nullptr;
    }

    napi_value object = argv[0];

    if (!napi_util::is_object(env, object)) {
        return napi_util::get_false(env);
    }

    auto clazz = reinterpret_cast<jclass>(data);
    auto runtime = Runtime::GetRuntime(env);

    auto objectManager = runtime->GetObjectManager();
    auto obj = objectManager->GetJavaObjectByJsObject(object);

    if (obj.IsNull()) {
        // Couldn't find a corresponding java instance counterpart. This could happen
        // if the "instanceof" operator is invoked on a pure javascript instance
        return napi_util::get_false(env);
    }

    JEnv jEnv;
    auto isInstanceOf = jEnv.IsInstanceOf(obj, clazz);

    napi_value result;
    NAPI_GUARD(napi_get_boolean(env, isInstanceOf, &result)) {
        return nullptr;
    }

    return result;

}


std::string MetadataNode::GetJniClassName(const MetadataTreeNode *node) {
    std::stack<string> s;

    while (node != nullptr && !node->name.empty()) {
        s.push(node->name);
        node = node->parent;
    }

    string fullClassName;
    while (!s.empty()) {
        auto top = s.top();
        fullClassName = (fullClassName.empty()) ? top : fullClassName + "/" + top;
        s.pop();
    }

    return fullClassName;
}

napi_value MetadataNode::CreatePackageObject(napi_env env) {
    napi_status status;
    napi_value packageObj;
    NAPI_GUARD(napi_create_object(env, &packageObj)) {
        return nullptr;
    }

    auto ptrChildren = this->m_treeNode->children;

    if (ptrChildren != nullptr) {
        const auto &children = *ptrChildren;
        auto lastChildName = "";
        for (auto childNode: children) {
            if (strcmp(childNode->name.c_str(), lastChildName) == 0) {
                continue;
            }
            lastChildName = childNode->name.c_str();
            napi_property_descriptor descriptor{
                    childNode->name.c_str(),
                    nullptr,
                    nullptr,
                    PackageGetterCallback,
                    nullptr,
                    nullptr,
                    napi_default_jsproperty,
                    childNode};
            NAPI_GUARD(napi_define_properties(env, packageObj, 1, &descriptor)) {}
        }
    }

    return packageObj;
}

std::vector<MetadataNode::MethodCallbackData *> MetadataNode::SetClassMembers(
        napi_env env, napi_value constructor,
        std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
        const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
        MetadataTreeNode *treeNode) {

    if (treeNode->metadata != nullptr) {
        return SetInstanceMembersFromRuntimeMetadata(
                env, constructor, instanceMethodsCallbackData,
                baseInstanceMethodsCallbackData, treeNode);
    }

    return SetClassMembersFromStaticMetadata(
            env, constructor, instanceMethodsCallbackData,
            baseInstanceMethodsCallbackData, treeNode);
}

std::vector<MetadataNode::MethodCallbackData *> MetadataNode::SetClassMembersFromStaticMetadata(
        napi_env env, napi_value constructor,
        std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
        const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
        MetadataTreeNode *treeNode) {

    napi_status status;
    std::vector<MethodCallbackData *> instanceMethodData;

    uint8_t *curPtr = s_metadataReader.GetValueData() + treeNode->offsetValue + 1;

    auto nodeType = s_metadataReader.GetNodeType(treeNode);
    auto curType = s_metadataReader.ReadTypeName(treeNode);
    curPtr += sizeof(uint16_t /* baseClassId */);

    if (s_metadataReader.IsNodeTypeInterface(nodeType)) {
        curPtr += sizeof(uint8_t) + sizeof(uint32_t);
    }

    std::string lastMethodName;
    MethodCallbackData *callbackData = nullptr;

    robin_hood::unordered_map<std::string, MethodCallbackData *> collectedExtensionMethods;

    napi_value prototype = napi_util::get_prototype(env, constructor);

    // Strong reference to the prototype shared by every instance field/property
    // accessor below. When host objects are disabled the accessors use it to
    // identity-compare the receiver and short-circuit Class.prototype.<member>
    // access. The prototype lives for the class' lifetime, so this never frees.
    napi_ref prototypeRef = nullptr;
    NAPI_GUARD(napi_create_reference(env, prototype, 1, &prototypeRef)) {}

    auto objectManager = Runtime::GetObjectManager(env);
    auto extensionFunctionsCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    collectedExtensionMethods.reserve(extensionFunctionsCount);

    for (auto i = 0; i < extensionFunctionsCount; i++) {
        auto entry = MetadataReader::ReadExtensionFunctionEntry(&curPtr);

        auto &methodName = entry.getName();
        if (methodName != lastMethodName) {
            callbackData = tryGetExtensionMethodCallbackData(collectedExtensionMethods,
                                                             methodName);

            if (callbackData == nullptr) {
                callbackData = new MethodCallbackData(this);

                napi_value method;
                NAPI_GUARD(napi_create_function(env, methodName.c_str(), methodName.size(), MethodCallback,
                                     callbackData, &method)) {}

                napi_util::define_property_value(env, prototype, methodName.c_str(), method, napi_default_method);
                lastMethodName = methodName;
                collectedExtensionMethods.emplace(methodName, callbackData);

            }
        }

        callbackData->candidates.push_back(std::move(entry));
        callbackData->objectManager = objectManager;
    }

    auto instanceMethodCount = *reinterpret_cast<uint16_t *>(curPtr);
    collectedExtensionMethods.reserve(instanceMethodCount);
    curPtr += sizeof(uint16_t);

    for (auto i = 0; i < instanceMethodCount; i++) {
        auto entry = MetadataReader::ReadInstanceMethodEntry(&curPtr);
        auto &methodName = entry.getName();
        if (methodName != lastMethodName) {
            callbackData = tryGetExtensionMethodCallbackData(collectedExtensionMethods,
                                                             methodName);


            if (callbackData == nullptr) {
                callbackData = new MethodCallbackData(this);
                napi_value method;
                NAPI_GUARD(napi_create_function(env, methodName.c_str(), methodName.size(), MethodCallback,
                                     callbackData, &method)) {}
                napi_util::define_property_value(env, prototype, methodName.c_str(), method, napi_default_method);
                collectedExtensionMethods.emplace(methodName, callbackData);
            }

            instanceMethodData.push_back(callbackData);
            instanceMethodsCallbackData.push_back(callbackData);

            auto itFound = std::find_if(baseInstanceMethodsCallbackData.begin(),
                                        baseInstanceMethodsCallbackData.end(),
                                        [&methodName](MethodCallbackData *x) {
                                            return x->candidates.front().name == methodName;
                                        });
            if (itFound != baseInstanceMethodsCallbackData.end()) {
                callbackData->parent = *itFound;
            }

            lastMethodName = methodName;
        }

        callbackData->candidates.push_back(std::move(entry));
        callbackData->objectManager = objectManager;
    }
    auto instanceFieldCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < instanceFieldCount; i++) {
        auto entry = MetadataReader::ReadInstanceFieldEntry(&curPtr);
        auto &fieldName = entry.getName();
        auto fieldInfo = new FieldCallbackData(entry);
        fieldInfo->metadata.declaringType = curType;
        fieldInfo->prototype = prototypeRef;
        fieldInfo->objectManager = objectManager;
        napi_util::define_property(env, prototype, fieldName.c_str(), nullptr,
                                   FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                   fieldInfo);

        MetadataNode::GetMetadataNodeCache(env)->fieldCallbackData.push_back(fieldInfo);

    }

    auto kotlinPropertiesCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (int i = 0; i < kotlinPropertiesCount; ++i) {
        uint32_t nameOffset = *reinterpret_cast<uint32_t *>(curPtr);
        auto propertyName = s_metadataReader.ReadName(nameOffset);
        curPtr += sizeof(uint32_t);

        auto hasGetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        // Keep the full method entry (not just its name) so the accessor can call
        // CallJavaMethod directly instead of looking up + invoking the JS method.
        MetadataEntry *getterEntry = nullptr;
        std::string getterMethodName;
        if (hasGetter >= 1) {
            getterEntry = new MetadataEntry(MetadataReader::ReadInstanceMethodEntry(&curPtr));
            getterMethodName = getterEntry->getName();
        }

        auto hasSetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        MetadataEntry *setterEntry = nullptr;
        std::string setterMethodName;
        if (hasSetter >= 1) {
            setterEntry = new MetadataEntry(MetadataReader::ReadInstanceMethodEntry(&curPtr));
            setterMethodName = setterEntry->getName();
        }

        auto propertyInfo = new PropertyCallbackData(propertyName, getterMethodName,
                                                     setterMethodName);
        propertyInfo->prototype = prototypeRef;
        propertyInfo->getterEntry = getterEntry;
        propertyInfo->setterEntry = setterEntry;
        propertyInfo->node = this;
        propertyInfo->objectManager = objectManager;
        napi_util::define_property(env, prototype, propertyName.c_str(), nullptr,
                                   PropertyAccessorGetterCallback, PropertyAccessorSetterCallback,
                                   propertyInfo);
    }

    // Set static class members on constructor
    lastMethodName.clear();
    callbackData = nullptr;

    auto origin = Constants::APP_ROOT_FOLDER_PATH + this->m_name;

    // get candidates from static methods metadata
    auto staticMethodCout = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < staticMethodCout; i++) {
        auto entry = MetadataReader::ReadStaticMethodEntry(&curPtr);
        // In java there can be multiple methods of same name with different parameters.
        auto &methodName = entry.getName();
        if (methodName != lastMethodName) {
            callbackData = new MethodCallbackData(this);
            napi_value method;
            NAPI_GUARD(napi_create_function(env, methodName.c_str(), methodName.size(), MethodCallback,
                                 callbackData, &method)) {}

            napi_util::define_property_value(env, constructor, methodName.c_str(), method, napi_default_method);
            lastMethodName = methodName;
        }
        callbackData->candidates.push_back(std::move(entry));
        callbackData->objectManager = objectManager;
    }

    napi_value extendMethod;
    NAPI_GUARD(napi_create_function(env, PROP_KEY_EXTEND, sizeof(PROP_KEY_EXTEND), ExtendMethodCallback, this,
                         &extendMethod)) {}
    NAPI_GUARD(napi_set_named_property(env, constructor, PROP_KEY_EXTEND, extendMethod)) {}

    // get candidates from static fields metadata
    auto staticFieldCout = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < staticFieldCout; i++) {
        auto entry = MetadataReader::ReadStaticFieldEntry(&curPtr);
        auto &fieldName = entry.getName();
        auto fieldInfo = new FieldCallbackData(entry);
        napi_value method;
        napi_util::define_property(env, constructor, fieldName.c_str(), nullptr,
                                   FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                   fieldInfo);
        MetadataNode::GetMetadataNodeCache(env)->fieldCallbackData.push_back(fieldInfo);
        fieldInfo->objectManager = objectManager;
    }


    napi_util::define_property(env, constructor, PROP_KEY_NULLOBJECT, nullptr,
                               NullObjectAccessorGetterCallback, nullptr, this);


    std::string tname = s_metadataReader.ReadTypeName(treeNode);
    NAPI_GUARD(napi_set_named_property(env, constructor, PRIVATE_TYPE_NAME,
                            ArgConverter::convertToJsString(env, tname))) {}

    SetClassAccessor(env, constructor);

    return instanceMethodData;
}

MetadataNode::MethodCallbackData *MetadataNode::tryGetExtensionMethodCallbackData(
        const robin_hood::unordered_map<std::string, MethodCallbackData *> &collectedMethodCallbackData,
        const std::string &lookupName) {

    if (collectedMethodCallbackData.empty()) {
        return nullptr;
    }

    auto itFound = collectedMethodCallbackData.find(lookupName);
    if (itFound != collectedMethodCallbackData.end()) {
        return itFound->second;
    }

    return nullptr;
}

bool MetadataNode::IsNodeTypeInterface() {
    uint8_t nodeType = s_metadataReader.GetNodeType(m_treeNode);
    return s_metadataReader.IsNodeTypeInterface(nodeType);
}

std::vector<MetadataNode::MethodCallbackData *> MetadataNode::SetInstanceMembersFromRuntimeMetadata(
        napi_env env, napi_value constructor,
        std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
        const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
        MetadataTreeNode *treeNode) {
    assert(treeNode->metadata != nullptr);

    napi_status status;
    std::vector<MethodCallbackData *> instanceMethodData;

    std::string line;
    const std::string &metadata = *treeNode->metadata;
    std::stringstream s(metadata);

    std::string kind;
    std::string name;
    std::string signature;
    int paramCount;

    std::getline(s, line); // type line
    std::getline(s, line); // base class line

    std::string lastMethodName;
    MethodCallbackData *callbackData = nullptr;

    napi_value proto = napi_util::get_prototype(env, constructor);
    while (std::getline(s, line)) {
        std::stringstream tmp(line);
        tmp >> kind >> name >> signature >> paramCount;

        char chKind = kind[0];

        assert((chKind == 'M') || (chKind == 'F'));

        MetadataEntry entry(nullptr, NodeType::Field);

        entry.name = name;
        entry.sig = signature;
        entry.paramCount = paramCount;
        entry.isStatic = false;
        if (chKind == 'M') {
            if (entry.name != lastMethodName) {
                entry.type = NodeType::Method;
                callbackData = new MethodCallbackData(this);
                instanceMethodData.push_back(callbackData);
                instanceMethodsCallbackData.push_back(callbackData);

                auto itFound = std::find_if(baseInstanceMethodsCallbackData.begin(),
                                            baseInstanceMethodsCallbackData.end(),
                                            [&entry](MethodCallbackData *x) {
                                                return x->candidates.front().name == entry.name;
                                            });
                if (itFound != baseInstanceMethodsCallbackData.end()) {
                    callbackData->parent = *itFound;
                }

                napi_value method;
                NAPI_GUARD(napi_create_function(env, entry.name.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method)) {}
                NAPI_GUARD(napi_set_named_property(env, proto, entry.name.c_str(), method)) {}

                lastMethodName = entry.name;
            }
            callbackData->candidates.push_back(std::move(entry));
        } else if (chKind == 'F') {
            entry.type = NodeType::Field;
            auto *fieldInfo = new FieldCallbackData(entry);
            napi_util::define_property(env, proto, entry.name.c_str(), nullptr,
                                       FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                       fieldInfo);

            MetadataNode::GetMetadataNodeCache(env)->fieldCallbackData.push_back(fieldInfo);
        }
    }

    return instanceMethodData;
}

void MetadataNode::SetClassAccessor(napi_env env, napi_value constructor) {
    napi_util::define_property(env, constructor, PROP_KEY_CLASS, nullptr,
                               ClassAccessorGetterCallback, nullptr, nullptr);
}

napi_value MetadataNode::ClassAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    try {
        napi_value name;
        NAPI_GUARD(napi_get_named_property(env, jsThis, PRIVATE_TYPE_NAME, &name)) {
            return nullptr;
        }
        const char *nameValue = napi_util::get_string_value(env, name);
        return CallbackHandlers::FindClass(env, nameValue);
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::GetConstructorFunction(napi_env env) {
    std::vector<MethodCallbackData *> instanceMethodsCallbackData;
    return GetConstructorFunctionInternal(env, m_treeNode, instanceMethodsCallbackData);
}

napi_value MetadataNode::GetConstructorFunctionInternal(napi_env env, MetadataTreeNode *treeNode,
                                                        std::vector<MethodCallbackData *> instanceMethodsCallbackData) {

    napi_status status;
    auto cache = GetMetadataNodeCache(env);
    auto itFound = cache->CtorFuncCache.find(treeNode);
    if (itFound != cache->CtorFuncCache.end()) {
        if (itFound->second.constructorFunction != nullptr) {
            auto value = napi_util::get_ref_value(env, itFound->second.constructorFunction);
            if (!napi_util::is_null_or_undefined(env, value)) {
                instanceMethodsCallbackData = itFound->second.instanceMethodCallbacks;
                return value;
            }
        }
    }

    if (itFound != cache->CtorFuncCache.end()) {
#ifndef __JSC__
        for (auto data: itFound->second.instanceMethodCallbacks) {
            delete data;
        }
#endif
        itFound->second.instanceMethodCallbacks.clear();
        if (itFound->second.constructorFunction != nullptr) {
            NAPI_GUARD(napi_delete_reference(env, itFound->second.constructorFunction)) {}
        }
        cache->CtorFuncCache.erase(itFound);
    }

    auto node = GetOrCreateInternal(treeNode);

    JEnv jEnv;
    // if we already have an exception (which will be rethrown later)
    // then we don't want to ignore the next exception
    bool ignoreFindClassException = jEnv.ExceptionCheck() == JNI_FALSE;
    auto currentClass = jEnv.FindClass(node->m_name);
    if (ignoreFindClassException && jEnv.ExceptionCheck()) {
        jEnv.ExceptionClear();
        // JNI found an exception looking up this class
        // but we don't care, because this means this class doesn't exist
        // like when you try to get a class that only exists in a higher API level
        CtorCacheData ctorCacheItem(nullptr, instanceMethodsCallbackData);
        cache->CtorFuncCache.emplace(treeNode, ctorCacheItem);
        return nullptr;
    };

    auto currentNode = treeNode;
    std::string finalName(currentNode->name);
    while (currentNode->parent) {
        if (!currentNode->parent->name.empty()) {
            finalName = currentNode->parent->name + "." + finalName;
        }
        currentNode = currentNode->parent;
    }

    // 1. Create the class and get the constructor

    napi_value constructor;
    auto isInterface = s_metadataReader.IsNodeTypeInterface(treeNode->type);
    NAPI_GUARD(napi_define_class(env, finalName.c_str(), NAPI_AUTO_LENGTH,
                      isInterface ? InterfaceConstructorCallback : ClassConstructorCallback,
                      node, 0, nullptr, &constructor)) {
        return nullptr;
    }

    // Mark this constructor's prototype as a runtime object.
    ObjectManager::MarkObject(env, napi_util::get_prototype(env, constructor));

    // 2. Create the base constructor if it doesn't exist and inherit from it.
    napi_value baseConstructor;
    std::vector<MethodCallbackData *> baseInstanceMethodsCallbackData;
    auto tmpTreeNode = treeNode;
    std::vector<MetadataTreeNode *> skippedBaseTypes;

    while (true) {
        auto baseTreeNode = s_metadataReader.GetBaseClassNode(tmpTreeNode);
        if (CheckClassHierarchy(jEnv, currentClass, treeNode, baseTreeNode, skippedBaseTypes)) {
            tmpTreeNode = baseTreeNode;
            continue;
        }

        if ((baseTreeNode != treeNode) && (baseTreeNode != nullptr) &&
            (baseTreeNode->offsetValue > 0)) {
            baseConstructor = GetConstructorFunctionInternal(env, baseTreeNode,
                                                             baseInstanceMethodsCallbackData);


            if (baseConstructor != nullptr) {
                napi_util::napi_inherits(env, constructor, baseConstructor);
            }
        } else {
            baseConstructor = nullptr;
        }
        break;
    }

    // 3. Define the class members now.
    auto instanceMethodData = node->SetClassMembers(env, constructor,
                                                    instanceMethodsCallbackData,
                                                    baseInstanceMethodsCallbackData, treeNode);

    if (!skippedBaseTypes.empty()) {
        // If there is a mismatch between base type of this class in metadata compared to the class
        // at runtime, we will add methods of base class to this class's prototype.
        node->SetMissingBaseMethods(env, skippedBaseTypes, instanceMethodData, constructor);
    }


    SetInnerTypes(env, constructor, treeNode);

    napi_ref constructorRef = napi_util::make_ref(env, constructor);

    if (baseConstructor != nullptr && !napi_util::is_undefined(env, baseConstructor)) {
        napi_util::setPrototypeOf(env, constructor, baseConstructor);
    }

    CtorCacheData ctorCacheItem(constructorRef, instanceMethodsCallbackData);
    cache->CtorFuncCache.emplace(treeNode, ctorCacheItem);

    return constructor;
}

void MetadataNode::SetInnerTypes(napi_env env, napi_value constructor, MetadataTreeNode *treeNode) {
    if (treeNode->children != nullptr) {
        const auto &children = *treeNode->children;
        std::vector<std::string> childNames(children.size());

        napi_status status;
        for (auto curChild: children) {
            bool hasOwnProperty = false;
            napi_value childName;
            NAPI_GUARD(napi_create_string_utf8(env, curChild->name.c_str(), curChild->name.size(), &childName)) {}
            NAPI_GUARD(napi_has_own_property(env, constructor, childName, &hasOwnProperty)) {}
            if (!hasOwnProperty) {
                napi_util::define_property(env, constructor, curChild->name.c_str(), nullptr,
                                           InnerTypeGetterCallback, nullptr, curChild);
            }
        }
    }
}

napi_value MetadataNode::InnerTypeGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    try {
        auto curChild = reinterpret_cast<MetadataTreeNode *>(data);
        auto childNode = GetOrCreateInternal(curChild);
        // GetConstructorFunction caches per node (CtorFuncCache); inner types are
        // always class/interface, both resolved here.
        napi_value constructor = childNode->GetConstructorFunction(env);

        // Java interfaces need Symbol.hasInstance for `instanceof` support, just
        // like package-level interfaces in PackageGetterCallback.
        uint8_t childNodeType = s_metadataReader.GetNodeType(curChild);
        if (s_metadataReader.IsNodeTypeInterface(childNodeType)) {
            RegisterSymbolHasInstanceCallback(env, curChild, constructor);
        }

        // Replace this accessor on the receiver (the outer type) with the resolved
        // inner class/interface as a plain (configurable) data property, so every
        // subsequent Outer.Inner access is a direct, inline-cacheable property load
        // instead of re-invoking this getter.
        napi_property_descriptor dataProp = {
                curChild->name.c_str(), nullptr, nullptr, nullptr, nullptr,
                constructor, napi_default_jsproperty, nullptr};
        NAPI_GUARD(napi_define_properties(env, jsThis, 1, &dataProp)) {}

        return constructor;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

MetadataReader *MetadataNode::getMetadataReader() {
    return &MetadataNode::s_metadataReader;
}

napi_value MetadataNode::NullObjectAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    try {

        bool value;
        napi_value nullNodeKey;
        NAPI_GUARD(napi_create_string_utf8(env, PROP_KEY_NULL_NODE_NAME, NAPI_AUTO_LENGTH, &nullNodeKey)) {
            return nullptr;
        }
        NAPI_GUARD(napi_has_own_property(env, jsThis, nullNodeKey, &value)) {
            return nullptr;
        }

        if (!value) {
            auto node = reinterpret_cast<MetadataNode *>(data);
            napi_value external;
            NAPI_GUARD(napi_create_external(env, node, [](napi_env env, void *d1, void *d2) {}, node,
                                 &external)) {
                return nullptr;
            }
            NAPI_GUARD(napi_set_named_property(env, jsThis, PROP_KEY_NULL_NODE_NAME, external)) {}

            napi_util::napi_set_function(env,
                                         jsThis,
                                         "valueOf", MetadataNode::NullValueOfCallback);
        }

        return jsThis;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::NullValueOfCallback(napi_env env, napi_callback_info info) {
    napi_status status;
    napi_value nullValue;
    NAPI_GUARD(napi_get_null(env, &nullValue)) {
        return nullptr;
    }
    return nullValue;
}

bool MetadataNode::IsInstanceReceiver(napi_env env, napi_value jsThis, napi_ref prototypeRef) {
#ifdef USE_HOST_OBJECT
    // Real instances are host-object proxies; the class prototype is not. A
    // non-host receiver means someone touched Class.prototype.<member>.
    (void) prototypeRef;
    napi_status status;
    bool isHostObject = false;
    NAPI_GUARD(napi_is_host_object(env, jsThis, &isHostObject)) {}
    return isHostObject;
#else
    // Fallback: identity-compare the receiver against the cached prototype.
    if (prototypeRef == nullptr) return true;
    napi_value prototype = napi_util::get_ref_value(env, prototypeRef);
    napi_status status;
    bool isHolder = false;
    NAPI_GUARD(napi_strict_equals(env, jsThis, prototype, &isHolder)) {}
    return !isHolder;
#endif
}

napi_value MetadataNode::FieldAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    try {
        auto fieldData = reinterpret_cast<FieldCallbackData *>(data);
        auto &fieldMetadata = fieldData->metadata;

        if (fieldMetadata.getDeclaringType().empty()) {
            return UNDEFINED;
        }

        if (fieldData->objectManager == nullptr) {
            fieldData->objectManager = Runtime::GetRuntime(env)->GetObjectManager();
        }

        if (fieldMetadata.isStatic) {
            return CallbackHandlers::GetJavaField(env, jsThis, fieldData,
                                                  fieldData->objectManager);
        }

        // A single probe both validates the receiver and resolves the java
        // object; null + non-host means Class.prototype.<field> access.
        JniLocalRef target = fieldData->objectManager->GetJavaObjectByJsObjectFast(jsThis);
        if (target.IsNull() && !IsInstanceReceiver(env, jsThis, fieldData->prototype)) {
            return UNDEFINED;
        }
        return CallbackHandlers::GetJavaField(env, jsThis, fieldData,
                                              fieldData->objectManager, std::move(target));

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return UNDEFINED;
}
napi_ref propRef = nullptr;
napi_value MetadataNode::FieldAccessorSetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    try {
        auto fieldData = reinterpret_cast<FieldCallbackData *>(data);
        auto &fieldMetadata = fieldData->metadata;

        if (fieldData->objectManager == nullptr) {
            fieldData->objectManager = Runtime::GetRuntime(env)->GetObjectManager();
        }

        // A single probe both validates the receiver and resolves the java
        // object; null + non-host means Class.prototype.<field> access.
        JniLocalRef target;
        if (!fieldMetadata.isStatic) {
            target = fieldData->objectManager->GetJavaObjectByJsObjectFast(jsThis);
            if (target.IsNull() && !IsInstanceReceiver(env, jsThis, fieldData->prototype)) {
                return UNDEFINED;
            }
        }

        if (fieldMetadata.getIsFinal()) {
            stringstream ss;
            ss << "You are trying to set \"" << fieldMetadata.getName()
               << "\" which is a final field! Final fields can only be read.";
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        } else {
            CallbackHandlers::SetJavaField(env, jsThis, argv[0], fieldData,
                                           fieldData->objectManager, std::move(target));
            return argv[0];
        }

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return UNDEFINED;
}

napi_value MetadataNode::PropertyAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    try {
        auto propertyCallbackData = reinterpret_cast<PropertyCallbackData *>(data);

        if (propertyCallbackData->getterEntry == nullptr) {
            return nullptr;
        }

        if (!IsInstanceReceiver(env, jsThis, propertyCallbackData->prototype)) {
            return nullptr;
        }

        // Call the Java getter directly — no JS method lookup, no nested
        // MethodCallback. Invariants are resolved once and cached.
        if (propertyCallbackData->cachedIsFromInterface < 0) {
            propertyCallbackData->cachedIsFromInterface =
                    propertyCallbackData->node->IsNodeTypeInterface() ? 1 : 0;
        }
        if (propertyCallbackData->objectManager == nullptr) {
            propertyCallbackData->objectManager =
                    Runtime::GetRuntime(env)->GetObjectManager();
        }
        return CallbackHandlers::CallJavaMethod(
                env, jsThis, propertyCallbackData->node->m_name,
                propertyCallbackData->getterMethodName, propertyCallbackData->getterEntry,
                propertyCallbackData->cachedIsFromInterface == 1,
                propertyCallbackData->getterEntry->isStatic, info, 0, nullptr,
                propertyCallbackData->objectManager);

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::PropertyAccessorSetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    try {
        auto propertyCallbackData = reinterpret_cast<PropertyCallbackData *>(data);

        if (propertyCallbackData->setterEntry == nullptr) {
            return nullptr;
        }

        if (!IsInstanceReceiver(env, jsThis, propertyCallbackData->prototype)) {
            return nullptr;
        }

        // Call the Java setter directly — no JS method lookup, no nested
        // MethodCallback. Invariants are resolved once and cached.
        if (propertyCallbackData->cachedIsFromInterface < 0) {
            propertyCallbackData->cachedIsFromInterface =
                    propertyCallbackData->node->IsNodeTypeInterface() ? 1 : 0;
        }
        if (propertyCallbackData->objectManager == nullptr) {
            propertyCallbackData->objectManager =
                    Runtime::GetRuntime(env)->GetObjectManager();
        }
        return CallbackHandlers::CallJavaMethod(
                env, jsThis, propertyCallbackData->node->m_name,
                propertyCallbackData->setterMethodName, propertyCallbackData->setterEntry,
                propertyCallbackData->cachedIsFromInterface == 1,
                propertyCallbackData->setterEntry->isStatic, info, 1, &argv[0],
                propertyCallbackData->objectManager);
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::ExtendMethodCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(8)

    try {
        napi_value extendName;
        napi_value implementationObject;
        string extendLocation;

        auto hasDot = false;
        auto isTypeScriptExtend = false;

        if (argc == 2) {
            if (!napi_util::is_of_type(env, argv[0], napi_string)) {
                stringstream ss;
                ss << "Invalid extend() call. No name for extend specified at location: "
                   << extendLocation.c_str();
                string exceptionMessage = ss.str();

                throw NativeScriptException(exceptionMessage);
            }

            if (!napi_util::is_of_type(env, argv[1], napi_object)) {
                stringstream ss;
                ss << "Invalid extend() call. No implementation object specified at location: "
                   << extendLocation.c_str();
                string exceptionMessage = ss.str();

                throw NativeScriptException(exceptionMessage);
            }

            string strName = napi_util::get_string_value(env, argv[0]);
            hasDot = strName.find('.') != string::npos;
        } else if (argc == 3) {
            if (napi_util::is_of_type(env, argv[2], napi_boolean)) {
                NAPI_GUARD(napi_get_value_bool(env, argv[2], &isTypeScriptExtend)) {}
            };
        }

        auto node = reinterpret_cast<MetadataNode *>(data);

        if (hasDot) {
            extendName = argv[0];
            implementationObject = argv[1];
        } else {
            bool validExtend = GetExtendLocation(env, extendLocation, isTypeScriptExtend);
            NAPI_GUARD(napi_create_string_utf8(env, "", 0, &extendName)) {
                return nullptr;
            }
            auto validArgs = ValidateExtendArguments(env, argc, argv, validExtend,
                                                     extendLocation,
                                                     &extendName, &implementationObject,
                                                     isTypeScriptExtend);
            if (!validArgs) {
                return nullptr;
            }
        }


        string extendNameAndLocation =
                extendLocation + ArgConverter::ConvertToString(env, extendName);
        string fullClassName;
        string baseClassName = node->m_name;
        if (!hasDot) {
            fullClassName = TNS_PREFIX + CreateFullClassName(baseClassName, extendNameAndLocation);
        } else {
            fullClassName = ArgConverter::ConvertToString(env, argv[0]);
        }

        uint8_t nodeType = s_metadataReader.GetNodeType(node->m_treeNode);
        bool isInterface = s_metadataReader.IsNodeTypeInterface(nodeType);
        auto clazz = CallbackHandlers::ResolveClass(env, baseClassName, fullClassName,
                                                    implementationObject, isInterface);
        auto fullExtendedName = CallbackHandlers::ResolveClassName(env, clazz);

        auto cachedData = GetCachedExtendedClassData(env, fullExtendedName);
        if (cachedData.extendedCtorFunction != nullptr) {
            auto value = napi_util::get_ref_value(env, cachedData.extendedCtorFunction);
            if (!napi_util::is_null_or_undefined(env, value)) return value;
        }

        napi_value implementationObjectName;
        NAPI_GUARD(napi_get_named_property(env, implementationObject, CLASS_IMPLEMENTATION_OBJECT,
                                &implementationObjectName)) {
            return nullptr;
        }

        if (napi_util::is_null_or_undefined(env, implementationObjectName)) {
            NAPI_GUARD(napi_set_named_property(env, implementationObject, CLASS_IMPLEMENTATION_OBJECT,
                                    ArgConverter::convertToJsString(env, fullExtendedName))) {}
        } else {
            string usedClassName = ArgConverter::ConvertToString(env, implementationObjectName);
            stringstream s;
            s << "This object is used to extend another class '" << usedClassName << "'";
            throw NativeScriptException(s.str());
        }

        auto baseClassCtorFunction = node->GetConstructorFunction(env);

        napi_value extendFuncCtor;
        NAPI_GUARD(napi_define_class(env, fullExtendedName.c_str(), NAPI_AUTO_LENGTH,
                          MetadataNode::ExtendedClassConstructorCallback,
                          new ExtendedClassCallbackData(node, extendNameAndLocation,
                                                        napi_util::make_ref(env,
                                                                            implementationObject),
                                                        fullClassName), 0, nullptr,
                          &extendFuncCtor)) {
            return nullptr;
        }
        napi_value extendFuncPrototype = napi_util::get_prototype(env, extendFuncCtor);
        ObjectManager::MarkObject(env, extendFuncPrototype);

        napi_util::setPrototypeOf(env, implementationObject,
                                  napi_util::get_prototype(env, baseClassCtorFunction));

        napi_util::define_property(
                env, implementationObject, PROP_KEY_SUPER, nullptr, SuperAccessorGetterCallback,
                nullptr, nullptr);

        napi_util::setPrototypeOf(env, extendFuncPrototype, implementationObject);

        napi_util::setPrototypeOf(env, extendFuncCtor, baseClassCtorFunction);

        SetClassAccessor(env, extendFuncCtor);

        NAPI_GUARD(napi_set_named_property(env, extendFuncCtor, PRIVATE_TYPE_NAME,
                                ArgConverter::convertToJsString(env, fullExtendedName))) {}

        s_name2NodeCache.emplace(fullExtendedName, node);

        ExtendedClassCacheData cacheData(napi_util::make_ref(env, extendFuncCtor), fullExtendedName,
                                         node);
        auto cache = GetMetadataNodeCache(env);
        cache->ExtendedCtorFuncCache.emplace(fullExtendedName, cacheData);

        return extendFuncCtor;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}


napi_value MetadataNode::SuperAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    try {

        napi_value superValue;
        NAPI_GUARD(napi_get_named_property(env, jsThis, PROP_KEY_SUPERVALUE, &superValue)) {
            return nullptr;
        }

        if (napi_util::is_null_or_undefined(env, superValue)) {
            auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
            superValue = objectManager->GetEmptyObject();

            NAPI_GUARD(napi_delete_property(env, superValue,
                                 ArgConverter::convertToJsString(env, PROP_KEY_TOSTRING), nullptr)) {}
            NAPI_GUARD(napi_delete_property(env, superValue,
                                 ArgConverter::convertToJsString(env, PROP_KEY_VALUEOF), nullptr)) {}
            ObjectManager::MarkSuperCall(env, superValue);

            napi_value superProto = napi_util::getPrototypeOf(env, napi_util::getPrototypeOf(env,
                                                                                             napi_util::getPrototypeOf(
                                                                                                     env,
                                                                                                     jsThis)));

            napi_util::setPrototypeOf(env, superValue, superProto);
            objectManager->CloneLink(jsThis, superValue);
            auto node = GetInstanceMetadata(env, jsThis);
            SetInstanceMetadata(env, superValue, node);

            int javaObjectID = -1;
            objectManager->GetJavaObjectByJsObject(jsThis, &javaObjectID);
            if (javaObjectID != -1) {
                superValue = objectManager->GetOrCreateProxyWeak(javaObjectID, superValue);
            }
            NAPI_GUARD(napi_set_named_property(env, jsThis, PROP_KEY_SUPERVALUE, superValue)) {}
        }

        return superValue;

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value MetadataNode::MethodCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(8)

    try {
        MetadataEntry *entry = nullptr;

        auto callbackData = reinterpret_cast<MethodCallbackData *>(data);
        auto initialCallbackData = reinterpret_cast<MethodCallbackData *>(data);

        string *className;
        auto &first = callbackData->candidates.front();
        auto &methodName = first.getName();

        // Fast path for the overwhelmingly common single-overload, non-extension
        // method with no parent chain: skip the candidate-search loop entirely.
        if (callbackData->parent == nullptr &&
            callbackData->candidates.size() == 1 &&
            !first.isExtensionFunction &&
            first.getParamCount() == argc) {
            className = &callbackData->node->m_name;
            entry = &first;
        }

        while ((callbackData != nullptr) && (entry == nullptr)) {
            auto &candidates = callbackData->candidates;

            className = &callbackData->node->m_name;

            // Iterates through all methods and finds the best match based on the number of arguments
            auto found = false;
            for (auto &c: candidates) {
                found = (!c.isExtensionFunction && c.getParamCount() == argc) ||
                        (c.isExtensionFunction && c.getParamCount() == argc + 1);
                if (found) {
                    if (c.isExtensionFunction) {
                        className = &c.getDeclaringType();
                    }
                    entry = &c;
                    DEBUG_WRITE("MetaDataEntry Method %s's signature is: %s",
                                entry->getName().c_str(),
                                entry->getSig().c_str());
                    break;
                }
            }

            // Iterates through the parent class's methods to find a good match
            if (!found) {
                callbackData = callbackData->parent;
            }
        }


        if (initialCallbackData->cachedIsValueOf < 0) {
            initialCallbackData->cachedIsValueOf =
                    (methodName == PROP_KEY_VALUEOF) ? 1 : 0;
        }
        if (argc == 0 && initialCallbackData->cachedIsValueOf == 1) {
            return jsThis;
        } else {
//            Runtime::GetRuntime(env)->clearPendingError();
            if (initialCallbackData->cachedIsFromInterface < 0) {
                initialCallbackData->cachedIsFromInterface =
                        initialCallbackData->node->IsNodeTypeInterface() ? 1 : 0;
            }
            bool isFromInterface = initialCallbackData->cachedIsFromInterface == 1;
            if (initialCallbackData->objectManager == nullptr) {
                initialCallbackData->objectManager =
                        Runtime::GetRuntime(env)->GetObjectManager();
            }
            napi_value result = CallbackHandlers::CallJavaMethod(env, jsThis, *className, methodName, entry,
                                                    isFromInterface, first.isStatic, info,
                                                    argc, argv, initialCallbackData->objectManager);
//            napi_value error;
//            error = Runtime::GetRuntime(env)->getPendingError();
//            if (error) {
//                throw NativeScriptException(env, error);
//            }
            return result;
        }

    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

/**
 * Compare class hierarchy in metadata with that at runtime. If a base class is missing
 * at runtime, we must add all it's methods to the current class.
 */
bool
MetadataNode::CheckClassHierarchy(JEnv &env, jclass currentClass, MetadataTreeNode *currentTreeNode,
                                  MetadataTreeNode *baseTreeNode,
                                  std::vector<MetadataTreeNode *> &skippedBaseTypes) {
    auto shouldSkipBaseClass = false;
    if ((currentClass != nullptr) && (baseTreeNode != currentTreeNode) &&
        (baseTreeNode != nullptr) &&
        (baseTreeNode->offsetValue > 0)) {
        auto baseNode = GetOrCreateInternal(baseTreeNode);
        auto baseClass = env.FindClass(baseNode->m_name);
        if (baseClass != nullptr) {
            auto isBaseClass = env.IsAssignableFrom(currentClass, baseClass) == JNI_TRUE;
            if (!isBaseClass) {
                skippedBaseTypes.push_back(baseTreeNode);
                shouldSkipBaseClass = true;
            }
        }
    }
    return shouldSkipBaseClass;
}

void MetadataNode::SetMissingBaseMethods(
        napi_env env, const std::vector<MetadataTreeNode *> &skippedBaseTypes,
        const std::vector<MethodCallbackData *> &instanceMethodData,
        napi_value constructor) {
    napi_status status;
    for (auto treeNode: skippedBaseTypes) {
        uint8_t *curPtr = s_metadataReader.GetValueData() + treeNode->offsetValue + 1;

        auto nodeType = s_metadataReader.GetNodeType(treeNode);
        auto curType = s_metadataReader.ReadTypeName(treeNode);
        curPtr += sizeof(uint16_t /* baseClassId */);

        if (s_metadataReader.IsNodeTypeInterface(nodeType)) {
            curPtr += sizeof(uint8_t) + sizeof(uint32_t);
        }

        // Get candidates from instance methods metadata
        auto instanceMethodCount = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);
        MethodCallbackData *callbackData = nullptr;

        for (auto i = 0; i < instanceMethodCount; i++) {
            auto entry = MetadataReader::ReadInstanceMethodEntry(&curPtr);
            auto &methodName = entry.getName();
            auto isConstructor = methodName == "<init>";
            if (isConstructor) {
                continue;
            }

            for (auto data: instanceMethodData) {
                if (data->candidates.front().name == methodName) {
                    callbackData = data;
                    break;
                }
            }

            if (callbackData == nullptr) {
                callbackData = new MethodCallbackData(this);
                napi_value proto = napi_util::get_prototype(env, constructor);
                napi_value method;
                NAPI_GUARD(napi_create_function(env, methodName.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method)) {}
                NAPI_GUARD(napi_set_named_property(env, proto, methodName.c_str(), method)) {}
            }

            bool foundSameSig = false;
            for (auto &m: callbackData->candidates) {
                foundSameSig = m.getSig() == entry.getSig();
                if (foundSameSig) {
                    break;
                }
            }

            if (!foundSameSig) {
                callbackData->candidates.push_back(std::move(entry));
            }
        }
    }
}

void MetadataNode::BuildMetadata(const std::string &filesPath) {
    s_metadataReader = MetadataBuilder::BuildMetadata(filesPath);
}

void MetadataNode::onDisposeEnv(napi_env env) {
    napi_status status;
    {
        auto it = s_metadata_node_cache.Get(env);
        if (it != nullptr) {
            for (const auto &entry: it->CtorFuncCache) {
                if (entry.second.constructorFunction == nullptr) {
                    NAPI_GUARD(napi_delete_reference(env, entry.second.constructorFunction)) {}
                }
                for (const auto data: entry.second.instanceMethodCallbacks) {
                    delete data;
                }
            }
            it->CtorFuncCache.clear();

            for (const auto &entry: it->ExtendedCtorFuncCache) {
                if (entry.second.extendedCtorFunction == nullptr) {
                    NAPI_GUARD(napi_delete_reference(env, entry.second.extendedCtorFunction)) {}
                }
            }
            it->ExtendedCtorFuncCache.clear();

            for (const auto &entry: it->fieldCallbackData) {
                delete entry;
            }
        }
        s_metadata_node_cache.Remove(env);
        delete it;
    }
    {
        auto it = s_arrayObjects.find(env);
        if (it != s_arrayObjects.end()) {
            if (it->second != nullptr) {
                NAPI_GUARD(napi_delete_reference(env, it->second)) {}
            }
            s_arrayObjects.erase(it);
        }
    }
}


string MetadataNode::TNS_PREFIX = "com/tns/gen/";
MetadataReader MetadataNode::s_metadataReader;
robin_hood::unordered_map<std::string, MetadataNode *> MetadataNode::s_name2NodeCache;
robin_hood::unordered_map<std::string, MetadataTreeNode *> MetadataNode::s_name2TreeNodeCache;
robin_hood::unordered_map<MetadataTreeNode *, MetadataNode *> MetadataNode::s_treeNode2NodeCache;
tns::ConcurrentMap<napi_env, MetadataNode::MetadataNodeCache *> MetadataNode::s_metadata_node_cache;
robin_hood::unordered_map<napi_env, napi_ref> MetadataNode::s_arrayObjects;

bool MetadataNode::s_profilerEnabled = false;
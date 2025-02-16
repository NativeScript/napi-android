//
// Created by Ammar Ahmed on 20/09/2024.
//
#include <cassert>
#include <chrono>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <dlfcn.h>
#include "JEnv.h"
#include "CallbackHandlers.h"
#include "Util.h"
#include "JniLocalRef.h"
#include "MetadataNode.h"
#include "MethodCache.h"
#include "ArgConverter.h"
#include "JsArgConverter.h"
#include "GlobalHelpers.h"
#include <regex>

#ifdef USE_MIMALLOC

#include "mimalloc.h"

#endif

using namespace std;
using namespace tns;

void CallbackHandlers::Init(napi_env env) {
    JEnv jEnv;

    JAVA_LANG_STRING = jEnv.FindClass("java/lang/String");
    assert(JAVA_LANG_STRING != nullptr);

    RUNTIME_CLASS = jEnv.FindClass("com/tns/Runtime");
    assert(RUNTIME_CLASS != nullptr);

    RESOLVE_CLASS_METHOD_ID = jEnv.GetMethodID(RUNTIME_CLASS, "resolveClass",
                                               "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;Z)Ljava/lang/Class;");
    assert(RESOLVE_CLASS_METHOD_ID != nullptr);

    CURRENT_OBJECTID_FIELD_ID = jEnv.GetFieldID(RUNTIME_CLASS, "currentObjectId", "I");
    assert(CURRENT_OBJECTID_FIELD_ID != nullptr);

    MAKE_INSTANCE_STRONG_ID = jEnv.GetMethodID(RUNTIME_CLASS, "makeInstanceStrong",
                                               "(Ljava/lang/Object;I)V");
    assert(MAKE_INSTANCE_STRONG_ID != nullptr);

    GET_TYPE_METADATA = jEnv.GetStaticMethodID(RUNTIME_CLASS, "getTypeMetadata",
                                               "(Ljava/lang/String;I)[Ljava/lang/String;");
    assert(GET_TYPE_METADATA != nullptr);

    ENABLE_VERBOSE_LOGGING_METHOD_ID = jEnv.GetMethodID(RUNTIME_CLASS, "enableVerboseLogging",
                                                        "()V");
    assert(ENABLE_VERBOSE_LOGGING_METHOD_ID != nullptr);

    DISABLE_VERBOSE_LOGGING_METHOD_ID = jEnv.GetMethodID(RUNTIME_CLASS, "disableVerboseLogging",
                                                         "()V");
    assert(ENABLE_VERBOSE_LOGGING_METHOD_ID != nullptr);

    INIT_WORKER_METHOD_ID = jEnv.GetStaticMethodID(RUNTIME_CLASS, "initWorker",
                                                   "(Ljava/lang/String;Ljava/lang/String;I)V");

    assert(INIT_WORKER_METHOD_ID != nullptr);

    SEND_MESSAGE_TO_WORKER_METHOD_ID = jEnv.GetStaticMethodID(RUNTIME_CLASS,
                                                              "sendMessageFromMainToWorker",
                                                              "(ILjava/lang/String;)V");
    assert(SEND_MESSAGE_TO_WORKER_METHOD_ID != nullptr);

    SEND_MESSAGE_TO_MAIN_METHOD_ID = jEnv.GetStaticMethodID(RUNTIME_CLASS,
                                                            "sendMessageFromWorkerToMain",
                                                            "(Ljava/lang/String;)V");
    assert(SEND_MESSAGE_TO_MAIN_METHOD_ID != nullptr);

    TERMINATE_WORKER_METHOD_ID = jEnv.GetStaticMethodID(RUNTIME_CLASS, "workerObjectTerminate",
                                                        "(I)V");
    assert(TERMINATE_WORKER_METHOD_ID != nullptr);

    WORKER_SCOPE_CLOSE_METHOD_ID = jEnv.GetStaticMethodID(RUNTIME_CLASS, "workerScopeClose", "()V");
    assert(WORKER_SCOPE_CLOSE_METHOD_ID != nullptr);

    MetadataNode::Init(env);

    MethodCache::Init();
}

napi_value CallbackHandlers::CallJavaMethod(napi_env env, napi_value caller, const string &className,
                                 const string &methodName, MetadataEntry *entry,
                                 bool isFromInterface, bool isStatic, napi_callback_info info, size_t argc, napi_value* argv) {

    JEnv jEnv;
    jclass clazz;
    jmethodID mid;
    string *sig = nullptr;
    string *returnType = nullptr;
    auto retType = MethodReturnType::Unknown;
    MethodCache::CacheMethodInfo mi;
    bool isSuper = false;

    if ((entry != nullptr) && entry->getIsResolved()) {
        auto &entrySignature = entry->getSig();
        isStatic = entry->isStatic;

        if (entry->memberId == nullptr) {
            clazz = jEnv.FindClass(className);

            if (clazz == nullptr) {
                MetadataNode *callerNode = MetadataNode::GetNodeFromHandle(env, caller);
                const string callerClassName = callerNode->GetName();

                DEBUG_WRITE("Cannot resolve class: %s while calling method: %s callerClassName: %s",
                            className.c_str(), methodName.c_str(), callerClassName.c_str());
                clazz = jEnv.FindClass(callerClassName);
                if (clazz == nullptr) {
                    //todo: plamen5kov: throw exception here
                    DEBUG_WRITE("Cannot resolve caller's class name: %s", callerClassName.c_str());
                    return nullptr;
                }

                if (isStatic) {
                    if (isFromInterface) {
                        auto methodAndClassPair = jEnv.GetInterfaceStaticMethodIDAndJClass(
                                className,
                                methodName,
                                entrySignature);
                        entry->memberId = methodAndClassPair.first;
                        clazz = methodAndClassPair.second;
                    } else {
                        entry->memberId = jEnv.GetStaticMethodID(clazz, methodName, entrySignature);
                    }
                } else {
                    entry->memberId = jEnv.GetMethodID(clazz, methodName, entrySignature);
                }

                if (entry->memberId == nullptr) {
                    //todo: plamen5kov: throw exception here
                    DEBUG_WRITE("Cannot resolve a method %s on caller class: %s",
                                methodName.c_str(), callerClassName.c_str());
                    return nullptr;
                }
            } else {
                if (isStatic) {
                    if (isFromInterface) {
                        auto methodAndClassPair = jEnv.GetInterfaceStaticMethodIDAndJClass(
                                className,
                                methodName, entrySignature);
                        entry->memberId = methodAndClassPair.first;
                        clazz = methodAndClassPair.second;
                    } else {
                        entry->memberId = jEnv.GetStaticMethodID(clazz, methodName, entrySignature);
                    }
                } else {
                    entry->memberId = jEnv.GetMethodID(clazz, methodName, entrySignature);
                }

                if (entry->memberId == nullptr) {
                    //todo: plamen5kov: throw exception here
                    DEBUG_WRITE("Cannot resolve a method %s on class: %s", methodName.c_str(),
                                className.c_str());
                    return nullptr;
                }
            }
            entry->clazz = clazz;
        }

        mid = reinterpret_cast<jmethodID>(entry->memberId);
        clazz = entry->clazz;
        sig = &entry->getSig();
        returnType = &entry->getReturnType();
        retType = entry->getRetType();
    } else {
        DEBUG_WRITE("Resolving method: %s on className %s", methodName.c_str(), className.c_str());

        clazz = jEnv.FindClass(className);
        if (clazz != nullptr) {
            mi = MethodCache::ResolveMethodSignature(env, className, methodName, argc, argv, isStatic);
            if (mi.mid == nullptr) {
                DEBUG_WRITE("Cannot resolve class=%s, method=%s, isStatic=%d, isSuper=%d",
                            className.c_str(), methodName.c_str(), isStatic, isSuper);
                return nullptr;
            }
        } else {
            MetadataNode *callerNode = MetadataNode::GetNodeFromHandle(env, caller);
            const string callerClassName = callerNode->GetName();
            DEBUG_WRITE("Resolving method on caller class: %s.%s on className %s",
                        callerClassName.c_str(), methodName.c_str(), className.c_str());
            mi = MethodCache::ResolveMethodSignature(env, callerClassName, methodName, argc, argv,
                                                     isStatic);
            if (mi.mid == nullptr) {
                DEBUG_WRITE(
                        "Cannot resolve class=%s, method=%s, isStatic=%d, isSuper=%d, callerClass=%s",
                        className.c_str(), methodName.c_str(), isStatic, isSuper,
                        callerClassName.c_str());
                return nullptr;
            }
        }

        clazz = mi.clazz;
        mid = mi.mid;
        sig = &mi.signature;
        returnType = &mi.returnType;
        retType = mi.retType;
    }

    if (!isStatic) {
        DEBUG_WRITE("CallJavaMethod on instance %s", methodName.c_str());
    } else {
        DEBUG_WRITE("CallJavaMethod on class %s", methodName.c_str());
    }

    JsArgConverter argConverter = (entry != nullptr && entry->isExtensionFunction)
                                  ? JsArgConverter(env, caller, argv, argc, *sig, entry)
                                  : JsArgConverter(env, argv, argc, false, *sig, entry);


    if (!argConverter.IsValid()) {
        JsArgConverter::Error err = argConverter.GetError();
        throw NativeScriptException(err.msg);
    }

    JniLocalRef callerJavaObject;

    jvalue *javaArgs = argConverter.ToArgs();

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    if (!isStatic) {
        int objectId = -1;

        callerJavaObject = objectManager->GetJavaObjectByJsObject(caller, &objectId);
        isSuper = objectManager->GetIsSuper(objectId, caller);

        if (callerJavaObject.IsNull()) {
            stringstream ss;

            napi_value new_target;
            napi_get_new_target(env, info, &new_target);
            if (!napi_util::is_null_or_undefined(env, new_target)) {
                ss << "No java object found on which to call \"" << methodName
                   << "\" method. It is possible your Javascript object is not linked with the corresponding Java class. Try passing context(this) to the constructor function.";
            } else {
                ss << "Failed calling " << methodName << " on a " << className
                   << " instance. The JavaScript instance no longer has available Java instance counterpart.";
            }
            throw NativeScriptException(ss.str());
        }
    }

    napi_value returnValue;

    switch (retType) {
        case MethodReturnType::Void: {
            if (isStatic) {
                jEnv.CallStaticVoidMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                jEnv.CallNonvirtualVoidMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                jEnv.CallVoidMethodA(callerJavaObject, mid, javaArgs);
            }
            returnValue = nullptr;
            break;
        }
        case MethodReturnType::Boolean: {
            jboolean result;
            if (isStatic) {
                result = jEnv.CallStaticBooleanMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualBooleanMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallBooleanMethodA(callerJavaObject, mid, javaArgs);
            }

            napi_get_boolean(env, result != 0, &returnValue);
            break;
        }
        case MethodReturnType::Byte: {
            jbyte result;
            if (isStatic) {
                result = jEnv.CallStaticByteMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualByteMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallByteMethodA(callerJavaObject, mid, javaArgs);
            }

            napi_create_int32(env, result, &returnValue);
            break;
        }
        case MethodReturnType::Char: {
            jchar result;
            if (isStatic) {
                result = jEnv.CallStaticCharMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualCharMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallCharMethodA(callerJavaObject, mid, javaArgs);
            }

            JniLocalRef str(jEnv.NewString(&result, 1));
            jboolean bol = true;
            const char *resP = jEnv.GetStringUTFChars(str, &bol);
            returnValue = ArgConverter::convertToJsString(env, resP, 1);
            jEnv.ReleaseStringUTFChars(str, resP);
            break;
        }
        case MethodReturnType::Short: {
            jshort result;
            if (isStatic) {
                result = jEnv.CallStaticShortMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualShortMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallShortMethodA(callerJavaObject, mid, javaArgs);
            }

            napi_create_int32(env, result, &returnValue);

            break;
        }
        case MethodReturnType::Int: {
            jint result;
            if (isStatic) {
                result = jEnv.CallStaticIntMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualIntMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallIntMethodA(callerJavaObject, mid, javaArgs);
            }
            napi_create_int32(env, result, &returnValue);
            break;

        }
        case MethodReturnType::Long: {
            jlong result;
            if (isStatic) {
                result = jEnv.CallStaticLongMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualLongMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallLongMethodA(callerJavaObject, mid, javaArgs);
            }
            returnValue = ArgConverter::ConvertFromJavaLong(env, result);
            break;
        }
        case MethodReturnType::Float: {
            jfloat result;
            if (isStatic) {
                result = jEnv.CallStaticFloatMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualFloatMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallFloatMethodA(callerJavaObject, mid, javaArgs);
            }
            napi_create_double(env, (double) result, &returnValue);
            break;
        }
        case MethodReturnType::Double: {
            jdouble result;
            if (isStatic) {
                result = jEnv.CallStaticDoubleMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualDoubleMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallDoubleMethodA(callerJavaObject, mid, javaArgs);
            }
            napi_create_double(env, (double) result, &returnValue);
            break;
        }
        case MethodReturnType::String: {
            jobject result = nullptr;
            bool exceptionOccurred;

            if (isStatic) {
                result = jEnv.CallStaticObjectMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualObjectMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallObjectMethodA(callerJavaObject, mid, javaArgs);
            }

            if (result != nullptr) {
                returnValue = ArgConverter::jstringToJsString(env, static_cast<jstring>(result));
                jEnv.DeleteLocalRef(result);
            } else {
                napi_get_null(env, &returnValue);
            }

            break;
        }
        case MethodReturnType::Object: {
            jobject result = nullptr;
            bool exceptionOccurred;

            if (isStatic) {
                result = jEnv.CallStaticObjectMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = jEnv.CallNonvirtualObjectMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = jEnv.CallObjectMethodA(callerJavaObject, mid, javaArgs);
            }

            if (result != nullptr) {
                auto isString = jEnv.IsInstanceOf(result, JAVA_LANG_STRING);

                if (isString) {
                    returnValue = ArgConverter::jstringToJsString(env, (jstring) result);
                } else {
                    jint javaObjectID = objectManager->GetOrCreateObjectId(result);
                    returnValue = objectManager->GetJsObjectByJavaObject(javaObjectID);

                    if (napi_util::is_null_or_undefined(env, returnValue)) {
                        returnValue = objectManager->CreateJSWrapper(javaObjectID, *returnType,
                                                                     result);
                    }
                }

                jEnv.DeleteLocalRef(result);
            } else {
                napi_get_null(env, &returnValue);
            }

            break;
        }
        default: {
            returnValue = napi_util::undefined(env);
            assert(false);
            break;
        }
    }


    return returnValue;
}


bool CallbackHandlers::RegisterInstance(napi_env env, napi_value jsObject,
                                        const std::string &fullClassName,
                                        const ArgsWrapper &argWrapper,
                                        napi_value implementationObject,
                                        bool isInterface,
                                        napi_value *jsThisProxy,
                                        const std::string &baseClassName) {
    bool success;

    DEBUG_WRITE("RegisterInstance called for '%s'", fullClassName.c_str());

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    JEnv jEnv;

    jclass generatedJavaClass = ResolveClass(env, baseClassName, fullClassName,
                                             implementationObject,
                                             isInterface);

    int javaObjectID = objectManager->GenerateNewObjectID();

    objectManager->Link(jsObject, javaObjectID, nullptr);

    // resolve constructor
    auto mi = MethodCache::ResolveConstructorSignature(env, argWrapper, fullClassName,
                                                       generatedJavaClass, isInterface);

    // while the "instance" is being created, if an exception is thrown during the construction
    // this scope will guarantee the "javaObjectID" will be set to -1 and won't have an invalid value
    jobject instance;
    {
        JavaObjectIdScope objIdScope(jEnv, CURRENT_OBJECTID_FIELD_ID, runtime->GetJavaRuntime(),
                                     javaObjectID);

        if (argWrapper.type == ArgType::Interface) {
            instance = jEnv.NewObject(generatedJavaClass, mi.mid);
        } else {
            // resolve arguments before passing them on to the constructor
            //            JSToJavaConverter argConverter(isolate, argWrapper.args, mi.signature);



            JsArgConverter argConverter(env, argWrapper.argv, argWrapper.argc, mi.signature);
            auto ctorArgs = argConverter.ToArgs();

            instance = jEnv.NewObjectA(generatedJavaClass, mi.mid, ctorArgs);
        }
    }

    // Set runtimeId field on interface and extended classes
    if (runtime->GetId() != 0 && (isInterface || implementationObject != nullptr)) {
        jfieldID runtimeIdField;
        auto itFound = jclass_to_runtimeId_cache.find(generatedJavaClass);
        if (itFound != jclass_to_runtimeId_cache.end()) {
            runtimeIdField = itFound->second;
        } else {
            runtimeIdField = jEnv.GetFieldID(generatedJavaClass, "runtimeId", "I");
            jclass_to_runtimeId_cache.emplace(generatedJavaClass, runtimeIdField);
        }
        if (runtimeIdField != nullptr) {
            jint runtimeId = runtime->GetId(); // Assuming GetId() returns the current runtime's id
            DEBUG_WRITE("Setting runtimeId %d on instance of %s", runtimeId, fullClassName.c_str());
            jEnv.SetIntField(instance, runtimeIdField, runtimeId);
        }
    }

    jEnv.CallVoidMethod(runtime->GetJavaRuntime(), MAKE_INSTANCE_STRONG_ID, instance, javaObjectID);

    AdjustAmountOfExternalAllocatedMemory(env);

    JniLocalRef localInstance(instance);
    success = !localInstance.IsNull();

    if (success) {
        jclass instanceClass = jEnv.FindClass(fullClassName);
        objectManager->SetJavaClass(jsObject, instanceClass);
        *jsThisProxy = objectManager->GetOrCreateProxy(javaObjectID, jsObject);
    } else {
        DEBUG_WRITE_FORCE("RegisterInstance failed with null new instance class: %s",
                          fullClassName.c_str());
    }

    return success;
}

jclass CallbackHandlers::ResolveClass(napi_env env, const string &baseClassName,
                                      const string &fullClassName,
                                      napi_value implementationObject, bool isInterface) {
    JEnv jEnv;
    jclass globalRefToGeneratedClass = jEnv.CheckForClassInCache(fullClassName);

    if (globalRefToGeneratedClass == nullptr) {

        // get needed arguments in order to load binding
        JniLocalRef javaBaseClassName(jEnv.NewStringUTF(baseClassName.c_str()));
        JniLocalRef javaFullClassName(jEnv.NewStringUTF(fullClassName.c_str()));

        jobjectArray methodOverrides = GetMethodOverrides(env, jEnv, implementationObject);

        jobjectArray implementedInterfaces = GetImplementedInterfaces(env, jEnv,
                                                                      implementationObject);

        auto runtime = Runtime::GetRuntime(env);

        // create or load generated binding (java class)
        jclass generatedClass = (jclass) jEnv.CallObjectMethod(runtime->GetJavaRuntime(),
                                                               RESOLVE_CLASS_METHOD_ID,
                                                               (jstring) javaBaseClassName,
                                                               (jstring) javaFullClassName,
                                                               methodOverrides,
                                                               implementedInterfaces,
                                                               isInterface);

        globalRefToGeneratedClass = jEnv.InsertClassIntoCache(fullClassName, generatedClass);

        jEnv.DeleteGlobalRef(methodOverrides);
        jEnv.DeleteGlobalRef(implementedInterfaces);
    }

    return globalRefToGeneratedClass;
}

// Called by ExtendMethodCallback when extending a class
string CallbackHandlers::ResolveClassName(napi_env env, jclass &clazz) {
    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();
    auto className = objectManager->GetClassName(clazz);
    return className;
}

napi_value CallbackHandlers::GetArrayElement(napi_env env, napi_value array,
                                             uint32_t index, const string &arraySignature) {
    return arrayElementAccessor.GetArrayElement(env, array, index, arraySignature);
}

void CallbackHandlers::SetArrayElement(napi_env env, napi_value array,
                                       uint32_t index,
                                       const string &arraySignature, napi_value value) {

    arrayElementAccessor.SetArrayElement(env, array, index, arraySignature, value);
}

napi_value CallbackHandlers::GetJavaField(napi_env env, napi_value caller,
                                          FieldCallbackData *fieldData) {
    return fieldAccessor.GetJavaField(env, caller, fieldData);
}

void CallbackHandlers::SetJavaField(napi_env env, napi_value target,
                                    napi_value value, FieldCallbackData *fieldData) {
    fieldAccessor.SetJavaField(env, target, value, fieldData);
}

void CallbackHandlers::AdjustAmountOfExternalAllocatedMemory(napi_env env) {
    auto runtime = Runtime::GetRuntime(env);
     runtime->AdjustAmountOfExternalAllocatedMemory();
     runtime->TryCallGC();
}

napi_value CallbackHandlers::CreateJSWrapper(napi_env env, jint javaObjectID,
                                             const string &typeName) {
    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    return objectManager->CreateJSWrapper(javaObjectID, typeName);
}

jobjectArray
CallbackHandlers::GetImplementedInterfaces(napi_env env, JEnv &jEnv,
                                           napi_value implementationObject) {
    if (implementationObject == nullptr || napi_util::is_undefined(env, implementationObject)) {
        return CallbackHandlers::GetJavaStringArray(jEnv, 0);
    }

    vector<jstring> interfacesToImplement;

    napi_value prop;
    napi_get_named_property(env, implementationObject, "interfaces", &prop);
    bool isArray;
    napi_is_array(env, prop, &isArray);

    if (isArray) {
        uint32_t length;
        napi_get_array_length(env, prop, &length);

        for (int j = 0; j < length; j++) {
            napi_value element;
            napi_get_element(env, prop, j, &element);

            if (napi_util::is_object(env, element)) {
                auto node = MetadataNode::GetTypeMetadataName(env, element);

                node = Util::ReplaceAll(node, std::string("/"), std::string("."));

                jstring value = jEnv.NewStringUTF(node.c_str());
                interfacesToImplement.push_back(value);
            }
        }
    }

    int interfacesCount = interfacesToImplement.size();

    jobjectArray implementedInterfaces = CallbackHandlers::GetJavaStringArray(jEnv,
                                                                              interfacesCount);
    for (int i = 0; i < interfacesCount; i++) {
        jEnv.SetObjectArrayElement(implementedInterfaces, i, interfacesToImplement[i]);
    }

    for (int i = 0; i < interfacesCount; i++) {
        jEnv.DeleteLocalRef(interfacesToImplement[i]);
    }

    return implementedInterfaces;
}

jobjectArray
CallbackHandlers::GetMethodOverrides(napi_env env, JEnv &jEnv, napi_value implementationObject) {
    if (implementationObject == nullptr || napi_util::is_undefined(env, implementationObject)) {
        return CallbackHandlers::GetJavaStringArray(jEnv, 0);
    }

    vector<jstring> methodNames;

    napi_value propNames;

    napi_get_all_property_names(env, implementationObject, napi_key_own_only,
                                napi_key_all_properties, napi_key_numbers_to_strings, &propNames);

    uint32_t length;
    napi_get_array_length(env, propNames, &length);

    for (int i = 0; i < length; i++) {
        napi_value element;
        napi_get_element(env, propNames, i, &element);
        auto name = ArgConverter::ConvertToString(env, element);

        if (name == "super") {
            continue;
        }

        napi_value method;

        napi_get_property(env, implementationObject, element, &method);

        bool methodFound = napi_util::is_of_type(env, method, napi_function);

        if (methodFound) {
            jstring value = jEnv.NewStringUTF(name.c_str());
            methodNames.push_back(value);
        }
    }

    int methodCount = methodNames.size();

    jobjectArray methodOverrides = CallbackHandlers::GetJavaStringArray(jEnv, methodCount);
    for (int i = 0; i < methodCount; i++) {
        jEnv.SetObjectArrayElement(methodOverrides, i, methodNames[i]);
    }

    for (int i = 0; i < methodCount; i++) {
        jEnv.DeleteLocalRef(methodNames[i]);
    }

    return methodOverrides;
}

napi_value CallbackHandlers::RunOnMainThreadCallback(napi_env env, napi_callback_info info) {

    NapiScope scope(env);

    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    assert(argc == 1);
    assert(napi_util::is_of_type(env, args[0], napi_function));

    uint64_t key = ++count_;
    bool inserted;

    std::tie(std::ignore, inserted) = cache_.try_emplace(key, env, args[0]);
    assert(inserted && "Main thread callback ID should not be duplicated");

    auto value = Callback(key);
    auto size = sizeof(Callback);
    auto wrote = write(Runtime::GetWriter(), &value, size);



    return nullptr;
}

int CallbackHandlers::RunOnMainThreadFdCallback(int fd, int events, void *data) {
    struct Callback value;
    auto size = sizeof(Callback);
    ssize_t nr = read(fd, &value, sizeof(value));

    auto key = value.id_;

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return 1;
    }

    napi_env env = it->second.env_;
    napi_ref callback_ref = it->second.callback_;

    NapiScope scope(env);

    napi_value cb = napi_util::get_ref_value(env, callback_ref);

    napi_value global;
    napi_get_global(env, &global);

    cache_.erase(it);

    napi_value result;
    napi_status status = napi_call_function(env, global, cb, 0, nullptr, &result);

    if (status != napi_ok) {
        napi_throw_error(env, nullptr, "Error calling JavaScript callback");
    }


    return 1;
}

napi_value CallbackHandlers::LogMethodCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    try {
        if (argc > 0) {
            napi_valuetype valuetype;
            napi_typeof(env, args[0], &valuetype);
            if (valuetype == napi_string) {
                size_t str_size;
                napi_get_value_string_utf8(env, args[0], nullptr, 0, &str_size);
                std::string message(str_size + 1, '\0');
                napi_get_value_string_utf8(env, args[0], &message[0], str_size + 1, &str_size);
                DEBUG_WRITE("%s", message.c_str());
            }
        }
    }
    catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    }
    catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value CallbackHandlers::DrainMicrotaskCallback(napi_env env, napi_callback_info info) {
    js_execute_pending_jobs(env);
    return nullptr;
}

napi_value CallbackHandlers::TimeCallback(napi_env env, napi_callback_info info) {
    auto nano = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
    double duration = nano.time_since_epoch().count();
    napi_value result;
    napi_create_double(env, duration, &result);
    return result;
}

napi_value
CallbackHandlers::ReleaseNativeCounterpartCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS();

    if (argc != 1) {
        napi_throw_error(env, "0", "Unexpected arguments count!");
        return napi_util::undefined(env);
    }

    if (!napi_util::is_of_type(env, argv[0], napi_object)) {
        napi_throw_error(env, "0", "Argument is not an object!");
        return napi_util::undefined(env);
    }


    Runtime::GetRuntime(env)->GetObjectManager()->ReleaseNativeObject(env, argv[0]);
    return napi_util::undefined(env);
}

void CallbackHandlers::validateProvidedArgumentsLength(napi_env env, napi_callback_info info,
                                                       int expectedSize) {
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr);
    if ((int) argc != expectedSize) {
        throw NativeScriptException("Unexpected arguments count!");
    }
}

napi_value
CallbackHandlers::DumpReferenceTablesMethodCallback(napi_env env, napi_callback_info info) {
    DumpReferenceTablesMethod();
    return nullptr;
}

void CallbackHandlers::DumpReferenceTablesMethod() {
    try {
        JEnv jEnv;
        jclass vmDbgClass = jEnv.FindClass("dalvik/system/VMDebug");
        if (vmDbgClass != nullptr) {
            jmethodID mid = jEnv.GetStaticMethodID(vmDbgClass, "dumpReferenceTables", "()V");
            if (mid != 0) {
                jEnv.CallStaticVoidMethod(vmDbgClass, mid);
            }
        }
    }
    catch (NativeScriptException &e) {
        // e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        // nsEx.ReThrowToV8();
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        // nsEx.ReThrowToV8();
    }
}

napi_value
CallbackHandlers::EnableVerboseLoggingMethodCallback(napi_env env, napi_callback_info info) {
    try {
        tns::LogEnabled = true;
        JEnv jEnv;
        jEnv.CallVoidMethod(Runtime::GetRuntime(env)->GetJavaRuntime(),
                            ENABLE_VERBOSE_LOGGING_METHOD_ID);
    }
    catch (NativeScriptException &e) {
        // e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        // nsEx.ReThrowToV8();
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        // nsEx.ReThrowToV8();
    }
    return nullptr;
}

napi_value
CallbackHandlers::DisableVerboseLoggingMethodCallback(napi_env env, napi_callback_info info) {
    try {
        tns::LogEnabled = false;
        JEnv jEnv;
        jEnv.CallVoidMethod(Runtime::GetRuntime(env)->GetJavaRuntime(),
                            DISABLE_VERBOSE_LOGGING_METHOD_ID);
    }
    catch (NativeScriptException &e) {
        // e.ReThrowToNapi(env);
    }
    catch (std::exception e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        // nsEx.ReThrowToV8();
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        // nsEx.ReThrowToV8();
    }
    return nullptr;
}

napi_value CallbackHandlers::ExitMethodCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);
    auto msg = ArgConverter::ConvertToString(env, argv[0]);
    DEBUG_WRITE_FATAL("FORCE EXIT: %s", msg.c_str());
    exit(-1);
    return nullptr;
}

void CallbackHandlers::CreateGlobalCastFunctions(napi_env env) {
    napi_value global;
    napi_get_global(env, &global);
    castFunctions.CreateGlobalCastFunctions(env, global);
}

vector<string> CallbackHandlers::GetTypeMetadata(const string &name, int index) {
    JEnv env;

    string canonicalName = Util::ConvertFromJniToCanonicalName(name);

    JniLocalRef className(env.NewStringUTF(canonicalName.c_str()));
    jint idx = index;

    JniLocalRef pubApi(
            env.CallStaticObjectMethod(RUNTIME_CLASS, GET_TYPE_METADATA, (jstring) className, idx));

    jsize length = env.GetArrayLength(pubApi);

    assert(length > 0);

    vector<string> result;

    for (jsize i = 0; i < length; i++) {
        JniLocalRef s(env.GetObjectArrayElement(pubApi, i));
        const char *pc = env.GetStringUTFChars(s, nullptr);
        result.push_back(string(pc));
        env.ReleaseStringUTFChars(s, pc);
    }

    return result;
}

napi_value CallbackHandlers::CallJSMethod(napi_env env, JNIEnv *_jEnv,
                                          napi_value jsObject, jclass claz,const string &methodName,int javaObjectId,
                                          jobjectArray args) {
    JEnv jEnv(_jEnv);
    napi_value result;
    napi_value method;
    auto runtime = Runtime::GetRuntime(env);

    method = runtime->js_method_cache->getCachedMethod(javaObjectId,methodName);
    if (!method) {
        napi_get_named_property(env, jsObject, methodName.c_str(), &method);
        runtime->js_method_cache->cacheMethod(javaObjectId, methodName, method);
    }

    if (method == nullptr || !napi_util::is_of_type(env, method, napi_function)) {
        stringstream ss;
        ss << "Cannot find method '" << methodName << "' implementation";
        throw NativeScriptException(ss.str());
    } else {
        DEBUG_WRITE("Calling JS Method %s", methodName.c_str());

        bool exceptionPending;
        napi_is_exception_pending(env, &exceptionPending);

        int argc = jEnv.GetArrayLength(args) / 3;
        if (argc > 0) {
            napi_value* jsArgs = nullptr;
            napi_value stack_args[8];
            if (argc <= 8) {
                jsArgs = stack_args;
            } else {
#ifdef USE_MIMALLOC
                jsArgs = (napi_value *) mi_malloc(sizeof(napi_value) * argc);
#else
                jsArgs = (napi_value *) malloc(sizeof(napi_value) * argc);
#endif
            }
            ArgConverter::ConvertJavaArgsToJsArgs(env, args, argc, jsArgs);
            napi_call_function(env, jsObject, method, argc, jsArgs, &result);

            if (argc > 8) {
#ifdef USE_MIMALLOC
                mi_free(jsArgs);
#else
                free(jsArgs);
#endif
            }
        } else {
            napi_call_function(env, jsObject, method, 0, nullptr, &result);
        }

        if (!exceptionPending) {
            napi_is_exception_pending(env, &exceptionPending);
            if (exceptionPending) {
                napi_value error;
                napi_get_and_clear_last_exception(env, &error);
                throw NativeScriptException(env, error, "Error calling js method: " + methodName);
            }
        }
    }


    return result;
}

napi_value CallbackHandlers::FindClass(napi_env env, const char *name) {
    napi_value clazz = nullptr;
    JEnv jEnv;
    jclass javaClass = jEnv.FindClass(name);
    if (jEnv.ExceptionCheck() == JNI_FALSE) {
        auto runtime = Runtime::GetRuntime(env);
        auto objectManager = runtime->GetObjectManager();

        jint javaObjectID = objectManager->GetOrCreateObjectId(javaClass);
        clazz = objectManager->GetJsObjectByJavaObject(javaObjectID);

        if (clazz == nullptr) {
            clazz = objectManager->CreateJSWrapper(javaObjectID, "Ljava/lang/Class;", javaClass);
        }
    }
    return clazz;
}

int CallbackHandlers::GetArrayLength(napi_env env, napi_value arr) {
    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    JEnv jEnv;

    auto javaArr = objectManager->GetJavaObjectByJsObjectFast(arr);

    auto length = jEnv.GetArrayLength(javaArr);

    return length;
}

jobjectArray CallbackHandlers::GetJavaStringArray(JEnv &jEnv, int length) {
    if (length > CallbackHandlers::MAX_JAVA_STRING_ARRAY_LENGTH) {
        stringstream ss;
        ss << "You are trying to override more methods than the limit of "
           << CallbackHandlers::MAX_JAVA_STRING_ARRAY_LENGTH;
        throw NativeScriptException(ss.str());
    }

    JniLocalRef tmpArr(jEnv.NewObjectArray(length, JAVA_LANG_STRING, nullptr));
    return (jobjectArray) jEnv.NewGlobalRef(tmpArr);
}

CallbackHandlers::func_AChoreographer_getInstance AChoreographer_getInstance_;

CallbackHandlers::func_AChoreographer_postFrameCallback AChoreographer_postFrameCallback_;
CallbackHandlers::func_AChoreographer_postFrameCallbackDelayed AChoreographer_postFrameCallbackDelayed_;

CallbackHandlers::func_AChoreographer_postFrameCallback64 AChoreographer_postFrameCallback64_;
CallbackHandlers::func_AChoreographer_postFrameCallbackDelayed64 AChoreographer_postFrameCallbackDelayed64_;

void CallbackHandlers::PostCallback(napi_env env, napi_callback_info info,
                                    CallbackHandlers::FrameCallbackCacheEntry *entry) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    ALooper_prepare(0);
    auto instance = AChoreographer_getInstance_();
    napi_value delay = args[1];
    napi_valuetype delayType;
    napi_typeof(env, delay, &delayType);

    if (android_get_device_api_level() >= 29) {
        if (delayType == napi_number) {
            uint32_t delayValue;
            napi_get_value_uint32(env, delay, &delayValue);
            AChoreographer_postFrameCallbackDelayed64_(instance, entry->frameCallback64_, entry,
                                                       delayValue);
        } else {
            AChoreographer_postFrameCallback64_(instance, entry->frameCallback64_, entry);
        }
    } else {
        if (delayType == napi_number) {
            int64_t delayValue;
            napi_get_value_int64(env, delay, &delayValue);
            AChoreographer_postFrameCallbackDelayed_(instance, entry->frameCallback_, entry,
                                                     static_cast<long>(delayValue));
        } else {
            AChoreographer_postFrameCallback_(instance, entry->frameCallback_, entry);
        }
    }
}

napi_value CallbackHandlers::PostFrameCallback(napi_env env, napi_callback_info info) {
    if (android_get_device_api_level() >= 24) {
        NapiScope scope(env);
        InitChoreographer();

        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        if (argc < 1) {
            napi_throw_type_error(env, nullptr, "Frame callback argument is not a function");
            return nullptr;
        }

        napi_valuetype argType;
        napi_typeof(env, args[0], &argType);
        if (argType != napi_function) {
            napi_throw_type_error(env, nullptr, "Frame callback argument is not a function");
            return nullptr;
        }

        napi_value func = args[0];

        napi_value idKey;
        napi_create_string_utf8(env, "_postFrameCallbackId", NAPI_AUTO_LENGTH, &idKey);

        napi_value pId;
        napi_get_property(env, func, idKey, &pId);

        napi_valuetype pIdType;
        napi_typeof(env, pId, &pIdType);
        if (pIdType == napi_number) {
            int32_t id;
            napi_get_value_int32(env, pId, &id);
            auto cb = frameCallbackCache_.find(id);
            if (cb != frameCallbackCache_.end()) {
                bool shouldReschedule = !cb->second.isScheduled();
                cb->second.markScheduled();
                if (shouldReschedule) {
                    PostCallback(env, info, &cb->second);
                }
                return nullptr;
            }
        }

        uint64_t key = ++frameCallbackCount_;

        napi_value keyValue;
        napi_create_int64(env, key, &keyValue);
        napi_set_property(env, func, idKey, keyValue);

        auto [val, inserted] = frameCallbackCache_.try_emplace(key, env, func, key);
        assert(inserted && "Frame callback ID should not be duplicated");

        val->second.markScheduled();
        PostCallback(env, info, &val->second);

    }
    return nullptr;
}

napi_value CallbackHandlers::RemoveFrameCallback(napi_env env, napi_callback_info info) {
    if (android_get_device_api_level() >= 24) {
        NapiScope scope(env);
        InitChoreographer();

        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        if (argc < 1) {
            napi_throw_type_error(env, nullptr, "Frame callback argument is not a function");
            return nullptr;
        }

        napi_valuetype argType;
        napi_typeof(env, args[0], &argType);
        if (argType != napi_function) {
            napi_throw_type_error(env, nullptr, "Frame callback argument is not a function");
            return nullptr;
        }

        napi_value func = args[0];

        napi_value idKey;
        napi_create_string_utf8(env, "_postFrameCallbackId", NAPI_AUTO_LENGTH, &idKey);

        napi_value pId;
        napi_get_property(env, func, idKey, &pId);

        if (pId != nullptr && napi_util::is_of_type(env, pId, napi_number)) {
            int32_t id;
            napi_get_value_int32(env, pId, &id);
            auto cb = frameCallbackCache_.find(id);
            if (cb != frameCallbackCache_.end()) {
                cb->second.markRemoved();
            }
        }
    }
    return nullptr;
}

void CallbackHandlers::InitChoreographer() {
    if (AChoreographer_getInstance_ == nullptr) {
        void *lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (lib != nullptr) {
            AChoreographer_getInstance_ = reinterpret_cast<func_AChoreographer_getInstance>(
                    dlsym(lib, "AChoreographer_getInstance"));
            AChoreographer_postFrameCallback_ = reinterpret_cast<func_AChoreographer_postFrameCallback>(
                    dlsym(lib, "AChoreographer_postFrameCallback"));
            AChoreographer_postFrameCallbackDelayed_ = reinterpret_cast<func_AChoreographer_postFrameCallbackDelayed>(
                    dlsym(lib, "AChoreographer_postFrameCallbackDelayed"));

            assert(AChoreographer_getInstance_);
            assert(AChoreographer_postFrameCallback_);
            assert(AChoreographer_postFrameCallbackDelayed_);

            if (android_get_device_api_level() >= 29) {
                AChoreographer_postFrameCallback64_ = reinterpret_cast<func_AChoreographer_postFrameCallback64>(
                        dlsym(lib, "AChoreographer_postFrameCallback64"));
                AChoreographer_postFrameCallbackDelayed64_ = reinterpret_cast<func_AChoreographer_postFrameCallbackDelayed64>(
                        dlsym(lib, "AChoreographer_postFrameCallbackDelayed64"));

                assert(AChoreographer_postFrameCallback64_);
                assert(AChoreographer_postFrameCallbackDelayed64_);
            }
        }
    }
}

void CallbackHandlers::RemoveEnvEntries(napi_env env) {
    for (auto &item: cache_) {
        if (item.second.env_ == env) {
            cache_.erase(item.first);
        }
    }

    for (auto &item: frameCallbackCache_) {
        if (item.second.env == env) {
            frameCallbackCache_.erase(item.first);
        }
    }

}

// Worker

napi_value CallbackHandlers::NewThreadCallback(napi_env env, napi_callback_info info) {
    try {
        NAPI_CALLBACK_BEGIN_VARGS()

        napi_value newTarget;
        napi_get_new_target(env, info, &newTarget);
        if (napi_util::is_null_or_undefined(env, newTarget)) {
            throw NativeScriptException("Worker should be called as a constructor!");
        }

        if (argc != 1 || !napi_util::is_of_type(env, argv[0], napi_string)) {
            throw NativeScriptException(
                    "Worker should be called with one string parameter (name of file to run)!");
        }

        napi_value global;
        napi_get_global(env, &global);

        auto frames = tns::BuildStacktraceFrames(env, nullptr, 1);
        string currentExecutingScriptNameStr =
                frames.size() < 3 ? frames[0].filename : frames[2].filename;

        auto lastForwardSlash = currentExecutingScriptNameStr.find_last_of("/");
        auto currentDir = currentExecutingScriptNameStr.substr(0, lastForwardSlash + 1);
        std::string fileSchema("file://");
        if (currentDir.compare(0, fileSchema.length(), fileSchema) == 0) {
            currentDir = currentDir.substr(fileSchema.length());
        }

        std::string workerPath = ArgConverter::ConvertToString(env, argv[0]);
        DEBUG_WRITE("Worker Path: %s, Current Dir: %s", workerPath.c_str(), currentDir.c_str());

        // Will throw if path is invalid or doesn't exist
        ModuleInternal::CheckFileExists(env, workerPath, currentDir);

        auto workerId = nextWorkerId++;
        napi_value workerIdValue;
        napi_create_int32(env, workerId, &workerIdValue);
        napi_set_named_property(env, jsThis, "workerId", workerIdValue);

        id2WorkerMap.emplace(workerId, napi_util::make_ref(env, jsThis));

        DEBUG_WRITE("Called Worker constructor id=%d", workerId);

        JEnv jEnv;
        JniLocalRef filePath(ArgConverter::ConvertToJavaString(env, argv[0]));
        JniLocalRef dirPath(jEnv.NewStringUTF(currentDir.c_str()));
        jEnv.CallStaticVoidMethod(RUNTIME_CLASS, INIT_WORKER_METHOD_ID, (jstring) filePath,
                                  (jstring) dirPath, workerId);

        napi_value stack;
        napi_value error;
        napi_value empty;
        napi_create_string_utf8(env, "",0,  &empty);
        napi_create_error(env, empty, empty, &error);
        napi_get_named_property(env, error, "stack", &stack);
        napi_set_named_property(env, jsThis, "__stack__", stack);

        return jsThis;
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value
CallbackHandlers::WorkerObjectPostMessageCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS();

    try {
        if (argc != 1) {
            NativeScriptException exception(
                    "Failed to execute 'postMessage' on 'Worker': 1 argument required.");
            throw exception;
        }

        napi_value isTerminated;
        napi_get_named_property(env, jsThis, "isTerminated", &isTerminated);
        if (!napi_util::is_null_or_undefined(env, isTerminated)) {
            bool terminated;
            napi_get_value_bool(env, isTerminated, &terminated);
            if (terminated) {
                return nullptr;
            }
        }

        std::string msg = tns::JsonStringifyObject(env, argv[0], false);

        // get worker's ID that is associated on the other side - in Java
        napi_value jsId;
        napi_get_named_property(env, jsThis, "workerId", &jsId);
        auto id = napi_util::get_int32(env, jsId);

        JEnv jEnv;

        jstring jmsg = jEnv.NewStringUTF(msg.c_str());
        JniLocalRef jmsgRef(jmsg);

        jEnv.CallStaticVoidMethod(RUNTIME_CLASS, SEND_MESSAGE_TO_WORKER_METHOD_ID, id,
                                  (jstring) jmsgRef);

        DEBUG_WRITE(
                "MAIN: WorkerObjectPostMessageCallback called postMessage on Worker object(id=%d)",
                id);
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
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

void CallbackHandlers::WorkerGlobalOnMessageCallback(napi_env env, jstring message) {
    NapiScope scope(env);
    try {
        napi_value globalObject;
        napi_get_global(env, &globalObject);

        napi_value callback;
        napi_get_named_property(env, globalObject, "onmessage", &callback);

        if (napi_util::is_of_type(env, callback, napi_function)) {
            std::string msgString = ArgConverter::jstringToString(message);
            napi_value dataObject = tns::JsonParseString(env, msgString.c_str());

            napi_value obj;
            napi_create_object(env, &obj);
            if (napi_util::is_of_type(env, dataObject, napi_object)) {
                napi_set_named_property(env, obj, "data", dataObject);
            }

            napi_value args[1] = {
                    obj
            };

            napi_value result;
            napi_status status = napi_call_function(env, globalObject, callback, 1, args, &result);
            if (status == napi_pending_exception) {
                napi_value error;
                napi_get_and_clear_last_exception(env, &error);
                CallWorkerScopeOnErrorHandle(env, error);
            }
        } else {
            DEBUG_WRITE(
                    "WORKER: WorkerGlobalOnMessageCallback couldn't fire a worker's `onmessage` callback because it isn't implemented!");
        }
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
}


napi_value
CallbackHandlers::WorkerGlobalPostMessageCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    try {
        if (argc != 1) {
            napi_throw_error(env, nullptr,
                             "Failed to execute 'postMessage' on WorkerGlobalScope: 1 argument required.");
            return nullptr;
        }

        bool pendingException;
        napi_is_exception_pending(env, &pendingException);
        if (pendingException) {
            napi_value err;
            napi_get_and_clear_last_exception(env, &err);
            CallWorkerScopeOnErrorHandle(env, err);
        }

        napi_value objToStringify = argv[0];
        std::string msg = tns::JsonStringifyObject(env, objToStringify, false);

        JEnv jenv;
        auto jmsg = jenv.NewStringUTF(msg.c_str());
        JniLocalRef jmsgRef(jmsg);

        jenv.CallStaticVoidMethod(RUNTIME_CLASS, SEND_MESSAGE_TO_MAIN_METHOD_ID, (jstring) jmsgRef);

        DEBUG_WRITE("WORKER: WorkerGlobalPostMessageCallback called.");
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
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

void CallbackHandlers::WorkerObjectOnMessageCallback(napi_env env, jint workerId, jstring message) {
    NapiScope scope(env);
    try {

        auto workerFound = CallbackHandlers::id2WorkerMap.find(workerId);

        if (workerFound == CallbackHandlers::id2WorkerMap.end()) {
            DEBUG_WRITE(
                    "MAIN: WorkerObjectOnMessageCallback no worker instance was found with workerId=%d.",
                    workerId);
            return;
        }

        napi_ref workerPersistent = workerFound->second;

        napi_value worker;
        napi_get_reference_value(env, workerPersistent, &worker);

        napi_value global;
        napi_get_global(env, &global);

        napi_value callback;
        napi_get_named_property(env, worker, "onmessage", &callback);

        if (napi_util::is_of_type(env, callback, napi_function)) {
            std::string msgString = ArgConverter::jstringToString(message);

            napi_value dataObject = tns::JsonParseString(env, msgString.c_str());

            napi_value obj;
            napi_create_object(env, &obj);
            napi_set_named_property(env, obj, "data", dataObject);

            napi_value args[1] = {obj};

            napi_value result;
            napi_status status = napi_call_function(env, worker, callback, 1, args, &result);
            if (status != napi_ok) {
                throw NativeScriptException("Error calling onmessage callback");
            }
        } else {
            DEBUG_WRITE(
                    "MAIN: WorkerObjectOnMessageCallback couldn't fire a worker(id=%d) object's `onmessage` callback because it isn't implemented.",
                    workerId);
        }
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
}

napi_value CallbackHandlers::WorkerObjectTerminateCallback(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, nullptr, &thiz, nullptr);

    DEBUG_WRITE("WORKER: WorkerObjectTerminateCallback called.");

    try {
        napi_value global;
        napi_get_global(env, &global);

        napi_value jsId;
        napi_get_named_property(env, thiz, "workerId", &jsId);

        int32_t id;
        napi_get_value_int32(env, jsId, &id);

        napi_value isTerminated;
        napi_get_named_property(env, thiz, "isTerminated", &isTerminated);
        if (!napi_util::is_null_or_undefined(env, isTerminated)) {
            bool terminated;
            napi_get_value_bool(env, isTerminated, &terminated);
            if (terminated) {
                return nullptr;
            }
        }

        napi_value trueValue;
        napi_get_boolean(env, true, &trueValue);
        napi_set_named_property(env, thiz, "isTerminated", trueValue);

        JEnv jenv;
        jenv.CallStaticVoidMethod(RUNTIME_CLASS, TERMINATE_WORKER_METHOD_ID, id);

        CallbackHandlers::ClearWorkerPersistent(env, id);
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
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

napi_value CallbackHandlers::WorkerGlobalCloseCallback(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value thiz;
    napi_get_cb_info(env, info, &argc, nullptr, &thiz, nullptr);

    DEBUG_WRITE("WORKER: WorkerThreadCloseCallback called.");

    try {
        napi_value global;
        napi_get_global(env, &global);

        napi_value isTerminated;
        napi_get_named_property(env, global, "isTerminating", &isTerminated);
        if (!napi_util::is_null_or_undefined(env, isTerminated)) {
            bool terminated;
            napi_get_value_bool(env, isTerminated, &terminated);
            if (terminated) {
                return nullptr;
            }
        }

        napi_value trueValue;
        napi_get_boolean(env, true, &trueValue);
        napi_set_named_property(env, global, "isTerminating", trueValue);

        napi_value callback;
        napi_get_named_property(env, global, "onclose", &callback);
        if (napi_util::is_of_type(env, callback, napi_function)) {
            napi_value result;
            napi_call_function(env, global, callback, 0, nullptr, &result);
        }

        bool pendingException;
        napi_is_exception_pending(env, &pendingException);
        if (pendingException) {
            napi_value err;
            napi_get_and_clear_last_exception(env, &err);
            CallWorkerScopeOnErrorHandle(env, err);
        }

        JEnv jenv;
        jenv.CallStaticVoidMethod(RUNTIME_CLASS, WORKER_SCOPE_CLOSE_METHOD_ID);
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return napi_util::undefined(env);
}

void CallbackHandlers::CallWorkerScopeOnErrorHandle(napi_env env, napi_value error) {
    try {
        napi_value global;
        napi_get_global(env, &global);

        napi_value callback;
        napi_get_named_property(env, global, "onerror", &callback);

        napi_value message = nullptr;
        napi_value stack = nullptr;
        std::vector<JsStacktraceFrame> frames;
        if (napi_util::is_of_type(env, error, napi_object)) {
            frames = tns::BuildStacktraceFrames(env, error, 1);
            napi_get_named_property(env, error, "message", &message);
            napi_get_named_property(env, error, "stack", &stack);
        } else {
            napi_coerce_to_string(env, error, &message);
            napi_create_string_utf8(env, "", 0, &stack);
        }

        if (napi_util::is_of_type(env, callback, napi_function)) {
            napi_value args[1] = {error};
            napi_value result;
            napi_call_function(env, global, callback, 1, args, &result);

            bool pendingException;
            napi_is_exception_pending(env, &pendingException);
            if (pendingException) {
                napi_value perror = nullptr;
                napi_value pmessage = nullptr;
                napi_value pstack = nullptr;
                napi_get_and_clear_last_exception(env, &perror);

                std::vector<JsStacktraceFrame> pframes;
                if (napi_util::is_of_type(env, perror, napi_object)) {
                    pframes = tns::BuildStacktraceFrames(env, perror, 1);
                    napi_get_named_property(env, perror, "message", &pmessage);
                    napi_get_named_property(env, perror, "stack", &pstack);
                } else {
                    napi_coerce_to_string(env, perror, &pmessage);
                    napi_create_string_utf8(env, "", 0, &pstack);
                }

                auto line = 0;
                std::string filename;
                if (!pframes.empty()) {
                    line = pframes[0].line;
                    filename = pframes[0].filename;
                }
                Runtime::GetRuntime(env)->PassUncaughtExceptionFromWorkerToMainHandler(
                        pmessage,
                        pstack,
                        ArgConverter::convertToJsString(env, filename),
                        line
                );
            } else if (!napi_util::is_null_or_undefined(env, result)) {
                bool handled;
                napi_get_value_bool(env, result, &handled);
                if (handled) {
                    return;
                }
            }
        }

        auto line = 0;
        std::string filename;
        if (!frames.empty()) {
            line = frames[0].line;
            filename = frames[0].filename;
        }
        Runtime::GetRuntime(env)->PassUncaughtExceptionFromWorkerToMainHandler(
                message,
                stack,
                ArgConverter::convertToJsString(env, filename),
                line
        );


    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
}

void CallbackHandlers::CallWorkerObjectOnErrorHandle(napi_env env, jint workerId, jstring message,
                                                     jstring stackTrace, jstring filename,
                                                     jint lineno, jstring threadName) {
    NapiScope scope(env);
    try {
        auto workerFound = CallbackHandlers::id2WorkerMap.find(workerId);

        if (workerFound == CallbackHandlers::id2WorkerMap.end()) {
            DEBUG_WRITE(
                    "MAIN: CallWorkerObjectOnErrorHandle no worker instance was found with workerId=%d.",
                    workerId);
            return;
        }

        napi_ref workerPersistent = workerFound->second;

        napi_value worker;
        napi_get_reference_value(env, workerPersistent, &worker);

        napi_value callback;
        napi_get_named_property(env, worker, "onerror", &callback);

        if (napi_util::is_of_type(env, callback, napi_function)) {
            napi_value errEvent;
            napi_value msgValue = ArgConverter::jstringToJsString(env, message);
            napi_value codeValue;
            napi_create_string_utf8(env, "",0, &codeValue);
            napi_create_error(env,codeValue, msgValue, &errEvent);
            napi_value stack_main_thread;
            napi_get_named_property(env, worker, "__stack__", &stack_main_thread);
            std::string main_stack = napi_util::get_string_value(env, stack_main_thread);

            std::string curr_stack = ArgConverter::jstringToString(stackTrace);

            std::string full_stack = curr_stack + "\n" + main_stack.substr(main_stack.find_first_of("\n") + 1) ;

            napi_value full_stack_value;
            napi_create_string_utf8(env, full_stack.c_str(), full_stack.size(), &full_stack_value);

            napi_set_named_property(env, errEvent, "stack", full_stack_value);

            napi_value args[1] = {errEvent};

            napi_value result;
            napi_status status = napi_call_function(env, worker, callback, 1, args, &result);
            if (status != napi_ok) {
                napi_value exception;
                napi_get_and_clear_last_exception(env, &exception);
                if (!napi_util::is_null_or_undefined(env, exception)) {
                    throw NativeScriptException(env, exception,
                                                "Error calling onerror on Worker Object");
                } else {
                    throw NativeScriptException("Error calling onerror on Worker Object");
                }
            }

            bool handled;
            napi_get_value_bool(env, result, &handled);

            if (handled) {
                return;
            }
        }

        // Exception wasn't handled, or is critical -> Throw exception
        std::string strMessage = ArgConverter::jstringToString(message);
        std::string strFilename = ArgConverter::jstringToString(filename);
        std::string strThreadname = ArgConverter::jstringToString(threadName);
        std::string strStackTrace = ArgConverter::jstringToString(stackTrace);

        DEBUG_WRITE(
                "Unhandled exception in '%s' thread. file: %s, line %d, message: %s\nStackTrace: %s",
                strThreadname.c_str(), strFilename.c_str(), lineno, strMessage.c_str(),
                strStackTrace.c_str());
    } catch (NativeScriptException &ex) {
        ex.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }
}

void CallbackHandlers::ClearWorkerPersistent(napi_env env, int workerId) {
    NapiScope scope(env);
    DEBUG_WRITE("ClearWorkerPersistent called for workerId=%d", workerId);

    auto workerFound = CallbackHandlers::id2WorkerMap.find(workerId);

    if (workerFound == CallbackHandlers::id2WorkerMap.end()) {
        DEBUG_WRITE(
                "MAIN | WORKER: ClearWorkerPersistent no worker instance was found with workerId=%d ! The worker may already be terminated.",
                workerId);
        return;
    }

    napi_ref workerPersistent = workerFound->second;
    napi_delete_reference(env, workerPersistent);

    id2WorkerMap.erase(workerId);
}

void CallbackHandlers::TerminateWorkerThread(napi_env env) {
    JSEnterScope
    try {
        Runtime::GetRuntime(env)->DestroyRuntime();
    } catch (NativeScriptException &e) {
        e.ReThrowToNapi(env);
    } catch (std::exception e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

}

robin_hood::unordered_map<uint64_t, CallbackHandlers::CacheEntry> CallbackHandlers::cache_;
robin_hood::unordered_map<jclass, jfieldID> CallbackHandlers::jclass_to_runtimeId_cache;

robin_hood::unordered_map<uint64_t, CallbackHandlers::FrameCallbackCacheEntry> CallbackHandlers::frameCallbackCache_;

std::atomic_int64_t CallbackHandlers::count_ = {0};
std::atomic_uint64_t CallbackHandlers::frameCallbackCount_ = {0};

int CallbackHandlers::nextWorkerId = 0;
int CallbackHandlers::lastCallId = -1;
napi_value CallbackHandlers::lastCallValue = nullptr;
robin_hood::unordered_map<int, napi_ref> CallbackHandlers::id2WorkerMap;

short CallbackHandlers::MAX_JAVA_STRING_ARRAY_LENGTH = 100;
jclass CallbackHandlers::RUNTIME_CLASS = nullptr;
jclass CallbackHandlers::JAVA_LANG_STRING = nullptr;
jfieldID CallbackHandlers::CURRENT_OBJECTID_FIELD_ID = nullptr;
jmethodID CallbackHandlers::RESOLVE_CLASS_METHOD_ID = nullptr;
jmethodID CallbackHandlers::MAKE_INSTANCE_STRONG_ID = nullptr;
jmethodID CallbackHandlers::GET_TYPE_METADATA = nullptr;
jmethodID CallbackHandlers::ENABLE_VERBOSE_LOGGING_METHOD_ID = nullptr;
jmethodID CallbackHandlers::DISABLE_VERBOSE_LOGGING_METHOD_ID = nullptr;
jmethodID CallbackHandlers::INIT_WORKER_METHOD_ID = nullptr;
jmethodID CallbackHandlers::SEND_MESSAGE_TO_MAIN_METHOD_ID = nullptr;
jmethodID CallbackHandlers::SEND_MESSAGE_TO_WORKER_METHOD_ID = nullptr;
jmethodID CallbackHandlers::TERMINATE_WORKER_METHOD_ID = nullptr;
jmethodID CallbackHandlers::WORKER_SCOPE_CLOSE_METHOD_ID = nullptr;

NumericCasts CallbackHandlers::castFunctions;

ArrayElementAccessor CallbackHandlers::arrayElementAccessor;
FieldAccessor CallbackHandlers::fieldAccessor;
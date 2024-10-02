//
// Created by Ammar Ahmed on 20/09/2024.
//
#include <assert.h>
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

using namespace std;
using namespace ns;

void CallbackHandlers::Init(napi_env env) {
    JEnv jEnv;

    JAVA_LANG_STRING = jEnv.FindClass("java/lang/String");
    assert(JAVA_LANG_STRING != nullptr);

    RUNTIME_CLASS = jEnv.FindClass("org/nativescript/runtime/napi/Runtime");
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

    // INIT_WORKER_METHOD_ID = jEnv.GetStaticMethodID(RUNTIME_CLASS, "initWorker",
    //                                                "(Ljava/lang/String;Ljava/lang/String;I)V");

    // assert(INIT_WORKER_METHOD_ID != nullptr);

    MetadataNode::Init(env);

    MethodCache::Init();
}

napi_value CallbackHandlers::CallJavaMethod(napi_env env, napi_value caller, const string &className,
                                      const string &methodName, MetadataEntry *entry,
                                      bool isFromInterface, bool isStatic,
                                      bool isSuper,
                                      size_t argc, napi_value* argv) {

    JEnv env;

    jclass clazz;
    jmethodID mid;
    string *sig = nullptr;
    string *returnType = nullptr;
    auto retType = MethodReturnType::Unknown;
    MethodCache::CacheMethodInfo mi;
    
    string entrySignature = entry->getSig();

    auto isolate = args.GetIsolate();

    if ((entry != nullptr) && entry->getIsResolved()) {
        isStatic = entry->isStatic;

        if (entry->memberId == nullptr) {
            clazz = env.FindClass(className);

            if (clazz == nullptr) {
                MetadataNode *callerNode = MetadataNode::GetNodeFromHandle(caller);
                const string callerClassName = callerNode->GetName();

                DEBUG_WRITE("Cannot resolve class: %s while calling method: %s callerClassName: %s",
                            className.c_str(), methodName.c_str(), callerClassName.c_str());
                clazz = env.FindClass(callerClassName);
                if (clazz == nullptr) {
                    //todo: plamen5kov: throw exception here
                    DEBUG_WRITE("Cannot resolve caller's class name: %s", callerClassName.c_str());
                     return nullptr;
                }

                if (isStatic) {
                    if (isFromInterface) {
                        auto methodAndClassPair = env.GetInterfaceStaticMethodIDAndJClass(className,
                                                                                          methodName,
                                                                                          entrySignature);
                        entry->memberId = methodAndClassPair.first;
                        clazz = methodAndClassPair.second;
                    } else {
                        entry->memberId = env.GetStaticMethodID(clazz, methodName, entrySignature);
                    }
                } else {
                    entry->memberId = env.GetMethodID(clazz, methodName, entrySignature);
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
                        auto methodAndClassPair = env.GetInterfaceStaticMethodIDAndJClass(className,
                                                                                          methodName,
                                                                                          entrySignature);
                        entry->memberId = methodAndClassPair.first;
                        clazz = methodAndClassPair.second;
                    } else {
                        entry->memberId = env.GetStaticMethodID(clazz, methodName, entrySignature);
                    }
                } else {
                    entry->memberId = env.GetMethodID(clazz, methodName, entrySignature);
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
        sig = &entrySignature;
        returnType = &entry->getReturnType();
        retType = entry->getRetType();
    } else {
        DEBUG_WRITE("Resolving method: %s on className %s", methodName.c_str(), className.c_str());

        clazz = env.FindClass(className);
        if (clazz != nullptr) {
            mi = MethodCache::ResolveMethodSignature(className, methodName, args, isStatic);
            if (mi.mid == nullptr) {
                DEBUG_WRITE("Cannot resolve class=%s, method=%s, isStatic=%d, isSuper=%d",
                            className.c_str(), methodName.c_str(), isStatic, isSuper);
                 return nullptr;
            }
        } else {
            MetadataNode *callerNode = MetadataNode::GetNodeFromHandle(caller);
            const string callerClassName = callerNode->GetName();
            DEBUG_WRITE("Resolving method on caller class: %s.%s on className %s",
                        callerClassName.c_str(), methodName.c_str(), className.c_str());
            mi = MethodCache::ResolveMethodSignature(callerClassName, methodName, args, isStatic);
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
        DEBUG_WRITE("CallJavaMethod called %s.%s. Instance id: %d, isSuper=%d", className.c_str(),
                    methodName.c_str(), caller.IsEmpty() ? -42 : caller->GetIdentityHash(),
                    isSuper);
    } else {
        DEBUG_WRITE("CallJavaMethod called %s.%s. static method", className.c_str(),
                    methodName.c_str());
    }

//    JSToJavaConverter argConverter = (entry != nullptr && entry->isExtensionFunction)
//                                     ? JSToJavaConverter(isolate, args, *sig, entry, caller)
//                                     : JSToJavaConverter(isolate, args, *sig, entry);

    JsArgConverter argConverter = (entry != nullptr && entry->isExtensionFunction)
                                        ? JsArgConverter(env, caller, args.data(), argc, *sig, entry)
                                        : JsArgConverter(env, args.data(), argc, false, *sig, entry);


    if (!argConverter.IsValid()) {
        JsArgConverter::Error err = argConverter.GetError();
        throw NativeScriptException(err.msg);
    }

    JniLocalRef callerJavaObject;

    jvalue *javaArgs = argConverter.ToArgs();

    auto runtime = Runtime::GetRuntime(env);
    auto objectManager = runtime->GetObjectManager();

    if (!isStatic) {
        callerJavaObject = objectManager->GetJavaObjectByJsObject(env, caller);
        if (callerJavaObject.IsNull()) {
            stringstream ss;
            if (args.IsConstructCall()) {
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
                env.CallStaticVoidMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                env.CallNonvirtualVoidMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                env.CallVoidMethodA(callerJavaObject, mid, javaArgs);
            }
            break;
        }
        case MethodReturnType::Boolean: {
            jboolean result;
            if (isStatic) {
                result = env.CallStaticBooleanMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualBooleanMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallBooleanMethodA(callerJavaObject, mid, javaArgs);
            }

            napi_get_boolean(env, result != 0, &returnValue);
            break;
        }
        case MethodReturnType::Byte: {
            jbyte result;
            if (isStatic) {
                result = env.CallStaticByteMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualByteMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallByteMethodA(callerJavaObject, mid, javaArgs);
            }

            napi_create_int32(env, result, &returnValue);
            break;
        }
        case MethodReturnType::Char: {
            jchar result;
            if (isStatic) {
                result = env.CallStaticCharMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualCharMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallCharMethodA(callerJavaObject, mid, javaArgs);
            }

            JniLocalRef str(env.NewString(&result, 1));
            jboolean bol = true;
            const char *resP = env.GetStringUTFChars(str, &bol);
            returnValue = ArgConverter::convertToJsString(isolate, resP, 1);
            env.ReleaseStringUTFChars(str, resP);
            break;
        }
        case MethodReturnType::Short: {
            jshort result;
            if (isStatic) {
                result = env.CallStaticShortMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualShortMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallShortMethodA(callerJavaObject, mid, javaArgs);
            }
            
            napi_create_int32(env, result, &returnValue);

            break;
        }
        case MethodReturnType::Int: {
            jint result;
            if (isStatic) {
                result = env.CallStaticIntMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualIntMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
               result = env.CallIntMethodA(callerJavaObject, mid, javaArgs);
            }
            napi_create_int32(env, result, &returnValue);
            break;

        }
        case MethodReturnType::Long: {
            jlong result;
            if (isStatic) {
                result = env.CallStaticLongMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualLongMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallLongMethodA(callerJavaObject, mid, javaArgs);
            }
            returnValue = ArgConverter::ConvertFromJavaLong(env, result);
            break;
        }
        case MethodReturnType::Float: {
            jfloat result;
            if (isStatic) {
                result = env.CallStaticFloatMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualFloatMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallFloatMethodA(callerJavaObject, mid, javaArgs);
            }
            returnValue = napi_create_double(env, (double) result, &returnValue);
            break;
        }
        case MethodReturnType::Double: {
            jdouble result;
            if (isStatic) {
                result = env.CallStaticDoubleMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualDoubleMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallDoubleMethodA(callerJavaObject, mid, javaArgs);
            }
            returnValue = napi_create_double(env, (double) result, &returnValue);
            break;
        }
        case MethodReturnType::String: {
            jobject result = nullptr;
            bool exceptionOccurred;

            if (isStatic) {
                result = env.CallStaticObjectMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualObjectMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallObjectMethodA(callerJavaObject, mid, javaArgs);
            }

            if (result != nullptr) {
                returnValue = ArgConverter::jstringToJsString(env, static_cast<jstring>(result));
                env.DeleteLocalRef(result);
            } else {
                napi_get_null(env, &returnValue);
            }

            break;
        }
        case MethodReturnType::Object: {
            jobject result = nullptr;
            bool exceptionOccurred;

            if (isStatic) {
                result = env.CallStaticObjectMethodA(clazz, mid, javaArgs);
            } else if (isSuper) {
                result = env.CallNonvirtualObjectMethodA(callerJavaObject, clazz, mid, javaArgs);
            } else {
                result = env.CallObjectMethodA(callerJavaObject, mid, javaArgs);
            }

            if (result != nullptr) {
                auto isString = env.IsInstanceOf(result, JAVA_LANG_STRING);

                if (isString) {
                    returnValue = ArgConverter::jstringToJsString(env, (jstring) result);
                } else {
                    jint javaObjectID = objectManager->GetOrCreateObjectId(result);
                    returnValue = objectManager->GetJsObjectByJavaObject(javaObjectID);

                    if (returnValue == nullptr || napi_util::is_undefined(env, returnValue)) {
                        objectResult = objectManager->CreateJSWrapper(javaObjectID, *returnType,
                                                                      result);
                    }
                }

                env.DeleteLocalRef(result);
            } else {
                 napi_get_null(env, &returnValue);
            }

            break;
        }
        default: {
            assert(false);
            break;
        }
    }


    return returnValue;

//    static uint32_t adjustMemCount = 0;
//
//    if ((++adjustMemCount % 2) == 0) {
//        AdjustAmountOfExternalAllocatedMemory(env, isolate);
//    }
}


bool CallbackHandlers::RegisterInstance(napi_env env, napi_value jsObject,
                                        const std::string &fullClassName,
                                        const ArgsWrapper &argWrapper,
                                        napi_value implementationObject,
                                        bool isInterface,
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

            napi_callback_info info = argWrapper.args;
            size_t argc;
            napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr);
            std::vector<napi_value> argv(argc);
            if (argc > 0) {
                napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr);
            }

            JsArgConverter argConverter(env, argv.data(), argc, mi.signature);
            auto ctorArgs = argConverter.ToArgs();

            instance = jEnv.NewObjectA(generatedJavaClass, mi.mid, ctorArgs);
        }
    }

    jEnv.CallVoidMethod(runtime->GetJavaRuntime(), MAKE_INSTANCE_STRONG_ID, instance, javaObjectID);

    AdjustAmountOfExternalAllocatedMemory(jEnv, env);

    JniLocalRef localInstance(instance);
    success = !localInstance.IsNull();

    if (success) {
        jclass instanceClass = jEnv.FindClass(fullClassName);
        objectManager->SetJavaClass(jsObject, instanceClass);
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

void CallbackHandlers::AdjustAmountOfExternalAllocatedMemory(JEnv &jEnv, napi_env env) {
    auto runtime = Runtime::GetRuntime(env);
    // runtime->AdjustAmountOfExternalAllocatedMemory();
    // runtime->TryCallGC();
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

            if (napi_util::is_of_type(env, element, napi_function)) {
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
    napi_handle_scope handle_scope;
    napi_open_handle_scope(env, &handle_scope);

    napi_value cb = napi_util::get_ref_value(env, callback_ref);

    napi_value global;
    napi_get_global(env, &global);

    cache_.erase(it);

    napi_value result;
    napi_status status = napi_call_function(env, global, cb, 0, nullptr, &result);

    napi_close_handle_scope(env, handle_scope);

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
        // e.ReThrowToNapi(env);
    }
    catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        // nsEx.ReThrowToNapi(env);
    }
    catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        // nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value CallbackHandlers::DrainMicrotaskCallback(napi_env env, napi_callback_info info) {
    napi_run_microtasks(env);
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
    // NOOP
    return nullptr;
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
        // e.ReThrowToV8();
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
        ns::LogEnabled = true;
        JEnv jEnv;
        jEnv.CallVoidMethod(Runtime::GetRuntime(env)->GetJavaRuntime(),
                            ENABLE_VERBOSE_LOGGING_METHOD_ID);
    }
    catch (NativeScriptException &e) {
        // e.ReThrowToV8();
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
        ns::LogEnabled = false;
        JEnv jEnv;
        jEnv.CallVoidMethod(Runtime::GetRuntime(env)->GetJavaRuntime(),
                            DISABLE_VERBOSE_LOGGING_METHOD_ID);
    }
    catch (NativeScriptException &e) {
        // e.ReThrowToV8();
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
                                          napi_value jsObject, const string &methodName,
                                          jobjectArray args) {

    JEnv jEnv(_jEnv);
    napi_value result;

    napi_value method;

    napi_get_named_property(env, jsObject, methodName.c_str(), &method);

    if (method == nullptr || napi_util::is_undefined(env, method)) {
        stringstream ss;
        ss << "Cannot find method '" << methodName << "' implementation";
        throw NativeScriptException(ss.str());
    } else if (!napi_util::is_of_type(env, method, napi_function)) {
        stringstream ss;
        ss << "Property '" << methodName << "' is not a function";
        throw NativeScriptException(ss.str());
    } else {

        napi_escapable_handle_scope escapeScope;
        napi_open_escapable_handle_scope(env, &escapeScope);

        size_t argc;
        napi_value jsArgs = ArgConverter::ConvertJavaArgsToJsArgs(env, args, &argc);

        napi_value jsResult;
        napi_call_function(env, jsObject, method, argc, argc == 0 ? nullptr : &jsArgs, &jsResult);
        napi_escape_handle(env, escapeScope, jsResult, &result);
        napi_close_escapable_handle_scope(env, escapeScope);
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

    auto javaArr = objectManager->GetJavaObjectByJsObject(env, arr);

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
}

napi_value CallbackHandlers::RemoveFrameCallback(napi_env env, napi_callback_info info) {
    if (android_get_device_api_level() >= 24) {
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

robin_hood::unordered_map<uint64_t, CallbackHandlers::CacheEntry> CallbackHandlers::cache_;

robin_hood::unordered_map<uint64_t, CallbackHandlers::FrameCallbackCacheEntry> CallbackHandlers::frameCallbackCache_;

std::atomic_int64_t CallbackHandlers::count_ = {0};
std::atomic_uint64_t CallbackHandlers::frameCallbackCount_ = {0};

int CallbackHandlers::nextWorkerId = 0;
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

NumericCasts CallbackHandlers::castFunctions;
ArrayElementAccessor CallbackHandlers::arrayElementAccessor;
FieldAccessor CallbackHandlers::fieldAccessor;
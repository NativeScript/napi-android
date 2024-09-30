#include <string>
#include <sstream>
#include <cctype>
#include <dirent.h>
#include <set>
#include <cerrno>
#include <unistd.h>
#include "NativeScriptException.h"
#include "MetadataNode.h"
#include "CallbackHandlers.h"
#include "NativeScriptAssert.h"
#include "File.h"
#include "Constants.h"
#include "Runtime.h"
#include "ArgConverter.h"
#include "FieldCallbackData.h"
#include "MetadataBuilder.h"
#include "ArgsWrapper.h"
#include "Util.h"

using namespace std;

void MetadataNode::Init(napi_env env) {
    auto cache = GetMetadataNodeCache(env);
    cache->MetadataKey = napi_util::make_ref(env, ArgConverter::convertToJsString(env,
                                                                                  "tns::MetadataKey"));
}

napi_value MetadataNode::CreateArrayObjectConstructor(napi_env env) {
    auto it = s_arrayObjects.find(env);
    if (it != s_arrayObjects.end()) {
        return it->second;
    }

    napi_value arrayConstructor;
    const char *name = "ArrayObjectWrapper";
    napi_define_class(env, name, strlen(name),
                      [](napi_env env, napi_callback_info info) -> napi_value {
                          NAPI_CALLBACK_BEGIN(0)
                          return jsThis;
                      }, nullptr, 0, nullptr, &arrayConstructor);
    napi_value proto = napi_util::get_proto(env, arrayConstructor);
    ObjectManager::MarkObject(env, proto);

    napi_value setter;
    const char *setter_name = "setValueAtIndex";
    napi_value getter;
    const char *getter_name = "getValueAtIndex";

    napi_value length;
    const char *length_name = "length";

    napi_create_function(env, setter_name, strlen(setter_name), ArraySetterCallback, nullptr,
                         &setter);
    napi_create_function(env, getter_name, strlen(getter_name), ArrayGetterCallback, nullptr,
                         &getter);
    napi_create_function(env, length_name, strlen(length_name), ArrayLengthCallback, nullptr,
                         &length);

    napi_set_named_property(env, proto, setter_name, setter);
    napi_set_named_property(env, proto, getter_name, getter);
    napi_set_named_property(env, proto, length_name, length);

    napi_value global;
    napi_get_global(env, &global);
    napi_set_named_property(env, global, "ArrayObjectWrapper", arrayConstructor);
    s_arrayObjects.emplace(env, arrayConstructor);

    return arrayConstructor;
}

napi_value MetadataNode::WrapArrayObject(napi_env env, napi_value value) {
    static const char *script = R"((function(target) {
    return new Proxy(target, {
        get(target, prop) {
            if (prop === Symbol.iterator) {
                let index = 0;
                return function() {
                    return {
                        next: function() {
                            if (index < target.length) {
                                return { value: target.getValueByIndex(index++), done: false };
                            } else {
                                return { done: true };
                            }
                        }
                    };
                };
            }
            if (typeof prop === 'string' && !isNaN(prop)) {
                return target.getValueByIndex(Number(prop));
            }
            if (prop === 'map') {
                return function(callback) {
                    const result = [];
                    for (let i = 0; i < target.length; i++) {
                        result.push(callback(target.getValueByIndex(i), i, target));
                    }
                    return result;
                };
            }
            if (prop === 'forEach') {
                return function(callback) {
                    for (let i = 0; i < target.length; i++) {
                        callback(target.getValueByIndex(i), i, target);
                    }
                };
            }
            // Add other methods as needed
            return target[prop];
        },
        set(target, prop, value) {
            if (typeof prop === 'string' && !isNaN(prop)) {
                target.setValueByIndex(Number(prop), value);
                return true;
            }
            target[prop] = value;
            return true;
        }
    });
})();
)";

    napi_value proxyFunction;

    auto it = s_envToArrayProxyFunction.find(env);
    if (it == s_envToArrayProxyFunction.end()) {
        napi_value scriptValue;
        napi_create_string_utf8(env, script, strlen(script), &scriptValue);
        napi_run_script(env, scriptValue, &proxyFunction);
        napi_ref ref = napi_util::make_ref(env, proxyFunction);
        s_envToArrayProxyFunction.emplace(env, ref);
    } else {
        proxyFunction = napi_util::get_ref_value(env, it->second);
    }

    napi_value array;
    napi_value global;
    napi_get_global(env, &global);
    napi_value argv[1];
    argv[0] = value;
    napi_call_function(env, global, proxyFunction, 1, argv, &array);

    return array;
}

napi_value MetadataNode::CreateExtendedJSWrapper(napi_env env, ObjectManager *objectManager,
                                                 const std::string &proxyClassName) {
    napi_value extInstance = nullptr;

    auto cacheData = GetCachedExtendedClassData(env, proxyClassName);

    if (cacheData.node != nullptr) {
        extInstance = objectManager->GetEmptyObject(env);
        ObjectManager::MarkSuperCall(env, extInstance);

        napi_value extendedCtorFunc = napi_util::get_ref_value(env,
                                                                cacheData.extendedCtorFunction);

        napi_util::set_prototype(env, extInstance, napi_util::get_proto(env, extendedCtorFunc));

        napi_set_named_property(env, extInstance, napi_util::CONSTRUCTOR, extendedCtorFunc);

        SetInstanceMetadata(env, extInstance, cacheData.node);
    }

    return extInstance;
}

string MetadataNode::GetTypeMetadataName(napi_env env, napi_value value) {
    napi_value typeMetadataName;
    napi_get_named_property(env, value, PRIVATE_TYPE_NAME, &typeMetadataName);

    return napi_util::get_string_value(env, typeMetadataName);
}

MetadataNode *MetadataNode::GetNodeFromHandle(napi_env env, napi_value value) {
    auto node = GetInstanceMetadata(env, value);
    return node;
}

napi_value MetadataNode::CreateJSWrapper(napi_env env, ObjectManager *objectManager) {
    napi_value obj;

    if (m_isArray) {
        obj = CreateArrayWrapper(env);
    } else {
        obj = objectManager->GetEmptyObject(env);
        napi_value ctorFunc = GetConstructorFunction(env);
        napi_set_named_property(env, obj, napi_util::CONSTRUCTOR, ctorFunc);
        napi_util::set_prototype(env, obj, napi_util::get_proto(env, ctorFunc));
        SetInstanceMetadata(env, obj, this);
    }

    return obj;
}

napi_value MetadataNode::ArrayGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2);

    napi_value index = argv[0];
    int32_t indexValue;
    napi_get_value_int32(env, index, &indexValue);
    auto node = GetInstanceMetadata(env, jsThis);

    return CallbackHandlers::GetArrayElement(env, jsThis, indexValue, node->m_name);
}

napi_value MetadataNode::ArraySetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2);

    napi_value index = argv[0];
    napi_value value = argv[1];

    int32_t indexValue;
    napi_get_value_int32(env, index, &indexValue);
    auto node = GetInstanceMetadata(env, jsThis);

    CallbackHandlers::SetArrayElement(env, jsThis, indexValue, node->m_name, value);
    return value;
}

napi_value MetadataNode::ArrayLengthCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    int length = CallbackHandlers::GetArrayLength(env, jsThis);

    napi_value len;
    napi_create_int32(env, length, &len);
    return len;
}

napi_value MetadataNode::CreateArrayWrapper(napi_env env) {
    auto node = GetOrCreate("java/lang/Object");
    auto ctorFunc = node->GetConstructorFunction(env);

    napi_value constructor = CreateArrayObjectConstructor(env);
    napi_value instance;
    napi_new_instance(env, constructor, 0, nullptr, &instance);
    napi_util::set_prototype(env, instance, napi_util::get_proto(env, constructor));
    napi_value arrayProxy = WrapArrayObject(env, instance);

    SetInstanceMetadata(env, instance, this);

    return arrayProxy;
}

napi_value MetadataNode::GetImplementationObject(napi_env env, napi_value object) {
    auto target = object;
    napi_value currentPrototype = target;

    napi_value implementationObject;

    napi_get_named_property(env, currentPrototype, CLASS_IMPLEMENTATION_OBJECT,
                            &implementationObject);

    if (implementationObject != nullptr && !napi_util::is_undefined(env, implementationObject)) {
        return implementationObject;
    }

    bool hasProperty;

    napi_has_own_named_property(env, object, PROP_KEY_IS_PROTOTYPE_IMPLEMENTATION_OBJECT,
                                &hasProperty);

    if (hasProperty) {
        bool maybeHasOwnProperty;
        napi_has_own_named_property(env, object, napi_util::PROTOTYPE, &maybeHasOwnProperty);

        if (!maybeHasOwnProperty) {
            return nullptr;
        }

        return napi_util::get_proto(env, object);
    }

    napi_value activityImplementationObject;
    napi_get_named_property(env, object, "t::ActivityImplementationObject",
                            &activityImplementationObject);

    if (activityImplementationObject != nullptr &&
        !napi_util::is_undefined(env, activityImplementationObject)) {
        return activityImplementationObject;
    }

    napi_value lastPrototype;

    bool prototypeCycleDetected = false;

    bool foundImplementationObject = false;

    while (!foundImplementationObject) {
        currentPrototype = napi_util::get_proto(env, currentPrototype);

        if (napi_util::is_null(env, currentPrototype)) {
            break;
        }

        if (lastPrototype == currentPrototype) {
            auto abovePrototype = napi_util::get_proto(env, currentPrototype);
            prototypeCycleDetected = abovePrototype == currentPrototype;
            break;
        }

        if (currentPrototype == nullptr || napi_util::is_null(env, currentPrototype) ||
            prototypeCycleDetected) {
            return nullptr;
        } else {
            napi_value implObject;
            napi_get_named_property(env, currentPrototype, CLASS_IMPLEMENTATION_OBJECT,
                                    &implObject);

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
    auto cache = GetMetadataNodeCache(env);
    napi_wrap(env, object, node, nullptr, nullptr, nullptr);
}

MetadataNode *MetadataNode::GetInstanceMetadata(napi_env env, napi_value object) {
    void *node;
    napi_unwrap(env, object, &node);
    if (node == nullptr)
        return nullptr;
    return reinterpret_cast<MetadataNode *>(node);
}

napi_value MetadataNode::ExtendedClassConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    napi_value newTarget;
    napi_get_new_target(env, info, &newTarget);

    if (newTarget == nullptr) {
        return nullptr;
    }

    auto extData = reinterpret_cast<ExtendedClassCallbackData *>(data);
    SetInstanceMetadata(env, jsThis, extData->node);

    napi_value implObject = napi_util::get_ref_value(env, extData->implementationObject);
    ObjectManager::MarkSuperCall(env, jsThis);

    string fullClassName = extData->fullClassName;

    ArgsWrapper argWrapper(info, ArgType::Class);

    // TODO   bool success = CallbackHandlers::RegisterInstance(isolate, thiz, fullClassName, argWrapper, implementationObject, false, extData->node->m_name);

    return jsThis;
}

napi_value MetadataNode::InterfaceConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    napi_value arg1 = argv[0];
    napi_value arg2 = argv[1];

    napi_valuetype arg1Type;
    napi_typeof(env, arg1, &arg1Type);

    napi_valuetype arg2Type;
    napi_typeof(env, arg2, &arg2Type);

    napi_value implmentationObject;
    napi_value interfaceName;

    if (arg1Type == napi_object) {
        implmentationObject = arg1;
    } else if (arg1Type == napi_string && arg2Type == napi_object) {
        interfaceName = arg1;
        implmentationObject = arg2;
    } else if (arg2Type == napi_undefined && arg1Type != napi_object) {
        throw NativeScriptException(
                string("Invalid arguments provided, first argument must be an object if only one argument is provided"));
    } else {
        throw NativeScriptException(
                string("Invalid arguments provided, first argument must be a string and second argument must be an object"));
    }

    auto node = reinterpret_cast<MetadataNode *>(data);

    auto className = node->m_name;

    SetInstanceMetadata(env, jsThis, node);

    ObjectManager::MarkSuperCall(env, jsThis);

    napi_util::set_prototype(env, implmentationObject, napi_util::get_proto(env, jsThis));

    napi_util::set_prototype(env, jsThis, implmentationObject);

    napi_set_named_property(env, jsThis, CLASS_IMPLEMENTATION_OBJECT, implmentationObject);

    ArgsWrapper argsWrapper(info, ArgType::Interface);

    // TODO  auto success = CallbackHandlers::RegisterInstance(isolate, thiz, className, argWrapper, implementationObject, true);
    return jsThis;
}

napi_value MetadataNode::ConstructorFunctionCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    auto node = reinterpret_cast<MetadataNode *>(data);

    SetInstanceMetadata(env, jsThis, node);

    string extendName;
    auto className = node->m_name;

    string fullClassName = CreateFullClassName(className, extendName);

    // TODO  bool success = CallbackHandlers::RegisterInstance(isolate, thiz, fullClassName, argWrapper, Local<Object>(), false, className);
    return jsThis;
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
    string extendNam = ArgConverter::ConvertToString(env, name);

    for (int i = 0; i < extendNam.size(); i++) {
        char currentSymbol = extendNam[i];
        bool isValidExtendNameSymbol = isalpha(currentSymbol) ||
                                       isdigit(currentSymbol) ||
                                       currentSymbol == '_';
        if (!isValidExtendNameSymbol) {
            return false;
        }
    }

    return true;
}

bool MetadataNode::GetExtendLocation(napi_env env, std::string &extendLocation,
                                     bool isTypeScriptExtend) {
    stringstream extendLocationStream;

    napi_value error;
    napi_create_error(env, nullptr, ArgConverter::convertToJsString(env, "Error"), &error);

    napi_value error_stack;
    napi_get_named_property(env, error, "stack", &error_stack);

    std::string stackTrace = ArgConverter::ConvertToString(env, error_stack);

    extendLocationStream << "unknown_location";
    extendLocation = extendLocationStream.str();
    return true;
    extendLocation = extendLocationStream.str();
    return true;
}

bool MetadataNode::ValidateExtendArguments(napi_env env, napi_callback_info info,
                                           bool extendLocationFound, string &extendLocation,
                                           napi_value* extendName, napi_value* implementationObject,
                                           bool isTypeScriptExtend) {
    NAPI_CALLBACK_BEGIN(2);

    if (napi_util::is_undefined(env, argv[2])) {
        if (!extendLocationFound) {
            stringstream ss;
            ss << "Invalid extend() call. No name specified for extend at location: "
               << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        if (!napi_util::is_of_type(env, argv[0], napi_object)) {
            stringstream ss;
            ss << "Invalid extend() call. No implementation object specified at location: "
               << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        *implementationObject = argv[0];
    } else if (!napi_util::is_undefined(env, argv[2]) || isTypeScriptExtend) {
        if (!napi_util::is_of_type(env, argv[0], napi_string)) {
            stringstream ss;
            ss << "Invalid extend() call. No name for extend specified at location: "
               << extendLocation.c_str();
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        }

        if (!napi_util::is_of_type(env, argv[1], napi_object)) {
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
    MetadataNodeCache *cache;
    auto itFound = s_metadata_node_cache.find(env);
    if (itFound == s_metadata_node_cache.end()) {
        cache = new MetadataNodeCache;
        s_metadata_node_cache.emplace(env, cache);
    } else {
        cache = itFound->second;
    }
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
    napi_value global;

    napi_get_global(env, &global);

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
            napi_set_named_property(env, global, nameSpace.c_str(), packageObj);
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
        result = new MetadataNode(treeNode);

        s_treeNode2NodeCache.emplace(treeNode, result);
    }

    return result;
}

MetadataEntry MetadataNode::GetChildMetadataForPackage(MetadataNode *node, const char *propName) {
    assert(node->m_treeNode->children != nullptr);

    MetadataEntry child(nullptr, NodeType::Class);

    const auto &children = *node->m_treeNode->children;

    for (auto treeNodeChild: children) {
        if (propName == treeNodeChild->name.c_str()) {
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

bool MetadataNode::IsJavascriptKeyword(std::string word) {
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
    napi_escapable_handle_scope scope;
    napi_open_escapable_handle_scope(env, &scope);
    napi_value obj;
    uint8_t nodeType = s_metadataReader.GetNodeType(m_treeNode);
    bool isClass = s_metadataReader.IsNodeTypeClass(nodeType),
            isInterface = s_metadataReader.IsNodeTypeInterface(nodeType);
    napi_status status;

    if (isClass || isInterface) {
        obj = GetConstructorFunction(env);
    } else if (s_metadataReader.IsNodeTypePackage(nodeType)) {
        obj = CreatePackageObject(env);
    } else {
        std::stringstream ss;
        ss << "(InternalError): Can't create proxy for this type=" << static_cast<int>(nodeType);
        throw NativeScriptException(ss.str());
    }

    napi_value result;
    napi_escape_handle(env, scope, obj, &result);

    napi_close_escapable_handle_scope(env, scope);

    return result;
}

napi_value MetadataNode::PackageGetterCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thisArg;
    void *data;

    napi_get_cb_info(env, info, &argc, args, &thisArg, &data);

    auto *methodInfo = static_cast<PackageGetterMethodData *>(data);

    if (methodInfo->value != nullptr) {
        return napi_util::get_ref_value(env, methodInfo->value);
    }

    auto node = methodInfo->node;

    uint8_t nodeType = s_metadataReader.GetNodeType(node->m_treeNode);

    auto child = GetChildMetadataForPackage(node, methodInfo->utf8name);
    auto foundChild = child.treeNode != nullptr;

    if (foundChild) {
        auto childNode = MetadataNode::GetOrCreateInternal(child.treeNode);
        methodInfo->value = napi_util::make_ref(env, childNode->CreateWrapper(env));

        uint8_t childNodeType = s_metadataReader.GetNodeType(child.treeNode);
        bool isInterface = s_metadataReader.IsNodeTypeInterface(childNodeType);
        if (isInterface) {
            // For all java interfaces we register the special Symbol.hasInstance property
            // which is invoked by the instanceof operator (https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Symbol/hasInstance).
            // For example:
            //
            // Object.defineProperty(android.view.animation.Interpolator, Symbol.hasInstance, {
            //    value: function(obj) {
            //        return true;
            //    }
            // });
            //                    RegisterSymbolHasInstanceCallback(isolate, child, cachedItem);
        }

        //                if (node->m_name == "org/json" && child.name == "JSONObject") {
        //                    JSONObjectHelper::RegisterFromFunction(isolate, cachedItem);
        //                }
    }

    return napi_util::get_ref_value(env, methodInfo->value);
}

napi_value MetadataNode::CreatePackageObject(napi_env env) {
    napi_value packageObj;
    napi_create_object(env, &packageObj);

    auto ptrChildren = this->m_treeNode->children;

    if (ptrChildren != nullptr) {
        const auto &children = *ptrChildren;
        for (auto childNode: children) {

            auto *info = new PackageGetterMethodData();
            info->utf8name = childNode->name.c_str();
            info->node = this;
            info->value = nullptr;

            napi_property_descriptor descriptor{
                    childNode->name.c_str(),
                    nullptr,
                    nullptr,
                    PackageGetterCallback,
                    nullptr,
                    nullptr,
                    napi_default,
                    info};
            napi_define_properties(env, packageObj, 1, &descriptor);
        }
    }

    return packageObj;
}

std::vector<MetadataNode::MethodCallbackData *> MetadataNode::SetInstanceMembers(
        napi_env env, napi_value constructor,
        std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
        const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
        MetadataTreeNode *treeNode) {
    auto hasCustomMetadata = treeNode->metadata != nullptr;

    if (hasCustomMetadata) {
        return SetInstanceMembersFromRuntimeMetadata(
                env, constructor, instanceMethodsCallbackData,
                baseInstanceMethodsCallbackData, treeNode);
    }

    SetInstanceFieldsFromStaticMetadata(env, constructor, treeNode);
    return SetInstanceMethodsFromStaticMetadata(
            env, constructor, instanceMethodsCallbackData,
            baseInstanceMethodsCallbackData, treeNode);
}

std::vector<MetadataNode::MethodCallbackData *> MetadataNode::SetInstanceMethodsFromStaticMetadata(
        napi_env env, napi_value ctor,
        std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
        const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
        MetadataTreeNode *treeNode) {
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

    std::unordered_map<std::string, MethodCallbackData *> collectedExtensionMethodDatas;

    auto extensionFunctionsCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < extensionFunctionsCount; i++) {
        auto entry = s_metadataReader.ReadExtensionFunctionEntry(&curPtr);

        if (entry.getName() != lastMethodName) {
            callbackData = tryGetExtensionMethodCallbackData(collectedExtensionMethodDatas,
                                                             entry.name);
            if (callbackData == nullptr) {
                callbackData = new MethodCallbackData(this);
                napi_value proto = napi_util::get_proto(env, ctor);
                napi_value method;
                napi_create_function(env, entry.name.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, proto, entry.name.c_str(), method);

                lastMethodName = entry.name;
                collectedExtensionMethodDatas[entry.name] = callbackData;
            }
        }
        callbackData->candidates.push_back(entry);
    }

    auto instanceMethodCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);

    for (auto i = 0; i < instanceMethodCount; i++) {
        auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);

        if (entry.getName() != lastMethodName) {
            callbackData = tryGetExtensionMethodCallbackData(collectedExtensionMethodDatas,
                                                             entry.name);
            if (callbackData == nullptr) {
                callbackData = new MethodCallbackData(this);
                napi_value proto = napi_util::get_proto(env, ctor);
                napi_value method;
                napi_create_function(env, entry.name.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, proto, entry.name.c_str(), method);

                collectedExtensionMethodDatas[entry.name] = callbackData;
            }

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

            lastMethodName = entry.name;
        }

        callbackData->candidates.push_back(entry);
    }

    return instanceMethodData;
}

MetadataNode::MethodCallbackData *MetadataNode::tryGetExtensionMethodCallbackData(
        std::unordered_map<std::string, MethodCallbackData *> collectedMethodCallbackData,
        std::string lookupName) {

    auto iter = collectedMethodCallbackData.find(lookupName);
    if (iter != collectedMethodCallbackData.end()) {
        return iter->second;
    }

    return nullptr;
}

bool MetadataNode::IsNodeTypeInterface() {
    uint8_t nodeType = s_metadataReader.GetNodeType(m_treeNode);
    return s_metadataReader.IsNodeTypeInterface(nodeType);
}

void MetadataNode::SetInstanceFieldsFromStaticMetadata(
        napi_env env, napi_value constructor, MetadataTreeNode *treeNode) {
    uint8_t *curPtr = s_metadataReader.GetValueData() + treeNode->offsetValue + 1;

    auto nodeType = s_metadataReader.GetNodeType(treeNode);
    auto curType = s_metadataReader.ReadTypeName(treeNode);
    curPtr += sizeof(uint16_t /* baseClassId */);

    if (s_metadataReader.IsNodeTypeInterface(nodeType)) {
        curPtr += sizeof(uint8_t) + sizeof(uint32_t);
    }

    auto extensionFunctionsCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < extensionFunctionsCount; i++) {
        auto entry = s_metadataReader.ReadExtensionFunctionEntry(&curPtr);
    }

    auto instanceMethodCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < instanceMethodCount; i++) {
        auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);
    }

    auto instanceFieldCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < instanceFieldCount; i++) {
        auto entry = s_metadataReader.ReadInstanceFieldEntry(&curPtr);
        auto fieldInfo = new FieldCallbackData(&entry);
        fieldInfo->metadata->declaringType = curType;
        napi_value proto = napi_util::get_proto(env, constructor);
        napi_util::define_property(env, proto, entry.getName().c_str(), nullptr,
                                   FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                   fieldInfo);
    }

    auto kotlinPropertiesCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (int i = 0; i < kotlinPropertiesCount; ++i) {
        uint32_t nameOffset = *reinterpret_cast<uint32_t *>(curPtr);
        std::string propertyName = s_metadataReader.ReadName(nameOffset);
        curPtr += sizeof(uint32_t);

        auto hasGetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        std::string getterMethodName = "";
        if (hasGetter >= 1) {
            auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);
            getterMethodName = entry.getName();
        }

        auto hasSetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        std::string setterMethodName = "";
        if (hasSetter >= 1) {
            auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);
            setterMethodName = entry.getName();
        }

        auto propertyInfo = new PropertyCallbackData(propertyName, getterMethodName,
                                                     setterMethodName);
        napi_value proto = napi_util::get_proto(env, constructor);
        napi_util::define_property(env, proto, propertyName.c_str(), nullptr,
                                   PropertyAccessorGetterCallback, PropertyAccessorSetterCallback,
                                   propertyInfo);
    }
}

std::vector<MetadataNode::MethodCallbackData *> MetadataNode::SetInstanceMembersFromRuntimeMetadata(
        napi_env env, napi_value constructor,
        std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
        const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
        MetadataTreeNode *treeNode) {
    assert(treeNode->metadata != nullptr);

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

    napi_value proto = napi_util::get_proto(env, constructor);

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
                napi_create_function(env, entry.name.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, proto, entry.name.c_str(), method);

                lastMethodName = entry.name;
            }
            callbackData->candidates.push_back(entry);
        } else if (chKind == 'F') {
            entry.type = NodeType::Field;
            auto *fieldInfo = new FieldCallbackData(&entry);
            napi_util::define_property(env, proto, entry.name.c_str(), nullptr,
                                       FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                       fieldInfo);
        }
    }
    return instanceMethodData;
}

void
MetadataNode::SetStaticMembers(napi_env env, napi_value constructor, MetadataTreeNode *treeNode) {

    if (treeNode->metadata == nullptr)
        return;

    uint8_t *curPtr = s_metadataReader.GetValueData() + treeNode->offsetValue + 1;
    auto nodeType = s_metadataReader.GetNodeType(treeNode);
    auto curType = s_metadataReader.ReadTypeName(treeNode);
    curPtr += sizeof(uint16_t /* baseClassId */);
    if (s_metadataReader.IsNodeTypeInterface(nodeType)) {
        curPtr += sizeof(uint8_t) + sizeof(uint32_t);
    }

    auto extensionFunctionsCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < extensionFunctionsCount; i++) {
        auto entry = s_metadataReader.ReadExtensionFunctionEntry(&curPtr);
    }

    auto instanceMethodCout = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < instanceMethodCout; i++) {
        auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);
    }

    auto instanceFieldCout = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < instanceFieldCout; i++) {
        auto entry = s_metadataReader.ReadInstanceFieldEntry(&curPtr);
    }

    auto kotlinPropertiesCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (int i = 0; i < kotlinPropertiesCount; ++i) {
        uint32_t nameOffset = *reinterpret_cast<uint32_t *>(curPtr);
        std::string propertyName = s_metadataReader.ReadName(nameOffset);
        curPtr += sizeof(uint32_t);

        auto hasGetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        if (hasGetter >= 1) {
            auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);
        }

        auto hasSetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        if (hasSetter >= 1) {
            auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);
        }
    }

    std::string lastMethodName;
    MethodCallbackData *callbackData = nullptr;

    auto origin = Constants::APP_ROOT_FOLDER_PATH + GetOrCreateInternal(treeNode)->m_name;

    // get candidates from static methods metadata
    auto staticMethodCout = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < staticMethodCout; i++) {
        auto entry = s_metadataReader.ReadStaticMethodEntry(&curPtr);
        // In java there can be multiple methods of same name with different parameters.
        if (entry.getName() != lastMethodName) {
            callbackData = new MethodCallbackData(this);
            napi_value method;
            napi_create_function(env, entry.name.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                 callbackData, &method);
            napi_set_named_property(env, constructor, entry.name.c_str(), method);
            lastMethodName = entry.name;
        }
        callbackData->candidates.push_back(entry);
    }

    napi_value extendMethod;
    napi_create_function(env, PROP_KEY_EXTEND, NAPI_AUTO_LENGTH, ExtendMethodCallback, this,
                         &extendMethod);
    napi_set_named_property(env, constructor, PROP_KEY_EXTEND, extendMethod);

    // get candidates from static fields metadata
    auto staticFieldCout = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < staticFieldCout; i++) {
        auto entry = s_metadataReader.ReadStaticFieldEntry(&curPtr);
        auto fieldData = new FieldCallbackData(&entry);
        napi_util::define_property(env, constructor, entry.getName().c_str(), nullptr,
                                   FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                   fieldData);
    }

    napi_util::define_property(env, constructor, PROP_KEY_NULLOBJECT, nullptr,
                               NullObjectAccessorGetterCallback, nullptr, this);

    SetClassAccessor(env, constructor);
}

void MetadataNode::SetClassAccessor(napi_env env, napi_value constructor) {
    napi_util::define_property(env, constructor, PROP_KEY_CLASS, nullptr,
                               ClassAccessorGetterCallback, nullptr, nullptr);
}

napi_value MetadataNode::ClassAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    napi_value name;
    napi_get_named_property(env, jsThis, PRIVATE_TYPE_NAME, &name);
    const char *nameValue = napi_util::get_string_value(env, name, 0);
    return CallbackHandlers::FindClass(env, nameValue);
}

napi_value MetadataNode::GetConstructorFunction(napi_env env) {
    std::vector<MethodCallbackData *> instanceMethodsCallbackData;
    return GetConstructorFunctionInternal(env, m_treeNode, instanceMethodsCallbackData);
}

napi_value MetadataNode::GetConstructorFunctionInternal(napi_env env, MetadataTreeNode *treeNode,
                                                        std::vector<MethodCallbackData *> instanceMethodsCallbackData) {
    auto cache = GetMetadataNodeCache(env);
    auto itFound = cache->CtorFuncCache.find(treeNode);
    if (itFound != cache->CtorFuncCache.end()) {
        instanceMethodsCallbackData = itFound->second.instanceMethodCallbacks;
        if (itFound->second.constructorFunction == nullptr)
            return nullptr;
        return napi_util::get_ref_value(env, itFound->second.constructorFunction);
    }

    auto node = GetOrCreateInternal(m_treeNode);

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
    napi_define_class(env, finalName.c_str(), NAPI_AUTO_LENGTH,
                      isInterface ? InterfaceConstructorCallback : ConstructorFunctionCallback,
                      node, 0, nullptr, &constructor);

    // Mark this constructor's prototype as a runtime object.
    ObjectManager::MarkObject(env, napi_util::get_proto(env, constructor));

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
                napi_value proto = napi_util::get_proto(env, constructor);
                napi_value baseProto = napi_util::get_proto(env, baseConstructor);
                // Inherit from base constructor.
                napi_util::napi_inherits(env, constructor, baseConstructor);
            }
        } else {
            baseConstructor = nullptr;
        }
        break;
    }

    // 3. Define the instance functions now on the class prototype.
    auto instanceMethodData = node->SetInstanceMembers(env, constructor,
                                                       instanceMethodsCallbackData,
                                                       baseInstanceMethodsCallbackData, treeNode);

    if (!skippedBaseTypes.empty()) {
        // If there is a mismatch between base type of this class in metadata compared to the class
        // at runtime, we will add methods of base class to this class's prototype.
        node->SetMissingBaseMethods(env, skippedBaseTypes, instanceMethodData, constructor);
    }

    // 4. Define static fields and methods on the class constructor
    node->SetStaticMembers(env, constructor, treeNode);

    node->SetInnerTypes(env, constructor, treeNode);

    napi_ref constructorRef = napi_util::make_ref(env, constructor);


    // insert env-specific persistent function handle
    node->m_ctorCachePerEnv.insert({env, constructorRef});

    if (baseConstructor != nullptr && !napi_util::is_undefined(env, baseConstructor)) {
        napi_util::set_prototype(env, constructor, baseConstructor);
    }

    CtorCacheData ctorCacheItem(constructorRef, instanceMethodsCallbackData);
    cache->CtorFuncCache.emplace(treeNode, ctorCacheItem);

    return constructor;
}

void MetadataNode::SetInnerTypes(napi_env env, napi_value constructor, MetadataTreeNode *treeNode) {
    if (treeNode->children != nullptr) {
        const auto &children = *treeNode->children;
        for (auto curChild: children) {
            napi_util::define_property(env, constructor, curChild->name.c_str(), nullptr,
                                       SetInnerTypeCallback, nullptr, curChild);
        }
    }
}

napi_value MetadataNode::SetInnerTypeCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    auto curChild = reinterpret_cast<MetadataTreeNode *>(data);
    auto childNode = GetOrCreateInternal(curChild);
    auto itFound = childNode->m_ctorCachePerEnv.find(env);
    if (itFound != childNode->m_ctorCachePerEnv.end()) {
        return napi_util::get_ref_value(env, itFound->second);
    }
    napi_value constructor = childNode->GetConstructorFunction(env);
    return constructor;
}

MetadataReader *MetadataNode::getMetadataReader() {
    return &MetadataNode::s_metadataReader;
}

napi_value MetadataNode::NullObjectAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    napi_value value;
    status = napi_get_named_property(env, jsThis, PROP_KEY_NULL_NODE_NAME, &value);

    if (status != napi_ok || value == nullptr || napi_util::is_undefined(env, value)) {
        auto node = reinterpret_cast<MetadataNode *>(data);
        napi_value external;
        napi_create_external(env, node, nullptr, nullptr, &external);
        napi_set_named_property(env, jsThis, PROP_KEY_NULL_NODE_NAME, external);

        napi_value nullValueOfFunction;
        napi_create_function(env, nullptr, 0, MetadataNode::NullValueOfCallback, nullptr,
                             &nullValueOfFunction);

        napi_value key;
        napi_create_string_utf8(env, PROP_KEY_VALUEOF, strlen(PROP_KEY_VALUEOF), &key);
        napi_delete_property(env, jsThis, key, nullptr);
        napi_set_property(env, jsThis, key, nullValueOfFunction);
    }

    return jsThis;
}

napi_value MetadataNode::NullValueOfCallback(napi_env env, napi_callback_info info) {
    napi_value nullValue;
    napi_get_null(env, &nullValue);
    return nullValue;
}

napi_value MetadataNode::FieldAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    auto fieldData = reinterpret_cast<FieldCallbackData *>(data);

    if (!fieldData->metadata->isStatic
        // check whether there's a declaring type to get the class from it
        || (fieldData->metadata->getDeclaringType().empty())) {

        return nullptr;
    }

    return CallbackHandlers::GetJavaField(env, jsThis, fieldData);
}

napi_value MetadataNode::FieldAccessorSetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);
    auto fieldData = reinterpret_cast<FieldCallbackData *>(data);

    if (!fieldData->metadata->isStatic) {

        return nullptr;
    }

    if (fieldData->metadata->getIsFinal()) {
        stringstream ss;
        ss << "You are trying to set \"" << fieldData->metadata->getName()
           << "\" which is a final field! Final fields can only be read.";
        string exceptionMessage = ss.str();

        throw NativeScriptException(exceptionMessage);
    } else {

        CallbackHandlers::SetJavaField(env, jsThis, argv[1], fieldData);
        return argv[1];
    }
}

napi_value MetadataNode::PropertyAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    auto propertyCallbackData = reinterpret_cast<PropertyCallbackData *>(data);

    if (propertyCallbackData->getterMethodName.empty()) {
        return nullptr;
    }

    napi_value getter;
    napi_get_named_property(env, jsThis, propertyCallbackData->getterMethodName.c_str(), &getter);

    napi_value result;
    napi_call_function(env, jsThis, getter, 0, nullptr, &result);
    return result;
}

napi_value MetadataNode::PropertyAccessorSetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)
    auto propertyCallbackData = reinterpret_cast<PropertyCallbackData *>(data);

    if (propertyCallbackData->setterMethodName.empty()) {
        return nullptr;
    }

    napi_value setter;
    napi_get_named_property(env, jsThis, propertyCallbackData->setterMethodName.c_str(), &setter);

    napi_value result;
    napi_call_function(env, jsThis, setter, 1, &argv[0], &result);

    return result;
}

napi_value MetadataNode::ExtendMethodCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(3)

    napi_value extendName;
    napi_value implementationObject;
    string extendLocation;

    auto hasDot = false;
    auto isTypeScriptExtend = false;

    if (!napi_util::is_undefined(env, argv[2])) {
        if (napi_util::is_of_type(env, argv[2], napi_boolean)) {
            napi_get_value_bool(env, argv[2], &isTypeScriptExtend);
        };
    } else {
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
        hasDot = strName.find(".") != string::npos;
    }

    if (hasDot) {
        extendName = argv[0];
        implementationObject = argv[1];
    } else {
        auto isValidExtendLocation = GetExtendLocation(env, extendLocation, isTypeScriptExtend);
        auto validArgs = ValidateExtendArguments(env, info, isValidExtendLocation, extendLocation,
                                                 &extendName, &implementationObject,
                                                 isTypeScriptExtend);

        if (!validArgs) {
            return nullptr;
        }
    }

    auto node = reinterpret_cast<MetadataNode *>(data);

    string extendNameAndLocation = extendLocation + ArgConverter::ConvertToString(env, extendName);
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
        return napi_util::get_ref_value(env, cachedData.extendedCtorFunction);
    }

    napi_value implementationObjectName;
    napi_get_named_property(env, implementationObject, CLASS_IMPLEMENTATION_OBJECT,
                            &implementationObjectName);

    if (implementationObjectName == nullptr) {
        napi_set_named_property(env, implementationObject, CLASS_IMPLEMENTATION_OBJECT,
                                ArgConverter::convertToJsString(env, fullExtendedName));
    } else {
        string usedClassName = ArgConverter::ConvertToString(env, implementationObjectName);
        stringstream s;
        s << "This object is used to extend another class '" << usedClassName << "'";
        throw NativeScriptException(s.str());
    }

    auto baseClassCtorFunction = node->GetConstructorFunction(env);
    napi_value extendFuncCtor;
    napi_define_class(env, fullExtendedName.c_str(), NAPI_AUTO_LENGTH,
                      MetadataNode::ExtendedClassConstructorCallback,
                      new ExtendedClassCallbackData(node, extendNameAndLocation,
                                                    napi_util::make_ref(env, implementationObject),
                                                    fullClassName), 0, nullptr, &extendFuncCtor);
    napi_value extendFuncPrototype = napi_util::get_proto(env, extendFuncCtor);
    ObjectManager::MarkObject(env, extendFuncPrototype);

    napi_util::set_prototype(env, implementationObject,
                             napi_util::get_proto(env, baseClassCtorFunction));

    napi_util::define_property(
            env, implementationObject, PROP_KEY_SUPER, nullptr, SuperAccessorGetterCallback,
            nullptr, nullptr);


    napi_util::set_prototype(env, extendFuncPrototype, implementationObject);

    napi_util::set_prototype(env, extendFuncCtor, baseClassCtorFunction);

    SetClassAccessor(env, extendFuncCtor);

    napi_set_named_property(env, extendFuncCtor, PRIVATE_TYPE_NAME,
                            ArgConverter::convertToJsString(env, fullExtendedName));

    s_name2NodeCache.emplace(fullExtendedName, node);

    ExtendedClassCacheData cacheData(napi_util::make_ref(env, extendFuncCtor), fullExtendedName,
                                     node);
    auto cache = GetMetadataNodeCache(env);
    cache->ExtendedCtorFuncCache.emplace(fullExtendedName, cacheData);

    return extendFuncCtor;
}

napi_value MetadataNode::SuperAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    napi_value superValue;
    napi_get_named_property(env, jsThis, PROP_KEY_SUPERVALUE, &superValue);

    if (superValue == nullptr || napi_util::is_undefined(env, superValue)) {
        auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
        superValue = objectManager->GetEmptyObject(env);
        napi_delete_property(env, superValue,
                             ArgConverter::convertToJsString(env, PROP_KEY_TOSTRING), nullptr);
        napi_delete_property(env, superValue,
                             ArgConverter::convertToJsString(env, PROP_KEY_VALUEOF), nullptr);
        ObjectManager::MarkSuperCall(env, superValue);

        // jsThis.prototype.prototype.prototype
        napi_value superProto = napi_util::get_proto(env, napi_util::get_proto(env,
                                                                               napi_util::get_proto(
                                                                                       env,
                                                                                       jsThis)));
        napi_util::set_prototype(env, superValue, superProto);

        napi_set_named_property(env, jsThis, PROP_KEY_SUPERVALUE, superValue);
        objectManager->CloneLink(superValue, jsThis);
        auto node = GetInstanceMetadata(env, jsThis);
        SetInstanceMetadata(env, superValue, node);
    }

    return superValue;
}

napi_value MetadataNode::MethodCallback(napi_env env, napi_callback_info info) {

    void *data;
    napi_value jsThis;
    napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, &data);

    NAPI_GET_VARIABLE_ARGS();

    MetadataEntry *entry;

    auto callbackData = reinterpret_cast<MethodCallbackData *>(data);
    auto initialCallbackData = reinterpret_cast<MethodCallbackData *>(data);

    string *className;
    const auto &first = callbackData->candidates.front();
    const auto &methodName = first.name;

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
                DEBUG_WRITE("MetaDataEntry Method %s's signature is: %s", entry->getName().c_str(),
                            entry->getSig().c_str());
                break;
            }
        }

        // Iterates through the parent class's methods to find a good match
        if (!found) {
            callbackData = callbackData->parent;
        }
    }

    auto isSuper = false;

    if (!first.isStatic) {
        napi_value superValue;
        napi_get_named_property(env, jsThis, PRIVATE_CALLSUPER, &superValue);
        isSuper = napi_util::get_bool(env, superValue);
    }

    if (argc == 0 && methodName == PROP_KEY_VALUEOF) {
        return jsThis;
    } else {
        bool isFromInterface = initialCallbackData->node->IsNodeTypeInterface();
        // return CallbackHandlers::CallJavaMethod(thiz, *className, methodName, entry, isFromInterface, first.isStatic, isSuper, info);
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
            auto entry = s_metadataReader.ReadInstanceMethodEntry(&curPtr);

            auto isConstructor = entry.getName() == "<init>";
            if (isConstructor) {
                continue;
            }

            for (auto data: instanceMethodData) {
                if (data->candidates.front().getName() == entry.getName()) {
                    callbackData = data;
                    break;
                }
            }

            if (callbackData == nullptr) {
                callbackData = new MethodCallbackData(this);
                napi_value proto = napi_util::get_proto(env, constructor);
                napi_value method;
                napi_create_function(env, entry.getName().c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, proto, entry.getName().c_str(), method);
            }

            bool foundSameSig = false;
            for (auto m: callbackData->candidates) {
                foundSameSig = m.getSig() == entry.getSig();
                if (foundSameSig) {
                    break;
                }
            }

            if (!foundSameSig) {
                callbackData->candidates.push_back(entry);
            }
        }
    }
}

void MetadataNode::BuildMetadata(const std::string &filesPath) {
    s_metadataReader = MetadataBuilder::BuildMetadata(filesPath);
}

void MetadataNode::onDisposeEnv(napi_env env) {
    {
        auto it = s_metadata_node_cache.find(env);
        if (it != s_metadata_node_cache.end()) {
            delete it->second;
            s_metadata_node_cache.erase(it);
        }
    }
    {
        auto it = s_arrayObjects.find(env);
        if (it != s_arrayObjects.end()) {
            s_arrayObjects.erase(it);
        }
    }
    {
        for (auto & it : s_treeNode2NodeCache) {
            auto it2 = it.second->m_ctorCachePerEnv.find(env);
            if (it2 != it.second->m_ctorCachePerEnv.end()) {
                napi_reference_ref(env, it2->second, nullptr);
                it.second->m_ctorCachePerEnv.erase(it2);
            }
        }
    }
}


string MetadataNode::TNS_PREFIX = "com/tns/gen/";
MetadataReader MetadataNode::s_metadataReader;
robin_hood::unordered_map<std::string, MetadataNode *> MetadataNode::s_name2NodeCache;
robin_hood::unordered_map<std::string, MetadataTreeNode *> MetadataNode::s_name2TreeNodeCache;
robin_hood::unordered_map<MetadataTreeNode *, MetadataNode *> MetadataNode::s_treeNode2NodeCache;
robin_hood::unordered_map<napi_env, MetadataNode::MetadataNodeCache *> MetadataNode::s_metadata_node_cache;

// TODO
bool MetadataNode::s_profilerEnabled = false;
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
        return napi_util::get_ref_value(env, it->second);
    }

    auto node = GetOrCreate("java/lang/Object");
    auto objectConstructor = node->GetConstructorFunction(env);

    napi_value arrayConstructor;
    const char *name = "ArrayObjectWrapper";
    napi_define_class(env, name, strlen(name),
                      [](napi_env env, napi_callback_info info) -> napi_value {
                          NAPI_CALLBACK_BEGIN(0)
                          return jsThis;
                      }, nullptr, 0, nullptr, &arrayConstructor);
    napi_value proto = napi_util::get_prototype(env, arrayConstructor);
    ObjectManager::MarkObject(env, proto);

    napi_util::napi_set_function(env, proto, "setValueAtIndex", ArraySetterCallback, nullptr);
    napi_util::napi_set_function(env, proto, "getValueAtIndex", ArrayGetterCallback, nullptr);
    napi_util::napi_set_function(env, proto, "getAllValues", ArrayGetAllValuesCallback, nullptr);
    napi_util::define_property(env, proto, "length", nullptr, ArrayLengthCallback);

    napi_util::napi_inherits(env, arrayConstructor, objectConstructor);

    s_arrayObjects.emplace(env, napi_util::make_ref(env, arrayConstructor));

    return arrayConstructor;
}

napi_value MetadataNode::CreateExtendedJSWrapper(napi_env env, ObjectManager *objectManager,
                                                 const std::string &proxyClassName) {
    napi_value extInstance = nullptr;

    auto cacheData = GetCachedExtendedClassData(env, proxyClassName);

    if (cacheData.node != nullptr) {
        extInstance = objectManager->GetEmptyObject();
        ObjectManager::MarkSuperCall(env, extInstance);

        napi_value extendedCtorFunc = napi_util::get_ref_value(env,
                                                               cacheData.extendedCtorFunction);

        napi_util::setPrototypeOf(env, extInstance, napi_util::get_prototype(env, extendedCtorFunc));

        napi_set_property(env, extInstance, Constants::Get(env)->constructorValue, extendedCtorFunc);

        SetInstanceMetadata(env, extInstance, cacheData.node);
    }

    return extInstance;
}

string MetadataNode::GetTypeMetadataName(napi_env env, napi_value value) {
    napi_value typeMetadataName;
    napi_get_property(env, value, Constants::Get(env)->privateTypeNameValue, &typeMetadataName);

    return napi_util::get_string_value(env, typeMetadataName);
}

MetadataNode *MetadataNode::GetNodeFromHandle(napi_env env, napi_value value) {
    auto node = GetInstanceMetadata(env, value);
    return node;
}

bool MetadataNode::isArray() {
    return m_isArray;
}

napi_value MetadataNode::CreateJSWrapper(napi_env env, ObjectManager *objectManager) {
    napi_value obj;

    if (m_isArray) {
        obj = CreateArrayWrapper(env);
    } else {
        obj = objectManager->GetEmptyObject();
        napi_value ctorFunc = GetConstructorFunction(env);
        napi_set_property(env, obj, Constants::Get(env)->constructorValue, ctorFunc);
        napi_util::setPrototypeOf(env, obj, napi_util::get_prototype(env, ctorFunc));
        SetInstanceMetadata(env, obj, this);
    }

    return obj;
}

napi_value MetadataNode::ArrayGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2);

    try {

        napi_value index = argv[0];
        int32_t indexValue;
        napi_get_value_int32(env, index, &indexValue);
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
        napi_create_array(env, &arr);

        for (int i = 0; i < length; i++) {
            napi_value element = CallbackHandlers::GetArrayElement(env, jsThis, i, node->m_name);
            napi_set_element(env, arr, i, element);
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
        napi_get_value_int32(env, index, &indexValue);
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
        napi_create_int32(env, length, &len);
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

napi_value MetadataNode::CreateArrayWrapper(napi_env env) {
    napi_value constructor = CreateArrayObjectConstructor(env);
    napi_value instance;
    napi_new_instance(env, constructor, 0, nullptr, &instance);
    SetInstanceMetadata(env, instance, this);
    return instance;
}

napi_value MetadataNode::GetImplementationObject(napi_env env, napi_value object) {
    auto target = object;
    napi_value currentPrototype = target;

    napi_value implementationObject;

    napi_get_property(env, currentPrototype, Constants::Get(env)->classImplementationObjectValue,
                            &implementationObject);

    if (implementationObject != nullptr && !napi_util::is_undefined(env, implementationObject)) {
        return implementationObject;
    }

    bool hasProperty;

    napi_has_own_property(env, object, Constants::Get(env)->isPrototypeImplementationObjectValue, &hasProperty);

    if (hasProperty) {
        bool maybeHasOwnProperty;
        napi_has_own_property(env, object, Constants::Get(env)->prototypeValue, &maybeHasOwnProperty);

        if (!maybeHasOwnProperty) {
            return nullptr;
        }

        return napi_util::get_prototype(env, object);
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
            napi_get_property(env, currentPrototype, Constants::Get(env)->classImplementationObjectValue,
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
    napi_value external;
    napi_create_external(env, node, nullptr,nullptr, &external);
    napi_set_named_property(env, object, "#instance_metadata", external);
//    napi_wrap(env, object, node, nullptr, nullptr, nullptr);
}

MetadataNode *MetadataNode::GetInstanceMetadata(napi_env env, napi_value object) {
    void *node;
    napi_value external;
    napi_get_named_property(env, object, "#instance_metadata", &external);

    if (napi_util::is_null_or_undefined(env, external)) return nullptr;

    napi_get_value_external(env, external, &node);
//    napi_unwrap(env, object, &node);
    if (node == nullptr)
        return nullptr;
    return reinterpret_cast<MetadataNode *>(node);
}

napi_value MetadataNode::ExtendedClassConstructorCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)

    try {
        napi_value newTarget;
        napi_get_new_target(env, info, &newTarget);
        if (napi_util::is_null_or_undefined(env, newTarget)) return nullptr;

        auto extData = reinterpret_cast<ExtendedClassCallbackData *>(data);
        SetInstanceMetadata(env, jsThis, extData->node);

        napi_value implementationObject = napi_util::get_ref_value(env,
                                                                   extData->implementationObject);
        ObjectManager::MarkSuperCall(env, jsThis);

        string fullClassName = extData->fullClassName;

        ArgsWrapper argWrapper(info, ArgType::Class);
        napi_value jsThisProxy;
        bool success = CallbackHandlers::RegisterInstance(env, jsThis, fullClassName, argWrapper,
                                                          implementationObject, false,
                                                          &jsThisProxy, extData->node->m_name);

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
    NAPI_CALLBACK_BEGIN_VARGS()

    try {

        napi_valuetype arg1Type;
        napi_valuetype arg2Type;

        napi_typeof(env, argv[0], &arg1Type);

        if (argc == 2) {
            napi_typeof(env, argv[1], &arg2Type);
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


        napi_util::setPrototypeOf(env, implementationObject, napi_util::getPrototypeOf(env, jsThis));

        napi_util::setPrototypeOf(env, jsThis, implementationObject);

        napi_set_property(env, jsThis, Constants::Get(env)->classImplementationObjectValue, implementationObject);

        ArgsWrapper argsWrapper(info, ArgType::Interface);

        napi_value jsThisProxy;
        auto success = CallbackHandlers::RegisterInstance(env, jsThis, className, argsWrapper,
                                                          implementationObject, true, &jsThisProxy);
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
    NAPI_CALLBACK_BEGIN(0)

    try {

        auto node = reinterpret_cast<MetadataNode *>(data);

        SetInstanceMetadata(env, jsThis, node);

        string extendName;
        auto className = node->m_name;

        string fullClassName = CreateFullClassName(className, extendName);

        ArgsWrapper argsWrapper(info, ArgType::Class);
        napi_value jsThisProxy;
        bool success = CallbackHandlers::RegisterInstance(env, jsThis, fullClassName, argsWrapper,
                                                          nullptr, false, &jsThisProxy, className);


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

    auto frames = tns::BuildStacktraceFrames(env, nullptr, 3);
    tns::JsStacktraceFrame *frame;
    if (isTypeScriptExtend) {
        frame = &frames[2]; // the _super.apply call to ts_helpers will always be the third call frame
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
    size_t argc = 1;
    napi_value args[1];
    napi_value thisArg;
    void *data;

    napi_get_cb_info(env, info, &argc, args, &thisArg, &data);

    try {

        auto *methodInfo = static_cast<PackageGetterMethodData *>(data);

        if (methodInfo->value != nullptr) {
            napi_value value = napi_util::get_ref_value(env, methodInfo->value);
            if (!napi_util::is_null_or_undefined(env, value)) {
                return value;
            }
        }

        auto node = methodInfo->node;

        uint8_t nodeType = s_metadataReader.GetNodeType(node->m_treeNode);

        auto child = GetChildMetadataForPackage(node, methodInfo->utf8name);
        auto foundChild = child.treeNode != nullptr;
        napi_value cachedItem = nullptr;
        if (foundChild) {
            auto childNode = MetadataNode::GetOrCreateInternal(child.treeNode);
            auto cache = GetMetadataNodeCache(env);
            if (methodInfo->value != nullptr) {
                cache->CtorFuncCache.erase(childNode->m_treeNode);
            }
            cachedItem = childNode->CreateWrapper(env);
            methodInfo->value = napi_util::make_ref(env, cachedItem);

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
                RegisterSymbolHasInstanceCallback(env, child, cachedItem);
            }

            if (node->m_name == "org/json" && child.name == "JSONObject") {
                JSONObjectHelper::RegisterFromFunction(env, cachedItem);
            }
        }

        return cachedItem;

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

void MetadataNode::RegisterSymbolHasInstanceCallback(napi_env env, const MetadataEntry &entry,
                                                     napi_value interface) {
    if (napi_util::is_undefined(env, interface) || napi_util::is_null(env, interface)) {
        return;
    }

    JEnv jEnv;
    auto className = GetJniClassName(entry);
    auto clazz = jEnv.FindClass(className);
    if (clazz == nullptr) {
        return;
    }

    napi_value hasInstance;
    napi_value symbol;
    napi_value global;
    napi_get_global(env, &global);
    napi_get_named_property(env, global, "Symbol", &symbol);
    napi_get_named_property(env, symbol, "hasInstance", &hasInstance);
    napi_value method;
    napi_create_function(env, "hasInstance", NAPI_AUTO_LENGTH, SymbolHasInstanceCallback, clazz,
                         &method);

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
    napi_define_properties(env, interface, 1, &desc);
}

napi_value MetadataNode::SymbolHasInstanceCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS();
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
    napi_get_boolean(env, isInstanceOf, &result);

    return result;

}

std::string MetadataNode::GetJniClassName(const MetadataEntry &entry) {
    std::stack<string> s;
    MetadataTreeNode *node = entry.treeNode;
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
    napi_value packageObj;
    napi_create_object(env, &packageObj);

    auto ptrChildren = this->m_treeNode->children;

    if (ptrChildren != nullptr) {
        const auto &children = *ptrChildren;

        auto lastChildName = "";

        for (auto childNode: children) {

            auto *info = new PackageGetterMethodData();
            info->utf8name = childNode->name.c_str();
            info->node = this;
            info->value = nullptr;

            if (strcmp(info->utf8name, lastChildName) == 0) {
                continue;
            }
            lastChildName = info->utf8name;
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
                napi_create_function(env, methodName.c_str(), methodName.size(), MethodCallback,
                                     callbackData, &method);

                napi_set_named_property(env, prototype, methodName.c_str(), method);
                lastMethodName = methodName;
                collectedExtensionMethods.emplace(methodName, callbackData);

            }
        }

        callbackData->candidates.push_back(std::move(entry));
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
                napi_create_function(env, methodName.c_str(), methodName.size(), MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, prototype, methodName.c_str(), method);
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
    }

    auto instanceFieldCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (auto i = 0; i < instanceFieldCount; i++) {
        auto entry = MetadataReader::ReadInstanceFieldEntry(&curPtr);
        auto &fieldName = entry.getName();
        auto fieldInfo = new FieldCallbackData(entry);
        fieldInfo->metadata.declaringType = curType;
        napi_util::define_property(env, prototype, fieldName.c_str(), nullptr,
                                   FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                   fieldInfo);
    }

    auto kotlinPropertiesCount = *reinterpret_cast<uint16_t *>(curPtr);
    curPtr += sizeof(uint16_t);
    for (int i = 0; i < kotlinPropertiesCount; ++i) {
        uint32_t nameOffset = *reinterpret_cast<uint32_t *>(curPtr);
        auto propertyName = s_metadataReader.ReadName(nameOffset);
        curPtr += sizeof(uint32_t);

        auto hasGetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        std::string getterMethodName;
        if (hasGetter >= 1) {
            auto entry = MetadataReader::ReadInstanceMethodEntry(&curPtr);
            getterMethodName = entry.getName();
        }

        auto hasSetter = *reinterpret_cast<uint16_t *>(curPtr);
        curPtr += sizeof(uint16_t);

        std::string setterMethodName;
        if (hasSetter >= 1) {
            auto entry = MetadataReader::ReadInstanceMethodEntry(&curPtr);
            setterMethodName = entry.getName();
        }

        auto propertyInfo = new PropertyCallbackData(propertyName, getterMethodName,
                                                     setterMethodName);
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
            napi_create_function(env, methodName.c_str(), methodName.size(), MethodCallback,
                                 callbackData, &method);
            napi_set_named_property(env, constructor, methodName.c_str(), method);
            lastMethodName = methodName;
        }
        callbackData->candidates.push_back(std::move(entry));
    }

    napi_value extendMethod;
    napi_create_function(env, nullptr, 0, ExtendMethodCallback, this,
                         &extendMethod);
    napi_set_property(env, extendMethod, Constants::Get(env)->nameValue, Constants::Get(env)->extendValue);
    napi_set_property(env, constructor, Constants::Get(env)->extendValue, extendMethod);

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
    }


    napi_util::define_property_named(env, constructor, Constants::Get(env)->nullObjectValue, nullptr,
                               NullObjectAccessorGetterCallback, nullptr, this);


    std::string tname = s_metadataReader.ReadTypeName(treeNode);
    napi_set_property(env, constructor, Constants::Get(env)->privateTypeNameValue,
                            ArgConverter::convertToJsString(env, tname));

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
                napi_create_function(env, entry.name.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, proto, entry.name.c_str(), method);

                lastMethodName = entry.name;
            }
            callbackData->candidates.push_back(std::move(entry));
        } else if (chKind == 'F') {
            entry.type = NodeType::Field;
            auto *fieldInfo = new FieldCallbackData(entry);
            napi_util::define_property(env, proto, entry.name.c_str(), nullptr,
                                       FieldAccessorGetterCallback, FieldAccessorSetterCallback,
                                       fieldInfo);
        }
    }
    return instanceMethodData;
}

void MetadataNode::SetClassAccessor(napi_env env, napi_value constructor) {
    napi_util::define_property_named(env, constructor, Constants::Get(env)->classValue, nullptr,
                               ClassAccessorGetterCallback, nullptr, nullptr);
}

napi_value MetadataNode::ClassAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    try {
        napi_value name;
        napi_get_property(env, jsThis, Constants::Get(env)->privateTypeNameValue, &name);
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

    auto cache = GetMetadataNodeCache(env);
    auto itFound = cache->CtorFuncCache.find(treeNode);
    if (itFound != cache->CtorFuncCache.end()) {
        instanceMethodsCallbackData = itFound->second.instanceMethodCallbacks;
        if (itFound->second.constructorFunction == nullptr)
            return nullptr;
        return napi_util::get_ref_value(env, itFound->second.constructorFunction);
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
    napi_define_class(env, finalName.c_str(), NAPI_AUTO_LENGTH,
                      isInterface ? InterfaceConstructorCallback : ClassConstructorCallback,
                      node, 0, nullptr, &constructor);

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
        for (auto curChild: children) {
            napi_util::define_property(env, constructor, curChild->name.c_str(), nullptr,
                                       SetInnerTypeCallback, nullptr, curChild);
        }
    }
}

napi_value MetadataNode::SetInnerTypeCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    try {
        auto curChild = reinterpret_cast<MetadataTreeNode *>(data);
        auto childNode = GetOrCreateInternal(curChild);
        auto cache = GetMetadataNodeCache(env);
        auto itFound = cache->CtorFuncCache.find(curChild);
        if (itFound != cache->CtorFuncCache.end()) {
            return napi_util::get_ref_value(env, itFound->second.constructorFunction);
        }
        napi_value constructor = childNode->GetConstructorFunction(env);
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
        napi_has_own_property(env, jsThis, Constants::Get(env)->nullNodeNameValue, &value);

        if (!value) {
            auto node = reinterpret_cast<MetadataNode *>(data);
            napi_value external;
            napi_create_external(env, node, nullptr, nullptr, &external);
            napi_set_property(env, jsThis, Constants::Get(env)->nullNodeNameValue, external);

            napi_util::napi_set_function(env,
                                         jsThis,
                                         "valueOf",  MetadataNode::NullValueOfCallback);
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
    napi_value nullValue;
    napi_get_null(env, &nullValue);
    return nullValue;
}

napi_value MetadataNode::FieldAccessorGetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    try {
        auto fieldData = reinterpret_cast<FieldCallbackData *>(data);
        auto &fieldMetadata = fieldData->metadata;

        if (fieldMetadata.getDeclaringType().empty()) {
            return UNDEFINED;
        }

        if (!fieldMetadata.isStatic) {
            napi_value constructor;
            napi_value prototype;
            napi_get_property(env, jsThis, Constants::Get(env)->constructorValue, &constructor);
            if (!napi_util::is_null_or_undefined(env, constructor)) {
                napi_get_property(env, constructor, Constants::Get(env)->prototypeValue, &prototype);
                bool isHolder;
                napi_strict_equals(env, prototype, jsThis, &isHolder);
                if (isHolder) {
                    return UNDEFINED;
                }
            }
        }

        return CallbackHandlers::GetJavaField(env, jsThis, fieldData);

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

napi_value MetadataNode::FieldAccessorSetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1);

    try {
        auto fieldData = reinterpret_cast<FieldCallbackData *>(data);
        auto &fieldMetadata = fieldData->metadata;


        if (!fieldMetadata.isStatic) {
            napi_value constructor;
            napi_value prototype;
            napi_get_named_property(env, jsThis, "constructor", &constructor);
            if (!napi_util::is_null_or_undefined(env, constructor)) {
                napi_get_named_property(env, constructor, "prototype", &prototype);
                bool isHolder;
                napi_strict_equals(env, prototype, jsThis, &isHolder);
                if (isHolder) {
                    return UNDEFINED;
                }
            }
        }

        if (fieldMetadata.getIsFinal()) {
            stringstream ss;
            ss << "You are trying to set \"" << fieldMetadata.getName()
               << "\" which is a final field! Final fields can only be read.";
            string exceptionMessage = ss.str();

            throw NativeScriptException(exceptionMessage);
        } else {
            CallbackHandlers::SetJavaField(env, jsThis, argv[0], fieldData);
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

        if (propertyCallbackData->getterMethodName.empty()) {
            return nullptr;
        }

        napi_value getter;
        napi_get_named_property(env, jsThis, propertyCallbackData->getterMethodName.c_str(),
                                &getter);

        napi_value result;
        napi_call_function(env, jsThis, getter, 0, nullptr, &result);
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

napi_value MetadataNode::PropertyAccessorSetterCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    try {
        auto propertyCallbackData = reinterpret_cast<PropertyCallbackData *>(data);

        if (propertyCallbackData->setterMethodName.empty()) {
            return nullptr;
        }

        napi_value setter;
        napi_get_named_property(env, jsThis, propertyCallbackData->setterMethodName.c_str(),
                                &setter);

        napi_value result;
        napi_call_function(env, jsThis, setter, 1, &argv[0], &result);

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

napi_value MetadataNode::ExtendMethodCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS()

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
                napi_get_value_bool(env, argv[2], &isTypeScriptExtend);
            };
        }

        auto node = reinterpret_cast<MetadataNode *>(data);

        if (hasDot) {
            extendName = argv[0];
            implementationObject = argv[1];
        } else {
            bool validExtend = GetExtendLocation(env, extendLocation, isTypeScriptExtend);
            napi_create_string_utf8(env, "", 0, &extendName);
            auto validArgs = ValidateExtendArguments(env, argc, argv.data(), validExtend,
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
            return napi_util::get_ref_value(env, cachedData.extendedCtorFunction);
        }

        napi_value implementationObjectName;
        napi_get_property(env, implementationObject, Constants::Get(env)->classImplementationObjectValue,
                                &implementationObjectName);

        if (napi_util::is_null_or_undefined(env, implementationObjectName)) {
            napi_set_property(env, implementationObject, Constants::Get(env)->classImplementationObjectValue,
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
                                                        napi_util::make_ref(env,
                                                                            implementationObject),
                                                        fullClassName), 0, nullptr,
                          &extendFuncCtor);
        napi_value extendFuncPrototype = napi_util::get_prototype(env, extendFuncCtor);
        ObjectManager::MarkObject(env, extendFuncPrototype);

        napi_util::setPrototypeOf(env, implementationObject,
                                  napi_util::get_prototype(env, baseClassCtorFunction));

        napi_util::define_property_named(
                env, implementationObject, Constants::Get(env)->superValue, nullptr, SuperAccessorGetterCallback,
                nullptr, nullptr);

        napi_util::setPrototypeOf(env, extendFuncPrototype, implementationObject);

        napi_util::setPrototypeOf(env, extendFuncCtor, baseClassCtorFunction);

        SetClassAccessor(env, extendFuncCtor);

        napi_set_property(env, extendFuncCtor, Constants::Get(env)->privateTypeNameValue,
                                ArgConverter::convertToJsString(env, fullExtendedName));

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
        napi_get_property(env, jsThis, Constants::Get(env)->superValueValue, &superValue);

        if (napi_util::is_null_or_undefined(env, superValue)) {
            auto objectManager = Runtime::GetRuntime(env)->GetObjectManager();
            superValue = objectManager->GetEmptyObject();
            napi_delete_property(env, superValue,
                                 Constants::Get(env)->toStringValue, nullptr);
            napi_delete_property(env, superValue,
                                 Constants::Get(env)->valueOfValue, nullptr);
            ObjectManager::MarkSuperCall(env, superValue);

            napi_value superProto = napi_util::getPrototypeOf(env, napi_util::getPrototypeOf(env,
                                                                                             napi_util::getPrototypeOf(
                                                                                                     env,
                                                                                                     jsThis)));
            napi_util::setPrototypeOf(env, superValue, superProto);
            napi_set_property(env, jsThis,  Constants::Get(env)->superValueValue, superValue);
            objectManager->CloneLink(jsThis, superValue);
            auto node = GetInstanceMetadata(env, jsThis);
            SetInstanceMetadata(env, superValue, node);
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
    NAPI_CALLBACK_BEGIN_VARGS()

    try {
        MetadataEntry *entry = nullptr;

        auto callbackData = reinterpret_cast<MethodCallbackData *>(data);
        auto initialCallbackData = reinterpret_cast<MethodCallbackData *>(data);

        string *className;
        auto &first = callbackData->candidates.front();
        auto &methodName = first.getName();

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

        auto isSuper = false;

        if (!first.isStatic) {
            napi_value superValue;
            napi_get_property(env, jsThis,  Constants::Get(env)->privateCallSuperValue, &superValue);
            isSuper = napi_util::get_bool(env, superValue);
        }

        if (argc == 0 && methodName == "valueOf") {
            return jsThis;
        } else {
            bool isFromInterface = initialCallbackData->node->IsNodeTypeInterface();
            return CallbackHandlers::CallJavaMethod(env, jsThis, *className, methodName, entry,
                                                    isFromInterface, first.isStatic, isSuper, info);
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
                napi_create_function(env, methodName.c_str(), NAPI_AUTO_LENGTH, MethodCallback,
                                     callbackData, &method);
                napi_set_named_property(env, proto, methodName.c_str(), method);
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
}


string MetadataNode::TNS_PREFIX = "com/tns/gen/";
MetadataReader MetadataNode::s_metadataReader;
robin_hood::unordered_map<std::string, MetadataNode *> MetadataNode::s_name2NodeCache;
robin_hood::unordered_map<std::string, MetadataTreeNode *> MetadataNode::s_name2TreeNodeCache;
robin_hood::unordered_map<MetadataTreeNode *, MetadataNode *> MetadataNode::s_treeNode2NodeCache;
robin_hood::unordered_map<napi_env, MetadataNode::MetadataNodeCache *> MetadataNode::s_metadata_node_cache;
robin_hood::unordered_map<napi_env, napi_ref> MetadataNode::s_arrayObjects;

// TODO
bool MetadataNode::s_profilerEnabled = false;
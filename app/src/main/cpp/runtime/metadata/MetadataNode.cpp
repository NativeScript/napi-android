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
#include "Runtime.h"
#include "ArgConverter.h"


using namespace std;

void MetadataNode::Init(napi_env env) {
    auto cache = GetMetadataNodeCache(env);
    cache->MetadataKey = java_bridge::make_ref(env, ArgConverter::ConvertToV8String(env,
                                                                                    "tns::MetadataKey"));
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


MetadataNode::MetadataNode(MetadataTreeNode *treeNode) :
        m_treeNode(treeNode) {
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
                     :
                     impTypeName;
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

napi_value MetadataNode::CreateJSWrapper(napi_env env, ns::ObjectManager *objectManager) {
    return nullptr;
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

void MetadataNode::BuildMetadata(const std::string &filesPath) {
    auto start = std::chrono::high_resolution_clock::now();

    std::string baseDir = filesPath + "/metadata";
    DIR *dir = opendir(baseDir.c_str());

    if (!dir) {
        std::stringstream ss;
        ss << "metadata folder couldn't be opened! (Error: " << errno << ") ";
        if (errno == ENOENT || errno == EACCES) {
        } else {
           throw NativeScriptException(ss.str());
        }
    }
    closedir(dir);

    int lenNodes, lenNames, lenValues;
    auto nodes = File::ReadFile(baseDir + "/treeNodeStream.dat", lenNodes);
    assert((lenNodes % sizeof(MetadataTreeNodeRawData)) == 0);

    const int _512KB = 524288;
    auto names = File::ReadFile(baseDir + "/treeStringsStream.dat", lenNames, _512KB);
    auto values = File::ReadFile(baseDir + "/treeValueStream.dat", lenValues, _512KB);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    DEBUG_WRITE("lenNodes=%d, lenNames=%d, lenValues=%d", lenNodes, lenNames, lenValues);
    DEBUG_WRITE("time=%ld", duration);

    BuildMetadata(lenNodes, reinterpret_cast<uint8_t *>(nodes.get()), lenNames,
                  reinterpret_cast<uint8_t *>(names.get()), lenValues,
                  reinterpret_cast<uint8_t *>(values.get()));
}

void MetadataNode::BuildMetadata(uint32_t nodesLength, uint8_t *nodeData, uint32_t nameLength,
                                 uint8_t *nameData, uint32_t valueLength, uint8_t *valueData) {
    s_metadataReader = MetadataReader(nodesLength, nodeData, nameLength, nameData, valueLength,
                                      valueData, CallbackHandlers::GetTypeMetadata);
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
                    "with", "yield"
        };

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


typedef struct PackageGetterMethodInfo {
    const char *utf8name;
    MetadataNode *data;
    napi_ref value;
} PackageGetterMethodInfo;

napi_value MetadataNode::PackageGetter(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_value thisArg;
    void *data;

    napi_get_cb_info(env, info, &argc, args, &thisArg, &data);

    auto *methodInfo = static_cast<PackageGetterMethodInfo *>(data);

    if (methodInfo->value != nullptr) {
        return java_bridge::get_ref_value(env, methodInfo->value);
    }

    auto node = methodInfo->data;

    uint8_t nodeType = s_metadataReader.GetNodeType(node->m_treeNode);

    auto child = GetChildMetadataForPackage(node, methodInfo->utf8name);
    auto foundChild = child.treeNode != nullptr;

    if (foundChild) {
        auto childNode = MetadataNode::GetOrCreateInternal(child.treeNode);
        methodInfo->value = java_bridge::make_ref(env, childNode->CreateWrapper(env));

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

    return java_bridge::get_ref_value(env, methodInfo->value);
}


napi_value MetadataNode::CreatePackageObject(napi_env env) {
    napi_value packageObj;
    napi_create_object(env, &packageObj);

    auto ptrChildren = this->m_treeNode->children;

    if (ptrChildren != nullptr) {
        const auto &children = *ptrChildren;
        for (auto childNode: children) {

            auto *info = (PackageGetterMethodInfo *) malloc(sizeof(PackageGetterMethodInfo));
            info->utf8name = childNode->name.c_str();
            info->data = this;
            info->value = nullptr;

            napi_property_descriptor descriptor{
                    childNode->name.c_str(),
                    nullptr,
                    nullptr,
                    PackageGetter,
                    nullptr,
                    nullptr,
                    napi_default,
                    info
            };
            napi_define_properties(env, packageObj, 1, &descriptor);
        }
    }

    return packageObj;
}

napi_value MetadataNode::ConstructorFunction(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0);
    return jsThis;
}

napi_value MetadataNode::GetConstructorFunction(napi_env env) {
    auto cache = GetMetadataNodeCache(env);
    auto itFound = cache->CtorFuncCache.find(m_treeNode);
    if (itFound != cache->CtorFuncCache.end()) {
        return java_bridge::get_ref_value(env, itFound->second);
    }

    auto node = GetOrCreateInternal(m_treeNode);

    JEnv jEnv;

    auto currentNode = m_treeNode;
    std::string finalName(currentNode->name);
    while (currentNode->parent) {
        if (!currentNode->parent->name.empty()) {
            finalName = currentNode->parent->name + "." + finalName;
        }
        currentNode = currentNode->parent;
    }

    // TODO
    // 1. Create the class and get the constructor
    // 2. Define static fields and methods on the constructor
    // 3. Find and init this class's super class and inherit from it using napi_inherit
    // 4. Define any instance functions and fields on the class
    // 5. Bind the treeNode data to class constructor with napi_wrap
    // 6. Cache the class for future access
    // 7. Keep everything on demand, define and init properties and their relavant metadata when accessed.

    // 1.

    napi_value constructor;
    napi_define_class(env, finalName.c_str(), NAPI_AUTO_LENGTH, MetadataNode::ConstructorFunction,
                      node, 0, nullptr, &constructor);

    // 2. Define static fields and methods on the class

    return constructor;
}

MetadataReader *MetadataNode::getMetadataReader() {
    return &MetadataNode::s_metadataReader;
}

MetadataReader MetadataNode::s_metadataReader;
robin_hood::unordered_map<std::string, MetadataNode *> MetadataNode::s_name2NodeCache;
robin_hood::unordered_map<std::string, MetadataTreeNode *> MetadataNode::s_name2TreeNodeCache;
robin_hood::unordered_map<MetadataTreeNode *, MetadataNode *> MetadataNode::s_treeNode2NodeCache;
robin_hood::unordered_map<napi_env, MetadataNode::MetadataNodeCache *> MetadataNode::s_metadata_node_cache;
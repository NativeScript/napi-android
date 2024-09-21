//
// Created by Ammar Ahmed on 20/09/2024.
//
#include <string>
#include <sstream>
#include <cctype>
#include <dirent.h>
#include <set>
#include <errno.h>
#include <unistd.h>
#include "NativeScriptException.h"
#include "MetadataNode.h"
#include "CallbackHandlers.h"
#include "NativeScriptAssert.h"
#include "File.h"
#include "Runtime.h"

using namespace std;

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

            napi_ref ref = node->CreateWrapper(env);
            napi_value packageObj;
            napi_get_reference_value(env, ref, &packageObj);

            string nameSpace = node->m_treeNode->name;
            // if the namespaces matches a javascript keyword, prefix it with $ to avoid TypeScript and JavaScript errors
            if (IsJavascriptKeyword(nameSpace)) {
                nameSpace = "$" + nameSpace;
            }
            napi_set_named_property(env, global, nameSpace.c_str(), packageObj);
        }
    }
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
//            throw NativeScriptException(ss.str());
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
    MetadataEntry child;

    assert(node->m_treeNode->children != nullptr);

    const auto &children = *node->m_treeNode->children;

    for (auto treeNodeChild: children) {
        if (propName == treeNodeChild->name.c_str()) {
            child.name = propName;
            child.treeNode = treeNodeChild;

            uint8_t childNodeType = s_metadataReader.GetNodeType(treeNodeChild);
            if (s_metadataReader.IsNodeTypeInterface(childNodeType)) {
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

napi_ref MetadataNode::CreateWrapper(napi_env env) {
    napi_ref ref;
    uint8_t nodeType = s_metadataReader.GetNodeType(m_treeNode);
    bool isClass = s_metadataReader.IsNodeTypeClass(nodeType),
            isInterface = s_metadataReader.IsNodeTypeInterface(nodeType);
    napi_status status;
    napi_handle_scope scope;
    napi_open_handle_scope(env, &scope);

    if (isClass || isInterface) {
//        obj = GetConstructorFunction(env);
    } else if (s_metadataReader.IsNodeTypePackage(nodeType)) {
        napi_value obj = CreatePackageObject(env);
        napi_create_reference(env, obj, 1, &ref);
        napi_close_handle_scope(env, scope);
    } else {
        std::stringstream ss;
        ss << "(InternalError): Can't create proxy for this type=" << static_cast<int>(nodeType);
//        throw NativeScriptException(ss.str());
    }


    return ref;
}


typedef struct PackageGetterMethodInfo {
    const char *utf8name;
    MetadataNode *data;
    napi_ref ref;
} PackageGetterMethodInfo;

napi_value MetadataNode::PackageGetter(napi_env env, napi_callback_info info) {

    size_t argc = 1;
    napi_value args[1];
    napi_value thisArg;
    void *data;

    napi_get_cb_info(env, info, &argc, args, &thisArg, &data);

    auto *methodInfo = static_cast<PackageGetterMethodInfo *>(data);

    if (methodInfo->ref != nullptr) {
        napi_value result;
        napi_get_reference_value(env,  methodInfo->ref, &result);
        return result;
    }

    auto node = methodInfo->data;

    uint8_t nodeType = s_metadataReader.GetNodeType(node->m_treeNode);

    auto child = GetChildMetadataForPackage(node, methodInfo->utf8name);
    auto foundChild = child.treeNode != nullptr;

    if (foundChild) {
        auto childNode = MetadataNode::GetOrCreateInternal(child.treeNode);
        methodInfo->ref = childNode->CreateWrapper(env);

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

    napi_value result;
    napi_get_reference_value(env,  methodInfo->ref, &result);
    return result;
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
            info->ref = nullptr;

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



napi_value MetadataNode::GetConstructorFunction(napi_env env) {
//    auto node = GetOrCreateInternal(m_treeNode);
//
//    JEnv jEnv;
//
//    auto currentNode = m_treeNode;
//    std::string finalName(currentNode->name);
//    while (currentNode->parent) {
//        if (!currentNode->parent->name.empty()) {
//            finalName = currentNode->parent->name + "." + finalName;
//        }
//        currentNode = currentNode->parent;
//    }

    return nullptr;
}

MetadataReader MetadataNode::s_metadataReader;
robin_hood::unordered_map<std::string, MetadataNode *> MetadataNode::s_name2NodeCache;
robin_hood::unordered_map<std::string, MetadataTreeNode *> MetadataNode::s_name2TreeNodeCache;
robin_hood::unordered_map<MetadataTreeNode *, MetadataNode *> MetadataNode::s_treeNode2NodeCache;
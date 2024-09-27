#pragma once

#include <string>
#include "MetadataTreeNode.h"
#include "MetadataEntry.h"
#include "robin_hood.h"
#include "MetadataReader.h"
#include "Runtime.h"


using namespace ns;

class MetadataNode {
public:
    static void Init(napi_env env);
    static void BuildMetadata(const std::string& filesPath);
    static void CreateTopLevelNamespaces(napi_env env);
    napi_value CreateJSWrapper(napi_env env, ns::ObjectManager* objectManager);
    static MetadataNode* GetOrCreate(const std::string& className);

    static MetadataReader* getMetadataReader();

    std::string GetName();
private:
    struct MethodCallbackData;

    struct ExtendedClassCallbackData;

    struct ExtendedClassCacheData;

    struct TypeMetadata;

    struct MetadataNodeCache;

    static void BuildMetadata(uint32_t nodesLength, uint8_t* nodeData, uint32_t nameLength, uint8_t* nameData, uint32_t valueLength, uint8_t* valueData);
    static MetadataNode* GetOrCreateInternal(MetadataTreeNode* treeNode);

    static MetadataNodeCache* GetMetadataNodeCache(napi_env env);

    explicit MetadataNode(MetadataTreeNode* treeNode);

    napi_value CreateWrapper(napi_env env);
    napi_value GetConstructorFunction(napi_env env);
    napi_value CreatePackageObject(napi_env env);
    static napi_value ConstructorFunction(napi_env env, napi_callback_info info);

    static napi_value PackageGetter(napi_env env, napi_callback_info info);
    static MetadataEntry GetChildMetadataForPackage(MetadataNode* node, const char * propName);

    static MetadataTreeNode* GetOrCreateTreeNodeByName(const std::string& className);

//    static MetadataEntry GetChildMetadataForPackage(MetadataNode* node, const std::string& propName);
    MetadataTreeNode* m_treeNode;
    std::string m_name;
    std::string m_implType;
    bool m_isArray;

    static bool IsJavascriptKeyword(std::string word);

    static std::string TNS_PREFIX;
    static MetadataReader s_metadataReader;
    static robin_hood::unordered_map<std::string, MetadataNode*> s_name2NodeCache;
    static robin_hood::unordered_map<std::string, MetadataTreeNode*> s_name2TreeNodeCache;
    static robin_hood::unordered_map<MetadataTreeNode*, MetadataNode*> s_treeNode2NodeCache;
    static robin_hood::unordered_map<napi_env, MetadataNodeCache*> s_metadata_node_cache;

    struct MethodCallbackData {
        MethodCallbackData()
                :
                node(nullptr), parent(nullptr), isSuper(false) {
        }

        MethodCallbackData(MetadataNode* _node)
                :
                node(_node), parent(nullptr), isSuper(false) {
        }

        std::vector<MetadataEntry> candidates;
        MetadataNode* node;
        MethodCallbackData* parent;
        bool isSuper;
    };

    struct ExtendedClassCacheData {
        ExtendedClassCacheData()
                :
                extendedCtorFunction(nullptr), node(nullptr) {
        }
        ExtendedClassCacheData(napi_ref extCtorFunc, const std::string& _extendedName, MetadataNode* _node)
                :
                extendedName(_extendedName), node(_node) {
            extendedCtorFunction = extCtorFunc;
        }
        napi_ref extendedCtorFunction;
        std::string extendedName;
        MetadataNode* node;
    };

    struct PropertyCallbackData {
        PropertyCallbackData(std::string _propertyName, std::string _getterMethodName, std::string _setterMethodName)
                :
                propertyName(_propertyName), getterMethodName(_getterMethodName), setterMethodName(_setterMethodName) {

        }
        std::string propertyName;
        std::string getterMethodName;
        std::string setterMethodName;
    };

    struct ExtendedClassCallbackData {
        ExtendedClassCallbackData(MetadataNode* _node, const std::string& _extendedName, napi_ref _implementationObject, std::string _fullClassName)
                :
                node(_node), extendedName(_extendedName), fullClassName(_fullClassName) {
            implementationObject = _implementationObject;
        }

        MetadataNode* node;
        std::string extendedName;
        napi_ref implementationObject;

        std::string fullClassName;
    };

    struct TypeMetadata {
        TypeMetadata(const std::string& _name)
                :
                name(_name) {
        }

        std::string name;
    };

    struct MetadataNodeCache {
        napi_ref MetadataKey;

        robin_hood::unordered_map<MetadataTreeNode*, napi_ref> CtorFuncCache;

        robin_hood::unordered_map<std::string, MetadataNode::ExtendedClassCacheData> ExtendedCtorFuncCache;
    };

};

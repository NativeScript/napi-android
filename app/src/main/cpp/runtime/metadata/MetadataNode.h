#pragma once

#include <string>
#include "MetadataTreeNode.h"
#include "MetadataEntry.h"
#include "robin_hood.h"
#include "MetadataReader.h"


#ifndef TESTAPPNAPI_METADATANODE_H
#define TESTAPPNAPI_METADATANODE_H

using namespace ns;

class MetadataNode {
public:
    static void BuildMetadata(const std::string& filesPath);
    static void CreateTopLevelNamespaces(napi_env env);
//    std::string GetName();
private:
    static void BuildMetadata(uint32_t nodesLength, uint8_t* nodeData, uint32_t nameLength, uint8_t* nameData, uint32_t valueLength, uint8_t* valueData);
    static MetadataNode* GetOrCreateInternal(MetadataTreeNode* treeNode);

    explicit MetadataNode(MetadataTreeNode* treeNode);

    napi_ref CreateWrapper(napi_env env);
    napi_value GetConstructorFunction(napi_env env);
    napi_value CreatePackageObject(napi_env env);
    static napi_value PackageGetter(napi_env env, napi_callback_info info);
    static MetadataEntry GetChildMetadataForPackage(MetadataNode* node, const char * propName);
//    static MetadataNode* GetOrCreateInternal(MetadataTreeNode* treeNode);

//    static MetadataTreeNode* GetOrCreateTreeNodeByName(const std::string& className);

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
//    static robin_hood::unordered_map<napi_env, MetadataNodeCache*> s_metadata_node_cache;
};


#endif //TESTAPPNAPI_METADATANODE_H

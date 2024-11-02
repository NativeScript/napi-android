#ifndef METADATA_NODE_H
#define METADATA_NODE_H

#include <string>
#include "MetadataTreeNode.h"
#include "MetadataEntry.h"
#include "robin_hood.h"
#include "MetadataReader.h"
#include "Runtime.h"

using namespace tns;

class MetadataNode {
public:
    static void Init(napi_env env);

    static void BuildMetadata(const std::string &filesPath);

    static void CreateTopLevelNamespaces(napi_env env);

    napi_value CreateWrapper(napi_env env);

    napi_value CreateJSWrapper(napi_env env, tns::ObjectManager *objectManager);

    napi_value CreateArrayWrapper(napi_env env);

    static MetadataNode *GetOrCreate(const std::string &className);

    static MetadataReader *getMetadataReader();

    static napi_value GetImplementationObject(napi_env env, napi_value object);

    static MetadataNode* GetInstanceMetadata(napi_env env, napi_value object);

    static MetadataNode* GetNodeFromHandle(napi_env env, napi_value value);

    static string GetTypeMetadataName(napi_env env, napi_value value);

    static napi_value CreateExtendedJSWrapper(napi_env env, ObjectManager *objectManager,
                                       const std::string &proxyClassName);

    std::string GetName();

    static void onDisposeEnv(napi_env env);

private:
    struct CtorCacheData;
    struct PackageGetterMethodData;
    struct MethodCallbackData;
    struct ExtendedClassCallbackData;
    struct ExtendedClassCacheData;
    struct MetadataNodeCache;

    static string CreateFullClassName(const std::string& className, const std::string& extendNameAndLocation);

    static napi_value CreateArrayObjectConstructor(napi_env env);

    static void SetInstanceMetadata(napi_env env, napi_value object, MetadataNode* node);


    static bool
    CheckClassHierarchy(JEnv &env, jclass currentClass, MetadataTreeNode *currentTreeNode,
                        MetadataTreeNode *baseTreeNode,
                        std::vector<MetadataTreeNode *> &skippedBaseTypes);

    static MetadataNode *GetOrCreateInternal(MetadataTreeNode *treeNode);

    static MetadataNodeCache *GetMetadataNodeCache(napi_env env);

    explicit MetadataNode(MetadataTreeNode *treeNode);

    void SetMissingBaseMethods(
            napi_env env, const std::vector<MetadataTreeNode *> &skippedBaseTypes,
            const std::vector<MethodCallbackData *> &instanceMethodData,
            napi_value ctor);




    napi_value GetConstructorFunction(napi_env env);

    napi_value GetConstructorFunctionInternal(napi_env env, MetadataTreeNode *treeNode,
                                              std::vector<MethodCallbackData *> instanceMethodsCallbackData);

    napi_value CreatePackageObject(napi_env env);


    static bool IsValidExtendName(napi_env env, napi_value name);
    static bool GetExtendLocation(napi_env env, std::string& extendLocation, bool isTypeScriptExtend);
    static ExtendedClassCacheData GetCachedExtendedClassData(napi_env env, const std::string& proxyClassName);
    static std::string GetJniClassName(const MetadataEntry &entry);


    static void SetClassAccessor(napi_env env, napi_value constructor);

    static MetadataEntry GetChildMetadataForPackage(MetadataNode *node, const char *propName);

    static MetadataTreeNode *GetOrCreateTreeNodeByName(const std::string &className);

    bool IsNodeTypeInterface();

    std::vector<MetadataNode::MethodCallbackData *> SetClassMembersFromStaticMetadata(
            napi_env env, napi_value constructor,
            std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
            const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
            MetadataTreeNode *treeNode);

    std::vector<MetadataNode::MethodCallbackData *> SetInstanceMembersFromRuntimeMetadata(
            napi_env env, napi_value constructor,
            std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
            const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
            MetadataTreeNode *treeNode);


    inline static MethodCallbackData *tryGetExtensionMethodCallbackData(
            const robin_hood::unordered_map<std::string, MethodCallbackData *> &collectedMethodCallbackData,
            const std::string &lookupName);

    std::vector<MetadataNode::MethodCallbackData *> SetClassMembers(
            napi_env env, napi_value constructor,
            std::vector<MethodCallbackData *> &instanceMethodsCallbackData,
            const std::vector<MethodCallbackData *> &baseInstanceMethodsCallbackData,
            MetadataTreeNode *treeNode);


    static napi_value NullObjectAccessorGetterCallback(napi_env env, napi_callback_info info);

    static napi_value FieldAccessorGetterCallback(napi_env env, napi_callback_info info);

    static napi_value FieldAccessorSetterCallback(napi_env env, napi_callback_info info);

    static napi_value ArraySetterCallback(napi_env env, napi_callback_info info);

    static napi_value ArrayGetterCallback(napi_env env, napi_callback_info info);

    static napi_value ArrayGetAllValuesCallback(napi_env env, napi_callback_info info);

    static napi_value ArrayLengthCallback(napi_env env, napi_callback_info info);

    static napi_value PropertyAccessorGetterCallback(napi_env env, napi_callback_info info);

    static napi_value PropertyAccessorSetterCallback(napi_env env, napi_callback_info info);

    static napi_value ExtendMethodCallback(napi_env env, napi_callback_info info);

    static napi_value MethodCallback(napi_env env, napi_callback_info info);

    static napi_value ClassAccessorGetterCallback(napi_env env, napi_callback_info info);

    static napi_value PackageGetterCallback(napi_env env, napi_callback_info info);

    static napi_value ExtendedClassConstructorCallback(napi_env env, napi_callback_info info);

    static napi_value InterfaceConstructorCallback(napi_env env, napi_callback_info info);

    static napi_value ClassConstructorCallback(napi_env env, napi_callback_info info);

    static void SetInnerTypes(napi_env env, napi_value constructor, MetadataTreeNode *treeNode);

    static napi_value SetInnerTypeCallback(napi_env env, napi_callback_info info);

    static napi_value NullValueOfCallback(napi_env env, napi_callback_info info);


    static void RegisterSymbolHasInstanceCallback(napi_env env, const MetadataEntry &entry, napi_value interface);

    static napi_value SymbolHasInstanceCallback(napi_env env, napi_callback_info info);

    static napi_value SuperAccessorGetterCallback(napi_env env, napi_callback_info info);

    static bool ValidateExtendArguments(napi_env env, size_t argc, napi_value * argv, bool extendLocationFound, string &extendLocation, napi_value* extendName, napi_value* implementationObject, bool isTypeScriptExtend);

    MetadataTreeNode *m_treeNode;

    std::string m_name;
    std::string m_implType;
    bool m_isArray;

    static bool IsJavascriptKeyword(const std::string &word);

    static std::string TNS_PREFIX;
    static MetadataReader s_metadataReader;

    static robin_hood::unordered_map<std::string, MetadataNode *> s_name2NodeCache;
    static robin_hood::unordered_map<std::string, MetadataTreeNode *> s_name2TreeNodeCache;
    static robin_hood::unordered_map<MetadataTreeNode *, MetadataNode *> s_treeNode2NodeCache;
    static robin_hood::unordered_map<napi_env, MetadataNodeCache *> s_metadata_node_cache;
    static robin_hood::unordered_map<napi_env, napi_ref> s_arrayObjects;

    struct CtorCacheData {
        CtorCacheData(napi_ref _constructorFunction,
                      std::vector<MethodCallbackData *> _instanceMethodCallbacks)
                :
                constructorFunction(_constructorFunction),
                instanceMethodCallbacks(_instanceMethodCallbacks) {
        }

        napi_ref constructorFunction;
        std::vector<MethodCallbackData *> instanceMethodCallbacks;
    };

    struct MethodCallbackData {
        MethodCallbackData()
                :
                node(nullptr), parent(nullptr), isSuper(false) {
        }

        MethodCallbackData(MetadataNode *_node)
                :
                node(_node), parent(nullptr), isSuper(false) {
        }

        std::vector<MetadataEntry> candidates;
        MetadataNode *node;
        MethodCallbackData *parent;
        bool isSuper;
    };

    struct PackageGetterMethodData {
        PackageGetterMethodData() : utf8name(nullptr), node(nullptr), value(nullptr) {}

        PackageGetterMethodData(const char *_utf8name, MetadataNode *_node, napi_ref _value)
                : utf8name(_utf8name), node(_node), value(_value) {}

        const char *utf8name;
        MetadataNode *node;
        napi_ref value;
    };

    struct ExtendedClassCacheData {
        ExtendedClassCacheData()
                :
                extendedCtorFunction(nullptr), node(nullptr) {
        }

        ExtendedClassCacheData(napi_ref extCtorFunc, const std::string &_extendedName,
                               MetadataNode *_node)
                :
                extendedName(_extendedName), node(_node) {
            extendedCtorFunction = extCtorFunc;
        }

        napi_ref extendedCtorFunction;
        std::string extendedName;
        MetadataNode *node;
    };

    struct PropertyCallbackData {
        PropertyCallbackData(std::string _propertyName, std::string _getterMethodName,
                             std::string _setterMethodName)
                :
                propertyName(_propertyName), getterMethodName(_getterMethodName),
                setterMethodName(_setterMethodName) {

        }

        std::string propertyName;
        std::string getterMethodName;
        std::string setterMethodName;
    };

    struct ExtendedClassCallbackData {
        ExtendedClassCallbackData(MetadataNode *_node, const std::string &_extendedName,
                                  napi_ref _implementationObject, std::string _fullClassName)
                :
                node(_node), extendedName(_extendedName), fullClassName(_fullClassName) {
            implementationObject = _implementationObject;
        }

        MetadataNode *node;
        std::string extendedName;
        napi_ref implementationObject;

        std::string fullClassName;
    };

    struct MetadataNodeCache {
        napi_ref MetadataKey;

        robin_hood::unordered_map<MetadataTreeNode *, CtorCacheData> CtorFuncCache;

        robin_hood::unordered_map<std::string, MetadataNode::ExtendedClassCacheData> ExtendedCtorFuncCache;
    };

    static bool s_profilerEnabled;

};

#endif //METADATA_NODE_H
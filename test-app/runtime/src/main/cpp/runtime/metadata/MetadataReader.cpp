#include "MetadataReader.h"
#include "MetadataMethodInfo.h"
#include <android/log.h>
#include "Util.h"
#include <sstream>

using namespace std;
using namespace tns;

MetadataReader::MetadataReader() : m_root(nullptr), m_nodesLength(0), m_nameLength(0),
                                   m_valueLength(0),
                                   m_nodeData(nullptr), m_nameData(nullptr), m_valueData(nullptr),
                                   m_getTypeMetadataCallback(nullptr) {}

MetadataReader::MetadataReader(uint32_t nodesLength, uint8_t *nodeData, uint32_t nameLength,
                               uint8_t *nameData, uint32_t valueLength, uint8_t *valueData,
                               GetTypeMetadataCallback getTypeMetadataCallback)
        :
        m_nodesLength(nodesLength), m_nameLength(nameLength),
        m_valueLength(valueLength), m_nodeData(nodeData), m_nameData(nameData),
        m_valueData(valueData),
        m_getTypeMetadataCallback(getTypeMetadataCallback) {
    m_root = BuildTree();
}



// helper debug function when need to convert a metadata node to its full name
//std::string toFullName(MetadataTreeNode* p) {
//    std::string final = p->name;
//    while((p = p->parent) && !p->name.empty()) {
//        final.insert(0,p->name + ".");
//    };
//    return final;
//}

MetadataTreeNode *MetadataReader::BuildTree() {
    MetadataTreeNodeRawData *rootNodeData = reinterpret_cast<MetadataTreeNodeRawData *>(m_nodeData);

    MetadataTreeNodeRawData *curNodeData = rootNodeData;

    int len = m_nodesLength / sizeof(MetadataTreeNodeRawData);

    m_v.resize(len + 1000);
    MetadataTreeNode *emptyNode = nullptr;
    fill(m_v.begin(), m_v.end(), emptyNode);

    for (int i = 0; i < len; i++) {
        MetadataTreeNode *node = GetNodeById(i);
        if (nullptr == node) {
            node = new MetadataTreeNode;
            node->name = ReadName(curNodeData->offsetName);
            node->offsetValue = curNodeData->offsetValue;
            m_v[i] = node;
        }

        uint16_t curNodeDataId = curNodeData - rootNodeData;

        if (curNodeDataId != curNodeData->firstChildId) {
            node->children = new vector<MetadataTreeNode *>;
            MetadataTreeNodeRawData *childNodeData = rootNodeData + curNodeData->firstChildId;
            while (true) {

                uint16_t childNodeDataId = childNodeData - rootNodeData;

                MetadataTreeNode *childNode;
                // node (and its next siblings) already visited, so we don't need to visit it again
                if (m_v[childNodeDataId] != emptyNode) {
                    childNode = m_v[childNodeDataId];
                    __android_log_print(ANDROID_LOG_ERROR, "TNS.error",
                                        "Consistency error in metadata. A child should never have been visited before its parent. Parent: %s Child: %s. Child metadata id: %u",
                                        node->name.c_str(), childNode->name.c_str(),
                                        childNodeDataId);
                    break;
                } else {
                    childNode = new MetadataTreeNode;
                    childNode->name = ReadName(childNodeData->offsetName);
                    childNode->offsetValue = childNodeData->offsetValue;
                }
                childNode->parent = node;

                node->children->push_back(childNode);

                m_v[childNodeDataId] = childNode;

                if (childNodeDataId == childNodeData->nextSiblingId) {
                    break;
                }

                childNodeData = rootNodeData + childNodeData->nextSiblingId;
            }
        }

        curNodeData++;
    }

    return GetNodeById(0);
}

MetadataTreeNode *MetadataReader::GetNodeById(uint16_t nodeId) {
    return m_v[nodeId];
}


string MetadataReader::ReadTypeName(MetadataTreeNode *treeNode) {
    string name;

    auto itFound = m_typeNameCache.find(treeNode);

    if (itFound != m_typeNameCache.end()) {
        name = itFound->second;
    } else {
        name = ReadTypeNameInternal(treeNode);

        m_typeNameCache.emplace(treeNode, name);
    }

    return name;
}

string MetadataReader::ReadTypeNameInternal(MetadataTreeNode *treeNode) {
    string name;

    uint8_t prevNodeType;

    while (treeNode->parent != nullptr) {
        int curNodeType = GetNodeType(treeNode);

        bool isArrayElement = treeNode->offsetValue > ARRAY_OFFSET;

        if (isArrayElement) {
            uint16_t forwardNodeId = treeNode->offsetValue - ARRAY_OFFSET;
            MetadataTreeNode *forwardNode = GetNodeById(forwardNodeId);
            name = ReadTypeName(forwardNode);
            uint8_t forwardNodeType = GetNodeType(forwardNode);
            if (IsNodeTypeInterface(forwardNodeType) || IsNodeTypeClass(forwardNodeType)) {
                name = "L" + name + ";";
            }
        } else {
            if (!name.empty()) {
                if (!IsNodeTypeArray(curNodeType)) {
                    if ((IsNodeTypeClass(prevNodeType) || IsNodeTypeInterface(prevNodeType))
                        && (IsNodeTypeClass(curNodeType) || IsNodeTypeInterface(curNodeType))) {
                        name = "$" + name;
                    } else {
                        name = "/" + name;
                    }
                }
            }

            name = treeNode->name + name;

            prevNodeType = curNodeType;
        }

        treeNode = treeNode->parent;
    }

    return name;
}

uint8_t *MetadataReader::GetValueData() const {
    return m_valueData;
}

uint16_t MetadataReader::GetNodeId(MetadataTreeNode *treeNode) {
    auto itFound = find(m_v.begin(), m_v.end(), treeNode);
    assert(itFound != m_v.end());
    uint16_t nodeId = itFound - m_v.begin();

    return nodeId;
}

MetadataTreeNode *MetadataReader::GetRoot() const {
    return m_root;
}

uint8_t MetadataReader::GetNodeType(MetadataTreeNode *treeNode) {
    if (treeNode->type == MetadataTreeNode::INVALID_TYPE) {
        uint8_t nodeType;

        uint32_t offsetValue = treeNode->offsetValue;

        if (offsetValue == 0) {
            nodeType = MetadataTreeNode::PACKAGE;
        } else if ((0 < offsetValue) && (offsetValue < ARRAY_OFFSET)) {
            nodeType = *(m_valueData + offsetValue);
        } else if (offsetValue == ARRAY_OFFSET) {
            nodeType = MetadataTreeNode::ARRAY;
        } else {
            uint16_t nodeId = offsetValue - ARRAY_OFFSET;
            MetadataTreeNode *arrElemNode = GetNodeById(nodeId);
            nodeType = *(m_valueData + arrElemNode->offsetValue);
        }

        treeNode->type = nodeType;
    }

    return treeNode->type;
}

MetadataTreeNode *MetadataReader::GetOrCreateTreeNodeByName(const string &className) {
    MetadataTreeNode *treeNode = GetRoot();

    int arrayIdx = -1;
    string arrayName = "[";

    while (className[++arrayIdx] == '[') {
        MetadataTreeNode *child = treeNode->GetChild(arrayName);

        if (child == nullptr) {
            vector<MetadataTreeNode *> *children = treeNode->children;
            if (children == nullptr) {
                children = treeNode->children = new vector<MetadataTreeNode *>;
            }

            child = new MetadataTreeNode;
            child->name = "[";
            child->parent = treeNode;
            child->offsetValue = ARRAY_OFFSET;

            children->push_back(child);
            m_v.push_back(child);
        }

        treeNode = child;
    }

    string cn = className.substr(arrayIdx);

    if (arrayIdx > 0) {
        char last = *cn.rbegin();
        if (last == ';') {
            cn = cn.substr(1, cn.length() - 2);
        }
    }

    vector<string> names;
    Util::SplitString(cn, "/$", names);

    if (arrayIdx > 0) {
        bool found = false;
        MetadataTreeNode *forwardedNode = GetOrCreateTreeNodeByName(cn);

        uint16_t forwardedNodeId = GetNodeId(forwardedNode);
        if (treeNode->children == nullptr) {
            treeNode->children = new vector<MetadataTreeNode *>();
        }
        vector<MetadataTreeNode *> &children = *treeNode->children;
        for (auto childNode: children) {
            uint32_t childNodeId = (childNode->offsetValue >= ARRAY_OFFSET)
                                   ? (childNode->offsetValue - ARRAY_OFFSET)
                                   :
                                   GetNodeId(childNode);

            if (childNodeId == forwardedNodeId) {
                treeNode = childNode;
                found = true;
                break;
            }
        }

        if (!found) {
            MetadataTreeNode *forwardNode = new MetadataTreeNode;
            forwardNode->offsetValue = forwardedNodeId + ARRAY_OFFSET;
            forwardNode->parent = treeNode;

            m_v.push_back(forwardNode);
            children.push_back(forwardNode);

            treeNode = forwardNode;
        }

        return treeNode;
    }

    int curIdx = 0;
    for (auto it = names.begin(); it != names.end(); ++it) {
        MetadataTreeNode *child = treeNode->GetChild(*it);

        if (child == nullptr) {
            vector<string> api = m_getTypeMetadataCallback(cn, curIdx);

            for (const auto &part: api) {
                vector<MetadataTreeNode *> *children = treeNode->children;
                if (children == nullptr) {
                    children = treeNode->children = new vector<MetadataTreeNode *>;
                }

                child = new MetadataTreeNode;
                child->name = *it++;
                child->parent = treeNode;

                string line;
                string kind;
                string name;
                stringstream s(part);

                getline(s, line);
                stringstream typeLine(line);
                typeLine >> kind >> name;
                auto cKind = kind[0];

                // package, class, interface
                assert((cKind == 'P') || (cKind == 'C') || (cKind == 'I'));

                if ((cKind == 'C') || (cKind == 'I')) {
                    child->metadata = new string(part);
                    child->type = (cKind == 'C') ? MetadataTreeNode::CLASS
                                                 : MetadataTreeNode::INTERFACE;
                    if (name == "S") {
                        child->type |= MetadataTreeNode::STATIC;
                    }

                    getline(s, line);
                    stringstream baseClassLine(line);
                    baseClassLine >> kind >> name;
                    cKind = kind[0];

                    assert(cKind == 'B');
                    auto baseClassTreeNode = GetOrCreateTreeNodeByName(name);
                    auto baseClassNodeId = GetNodeId(baseClassTreeNode);

                    child->offsetValue = m_valueLength;
                    m_valueData[m_valueLength++] = child->type;
                    *reinterpret_cast<uint16_t *>(m_valueData + m_valueLength) = baseClassNodeId;
                    m_valueLength += sizeof(uint16_t);
                } else {
                    child->type = MetadataTreeNode::PACKAGE;
                }

                m_v.push_back(child);
                children->push_back(child);

                treeNode = child;
            }

            return treeNode;
        } else {
            treeNode = child;
        }
        ++curIdx;
    }

    return treeNode;
}

MetadataTreeNode *MetadataReader::GetBaseClassNode(MetadataTreeNode *treeNode) {
    MetadataTreeNode *baseClassNode = nullptr;

    if (treeNode != nullptr) {
        uint16_t baseClassNodeId = *reinterpret_cast<uint16_t *>(m_valueData +
                                                                 treeNode->offsetValue + 1);

        size_t nodeCount = m_v.size();

        assert(baseClassNodeId < nodeCount);

        baseClassNode = GetNodeById(baseClassNodeId);
    }

    return baseClassNode;
}


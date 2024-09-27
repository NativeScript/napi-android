#include "MetadataMethodInfo.h"
#include "MetadataNode.h"


using namespace ns;

std::string MethodInfo::GetName() {
    string methodName = MetadataNode::getMetadataReader()->ReadName(nameOffset);

    return methodName;
}

uint8_t MethodInfo::CheckIsResolved() {

    return resolvedData;
}

uint16_t MethodInfo::GetSignatureLength() {
    m_signatureLength = *reinterpret_cast<uint16_t*>(m_pData);

    return m_signatureLength;
}

std::string MethodInfo::GetSignature() { //use nodeId's to read the whole signature
    auto m_reader = MetadataNode::getMetadataReader();
    string signature = "(";
    string ret;
    for (int i = 0; i < m_signatureLength; i++) {
        uint16_t nodeId = *nodeIdPtr++;
        string curArgTypeName = m_reader->ReadTypeName(nodeId);
        MetadataTreeNode* node = m_reader->GetNodeById(nodeId);

        uint8_t nodeType = m_reader->GetNodeType(node);
        bool isRefType = m_reader->IsNodeTypeClass(nodeType) || m_reader->IsNodeTypeInterface(nodeType);
        if (i == 0) {
            if ((curArgTypeName[0] != '[') && isRefType) {
                ret.append("L");
            }
            ret.append(curArgTypeName);
            if ((curArgTypeName[0] != '[') && isRefType) {
                ret.append(";");
            }
        } else {
            if ((curArgTypeName[0] != '[') && isRefType) {
                signature.append("L");
            }
            signature.append(curArgTypeName);
            if ((curArgTypeName[0] != '[') && isRefType) {
                signature.append(";");
            }
        }
    }
    if (ret.empty()) {
        ret = "V";
    }
    signature += ")" + ret;

    int sizeofReadNodeIds = m_signatureLength * sizeof(uint16_t);
    m_pData += sizeofReadNodeIds;

    return signature;
}

std::string MethodInfo::GetDeclaringType() {
    auto m_reader = MetadataNode::getMetadataReader();
    uint16_t nodeId = *declaringTypePtr;

    string declTypeName = m_reader->ReadTypeName(nodeId);

    return declTypeName;
}

int MethodInfo::GetSizeOfReadMethodInfo() {
    if (!sizeMeasured) {
        nameOffset = *reinterpret_cast<uint32_t*>(m_pData);
        resolvedData = *reinterpret_cast<uint8_t*>(m_pData);
        m_signatureLength = *reinterpret_cast<uint16_t*>(m_pData);

        int sizeofReadNodeIds = m_signatureLength * sizeof(uint16_t);
        m_pData += sizeofReadNodeIds;

        if (isStatic) {
            declaringTypePtr = reinterpret_cast<uint16_t*>(m_pData);
        }


        m_pData = m_pData + sizeof(nameOffset) + sizeof(resolvedData) + sizeof(nodeIdPtr) + sizeof(declaringTypePtr);
        sizeMeasured = true;
    }

    return m_pData - m_pStartData;
}

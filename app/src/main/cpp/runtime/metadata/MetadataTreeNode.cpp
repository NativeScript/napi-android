#include "MetadataTreeNode.h"

using namespace std;
using namespace ns;

MetadataTreeNode::MetadataTreeNode()
    :
    children(nullptr), parent(nullptr), metadata(nullptr), offsetValue(0), type(INVALID_TYPE) {
}

MetadataTreeNode* MetadataTreeNode::GetChild(const string& childName) {
    MetadataTreeNode* child = nullptr;

    if (children != nullptr) {
        auto itEnd = children->end();
        auto itFound = find_if(children->begin(), itEnd, [&childName] (MetadataTreeNode *x) {
            return x->name == childName;
        });
        if (itFound != itEnd) {
            child = *itFound;
        }
    }

    return child;
}


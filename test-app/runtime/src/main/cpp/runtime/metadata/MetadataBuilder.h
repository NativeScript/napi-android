//
// Created by Ammar Ahmed on 28/09/2024.
//

#ifndef TESTAPPNAPI_METADATABUILDER_H
#define TESTAPPNAPI_METADATABUILDER_H

#include <string>
#include "MetadataReader.h"

namespace tns {

    class MetadataBuilder {
    public:
        static MetadataReader BuildMetadata(const std::string &filesPath);

    private:
        static MetadataReader
        BuildMetadata(uint32_t nodesLength, uint8_t *nodeData, uint32_t nameLength,
                      uint8_t *nameData, uint32_t valueLength, uint8_t *valueData);
    };

} // tns

#endif //TESTAPPNAPI_METADATABUILDER_H

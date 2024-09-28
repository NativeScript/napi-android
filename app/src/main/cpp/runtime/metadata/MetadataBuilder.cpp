//
// Created by Ammar Ahmed on 28/09/2024.
//

#include "MetadataBuilder.h"
#include <string>
#include <sstream>
#include <cctype>
#include <dirent.h>
#include <set>
#include <cerrno>
#include <unistd.h>
#include "NativeScriptException.h"
#include "NativeScriptAssert.h"
#include "File.h"
#include "CallbackHandlers.h"


using namespace ns;

MetadataReader MetadataBuilder::BuildMetadata(const std::string &filesPath) {
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

    return BuildMetadata(lenNodes, reinterpret_cast<uint8_t *>(nodes.get()), lenNames,
                  reinterpret_cast<uint8_t *>(names.get()), lenValues,
                  reinterpret_cast<uint8_t *>(values.get()));
}

MetadataReader MetadataBuilder::BuildMetadata(uint32_t nodesLength, uint8_t *nodeData, uint32_t nameLength,
                                 uint8_t *nameData, uint32_t valueLength, uint8_t *valueData) {
    return MetadataReader(nodesLength, nodeData, nameLength, nameData, valueLength,
                                      valueData, CallbackHandlers::GetTypeMetadata);
}

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


using namespace tns;

MetadataReader MetadataBuilder::BuildMetadata(const std::string &filesPath) {
    timeval time1;
    gettimeofday(&time1, nullptr);

    string baseDir = filesPath;
    baseDir.append("/metadata");

    DIR* dir = opendir(baseDir.c_str());

    if(dir == nullptr){
        stringstream ss;
        ss << "metadata folder couldn't be opened! (Error: ";
        ss << errno;
        ss << ") ";

        // TODO: Is there a way to detect if the screen is locked as verification
        // We assume based on the error that this is the only way to get this specific error here at this point
        if (errno == ENOENT || errno == EACCES) {
            // Log the error with error code
            __android_log_print(ANDROID_LOG_ERROR, "TNS.error", "%s", ss.str().c_str());

            // While the screen is locked after boot; we cannot access our own apps directory on Android 9+
            // So the only thing to do at this point is just exit normally w/o crashing!

            // The only reason we should be in this specific path; is if:
            // 1) android:directBootAware="true" flag is set on receiver
            // 2) android.intent.action.LOCKED_BOOT_COMPLETED intent is set in manifest on above receiver
            // See:  https://developer.android.com/guide/topics/manifest/receiver-element
            //  and: https://developer.android.com/training/articles/direct-boot
            // This specific path occurs if you using the NativeScript-Local-Notification plugin, the
            // receiver code runs fine, but the app actually doesn't need to startup.  The Native code tries to
            // startup because the receiver is triggered.  So even though we are exiting, the receiver will have
            // done its job

            exit(0);
        }
        else {
            throw NativeScriptException(ss.str());
        }
    }

    string nodesFile = baseDir + "/treeNodeStream.dat";
    string namesFile = baseDir + "/treeStringsStream.dat";
    string valuesFile = baseDir + "/treeValueStream.dat";

    FILE* f = fopen(nodesFile.c_str(), "rb");
    if (f == nullptr) {
        stringstream ss;
        ss << "metadata file (treeNodeStream.dat) couldn't be opened! (Error: ";
        ss << errno;
        ss << ") ";

        throw NativeScriptException(ss.str());
    }
    fseek(f, 0, SEEK_END);
    int lenNodes = ftell(f);
    assert((lenNodes % sizeof(MetadataTreeNodeRawData)) == 0);
    char* nodes = new char[lenNodes];
    rewind(f);
    fread(nodes, 1, lenNodes, f);
    fclose(f);

    const int _512KB = 524288;

    f = fopen(namesFile.c_str(), "rb");
    if (f == nullptr) {
        stringstream ss;
        ss << "metadata file (treeStringsStream.dat) couldn't be opened! (Error: ";
        ss << errno;
        ss << ") ";
        throw NativeScriptException(ss.str());
    }
    fseek(f, 0, SEEK_END);
    int lenNames = ftell(f);
    char* names = new char[lenNames + _512KB];
    rewind(f);
    fread(names, 1, lenNames, f);
    fclose(f);

    f = fopen(valuesFile.c_str(), "rb");
    if (f == nullptr) {
        stringstream ss;
        ss << "metadata file (treeValueStream.dat) couldn't be opened! (Error: ";
        ss << errno;
        ss << ") ";
        throw NativeScriptException(ss.str());
    }
    fseek(f, 0, SEEK_END);
    int lenValues = ftell(f);
    char* values = new char[lenValues + _512KB];
    rewind(f);
    fread(values, 1, lenValues, f);
    fclose(f);

    timeval time2;
    gettimeofday(&time2, nullptr);

    DEBUG_WRITE("lenNodes=%d, lenNames=%d, lenValues=%d", lenNodes, lenNames, lenValues);

    long millis1 = (time1.tv_sec * 1000) + (time1.tv_usec / 1000);
    long millis2 = (time2.tv_sec * 1000) + (time2.tv_usec / 1000);

    DEBUG_WRITE("time=%ld", (millis2 - millis1));

    auto reader = BuildMetadata(lenNodes, reinterpret_cast<uint8_t*>(nodes), lenNames, reinterpret_cast<uint8_t*>(names), lenValues, reinterpret_cast<uint8_t*>(values));
    delete[] nodes;
    return reader;
}

MetadataReader MetadataBuilder::BuildMetadata(uint32_t nodesLength, uint8_t *nodeData, uint32_t nameLength,
                                 uint8_t *nameData, uint32_t valueLength, uint8_t *valueData) {
    return MetadataReader(nodesLength, nodeData, nameLength, nameData, valueLength,
                                      valueData, CallbackHandlers::GetTypeMetadata);


}

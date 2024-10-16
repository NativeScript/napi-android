/*
 * File.cpp
 *
 *  Created on: Jun 24, 2015
 *      Author: gatanasov
 */

#include "File.h"
#include <sstream>
#include <fstream>
#include <sys/mman.h>
#include <assert.h>

using namespace std;

namespace tns {

string File::ReadText(const string& filePath) {
    int len;
    bool isNew;
    const char* content = ReadText(filePath, len, isNew);

    string s(content, len);

    if (isNew) {
        delete[] content;
    }

    return s;
}

void* File::ReadBinary(const string& filePath, int& length) {
    length = 0;

    auto file = fopen(filePath.c_str(), READ_BINARY);
    if (!file) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    rewind(file);

    uint8_t* data = new uint8_t[length];
    fread(data, sizeof(uint8_t), length, file);
    fclose(file);

    return data;
}

bool File::WriteBinary(const string& filePath, const void* data, int length) {
    auto file = fopen(filePath.c_str(), WRITE_BINARY);
    if (!file) {
        return false;
    }

    auto writtenBytes = fwrite(data, sizeof(uint8_t), length, file);
    fclose(file);

    return writtenBytes == length;
}

const char* File::ReadText(const string& filePath, int& charLength, bool& isNew) {
    FILE* file = fopen(filePath.c_str(), "rb");
    fseek(file, 0, SEEK_END);

    charLength = ftell(file);
    isNew = charLength > BUFFER_SIZE;

    rewind(file);

    if (isNew) {
        char* newBuffer = new char[charLength];
        fread(newBuffer, 1, charLength, file);
        fclose(file);

        return newBuffer;
    }

    fread(Buffer, 1, charLength, file);
    fclose(file);

    return Buffer;
}

std::unique_ptr<char[]> File::ReadFile(const std::string &filePath, int &length, int extraBuffer) {
        FILE *file = fopen(filePath.c_str(), "rb");
        if (!file) {
            std::stringstream ss;
            ss << "metadata file (" << filePath << ") couldn't be opened! (Error: " << errno << ") ";
//        throw NativeScriptException(ss.str());
        }

        fseek(file, 0, SEEK_END);
        length = ftell(file);
        std::unique_ptr<char[]> buffer(new char[length + extraBuffer]);
        rewind(file);
        fread(buffer.get(), 1, length, file);
        fclose(file);

        return buffer;
    }

char* File::Buffer = new char[BUFFER_SIZE];

const char* File::WRITE_BINARY = "wb";
const char* File::READ_BINARY = "rb";
}

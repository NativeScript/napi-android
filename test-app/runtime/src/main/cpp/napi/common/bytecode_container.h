//
// Shared helpers for the NativeScript bytecode container used by the QuickJS
// family (and any engine that wraps engine bytecode in our container).
//
// Container layout (see tools/bytecode-compiler/native/*-compile.c):
//   [8-byte magic][4-byte format version, little-endian][engine payload]
//
// Hermes is the exception: it stores raw HBC (its own magic, no container), so
// it doesn't use these helpers.
//
#ifndef NS_BYTECODE_CONTAINER_H
#define NS_BYTECODE_CONTAINER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace nsbc {

// magic(8) + version(4)
static const size_t kHeaderLen = 12;

// Turn a script source URL into the on-disk path we read bytecode from. Returns
// false for synthetic / non-file sources (e.g. "<require_factory>").
inline bool ResolvePath(const char *file, std::string &out) {
    if (file == nullptr) return false;
    std::string f(file);
    static const std::string scheme = "file://";
    if (f.rfind(scheme, 0) == 0) {
        out = f.substr(scheme.size());
    } else if (!f.empty() && f[0] == '/') {
        out = f;
    } else {
        return false;
    }
    return !out.empty();
}

// Cheaply check whether `path` starts with the 8-byte container magic. `magic8`
// must point to 8 bytes (e.g. the string literal "NSBCQJS" — 7 chars + NUL).
inline bool HasMagic(const std::string &path, const char *magic8) {
    uint8_t head[8];
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    size_t n = fread(head, 1, sizeof(head), fp);
    fclose(fp);
    return n == sizeof(head) && memcmp(head, magic8, sizeof(head)) == 0;
}

}  // namespace nsbc

#endif  // NS_BYTECODE_CONTAINER_H

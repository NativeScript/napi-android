#include "Util.h"
#include <sstream>
#include <iostream>
#include <codecvt>

using namespace std;
namespace tns {


    string Util::JniClassPathToCanonicalName(const string &jniClassPath) {
        std::string canonicalName;

        const char prefix = jniClassPath[0];

        std::string rest;
        int lastIndex;

        switch (prefix) {
            case 'L':
                canonicalName = jniClassPath.substr(1, jniClassPath.size() - 2);
                std::replace(canonicalName.begin(), canonicalName.end(), '/', '.');
                std::replace(canonicalName.begin(), canonicalName.end(), '$', '.');
                break;

            case '[':
                canonicalName = jniClassPath;
                lastIndex = canonicalName.find_last_of('[');
                rest = canonicalName.substr(lastIndex + 1);
                canonicalName = canonicalName.substr(0, lastIndex + 1);
                canonicalName.append(JniClassPathToCanonicalName(rest));
                break;

            default:
                // TODO:
                canonicalName = jniClassPath;
                break;
        }
        return canonicalName;
    }

    void Util::SplitString(const string &str, const string &delimiters, vector <string> &tokens) {
        string::size_type delimPos = 0, tokenPos = 0, pos = 0;

        if (str.length() < 1) {
            return;
        }

        while (true) {
            delimPos = str.find_first_of(delimiters, pos);
            tokenPos = str.find_first_not_of(delimiters, pos);

            if (string::npos != delimPos) {
                if (string::npos != tokenPos) {
                    if (tokenPos < delimPos) {
                        tokens.push_back(str.substr(pos, delimPos - pos));
                    } else {
                        tokens.emplace_back("");
                    }
                } else {
                    tokens.emplace_back("");
                }
                pos = delimPos + 1;
            } else {
                if (string::npos != tokenPos) {
                    tokens.push_back(str.substr(pos));
                } else {
                    tokens.emplace_back("");
                }
                break;
            }
        }
    }

    bool Util::EndsWith(const string &str, const string &suffix) {
        bool res = false;
        if (str.size() > suffix.size()) {
            res = equal(suffix.rbegin(), suffix.rend(), str.rbegin());
        }
        return res;
    }

    string Util::ConvertFromJniToCanonicalName(const string &name) {
        string converted = name;
        replace(converted.begin(), converted.end(), '/', '.');
        return converted;
    }

    string Util::ConvertFromCanonicalToJniName(const string &name) {
        string converted = name;
        replace(converted.begin(), converted.end(), '.', '/');
        return converted;
    }

    string Util::ReplaceAll(string &str, const string &from, const string &to) {
        if (from.empty()) {
            return str;
        }

        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }

        return str;
    }

    u16string Util::ConvertFromUtf8ToUtf16(const string &str) {
        auto utf16String =
                std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().from_bytes(str);

        return utf16String;
    }

    void Util::JoinString(const std::vector<std::string> &list, const std::string &delimiter,
                          std::string &out) {
        out.clear();

        stringstream ss;

        for (auto it = list.begin(); it != list.end(); ++it) {
            ss << *it;

            if (it != list.end() - 1) {
                ss << delimiter;
            }
        }

        out = ss.str();
    }

    std::vector<uint16_t> Util::ToVector(const std::string &value) {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        // FIXME: std::codecvt_utf8_utf16 is deprecated
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
        std::u16string value16 = convert.from_bytes(value);

        const uint16_t *begin = reinterpret_cast<uint16_t const *>(value16.data());
        const uint16_t *end = reinterpret_cast<uint16_t const *>(value16.data() + value16.size());
        std::vector<uint16_t> vector(begin, end);
        return vector;
    }

};

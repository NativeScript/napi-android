// This file is generated by base_string_adapter_h.template.

// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef v8_inspector_protocol_BASE_STRING_ADAPTER_H
#define v8_inspector_protocol_BASE_STRING_ADAPTER_H

#include "third_party/inspector_protocol/crdtp/chromium/protocol_traits.h"


namespace v8_inspector {
namespace protocol {

class Value;

using String = std::string;
using Binary = crdtp::Binary;

class  StringUtil {
 public:
  static String fromUTF8(const uint8_t* data, size_t length) {
    return std::string(reinterpret_cast<const char*>(data), length);
  }

  static String fromUTF16LE(const uint16_t* data, size_t length);

  static const uint8_t* CharactersLatin1(const String& s) { return nullptr; }
  static const uint8_t* CharactersUTF8(const String& s) {
    return reinterpret_cast<const uint8_t*>(s.data());
  }
  static const uint16_t* CharactersUTF16(const String& s) { return nullptr; }
  static size_t CharacterCount(const String& s) { return s.size(); }
};

std::unique_ptr<Value> toProtocolValue(const base::Value& value, int depth);
base::Value toBaseValue(Value* value, int depth);

} // namespace v8_inspector
} // namespace protocol

#endif // !defined(v8_inspector_protocol_BASE_STRING_ADAPTER_H)

//
// Copyright 2019 WebAssembly Community Group participants
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef WASP_BINARY_VALUE_TYPE_ENCODING_H
#define WASP_BINARY_VALUE_TYPE_ENCODING_H

#include "wasp/base/macros.h"
#include "wasp/base/optional.h"
#include "wasp/base/types.h"
#include "wasp/binary/value_type.h"

namespace wasp {
namespace binary {
namespace encoding {

struct ValueType {
#define WASP_V(val, Name, str) static constexpr u8 Name = val;
#include "wasp/binary/value_type.def"
#undef WASP_V

  static u8 Encode(::wasp::binary::ValueType);
  static optional<::wasp::binary::ValueType> Decode(u8);
};

// static
inline u8 ValueType::Encode(::wasp::binary::ValueType decoded) {
  switch (decoded) {
#define WASP_V(val, Name, str)          \
  case ::wasp::binary::ValueType::Name: \
    return val;
#include "wasp/binary/value_type.def"
#undef WASP_V
    default:
      WASP_UNREACHABLE();
  }
}

// static
inline optional<::wasp::binary::ValueType> ValueType::Decode(u8 val) {
  switch (val) {
#define WASP_V(val, Name, str) \
  case Name:                   \
    return ::wasp::binary::ValueType::Name;
#include "wasp/binary/value_type.def"
#undef WASP_V
    default:
      return nullopt;
  }
}

}  // namespace encoding
}  // namespace binary
}  // namespace wasp

#endif // WASP_BINARY_VALUE_TYPE_ENCODING_H

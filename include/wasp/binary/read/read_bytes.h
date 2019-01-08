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

#ifndef WASP_BINARY_READ_READ_BYTES_H_
#define WASP_BINARY_READ_READ_BYTES_H_

#include "wasp/base/format.h"
#include "wasp/base/optional.h"
#include "wasp/base/span.h"

namespace wasp {
namespace binary {

template <typename Errors>
optional<SpanU8> ReadBytes(SpanU8* data, SpanU8::index_type N, Errors& errors) {
  if (data->size() < N) {
    errors.OnError(*data, format("Unable to read {} bytes", N));
    return nullopt;
  }

  SpanU8 result{data->begin(), N};
  remove_prefix(data, N);
  return result;
}

}  // namespace binary
}  // namespace wasp

#endif  // WASP_BINARY_READ_READ_BYTES_H_
//
// Copyright 2018 WebAssembly Community Group participants
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

#include <type_traits>

#include "absl/strings/str_format.h"

#include "src/base/to_string.h"
#include "src/binary/encoding.h"

namespace wasp {
namespace binary {

#define WASP_HOOK(call)           \
  if (call == HookResult::Stop) { \
    return {};                    \
  }

#define WASP_READ(var, call) \
  auto opt_##var = call;     \
  if (!opt_##var) {          \
    return absl::nullopt;    \
  }                          \
  auto var = *opt_##var /* No semicolon. */

#define WASP_READ_OR_ERROR(var, call, desc) \
  auto opt_##var = call;                    \
  if (!opt_##var) {                         \
    hooks.OnError("Unable to read " desc);  \
    return {};                              \
  }                                         \
  auto var = *opt_##var /* No semicolon. */

inline HookResult StopOnError(ReadResult result) {
  return result == ReadResult::Error ? HookResult::Stop : HookResult::Continue;
}

inline optional<u8> ReadU8(SpanU8* data) {
  if (data->size() < 1) {
    return absl::nullopt;
  }

  u8 result{(*data)[0]};
  data->remove_prefix(1);
  return result;
}

inline optional<SpanU8> ReadBytes(SpanU8* data, size_t N) {
  if (data->size() < N) {
    return absl::nullopt;
  }

  SpanU8 result{data->begin(), N};
  data->remove_prefix(N);
  return result;
}

template <typename S>
S SignExtend(typename std::make_unsigned<S>::type x, int N) {
  constexpr size_t kNumBits = sizeof(S) * 8;
  return static_cast<S>(x << (kNumBits - N - 1)) >> (kNumBits - N - 1);
}

template <typename T>
optional<T> ReadVarInt(SpanU8* data) {
  using U = typename std::make_unsigned<T>::type;
  constexpr bool is_signed = std::is_signed<T>::value;
  constexpr int kMaxBytes = (sizeof(T) * 8 + 6) / 7;
  constexpr int kUsedBitsInLastByte = sizeof(T) * 8 - 7 * (kMaxBytes - 1);
  constexpr int kMaskBits = kUsedBitsInLastByte - (is_signed ? 1 : 0);
  constexpr u8 kMask = ~((1 << kMaskBits) - 1);
  constexpr u8 kOnes = kMask & 0x7f;

  U result{};
  for (int i = 0;;) {
    WASP_READ(byte, ReadU8(data));

    const int shift = i * 7;
    result |= U(byte & 0x7f) << shift;

    if (++i == kMaxBytes) {
      if ((byte & kMask) == 0 || (is_signed && (byte & kMask) == kOnes)) {
        return static_cast<T>(result);
      }
      return absl::nullopt;
    } else if ((byte & 0x80) == 0) {
      return is_signed ? SignExtend<T>(result, 6 + shift) : result;
    }
  }
}

inline optional<u32> ReadVarU32(SpanU8* data) {
  return ReadVarInt<u32>(data);
}

inline optional<Index> ReadIndex(SpanU8* data) {
  return ReadVarU32(data);
}

inline optional<s32> ReadVarS32(SpanU8* data) {
  return ReadVarInt<s32>(data);
}

inline optional<s64> ReadVarS64(SpanU8* data) {
  return ReadVarInt<s64>(data);
}

inline optional<f32> ReadF32(SpanU8* data) {
  static_assert(sizeof(f32) == 4, "sizeof(f32) != 4");
  WASP_READ(bytes, ReadBytes(data, sizeof(f32)));
  f32 result;
  memcpy(&result, bytes.data(), sizeof(f32));
  return result;
}

inline optional<f64> ReadF64(SpanU8* data) {
  static_assert(sizeof(f64) == 8, "sizeof(f64) != 8");
  WASP_READ(bytes, ReadBytes(data, sizeof(f64)));
  f64 result;
  memcpy(&result, bytes.data(), sizeof(f64));
  return result;
}

template <typename T, typename F>
optional<std::vector<T>> ReadVec(SpanU8* data, F&& read_element) {
  std::vector<T> result;
  WASP_READ(len, ReadVarU32(data));
  result.reserve(len);
  for (u32 i = 0; i < len; ++i) {
    WASP_READ(elt, read_element(data));
    result.emplace_back(std::move(elt));
  }
  return result;
}

inline optional<SpanU8> ReadVecU8(SpanU8* data) {
  WASP_READ(len, ReadVarU32(data));
  if (len > data->size()) {
    return absl::nullopt;
  }

  SpanU8 result{data->data(), len};
  data->remove_prefix(len);
  return result;
}

inline optional<ValType> ReadValType(SpanU8* data) {
  WASP_READ(val_s32, ReadVarS32(data));
  switch (val_s32) {
    case encoding::ValType::I32:     return ValType::I32;
    case encoding::ValType::I64:     return ValType::I64;
    case encoding::ValType::F32:     return ValType::F32;
    case encoding::ValType::F64:     return ValType::F64;
    case encoding::ValType::Anyfunc: return ValType::Anyfunc;
    case encoding::ValType::Func:    return ValType::Func;
    case encoding::ValType::Void:    return ValType::Void;
    default: return absl::nullopt;
  }
}

inline optional<ExternalKind> ReadExternalKind(SpanU8* data) {
  WASP_READ(byte, ReadU8(data));
  switch (byte) {
    case encoding::ExternalKind::Func:   return ExternalKind::Func;
    case encoding::ExternalKind::Table:  return ExternalKind::Table;
    case encoding::ExternalKind::Memory: return ExternalKind::Memory;
    case encoding::ExternalKind::Global: return ExternalKind::Global;
    default: return absl::nullopt;
  }
}

inline optional<Mutability> ReadMutability(SpanU8* data) {
  WASP_READ(byte, ReadU8(data));
  switch (byte) {
    case encoding::Mutability::Var:   return Mutability::Var;
    case encoding::Mutability::Const: return Mutability::Const;
    default: return absl::nullopt;
  }
}

inline optional<string_view> ReadStr(SpanU8* data) {
  WASP_READ(len, ReadVarU32(data));
  if (len > data->size()) {
    return absl::nullopt;
  }

  string_view result{reinterpret_cast<const char*>(data->data()), len};
  data->remove_prefix(len);
  return result;
}

inline optional<Limits> ReadLimits(SpanU8* data) {
  const u32 kFlags_HasMax = 1;
  WASP_READ(flags, ReadVarU32(data));
  WASP_READ(min, ReadVarU32(data));

  if (flags & kFlags_HasMax) {
    WASP_READ(max, ReadVarU32(data));
    return Limits{min, max};
  } else {
    return Limits{min};
  }
}

inline optional<TableType> ReadTableType(SpanU8* data) {
  WASP_READ(elemtype, ReadValType(data));
  WASP_READ(limits, ReadLimits(data));
  return TableType{limits, elemtype};
}

inline optional<MemoryType> ReadMemoryType(SpanU8* data) {
  WASP_READ(limits, ReadLimits(data));
  return MemoryType{limits};
}

inline optional<GlobalType> ReadGlobalType(SpanU8* data) {
  WASP_READ(type, ReadValType(data));
  WASP_READ(mut, ReadMutability(data));
  return GlobalType{type, mut};
}

template <typename Hooks = ExprHooksNop>
optional<Expr> ReadExpr(SpanU8* data, Hooks&& hooks = Hooks{}) {
  const u8* const start = data->data();
  int ends_expected = 1;

  while (ends_expected != 0) {
    WASP_READ_OR_ERROR(opcode, ReadU8(data), "opcode");
    switch (opcode) {
      // End has no immediates, but is handled specially; we exit from the loop
      // when we've closed all open blocks (and the implicit outer block).
      case encoding::Opcode::End:
        --ends_expected;
        WASP_HOOK(hooks.OnOpcodeBare(opcode));
        break;

      // No immediates:
      case encoding::Opcode::Unreachable:
      case encoding::Opcode::Nop:
      case encoding::Opcode::Else:
      case encoding::Opcode::Return:
      case encoding::Opcode::Drop:
      case encoding::Opcode::Select:
      case encoding::Opcode::I32Add:
      case encoding::Opcode::I32Sub:
      case encoding::Opcode::I32Mul:
      case encoding::Opcode::I32DivS:
      case encoding::Opcode::I32DivU:
      case encoding::Opcode::I32RemS:
      case encoding::Opcode::I32RemU:
      case encoding::Opcode::I32And:
      case encoding::Opcode::I32Or:
      case encoding::Opcode::I32Xor:
      case encoding::Opcode::I32Shl:
      case encoding::Opcode::I32ShrU:
      case encoding::Opcode::I32ShrS:
      case encoding::Opcode::I32Rotr:
      case encoding::Opcode::I32Rotl:
      case encoding::Opcode::I64Add:
      case encoding::Opcode::I64Sub:
      case encoding::Opcode::I64Mul:
      case encoding::Opcode::I64DivS:
      case encoding::Opcode::I64DivU:
      case encoding::Opcode::I64RemS:
      case encoding::Opcode::I64RemU:
      case encoding::Opcode::I64And:
      case encoding::Opcode::I64Or:
      case encoding::Opcode::I64Xor:
      case encoding::Opcode::I64Shl:
      case encoding::Opcode::I64ShrU:
      case encoding::Opcode::I64ShrS:
      case encoding::Opcode::I64Rotr:
      case encoding::Opcode::I64Rotl:
      case encoding::Opcode::F32Add:
      case encoding::Opcode::F32Sub:
      case encoding::Opcode::F32Mul:
      case encoding::Opcode::F32Div:
      case encoding::Opcode::F32Min:
      case encoding::Opcode::F32Max:
      case encoding::Opcode::F32Copysign:
      case encoding::Opcode::F64Add:
      case encoding::Opcode::F64Sub:
      case encoding::Opcode::F64Mul:
      case encoding::Opcode::F64Div:
      case encoding::Opcode::F64Min:
      case encoding::Opcode::F64Max:
      case encoding::Opcode::F64Copysign:
      case encoding::Opcode::I32Eq:
      case encoding::Opcode::I32Ne:
      case encoding::Opcode::I32LtS:
      case encoding::Opcode::I32LeS:
      case encoding::Opcode::I32LtU:
      case encoding::Opcode::I32LeU:
      case encoding::Opcode::I32GtS:
      case encoding::Opcode::I32GeS:
      case encoding::Opcode::I32GtU:
      case encoding::Opcode::I32GeU:
      case encoding::Opcode::I64Eq:
      case encoding::Opcode::I64Ne:
      case encoding::Opcode::I64LtS:
      case encoding::Opcode::I64LeS:
      case encoding::Opcode::I64LtU:
      case encoding::Opcode::I64LeU:
      case encoding::Opcode::I64GtS:
      case encoding::Opcode::I64GeS:
      case encoding::Opcode::I64GtU:
      case encoding::Opcode::I64GeU:
      case encoding::Opcode::F32Eq:
      case encoding::Opcode::F32Ne:
      case encoding::Opcode::F32Lt:
      case encoding::Opcode::F32Le:
      case encoding::Opcode::F32Gt:
      case encoding::Opcode::F32Ge:
      case encoding::Opcode::F64Eq:
      case encoding::Opcode::F64Ne:
      case encoding::Opcode::F64Lt:
      case encoding::Opcode::F64Le:
      case encoding::Opcode::F64Gt:
      case encoding::Opcode::F64Ge:
      case encoding::Opcode::I32Clz:
      case encoding::Opcode::I32Ctz:
      case encoding::Opcode::I32Popcnt:
      case encoding::Opcode::I64Clz:
      case encoding::Opcode::I64Ctz:
      case encoding::Opcode::I64Popcnt:
      case encoding::Opcode::F32Abs:
      case encoding::Opcode::F32Neg:
      case encoding::Opcode::F32Ceil:
      case encoding::Opcode::F32Floor:
      case encoding::Opcode::F32Trunc:
      case encoding::Opcode::F32Nearest:
      case encoding::Opcode::F32Sqrt:
      case encoding::Opcode::F64Abs:
      case encoding::Opcode::F64Neg:
      case encoding::Opcode::F64Ceil:
      case encoding::Opcode::F64Floor:
      case encoding::Opcode::F64Trunc:
      case encoding::Opcode::F64Nearest:
      case encoding::Opcode::F64Sqrt:
      case encoding::Opcode::I32TruncSF32:
      case encoding::Opcode::I32TruncSF64:
      case encoding::Opcode::I32TruncUF32:
      case encoding::Opcode::I32TruncUF64:
      case encoding::Opcode::I32WrapI64:
      case encoding::Opcode::I64TruncSF32:
      case encoding::Opcode::I64TruncSF64:
      case encoding::Opcode::I64TruncUF32:
      case encoding::Opcode::I64TruncUF64:
      case encoding::Opcode::I64ExtendSI32:
      case encoding::Opcode::I64ExtendUI32:
      case encoding::Opcode::F32ConvertSI32:
      case encoding::Opcode::F32ConvertUI32:
      case encoding::Opcode::F32ConvertSI64:
      case encoding::Opcode::F32ConvertUI64:
      case encoding::Opcode::F32DemoteF64:
      case encoding::Opcode::F32ReinterpretI32:
      case encoding::Opcode::F64ConvertSI32:
      case encoding::Opcode::F64ConvertUI32:
      case encoding::Opcode::F64ConvertSI64:
      case encoding::Opcode::F64ConvertUI64:
      case encoding::Opcode::F64PromoteF32:
      case encoding::Opcode::F64ReinterpretI64:
      case encoding::Opcode::I32ReinterpretF32:
      case encoding::Opcode::I64ReinterpretF64:
      case encoding::Opcode::I32Eqz:
      case encoding::Opcode::I64Eqz:
        WASP_HOOK(hooks.OnOpcodeBare(opcode));
        break;

      // Type immediate.
      case encoding::Opcode::Block:
      case encoding::Opcode::Loop:
      case encoding::Opcode::If: {
        WASP_READ_OR_ERROR(type, ReadValType(data), "type index");
        WASP_HOOK(hooks.OnOpcodeType(opcode, type));
        // Each of these instructions opens a new block which must be closed by
        // an `End`.
        ++ends_expected;
        break;
      }

      // Index immediate.
      case encoding::Opcode::Br:
      case encoding::Opcode::BrIf:
      case encoding::Opcode::GetGlobal:
      case encoding::Opcode::GetLocal:
      case encoding::Opcode::SetGlobal:
      case encoding::Opcode::SetLocal:
      case encoding::Opcode::Call:
      case encoding::Opcode::TeeLocal: {
        WASP_READ_OR_ERROR(index, ReadIndex(data), "index");
        WASP_HOOK(hooks.OnOpcodeIndex(opcode, index));
        break;
      }

      // Index, reserved immediates.
      case encoding::Opcode::CallIndirect: {
        WASP_READ_OR_ERROR(index, ReadIndex(data), "index");
        WASP_READ_OR_ERROR(reserved, ReadU8(data), "reserved");
        WASP_HOOK(hooks.OnOpcodeCallIndirect(opcode, index, reserved));
        break;
      }

      // Index* immediates.
      case encoding::Opcode::BrTable: {
        WASP_READ_OR_ERROR(targets, ReadVec<Index>(data, ReadIndex),
                           "br_table targets");
        WASP_READ_OR_ERROR(default_target, ReadIndex(data),
                           "br_table default target");
        WASP_HOOK(
            hooks.OnOpcodeBrTable(opcode, std::move(targets), default_target));
        break;
      }

      // Memarg (alignment, offset) immediates.
      case encoding::Opcode::I32Load8S:
      case encoding::Opcode::I32Load8U:
      case encoding::Opcode::I32Load16S:
      case encoding::Opcode::I32Load16U:
      case encoding::Opcode::I64Load8S:
      case encoding::Opcode::I64Load8U:
      case encoding::Opcode::I64Load16S:
      case encoding::Opcode::I64Load16U:
      case encoding::Opcode::I64Load32S:
      case encoding::Opcode::I64Load32U:
      case encoding::Opcode::I32Load:
      case encoding::Opcode::I64Load:
      case encoding::Opcode::F32Load:
      case encoding::Opcode::F64Load:
      case encoding::Opcode::I32Store8:
      case encoding::Opcode::I32Store16:
      case encoding::Opcode::I64Store8:
      case encoding::Opcode::I64Store16:
      case encoding::Opcode::I64Store32:
      case encoding::Opcode::I32Store:
      case encoding::Opcode::I64Store:
      case encoding::Opcode::F32Store:
      case encoding::Opcode::F64Store: {
        WASP_READ_OR_ERROR(align_log2, ReadVarU32(data), "alignment");
        WASP_READ_OR_ERROR(offset, ReadVarU32(data), "offset");
        WASP_HOOK(hooks.OnOpcodeMemarg(opcode, MemArg{align_log2, offset}));
        break;
      }

      // Const immediates.
      case encoding::Opcode::I32Const: {
        WASP_READ_OR_ERROR(value, ReadVarS32(data), "i32 constant");
        WASP_HOOK(hooks.OnOpcodeI32Const(opcode, value));
        break;
      }

      case encoding::Opcode::I64Const: {
        WASP_READ_OR_ERROR(value, ReadVarS64(data), "i64 constant");
        WASP_HOOK(hooks.OnOpcodeI64Const(opcode, value));
        break;
      }

      case encoding::Opcode::F32Const: {
        WASP_READ_OR_ERROR(value, ReadF32(data), "f32 constant");
        WASP_HOOK(hooks.OnOpcodeF32Const(opcode, value));
        break;
      }

      case encoding::Opcode::F64Const: {
        WASP_READ_OR_ERROR(value, ReadF64(data), "f64 constant");
        WASP_HOOK(hooks.OnOpcodeF64Const(opcode, value));
        break;
      }

      default:
        hooks.OnError(absl::StrFormat("Unknown opcode 0x%02x", opcode));
        return absl::nullopt;
    }
  }

  const size_t len = data->data() - start;
  return Expr{SpanU8{start, len}};
}

template <typename Hooks>
ReadResult ReadSection(SpanU8* data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(code, ReadVarU32(data), "section code");
  WASP_READ_OR_ERROR(len, ReadVarU32(data), "section length");

  if (len > data->size()) {
    hooks.OnError(absl::StrFormat("Section length is too long: %zd > %zd", len,
                                  data->size()));
    return ReadResult::Error;
  }

  WASP_HOOK(hooks.OnSection(code, data->subspan(0, len)));
  data->remove_prefix(*opt_len);
  return ReadResult::Ok;
}

template <typename Hooks>
ReadResult ReadModule(SpanU8 data, Hooks&& hooks) {
  const SpanU8 kMagic{encoding::Magic};
  const SpanU8 kVersion{encoding::Version};

  auto opt_magic = ReadBytes(&data, 4);
  if (opt_magic != kMagic) {
    hooks.OnError(absl::StrFormat("Magic mismatch: expected %s, got %s",
                                  wasp::ToString(kMagic),
                                  wasp::ToString(*opt_magic)));
    return ReadResult::Error;
  }

  auto opt_version = ReadBytes(&data, 4);
  if (opt_version != kVersion) {
    hooks.OnError(absl::StrFormat("Version mismatch: expected %s, got %s",
                                  wasp::ToString(kVersion),
                                  wasp::ToString(*opt_version)));
    return ReadResult::Error;
  }

  while (!data.empty()) {
    if (ReadSection(&data, hooks) == ReadResult::Error) {
      return ReadResult::Error;
    }
  }

  return ReadResult::Ok;
}

template <typename Hooks>
ReadResult ErrorUnlessAtSectionEnd(SpanU8 data, Hooks&& hooks) {
  if (data.size() != 0) {
    hooks.OnError("Expected end of section");
    return ReadResult::Error;
  }
  return ReadResult::Ok;
}

template <typename Hooks>
ReadResult ReadTypeSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "type count");
  WASP_HOOK(hooks.OnTypeCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(form, ReadValType(&data), "type form");

    if (form != ValType::Func) {
      hooks.OnError(absl::StrFormat("Unknown type form: %d", form));
      return ReadResult::Error;
    }

    WASP_READ_OR_ERROR(param_types, ReadVec<ValType>(&data, ReadValType),
                       "param types");
    WASP_READ_OR_ERROR(result_types, ReadVec<ValType>(&data, ReadValType),
                       "result types");
    WASP_HOOK(hooks.OnFuncType(
        i, FuncType{std::move(param_types), std::move(result_types)}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadImportSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "import count");
  WASP_HOOK(hooks.OnImportCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(module, ReadStr(&data), "module name");
    WASP_READ_OR_ERROR(name, ReadStr(&data), "field name");
    WASP_READ_OR_ERROR(kind, ReadExternalKind(&data), "import kind");

    switch (kind) {
      case ExternalKind::Func: {
        WASP_READ_OR_ERROR(type_index, ReadIndex(&data), "func type index");
        WASP_HOOK(hooks.OnFuncImport(i, FuncImport{module, name, type_index}));
        break;
      }

      case ExternalKind::Table: {
        WASP_READ_OR_ERROR(table_type, ReadTableType(&data), "table type");
        WASP_HOOK(
            hooks.OnTableImport(i, TableImport{module, name, table_type}));
        break;
      }

      case ExternalKind::Memory: {
        WASP_READ_OR_ERROR(memory_type, ReadMemoryType(&data), "memory type");
        WASP_HOOK(
            hooks.OnMemoryImport(i, MemoryImport{module, name, memory_type}));
        break;
      }

      case ExternalKind::Global: {
        WASP_READ_OR_ERROR(global_type, ReadGlobalType(&data), "global type");
        WASP_HOOK(
            hooks.OnGlobalImport(i, GlobalImport{module, name, global_type}));
        break;
      }
    }
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadFunctionSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "func count");
  WASP_HOOK(hooks.OnFuncCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(type_index, ReadIndex(&data), "func type index");
    WASP_HOOK(hooks.OnFunc(i, Func{type_index}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadTableSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "table count");
  WASP_HOOK(hooks.OnTableCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(table_type, ReadTableType(&data), "table type");
    WASP_HOOK(hooks.OnTable(i, Table{table_type}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadMemorySection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "memory count");
  WASP_HOOK(hooks.OnMemoryCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(memory_type, ReadMemoryType(&data), "memory type");
    WASP_HOOK(hooks.OnMemory(i, Memory{memory_type}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadGlobalSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "global count");
  WASP_HOOK(hooks.OnGlobalCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(global_type, ReadGlobalType(&data), "global type");
    WASP_READ_OR_ERROR(init_expr, ReadExpr(&data),
                       "global initializer expression");
    WASP_HOOK(hooks.OnGlobal(i, Global{global_type, init_expr}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadExportSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "export count");
  WASP_HOOK(hooks.OnExportCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(name, ReadStr(&data), "export name");
    WASP_READ_OR_ERROR(kind, ReadExternalKind(&data), "export kind");
    WASP_READ_OR_ERROR(index, ReadIndex(&data), "export index");
    WASP_HOOK(hooks.OnExport(i, Export{kind, name, index}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadStartSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(func_index, ReadIndex(&data), "start function index");
  WASP_HOOK(hooks.OnStart(Start{func_index}));
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadElementSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "element segment count");
  WASP_HOOK(hooks.OnElementSegmentCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(table_index, ReadIndex(&data),
                       "element segment table index");
    WASP_READ_OR_ERROR(offset, ReadExpr(&data), "element segment offset");
    WASP_READ_OR_ERROR(init, ReadVec<Index>(&data, ReadIndex),
                       "element segment initializers");
    WASP_HOOK(hooks.OnElementSegment(
        i, ElementSegment{table_index, offset, std::move(init)}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadCodeSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "code count");
  WASP_HOOK(hooks.OnCodeCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(len, ReadIndex(&data), "code length");

    if (len > data.size()) {
      hooks.OnError(absl::StrFormat("Code length is too long: %zd > %zd", len,
                                    data.size()));
      return ReadResult::Error;
    }

    WASP_HOOK(hooks.OnCode(i, data.subspan(0, len)));
    data.remove_prefix(len);
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

inline optional<LocalDecl> ReadLocalDecl(SpanU8* data) {
  WASP_READ(count, ReadIndex(data));
  WASP_READ(type, ReadValType(data));
  return LocalDecl{count, type};
}

template <typename Hooks>
ReadResult ReadCode(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(local_decls, ReadVec<LocalDecl>(&data, ReadLocalDecl),
                     "locals");
  WASP_READ_OR_ERROR(body, ReadExpr(&data), "body");
  WASP_HOOK(hooks.OnCodeContents(std::move(local_decls), body));
  return ErrorUnlessAtSectionEnd(data, hooks);
}

template <typename Hooks>
ReadResult ReadDataSection(SpanU8 data, Hooks&& hooks) {
  WASP_READ_OR_ERROR(count, ReadIndex(&data), "data segment count");
  WASP_HOOK(hooks.OnDataSegmentCount(count));

  for (Index i = 0; i < count; ++i) {
    WASP_READ_OR_ERROR(table_index, ReadIndex(&data),
                       "data segment table index");
    WASP_READ_OR_ERROR(offset, ReadExpr(&data), "data segment offset");
    WASP_READ_OR_ERROR(init, ReadVecU8(&data), "data segment initializer");
    WASP_HOOK(hooks.OnDataSegment(i, DataSegment{table_index, offset, init}));
  }
  return ErrorUnlessAtSectionEnd(data, hooks);
}

#undef WASP_HOOK
#undef WASP_READ
#undef WASP_READ_OR_ERROR

}  // namespace binary
}  // namespace wasp

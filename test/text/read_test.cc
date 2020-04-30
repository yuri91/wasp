//
// Copyright 2020 WebAssembly Community Group participants
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

#include "wasp/text/read.h"

#include "gtest/gtest.h"
#include "test/test_utils.h"
#include "wasp/base/errors.h"
#include "wasp/text/formatters.h"
#include "wasp/text/read/context.h"
#include "wasp/text/read/tokenizer.h"

using namespace ::wasp;
using namespace ::wasp::text;
using namespace ::wasp::test;

class TextReadTest : public ::testing::Test {
 protected:
  // Read without checking the expected result.
  template <typename Func, typename... Args>
  void Read(Func&& func, SpanU8 span, Args&&... args) {
    Tokenizer tokenizer{span};
    func(tokenizer, context, std::forward<Args>(args)...);
    ExpectNoErrors(errors);
  }

  template <typename Func, typename T, typename... Args>
  void OK(Func&& func, const T& expected, SpanU8 span, Args&&... args) {
    Tokenizer tokenizer{span};
    auto actual = func(tokenizer, context, std::forward<Args>(args)...);
    ASSERT_EQ(MakeAt(span, expected), actual);
    ExpectNoErrors(errors);
  }

  // TODO: Remove and just use OK?
  template <typename Func, typename T, typename... Args>
  void OKVector(Func&& func, const T& expected, SpanU8 span, Args&&... args) {
    Tokenizer tokenizer{span};
    auto actual = func(tokenizer, context, std::forward<Args>(args)...);
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(expected[i], actual[i]);
    }
    ExpectNoErrors(errors);
  }

  template <typename Func, typename... Args>
  void Fail(Func&& func,
            const ExpectedError& error,
            SpanU8 span,
            Args&&... args) {
    Tokenizer tokenizer{span};
    func(tokenizer, context, std::forward<Args>(args)...);
    ExpectError(error, errors, span);
    errors.Clear();
  }

  template <typename Func, typename... Args>
  void Fail(Func&& func,
            const std::vector<ExpectedError>& expected_errors,
            SpanU8 span,
            Args&&... args) {
    Tokenizer tokenizer{span};
    func(tokenizer, context, std::forward<Args>(args)...);
    ExpectErrors(expected_errors, errors, span);
    errors.Clear();
  }

  TestErrors errors;
  Context context{errors};
};

// Helpers for handling InstructionList functions.

auto ReadBlockInstruction_ForTesting(Tokenizer& tokenizer, Context& context)
    -> InstructionList {
  InstructionList result;
  ReadBlockInstruction(tokenizer, context, result);
  return result;
}

auto ReadInstructionList_ForTesting(Tokenizer& tokenizer, Context& context)
    -> InstructionList {
  InstructionList result;
  ReadInstructionList(tokenizer, context, result);
  return result;
}

auto ReadExpressionList_ForTesting(Tokenizer& tokenizer, Context& context)
    -> InstructionList {
  InstructionList result;
  ReadExpressionList(tokenizer, context, result);
  return result;
}


TEST_F(TextReadTest, Nat32) {
  OK(ReadNat32, u32{123}, "123"_su8);
}

TEST_F(TextReadTest, Int32) {
  OK(ReadInt<u32>, u32{123}, "123"_su8);
  OK(ReadInt<u32>, u32{456}, "+456"_su8);
  OK(ReadInt<u32>, u32(-789), "-789"_su8);
}

TEST_F(TextReadTest, Var_Nat32) {
  OK(ReadVar, Var{Index{123}}, "123"_su8);
}

TEST_F(TextReadTest, Var_Id) {
  OK(ReadVar, Var{"$foo"_sv}, "$foo"_su8);
}

TEST_F(TextReadTest, VarOpt_Nat32) {
  OK(ReadVarOpt, Var{Index{3141}}, "3141"_su8);
  OK(ReadVarOpt, Var{"$bar"_sv}, "$bar"_su8);
}

TEST_F(TextReadTest, BindVarOpt) {
  NameMap name_map;
  OK(ReadBindVarOpt, BindVar{"$bar"_sv}, "$bar"_su8, name_map);

  ASSERT_TRUE(name_map.Has("$bar"));
  EXPECT_EQ(0u, name_map.Get("$bar"));
}

TEST_F(TextReadTest, VarList) {
  auto span = "$a $b 1 2"_su8;
  std::vector<At<Var>> expected{
      MakeAt("$a"_su8, Var{"$a"_sv}),
      MakeAt("$b"_su8, Var{"$b"_sv}),
      MakeAt("1"_su8, Var{Index{1}}),
      MakeAt("2"_su8, Var{Index{2}}),
  };
  OKVector(ReadVarList, expected, span);
}

TEST_F(TextReadTest, Text) {
  OK(ReadText, Text{"\"hello\""_sv, 5}, "\"hello\""_su8);
}

TEST_F(TextReadTest, TextList) {
  auto span = "\"hello, \" \"world\" \"123\""_su8;
  std::vector<At<Text>> expected{
      MakeAt("\"hello, \""_su8, Text{"\"hello, \""_sv, 7}),
      MakeAt("\"world\""_su8, Text{"\"world\""_sv, 5}),
      MakeAt("\"123\""_su8, Text{"\"123\""_sv, 3}),
  };
  OKVector(ReadTextList, expected, span);
}

TEST_F(TextReadTest, ValueType) {
  OK(ReadValueType, ValueType::I32, "i32"_su8);
  OK(ReadValueType, ValueType::I64, "i64"_su8);
  OK(ReadValueType, ValueType::F32, "f32"_su8);
  OK(ReadValueType, ValueType::F64, "f64"_su8);

  Fail(ReadValueType, {{0, "value type v128 not allowed"}}, "v128"_su8);
  Fail(ReadValueType, {{0, "value type funcref not allowed"}}, "funcref"_su8);
  Fail(ReadValueType, {{0, "value type anyref not allowed"}}, "anyref"_su8);
  Fail(ReadValueType, {{0, "value type nullref not allowed"}}, "nullref"_su8);
}

TEST_F(TextReadTest, ValueType_simd) {
  context.features.enable_simd();
  OK(ReadValueType, ValueType::V128, "v128"_su8);
}

TEST_F(TextReadTest, ValueType_reference_types) {
  context.features.enable_reference_types();
  OK(ReadValueType, ValueType::Funcref, "funcref"_su8);
  OK(ReadValueType, ValueType::Anyref, "anyref"_su8);
  OK(ReadValueType, ValueType::Nullref, "nullref"_su8);
}

TEST_F(TextReadTest, ValueType_exceptions) {
  context.features.enable_exceptions();
  OK(ReadValueType, ValueType::Exnref, "exnref"_su8);
}

TEST_F(TextReadTest, ValueTypeList) {
  auto span = "i32 f32 f64 i64"_su8;
  std::vector<At<ValueType>> expected{
      MakeAt("i32"_su8, ValueType::I32),
      MakeAt("f32"_su8, ValueType::F32),
      MakeAt("f64"_su8, ValueType::F64),
      MakeAt("i64"_su8, ValueType::I64),
  };
  OKVector(ReadValueTypeList, expected, span);
}

TEST_F(TextReadTest, ElementType) {
  OK(ReadElementType, ElementType::Funcref, "funcref"_su8);
}

TEST_F(TextReadTest, ElementType_reference_types) {
  context.features.enable_reference_types();
  OK(ReadElementType, ElementType::Funcref, "funcref"_su8);
  OK(ReadElementType, ElementType::Anyref, "anyref"_su8);
  OK(ReadElementType, ElementType::Nullref, "nullref"_su8);
}

TEST_F(TextReadTest, BoundParamList) {
  auto span = "(param i32 f32) (param $foo i64) (param)"_su8;
  std::vector<At<BoundValueType>> expected{
      MakeAt("i32"_su8,
             BoundValueType{nullopt, MakeAt("i32"_su8, ValueType::I32)}),
      MakeAt("f32"_su8,
             BoundValueType{nullopt, MakeAt("f32"_su8, ValueType::F32)}),
      MakeAt("$foo i64"_su8, BoundValueType{MakeAt("$foo"_su8, "$foo"_sv),
                                            MakeAt("i64"_su8, ValueType::I64)}),
  };

  NameMap name_map;
  OKVector(ReadBoundParamList, expected, span, name_map);

  ASSERT_TRUE(name_map.Has("$foo"));
  EXPECT_EQ(0u, name_map.Get("$foo"));
}

TEST_F(TextReadTest, BoundParamList_DuplicateName) {
  NameMap name_map;
  Fail(ReadBoundParamList, {{24, "Variable $foo is already bound to index 0"}},
       "(param $foo i32) (param $foo i64)"_su8, name_map);
}

TEST_F(TextReadTest, ParamList) {
  auto span = "(param i32 f32) (param i64) (param)"_su8;
  std::vector<At<ValueType>> expected{
      MakeAt("i32"_su8, ValueType::I32),
      MakeAt("f32"_su8, ValueType::F32),
      MakeAt("i64"_su8, ValueType::I64),
  };
  OKVector(ReadParamList, expected, span);
}

TEST_F(TextReadTest, ResultList) {
  auto span = "(result i32 f32) (result i64) (result)"_su8;
  std::vector<At<ValueType>> expected{
      MakeAt("i32"_su8, ValueType::I32),
      MakeAt("f32"_su8, ValueType::F32),
      MakeAt("i64"_su8, ValueType::I64),
  };
  OKVector(ReadResultList, expected, span);
}

TEST_F(TextReadTest, LocalList) {
  using VT = ValueType;
  using BVT = BoundValueType;
  auto span = "(local i32 f32) (local $foo i64) (local)"_su8;
  std::vector<At<BoundValueType>> expected{
      MakeAt("i32"_su8, BVT{nullopt, MakeAt("i32"_su8, VT::I32)}),
      MakeAt("f32"_su8, BVT{nullopt, MakeAt("f32"_su8, VT::F32)}),
      MakeAt("$foo i64"_su8,
             BVT{MakeAt("$foo"_su8, "$foo"_sv), MakeAt("i64"_su8, VT::I64)}),
  };

  NameMap name_map;
  OKVector(ReadLocalList, expected, span, name_map);

  ASSERT_TRUE(name_map.Has("$foo"));
  EXPECT_EQ(0u, name_map.Get("$foo"));
}

TEST_F(TextReadTest, BoundLocalList_DuplicateName) {
  NameMap name_map;
  Fail(ReadLocalList, {{24, "Variable $foo is already bound to index 0"}},
       "(local $foo i32) (local $foo i64)"_su8, name_map);
}

TEST_F(TextReadTest, TypeUseOpt) {
  OK(ReadTypeUseOpt, Var{Index{123}}, "(type 123)"_su8);
  OK(ReadTypeUseOpt, Var{"$foo"_sv}, "(type $foo)"_su8);
  OK(ReadTypeUseOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, FunctionTypeUse) {
  using VT = ValueType;

  // Empty.
  OK(ReadFunctionTypeUse, FunctionTypeUse{}, ""_su8);

  // Type use.
  OK(ReadFunctionTypeUse,
     FunctionTypeUse{MakeAt("(type 0)"_su8, Var{Index{0}}), {}},
     "(type 0)"_su8);

  // Function type.
  OK(ReadFunctionTypeUse,
     FunctionTypeUse{nullopt,
                     MakeAt("(param i32 f32) (result f64)"_su8,
                            FunctionType{{MakeAt("i32"_su8, VT::I32),
                                          MakeAt("f32"_su8, VT::F32)},
                                         {MakeAt("f64"_su8, VT::F64)}})},
     "(param i32 f32) (result f64)"_su8);

  // Type use and function type.
  OK(ReadFunctionTypeUse,
     FunctionTypeUse{MakeAt("(type $t)"_su8, Var{"$t"_sv}),
                     MakeAt("(result i32)"_su8,
                            FunctionType{{}, {MakeAt("i32"_su8, VT::I32)}})},
     "(type $t) (result i32)"_su8);
}

TEST_F(TextReadTest, FunctionTypeUse_ReuseType) {
  context.function_type_map.Define(
      BoundFunctionType{{BoundValueType{nullopt, ValueType::I32}}, {}});

  Read(ReadFunctionTypeUse, "(param i32)"_su8);

  ASSERT_EQ(1u, context.function_type_map.Size());
}

TEST_F(TextReadTest, FunctionTypeUse_DeferType) {
  using VT = ValueType;
  using BVT = BoundValueType;

  FunctionTypeMap& ftm = context.function_type_map;

  ftm.Define(BoundFunctionType{{BVT{nullopt, VT::I32}}, {}});
  Read(ReadFunctionTypeUse, "(param f32)"_su8);
  ftm.Define(BoundFunctionType{{BVT{nullopt, VT::I64}}, {}});
  ftm.EndModule();

  ASSERT_EQ(3u, ftm.Size());
  EXPECT_EQ((FunctionType{{VT::I32}, {}}), ftm.Get(0));
  EXPECT_EQ((FunctionType{{VT::I64}, {}}), ftm.Get(1));

  // Implicitly defined after other explicitly defined types.
  EXPECT_EQ((FunctionType{{MakeAt("f32"_su8, VT::F32)}, {}}), ftm.Get(2));
}

TEST_F(TextReadTest, InlineImport) {
  OK(ReadInlineImportOpt,
     InlineImport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                  MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})},
     R"((import "m" "n"))"_su8);
  OK(ReadInlineImportOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, InlineImport_AfterNonImport) {
  context.seen_non_import = true;
  Fail(ReadInlineImportOpt,
       {{1, "Imports must occur before all non-import definitions"}},
       "(import \"m\" \"n\")"_su8);
}

TEST_F(TextReadTest, InlineExport) {
  OK(ReadInlineExportOpt,
     InlineExport{MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})},
     R"((export "n"))"_su8);
  OK(ReadInlineExportOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, InlineExportList) {
  OKVector(ReadInlineExportList,
           InlineExportList{
               MakeAt("(export \"m\")"_su8,
                      InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})}),
               MakeAt("(export \"n\")"_su8,
                      InlineExport{MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})}),
           },
           R"((export "m") (export "n"))"_su8);
  OK(ReadInlineExportOpt, nullopt, ""_su8);
}


TEST_F(TextReadTest, BoundFunctionType) {
  using VT = ValueType;
  using BVT = BoundValueType;

  SpanU8 span =
      "(param i32 i32) (param $t i64) (result f32 f32) (result f64)"_su8;
  NameMap name_map;
  OK(ReadBoundFunctionType,
     BoundFunctionType{
         {MakeAt("i32"_su8, BVT{nullopt, MakeAt("i32"_su8, VT::I32)}),
          MakeAt("i32"_su8, BVT{nullopt, MakeAt("i32"_su8, VT::I32)}),
          MakeAt("$t i64"_su8,
                 BVT{MakeAt("$t"_su8, "$t"_sv), MakeAt("i64"_su8, VT::I64)})},
         {MakeAt("f32"_su8, VT::F32), MakeAt("f32"_su8, VT::F32),
          MakeAt("f64"_su8, VT::F64)}},
     span, name_map);

  ASSERT_TRUE(name_map.Has("$t"));
  EXPECT_EQ(0u, name_map.Get("$t"));
}

TEST_F(TextReadTest, FunctionType) {
  using VT = ValueType;

  SpanU8 span = "(param i32 i32) (param i64) (result f32 f32) (result f64)"_su8;
  OK(ReadFunctionType,
     FunctionType{{MakeAt("i32"_su8, VT::I32), MakeAt("i32"_su8, VT::I32),
                   MakeAt("i64"_su8, VT::I64)},
                  {MakeAt("f32"_su8, VT::F32), MakeAt("f32"_su8, VT::F32),
                   MakeAt("f64"_su8, VT::F64)}},
     span);
}

TEST_F(TextReadTest, TypeEntry) {
  using VT = ValueType;
  using BVT = BoundValueType;

  OK(ReadTypeEntry, TypeEntry{nullopt, BoundFunctionType{{}, {}}},
     "(type (func))"_su8);

  OK(ReadTypeEntry,
     TypeEntry{
         MakeAt("$foo"_su8, "$foo"_sv),
         MakeAt("(param $bar i32) (result i64)"_su8,
                BoundFunctionType{
                    {MakeAt("$bar i32"_su8, BVT{MakeAt("$bar"_su8, "$bar"_sv),
                                                MakeAt("i32"_su8, VT::I32)})},
                    {MakeAt("i64"_su8, VT::I64)}})},
     "(type $foo (func (param $bar i32) (result i64)))"_su8);
}

TEST_F(TextReadTest, TypeEntry_DuplicateName) {
  context.type_names.NewBound("$t"_sv);

  Fail(ReadTypeEntry, {{6, "Variable $t is already bound to index 0"}},
       "(type $t (func))"_su8);
}

TEST_F(TextReadTest, TypeEntry_DistinctTypes) {
  Read(ReadTypeEntry, "(type $a (func))"_su8);
  Read(ReadTypeEntry, "(type $b (func))"_su8);

  ASSERT_EQ(2u, context.function_type_map.Size());
}

TEST_F(TextReadTest, AlignOpt) {
  OK(ReadAlignOpt, u32{256}, "align=256"_su8);
  OK(ReadAlignOpt, u32{16}, "align=0x10"_su8);
  OK(ReadAlignOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, AlignOpt_NonPowerOfTwo) {
  Fail(ReadAlignOpt, {{0, "Alignment must be a power of two, got 3"}},
       "align=3"_su8);
}

TEST_F(TextReadTest, OffsetOpt) {
  OK(ReadOffsetOpt, u32{0}, "offset=0"_su8);
  OK(ReadOffsetOpt, u32{0x123}, "offset=0x123"_su8);
  OK(ReadOffsetOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, Limits) {
  OK(ReadLimits, Limits{MakeAt("1"_su8, 1u)}, "1"_su8);
  OK(ReadLimits, Limits{MakeAt("1"_su8, 1u), MakeAt("0x11"_su8, 17u)},
     "1 0x11"_su8);
  OK(ReadLimits,
     Limits{MakeAt("0"_su8, 0u), MakeAt("20"_su8, 20u),
            MakeAt("shared"_su8, Shared::Yes)},
     "0 20 shared"_su8);
}

TEST_F(TextReadTest, BlockImmediate) {
  // empty block type.
  OK(ReadBlockImmediate, BlockImmediate{}, ""_su8);

  // block type w/ label.
  OK(ReadBlockImmediate, BlockImmediate{MakeAt("$l"_su8, BindVar{"$l"_sv}), {}},
     "$l"_su8);

  // block type w/ function type use.
  OK(ReadBlockImmediate,
     BlockImmediate{nullopt,
                    FunctionTypeUse{MakeAt("(type 0)"_su8, Var{Index{0}}), {}}},
     "(type 0)"_su8);

  // block type w/ label and function type use.
  OK(ReadBlockImmediate,
     BlockImmediate{MakeAt("$l2"_su8, BindVar{"$l2"_sv}),
                    FunctionTypeUse{MakeAt("(type 0)"_su8, Var{Index{0}}), {}}},
     "$l2 (type 0)"_su8);
}

TEST_F(TextReadTest, PlainInstruction_Bare) {
  using I = Instruction;
  using O = Opcode;

  OK(ReadPlainInstruction, I{MakeAt("nop"_su8, O::Nop)}, "nop"_su8);
  OK(ReadPlainInstruction, I{MakeAt("i32.add"_su8, O::I32Add)}, "i32.add"_su8);
}

TEST_F(TextReadTest, PlainInstruction_Var) {
  using I = Instruction;
  using O = Opcode;

  OK(ReadPlainInstruction,
     I{MakeAt("br"_su8, O::Br), MakeAt("0"_su8, Var{Index{0}})}, "br 0"_su8);
  OK(ReadPlainInstruction,
     I{MakeAt("local.get"_su8, O::LocalGet), MakeAt("$x"_su8, Var{"$x"_sv})},
     "local.get $x"_su8);
}

TEST_F(TextReadTest, PlainInstruction_BrOnExn) {
  using I = Instruction;
  using O = Opcode;

  context.features.enable_exceptions();
  OK(ReadPlainInstruction,
     I{MakeAt("br_on_exn"_su8, O::BrOnExn),
       MakeAt("$l $e"_su8, BrOnExnImmediate{MakeAt("$l"_su8, Var{"$l"_sv}),
                                            MakeAt("$e"_su8, Var{"$e"_sv})})},
     "br_on_exn $l $e"_su8);
}

TEST_F(TextReadTest, PlainInstruction_BrTable) {
  using I = Instruction;
  using O = Opcode;

  // br_table w/ only default target.
  OK(ReadPlainInstruction,
     I{MakeAt("br_table"_su8, O::BrTable),
       MakeAt("0"_su8, BrTableImmediate{{}, MakeAt("0"_su8, Var{Index{0}})})},
     "br_table 0"_su8);

  // br_table w/ targets and default target.
  OK(ReadPlainInstruction,
     I{MakeAt("br_table"_su8, O::BrTable),
       MakeAt("0 1 $a $b"_su8,
              BrTableImmediate{{MakeAt("0"_su8, Var{Index{0}}),
                                MakeAt("1"_su8, Var{Index{1}}),
                                MakeAt("$a"_su8, Var{"$a"_sv})},
                               MakeAt("$b"_su8, Var{"$b"_sv})})},
     "br_table 0 1 $a $b"_su8);
}

TEST_F(TextReadTest, PlainInstruction_CallIndirect) {
  using I = Instruction;
  using O = Opcode;

  // Bare call_indirect.
  OK(ReadPlainInstruction,
     I{MakeAt("call_indirect"_su8, O::CallIndirect),
       MakeAt(""_su8, CallIndirectImmediate{})},
     "call_indirect"_su8);

  // call_indirect w/ function type use.
  OK(ReadPlainInstruction,
     I{MakeAt("call_indirect"_su8, O::CallIndirect),
       MakeAt("(type 0)"_su8,
              CallIndirectImmediate{
                  nullopt,
                  FunctionTypeUse{MakeAt("(type 0)"_su8, Var{Index{0}}), {}}})},
     "call_indirect (type 0)"_su8);
}

TEST_F(TextReadTest, PlainInstruction_CallIndirect_reference_types) {
  using I = Instruction;
  using O = Opcode;

  // In the reference types proposal, the call_indirect instruction also allows
  // a table var first.
  context.features.enable_reference_types();

  // call_indirect w/ table.
  OK(ReadPlainInstruction,
     I{MakeAt("call_indirect"_su8, O::CallIndirect),
       MakeAt("$t"_su8,
              CallIndirectImmediate{MakeAt("$t"_su8, Var{"$t"_sv}), {}})},
     "call_indirect $t"_su8);

  // call_indirect w/ table and type use.
  OK(ReadPlainInstruction,
     I{MakeAt("call_indirect"_su8, O::CallIndirect),
       MakeAt("0 (type 0)"_su8,
              CallIndirectImmediate{
                  MakeAt("0"_su8, Var{Index{0}}),
                  FunctionTypeUse{MakeAt("(type 0)"_su8, Var{Index{0}}), {}}})},
     "call_indirect 0 (type 0)"_su8);
}

TEST_F(TextReadTest, PlainInstruction_Const) {
  using I = Instruction;
  using O = Opcode;

  // i32.const
  OK(ReadPlainInstruction,
     I{MakeAt("i32.const"_su8, O::I32Const), MakeAt("12"_su8, u32{12})},
     "i32.const 12"_su8);

  // i64.const
  OK(ReadPlainInstruction,
     I{MakeAt("i64.const"_su8, O::I64Const), MakeAt("34"_su8, u64{34})},
     "i64.const 34"_su8);

  // f32.const
  OK(ReadPlainInstruction,
     I{MakeAt("f32.const"_su8, O::F32Const), MakeAt("56"_su8, f32{56})},
     "f32.const 56"_su8);

  // f64.const
  OK(ReadPlainInstruction,
     I{MakeAt("f64.const"_su8, O::F64Const), MakeAt("78"_su8, f64{78})},
     "f64.const 78"_su8);
}

TEST_F(TextReadTest, PlainInstruction_MemArg) {
  using I = Instruction;
  using O = Opcode;

  // No align, no offset.
  OK(ReadPlainInstruction,
     I{MakeAt("i32.load"_su8, O::I32Load),
       MakeAt(""_su8, MemArgImmediate{nullopt, nullopt})},
     "i32.load"_su8);

  // No align, offset.
  OK(ReadPlainInstruction,
     I{MakeAt("f32.load"_su8, O::F32Load),
       MakeAt("offset=12"_su8,
              MemArgImmediate{nullopt, MakeAt("offset=12"_su8, u32{12})})},
     "f32.load offset=12"_su8);

  // Align, no offset.
  OK(ReadPlainInstruction,
     I{MakeAt("i32.load8_u"_su8, O::I32Load8U),
       MakeAt("align=16"_su8,
              MemArgImmediate{MakeAt("align=16"_su8, u32{16}), nullopt})},
     "i32.load8_u align=16"_su8);

  // Align and offset.
  OK(ReadPlainInstruction,
     I{MakeAt("f64.store"_su8, O::F64Store),
       MakeAt("offset=123 align=32"_su8,
              MemArgImmediate{MakeAt("align=32"_su8, u32{32}),
                              MakeAt("offset=123"_su8, u32{123})})},
     "f64.store offset=123 align=32"_su8);
}

TEST_F(TextReadTest, PlainInstruction_Select) {
  using I = Instruction;
  using O = Opcode;

  OK(ReadPlainInstruction,
     I{MakeAt("select"_su8, O::Select), MakeAt(""_su8, ValueTypeList{})},
     "select"_su8);
}

TEST_F(TextReadTest, PlainInstruction_Select_reference_types) {
  using I = Instruction;
  using O = Opcode;
  using VT = ValueType;

  context.features.enable_reference_types();

  // select w/o types
  OK(ReadPlainInstruction,
     I{MakeAt("select"_su8, O::Select), MakeAt(""_su8, ValueTypeList{})},
     "select"_su8);

  // select w/ one type
  OK(ReadPlainInstruction,
     I{MakeAt("select"_su8, O::Select),
       MakeAt("(result i32)"_su8, ValueTypeList{MakeAt("i32"_su8, VT::I32)})},
     "select (result i32)"_su8);

  // select w/ multiple types
  OK(ReadPlainInstruction,
     I{MakeAt("select"_su8, O::Select),
       MakeAt("(result i32) (result i64)"_su8,
              ValueTypeList{MakeAt("i32"_su8, VT::I32),
                            MakeAt("i64"_su8, VT::I64)})},
     "select (result i32) (result i64)"_su8);
}

TEST_F(TextReadTest, PlainInstruction_SimdConst) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadPlainInstruction, {{0, "v128.const instruction not allowed"}},
       "v128.const i32x4 0 0 0 0"_su8);

  context.features.enable_simd();

  // i8x16
  OK(ReadPlainInstruction,
     I{MakeAt("v128.const"_su8, O::V128Const),
       MakeAt("0 1 2 3 4 5 6 7 8 9 0xa 0xb 0xc 0xd 0xe 0xf"_su8,
              v128{u8x16{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe,
                         0xf}})},
     "v128.const i8x16 0 1 2 3 4 5 6 7 8 9 0xa 0xb 0xc 0xd 0xe 0xf"_su8);

  // i16x8
  OK(ReadPlainInstruction,
     I{MakeAt("v128.const"_su8, O::V128Const),
       MakeAt("0 1 2 3 4 5 6 7"_su8, v128{u16x8{0, 1, 2, 3, 4, 5, 6, 7}})},
     "v128.const i16x8 0 1 2 3 4 5 6 7"_su8);

  // i32x4
  OK(ReadPlainInstruction,
     I{MakeAt("v128.const"_su8, O::V128Const),
       MakeAt("0 1 2 3"_su8, v128{u32x4{0, 1, 2, 3}})},
     "v128.const i32x4 0 1 2 3"_su8);

  // i64x2
  OK(ReadPlainInstruction,
     I{MakeAt("v128.const"_su8, O::V128Const),
       MakeAt("0 1"_su8, v128{u64x2{0, 1}})},
     "v128.const i64x2 0 1"_su8);

  // f32x4
  OK(ReadPlainInstruction,
     I{MakeAt("v128.const"_su8, O::V128Const),
       MakeAt("0 1 2 3"_su8, v128{f32x4{0, 1, 2, 3}})},
     "v128.const f32x4 0 1 2 3"_su8);

  // f64x2
  OK(ReadPlainInstruction,
     I{MakeAt("v128.const"_su8, O::V128Const),
       MakeAt("0 1"_su8, v128{f64x2{0, 1}})},
     "v128.const f64x2 0 1"_su8);
}

TEST_F(TextReadTest, PlainInstruction_SimdLane) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadPlainInstruction,
       {{0, "i8x16.extract_lane_s instruction not allowed"}},
       "i8x16.extract_lane_s 0"_su8);

  context.features.enable_simd();

  OK(ReadPlainInstruction,
     I{MakeAt("i8x16.extract_lane_s"_su8, O::I8X16ExtractLaneS),
       MakeAt("9"_su8, u32{9})},
     "i8x16.extract_lane_s 9"_su8);
  OK(ReadPlainInstruction,
     I{MakeAt("f32x4.replace_lane"_su8, O::F32X4ReplaceLane),
       MakeAt("3"_su8, u32{3})},
     "f32x4.replace_lane 3"_su8);
}

TEST_F(TextReadTest, PlainInstruction_Shuffle) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadPlainInstruction, {{0, "v8x16.shuffle instruction not allowed"}},
       "v8x16.shuffle 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"_su8);

  context.features.enable_simd();

  OK(ReadPlainInstruction,
     I{MakeAt("v8x16.shuffle"_su8, O::V8X16Shuffle),
       MakeAt("0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"_su8, ShuffleImmediate{})},
     "v8x16.shuffle 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"_su8);
}

TEST_F(TextReadTest, PlainInstruction_TableCopy) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadPlainInstruction, {{0, "table.copy instruction not allowed"}},
       "table.copy"_su8);

  // table.copy w/o dst and src.
  context.features.enable_bulk_memory();
  OK(ReadPlainInstruction,
     I{MakeAt("table.copy"_su8, O::TableCopy), MakeAt(""_su8, CopyImmediate{})},
     "table.copy"_su8);
}

TEST_F(TextReadTest, PlainInstruction_TableCopy_reference_types) {
  using I = Instruction;
  using O = Opcode;

  context.features.enable_reference_types();

  // table.copy w/o dst and src.
  OK(ReadPlainInstruction,
     I{MakeAt("table.copy"_su8, O::TableCopy), MakeAt(""_su8, CopyImmediate{})},
     "table.copy"_su8);

  // table.copy w/ dst and src
  OK(ReadPlainInstruction,
     I{MakeAt("table.copy"_su8, O::TableCopy),
       MakeAt("$d $s"_su8, CopyImmediate{MakeAt("$d"_su8, Var{"$d"_sv}),
                                         MakeAt("$s"_su8, Var{"$s"_sv})})},
     "table.copy $d $s"_su8);
}

TEST_F(TextReadTest, PlainInstruction_TableInit) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadPlainInstruction, {{0, "table.init instruction not allowed"}},
       "table.init 0"_su8);

  context.features.enable_bulk_memory();

  // table.init w/ segment index and table index.
  OK(ReadPlainInstruction,
     I{MakeAt("table.init"_su8, O::TableInit),
       MakeAt("$t $e"_su8, InitImmediate{MakeAt("$e"_su8, Var{"$e"_sv}),
                                         MakeAt("$t"_su8, Var{"$t"_sv})})},
     "table.init $t $e"_su8);

  // table.init w/ just segment index.
  OK(ReadPlainInstruction,
     I{MakeAt("table.init"_su8, O::TableInit),
       MakeAt("2"_su8, InitImmediate{MakeAt("2"_su8, Var{Index{2}}), nullopt})},
     "table.init 2"_su8);
}

TEST_F(TextReadTest, BlockInstruction_Block) {
  using I = Instruction;
  using O = Opcode;

  // Empty block.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("block"_su8,
                      I{MakeAt("block"_su8, O::Block), BlockImmediate{}}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "block end"_su8);

  // block w/ multiple instructions.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("block"_su8,
                      I{MakeAt("block"_su8, O::Block), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "block nop nop end"_su8);

  // Block w/ label.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("block $l"_su8,
                      I{MakeAt("block"_su8, O::Block),
                        MakeAt("$l"_su8,
                               BlockImmediate{MakeAt("$l"_su8, "$l"_sv), {}})}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "block $l nop end"_su8);

  // Block w/ label and matching end label.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("block $l2"_su8,
                 I{MakeAt("block"_su8, O::Block),
                   MakeAt("$l2"_su8,
                          BlockImmediate{MakeAt("$l2"_su8, "$l2"_sv), {}})}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "block $l2 nop end $l2"_su8);
}

TEST_F(TextReadTest, BlockInstruction_Block_MismatchedLabels) {
  Fail(ReadBlockInstruction_ForTesting, {{10, "Unexpected label $l2"}},
       "block end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{13, "Expected label $l, got $l2"}},
       "block $l end $l2"_su8);
}

TEST_F(TextReadTest, BlockInstruction_Loop) {
  using I = Instruction;
  using O = Opcode;

  // Empty loop.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("loop"_su8, I{MakeAt("loop"_su8, O::Loop), BlockImmediate{}}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "loop end"_su8);

  // loop w/ multiple instructions.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("loop"_su8, I{MakeAt("loop"_su8, O::Loop), BlockImmediate{}}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "loop nop nop end"_su8);

  // Loop w/ label.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("loop $l"_su8,
                      I{MakeAt("loop"_su8, O::Loop),
                        MakeAt("$l"_su8,
                               BlockImmediate{MakeAt("$l"_su8, "$l"_sv), {}})}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "loop $l nop end"_su8);

  // Loop w/ label and matching end label.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("loop $l2"_su8,
                 I{MakeAt("loop"_su8, O::Loop),
                   MakeAt("$l2"_su8,
                          BlockImmediate{MakeAt("$l2"_su8, "$l2"_sv), {}})}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "loop $l2 nop end $l2"_su8);
}

TEST_F(TextReadTest, BlockInstruction_Loop_MismatchedLabels) {
  Fail(ReadBlockInstruction_ForTesting, {{9, "Unexpected label $l2"}},
       "loop end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{12, "Expected label $l, got $l2"}},
       "loop $l end $l2"_su8);
}

TEST_F(TextReadTest, BlockInstruction_If) {
  using I = Instruction;
  using O = Opcode;

  // Empty if.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "if end"_su8);

  // if w/ non-empty block.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "if nop nop end"_su8);

  // if, w/ else.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("else"_su8, I{MakeAt("else"_su8, O::Else)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "if else end"_su8);

  // if, w/ else and non-empty blocks.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("else"_su8, I{MakeAt("else"_su8, O::Else)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "if nop nop else nop nop end"_su8);

  // If w/ label.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("if $l"_su8,
                      I{MakeAt("if"_su8, O::If),
                        MakeAt("$l"_su8,
                               BlockImmediate{MakeAt("$l"_su8, "$l"_sv), {}})}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "if $l nop end"_su8);

  // If w/ label and matching end label.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("if $l2"_su8,
                 I{MakeAt("if"_su8, O::If),
                   MakeAt("$l2"_su8,
                          BlockImmediate{MakeAt("$l2"_su8, "$l2"_sv), {}})}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "if $l2 nop end $l2"_su8);

  // If w/ label and matching else and end labels.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("if $l3"_su8,
                 I{MakeAt("if"_su8, O::If),
                   MakeAt("$l3"_su8,
                          BlockImmediate{MakeAt("$l3"_su8, "$l3"_sv), {}})}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("else"_su8, I{MakeAt("else"_su8, O::Else)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "if $l3 nop else $l3 nop end $l3"_su8);
}

TEST_F(TextReadTest, BlockInstruction_If_MismatchedLabels) {
  Fail(ReadBlockInstruction_ForTesting, {{7, "Unexpected label $l2"}},
       "if end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{8, "Unexpected label $l2"}},
       "if else $l2 end"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{10, "Expected label $l, got $l2"}},
       "if $l end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting,
       {
           {{11, "Expected label $l, got $l2"}},
           {{19, "Expected label $l, got $l2"}},
       },
       "if $l else $l2 end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{11, "Expected label $l, got $l2"}},
       "if $l else $l2 end $l"_su8);
}

TEST_F(TextReadTest, BlockInstruction_Try) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadBlockInstruction_ForTesting, {{0, "try instruction not allowed"}},
       "try catch end"_su8);

  context.features.enable_exceptions();

  // try/catch.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("try"_su8, I{MakeAt("try"_su8, O::Try), BlockImmediate{}}),
          MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "try catch end"_su8);

  // try/catch and non-empty blocks.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("try"_su8, I{MakeAt("try"_su8, O::Try), BlockImmediate{}}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "try nop nop catch nop nop end"_su8);

  // try w/ label.
  OKVector(ReadBlockInstruction_ForTesting,
           InstructionList{
               MakeAt("try $l"_su8,
                      I{MakeAt("try"_su8, O::Try),
                        MakeAt("$l"_su8,
                               BlockImmediate{MakeAt("$l"_su8, "$l"_sv), {}})}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
           },
           "try $l nop catch nop end"_su8);

  // try w/ label and matching end label.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("try $l2"_su8,
                 I{MakeAt("try"_su8, O::Try),
                   MakeAt("$l2"_su8,
                          BlockImmediate{MakeAt("$l2"_su8, "$l2"_sv), {}})}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "try $l2 nop catch nop end $l2"_su8);

  // try w/ label and matching catch and end labels.
  OKVector(
      ReadBlockInstruction_ForTesting,
      InstructionList{
          MakeAt("try $l3"_su8,
                 I{MakeAt("try"_su8, O::Try),
                   MakeAt("$l3"_su8,
                          BlockImmediate{MakeAt("$l3"_su8, "$l3"_sv), {}})}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
      },
      "try $l3 nop catch $l3 nop end $l3"_su8);
}

TEST_F(TextReadTest, BlockInstruction_Try_MismatchedLabels) {
  context.features.enable_exceptions();

  Fail(ReadBlockInstruction_ForTesting, {{14, "Unexpected label $l2"}},
       "try catch end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{10, "Unexpected label $l2"}},
       "try catch $l2 end"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{17, "Expected label $l, got $l2"}},
       "try $l catch end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting,
       {
           {{13, "Expected label $l, got $l2"}},
           {{21, "Expected label $l, got $l2"}},
       },
       "try $l catch $l2 end $l2"_su8);
  Fail(ReadBlockInstruction_ForTesting, {{13, "Expected label $l, got $l2"}},
       "try $l catch $l2 end $l"_su8);
}

TEST_F(TextReadTest, Label_ReuseNames) {
  using I = Instruction;
  using O = Opcode;

  OK(ReadInstructionList_ForTesting,
     InstructionList{
         MakeAt(
             "block $l"_su8,
             I{MakeAt("block"_su8, O::Block),
               MakeAt("$l"_su8,
                      BlockImmediate{MakeAt("$l"_su8, BindVar{"$l"_sv}), {}})}),
         MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
         MakeAt(
             "block $l"_su8,
             I{MakeAt("block"_su8, O::Block),
               MakeAt("$l"_su8,
                      BlockImmediate{MakeAt("$l"_su8, BindVar{"$l"_sv}), {}})}),
         MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
     },
     "block $l end block $l end"_su8);
}

TEST_F(TextReadTest, Label_DuplicateNames) {
  using I = Instruction;
  using O = Opcode;

  OK(ReadInstructionList_ForTesting,
     InstructionList{
         MakeAt("block $b"_su8,
                I{MakeAt("block"_su8, O::Block),
                  MakeAt("$b"_su8,
                         BlockImmediate{MakeAt("$b"_su8, "$b"_sv), {}})}),
         MakeAt("block $b"_su8,
                I{MakeAt("block"_su8, O::Block),
                  MakeAt("$b"_su8,
                         BlockImmediate{MakeAt("$b"_su8, "$b"_sv), {}})}),
         MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
         MakeAt("end"_su8, I{MakeAt("end"_su8, O::End)}),
     },
     "block $b block $b end end"_su8);
}

auto ReadExpression_ForTesting(Tokenizer& tokenizer, Context& context)
    -> InstructionList {
  InstructionList result;
  ReadExpression(tokenizer, context, result);
  return result;
}

TEST_F(TextReadTest, Expression_Plain) {
  using I = Instruction;
  using O = Opcode;

  // No immediates.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
           },
           "(nop)"_su8);

  // BrTable immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("br_table 0 0 0"_su8,
                 I{MakeAt("br_table"_su8, O::BrTable),
                   MakeAt("0 0 0"_su8,
                          BrTableImmediate{{MakeAt("0"_su8, Var{Index{0}}),
                                            MakeAt("0"_su8, Var{Index{0}})},
                                           MakeAt("0"_su8, Var{Index{0}})})}),
      },
      "(br_table 0 0 0)"_su8);

  // CallIndirect immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("call_indirect (type 0)"_su8,
                 I{MakeAt("call_indirect"_su8, O::CallIndirect),
                   MakeAt("(type 0)"_su8,
                          CallIndirectImmediate{
                              nullopt, FunctionTypeUse{MakeAt("(type 0)"_su8,
                                                              Var{Index{0}}),
                                                       {}}})}),
      },
      "(call_indirect (type 0))"_su8);

  // f32 immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("f32.const 1.0"_su8, I{MakeAt("f32.const"_su8, O::F32Const),
                                        MakeAt("1.0"_su8, f32{1.0f})}),
      },
      "(f32.const 1.0)"_su8);

  // f64 immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("f64.const 2.0"_su8, I{MakeAt("f64.const"_su8, O::F64Const),
                                        MakeAt("2.0"_su8, f64{2.0})}),
      },
      "(f64.const 2.0)"_su8);

  // i32 immediate.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("i32.const 3"_su8, I{MakeAt("i32.const"_su8, O::I32Const),
                                           MakeAt("3"_su8, u32{3})}),
           },
           "(i32.const 3)"_su8);

  // i64 immediate.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("i64.const 4"_su8, I{MakeAt("i64.const"_su8, O::I64Const),
                                           MakeAt("4"_su8, u64{4})}),
           },
           "(i64.const 4)"_su8);

  // MemArg immediate
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("i32.load align=1"_su8,
                      I{MakeAt("i32.load"_su8, O::I32Load),
                        MakeAt("align=1"_su8,
                               MemArgImmediate{MakeAt("align=1"_su8, u32{1}),
                                               nullopt})}),
           },
           "(i32.load align=1)"_su8);

  // Var immediate.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("br 0"_su8, I{MakeAt("br"_su8, O::Br),
                                    MakeAt("0"_su8, Var{Index{0}})}),
           },
           "(br 0)"_su8);
}

TEST_F(TextReadTest, Expression_Plain_exceptions) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadExpression_ForTesting, {{1, "br_on_exn instruction not allowed"}},
       "(br_on_exn 0 0)"_su8);

  context.features.enable_exceptions();

  // BrOnExn immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("br_on_exn 0 0"_su8,
                 I{MakeAt("br_on_exn"_su8, O::BrOnExn),
                   MakeAt("0 0"_su8,
                          BrOnExnImmediate{MakeAt("0"_su8, Var{Index{0}}),
                                           MakeAt("0"_su8, Var{Index{0}})})}),
      },
      "(br_on_exn 0 0)"_su8);
}

TEST_F(TextReadTest, Expression_Plain_simd) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadExpression_ForTesting, {{1, "v128.const instruction not allowed"}},
       "(v128.const i32x4 0 0 0 0)"_su8);

  context.features.enable_simd();

  // v128 immediate.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("v128.const i32x4 0 0 0 0"_su8,
                      I{MakeAt("v128.const"_su8, O::V128Const),
                        MakeAt("0 0 0 0"_su8, v128{u32x4{0, 0, 0, 0}})}),
           },
           "(v128.const i32x4 0 0 0 0)"_su8);

  // FeaturesSimd lane immediate.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("f32x4.replace_lane 3"_su8,
                      I{MakeAt("f32x4.replace_lane"_su8, O::F32X4ReplaceLane),
                        MakeAt("3"_su8, u32{3})}),
           },
           "(f32x4.replace_lane 3)"_su8);
}

TEST_F(TextReadTest, Expression_Plain_bulk_memory) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadExpression_ForTesting, {{1, "table.init instruction not allowed"}},
       "(table.init 0)"_su8);

  context.features.enable_bulk_memory();

  // Init immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("table.init 0"_su8,
                 I{MakeAt("table.init"_su8, O::TableInit),
                   MakeAt("0"_su8, InitImmediate{MakeAt("0"_su8, Var{Index{0}}),
                                                 nullopt})}),
      },
      "(table.init 0)"_su8);

  // Copy immediate.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("table.copy"_su8,
                 I{MakeAt("table.copy"_su8, O::TableCopy), CopyImmediate{}}),
      },
      "(table.copy)"_su8);
}


TEST_F(TextReadTest, Expression_PlainFolded) {
  using I = Instruction;
  using O = Opcode;

  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("i32.const 0"_su8, I{MakeAt("i32.const"_su8, O::I32Const),
                                           MakeAt("0"_su8, u32{0})}),
               MakeAt("i32.add"_su8, I{MakeAt("i32.add"_su8, O::I32Add)}),
           },
           "(i32.add (i32.const 0))"_su8);

  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("i32.const 0"_su8, I{MakeAt("i32.const"_su8, O::I32Const),
                                           MakeAt("0"_su8, u32{0})}),
               MakeAt("i32.const 1"_su8, I{MakeAt("i32.const"_su8, O::I32Const),
                                           MakeAt("1"_su8, u32{1})}),
               MakeAt("i32.add"_su8, I{MakeAt("i32.add"_su8, O::I32Add)}),
           },
           "(i32.add (i32.const 0) (i32.const 1))"_su8);
}

TEST_F(TextReadTest, Expression_Block) {
  using I = Instruction;
  using O = Opcode;

  // Block.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("block"_su8,
                      I{MakeAt("block"_su8, O::Block), BlockImmediate{}}),
               MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
           },
           "(block)"_su8);

  // Loop.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("loop"_su8, I{MakeAt("loop"_su8, O::Loop), BlockImmediate{}}),
          MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
      },
      "(loop)"_su8);
}

TEST_F(TextReadTest, Expression_If) {
  using I = Instruction;
  using O = Opcode;

  // If then.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
           },
           "(if (then))"_su8);

  // If then w/ nops.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
           },
           "(if (then (nop)))"_su8);

  // If condition then.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
           },
           "(if (nop) (then (nop)))"_su8);

  // If then else.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("else"_su8, I{MakeAt("else"_su8, O::Else)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
           },
           "(if (then (nop)) (else (nop)))"_su8);

  // If condition then else.
  OKVector(ReadExpression_ForTesting,
           InstructionList{
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("if"_su8, I{MakeAt("if"_su8, O::If), BlockImmediate{}}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("else"_su8, I{MakeAt("else"_su8, O::Else)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
           },
           "(if (nop) (then (nop)) (else (nop)))"_su8);
}

TEST_F(TextReadTest, Expression_Try) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadExpression_ForTesting, {{1, "try instruction not allowed"}},
       "(try (catch))"_su8);

  context.features.enable_exceptions();

  // Try catch.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("try"_su8, I{MakeAt("try"_su8, O::Try), BlockImmediate{}}),
          MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
          MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
      },
      "(try (catch))"_su8);

  // Try catch w/ nops.
  OKVector(
      ReadExpression_ForTesting,
      InstructionList{
          MakeAt("try"_su8, I{MakeAt("try"_su8, O::Try), BlockImmediate{}}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt("catch"_su8, I{MakeAt("catch"_su8, O::Catch)}),
          MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
          MakeAt(")"_su8, I{MakeAt(")"_su8, O::End)}),
      },
      "(try (nop) (catch (nop)))"_su8);
}

TEST_F(TextReadTest, ExpressionList) {
  using I = Instruction;
  using O = Opcode;

  OKVector(ReadExpressionList_ForTesting,
           InstructionList{
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
               MakeAt("drop"_su8, I{MakeAt("drop"_su8, O::Drop)}),
           },
           "(nop) (drop (nop))"_su8);
}

TEST_F(TextReadTest, TableType) {
  OK(ReadTableType,
     TableType{MakeAt("1 2"_su8,
                      Limits{MakeAt("1"_su8, u32{1}), MakeAt("2"_su8, u32{2})}),
               MakeAt("funcref"_su8, ElementType::Funcref)},
     "1 2 funcref"_su8);
}

TEST_F(TextReadTest, MemoryType) {
  OK(ReadMemoryType,
     MemoryType{MakeAt(
         "1 2"_su8, Limits{MakeAt("1"_su8, u32{1}), MakeAt("2"_su8, u32{2})})},
     "1 2"_su8);
}

TEST_F(TextReadTest, GlobalType) {
  OK(ReadGlobalType,
     GlobalType{MakeAt("i32"_su8, MakeAt("i32"_su8, ValueType::I32)),
                Mutability::Const},
     "i32"_su8);

  OK(ReadGlobalType,
     GlobalType{MakeAt("(mut i32)"_su8, MakeAt("i32"_su8, ValueType::I32)),
                MakeAt("mut"_su8, Mutability::Var)},
     "(mut i32)"_su8);
}

TEST_F(TextReadTest, EventType) {
  // Empty event type.
  OK(ReadEventType, EventType{EventAttribute::Exception, {}}, ""_su8);

  // Function type use.
  OK(ReadEventType,
     EventType{EventAttribute::Exception,
               FunctionTypeUse{MakeAt("(type 0)"_su8, Var{Index{0}}), {}}},
     "(type 0)"_su8);
}

TEST_F(TextReadTest, Function) {
  using I = Instruction;
  using O = Opcode;

  // Empty func.
  OK(ReadFunction, Function{}, "(func)"_su8);

  // Name.
  OK(ReadFunction,
     Function{
         FunctionDesc{MakeAt("$f"_su8, "$f"_sv), nullopt, {}}, {}, {}, {}, {}},
     "(func $f)"_su8);

  // Inline export.
  OK(ReadFunction,
     Function{{},
              {},
              {},
              {},
              InlineExportList{MakeAt(
                  "(export \"e\")"_su8,
                  InlineExport{MakeAt("\"e\""_su8, Text{"\"e\""_sv, 1})})}},
     "(func (export \"e\"))"_su8);

  // Locals.
  OK(ReadFunction,
     Function{
         {},
         BoundValueTypeList{
             MakeAt("i32"_su8,
                    BoundValueType{nullopt, MakeAt("i32"_su8, ValueType::I32)}),
             MakeAt("i64"_su8,
                    BoundValueType{nullopt, MakeAt("i64"_su8, ValueType::I64)}),
         },
         {},
         {},
         {}},
     "(func (local i32 i64))"_su8);

  // Instructions.
  OK(ReadFunction,
     Function{{},
              {},
              InstructionList{
                  MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
                  MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
                  MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
              },
              {},
              {}},
     "(func nop nop nop)"_su8);

  // Everything for defined Function.
  OK(ReadFunction,
     Function{FunctionDesc{MakeAt("$f2"_su8, "$f2"_sv), nullopt, {}},
              BoundValueTypeList{MakeAt(
                  "i32"_su8,
                  BoundValueType{nullopt, MakeAt("i32"_su8, ValueType::I32)})},
              InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
              {},
              InlineExportList{MakeAt(
                  "(export \"m\")"_su8,
                  InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(func $f2 (export \"m\") (local i32) nop)"_su8);
}

TEST_F(TextReadTest, Function_DuplicateName) {
  context.function_names.NewBound("$f"_sv);

  Fail(ReadFunction, {{6, "Variable $f is already bound to index 0"}},
       "(func $f)"_su8);
}

TEST_F(TextReadTest, Function_DuplicateParamLocalNames) {
  Fail(ReadFunction, {{28, "Variable $p is already bound to index 0"}},
       "(func (param $p i32) (param $p i32))"_su8);

  Fail(ReadFunction, {{28, "Variable $p is already bound to index 0"}},
       "(func (param $p i32) (local $p i32))"_su8);

  Fail(ReadFunction, {{28, "Variable $p is already bound to index 0"}},
       "(func (local $p i32) (local $p i32))"_su8);
}

TEST_F(TextReadTest, FunctionInlineImport) {
  // Import.
  OK(ReadFunction,
     Function{{},
              {},
              {},
              MakeAt("(import \"m\" \"n\")"_su8,
                     InlineImport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                                  MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})}),
              {}},
     "(func (import \"m\" \"n\"))"_su8);

  // Everything for imported Function.
  OK(ReadFunction,
     Function{
         FunctionDesc{
             MakeAt("$f"_su8, "$f"_sv), nullopt,
             MakeAt(
                 "(param i32)"_su8,
                 BoundFunctionType{
                     {MakeAt("i32"_su8,
                             BoundValueType{
                                 nullopt, MakeAt("i32"_su8, ValueType::I32)})},
                     {}})},
         {},
         {},
         MakeAt("(import \"a\" \"b\")"_su8,
                InlineImport{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                             MakeAt("\"b\""_su8, Text{"\"b\""_sv, 1})}),
         InlineExportList{
             MakeAt("(export \"m\")"_su8,
                    InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(func $f (export \"m\") (import \"a\" \"b\") (param i32))"_su8);
}

TEST_F(TextReadTest, Function_DeferType) {
  using VT = ValueType;
  using BVT = BoundValueType;

  FunctionTypeMap& ftm = context.function_type_map;

  ftm.Define(BoundFunctionType{{BVT{nullopt, VT::I32}}, {}});
  Read(ReadFunction, "(func (param f32))"_su8);
  ftm.Define(BoundFunctionType{{BVT{nullopt, VT::I64}}, {}});
  ftm.EndModule();

  ASSERT_EQ(3u, ftm.Size());
  EXPECT_EQ((FunctionType{{VT::I32}, {}}), ftm.Get(0));
  EXPECT_EQ((FunctionType{{VT::I64}, {}}), ftm.Get(1));

  // Implicitly defined after other explicitly defined types.
  EXPECT_EQ((FunctionType{{MakeAt("f32"_su8, VT::F32)}, {}}), ftm.Get(2));
}

TEST_F(TextReadTest, Table) {
  // Simplest table.
  OK(ReadTable,
     Table{
         TableDesc{
             {},
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         nullopt,
         {},
         {}},
     "(table 0 funcref)"_su8);

  // Name.
  OK(ReadTable,
     Table{
         TableDesc{
             MakeAt("$t"_su8, "$t"_sv),
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         nullopt,
         {},
         {}},
     "(table $t 0 funcref)"_su8);

  // Inline export.
  OK(ReadTable,
     Table{
         TableDesc{
             {},
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         nullopt,
         InlineExportList{
             MakeAt("(export \"m\")"_su8,
                    InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})},
         {}},
     "(table (export \"m\") 0 funcref)"_su8);

  // Name and inline export.
  OK(ReadTable,
     Table{
         TableDesc{
             MakeAt("$t2"_su8, "$t2"_sv),
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         nullopt,
         InlineExportList{
             MakeAt("(export \"m\")"_su8,
                    InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})},
         {}},
     "(table $t2 (export \"m\") 0 funcref)"_su8);

  // Inline element var list.
  OK(ReadTable,
     Table{TableDesc{{},
                     TableType{Limits{u32{3}, u32{3}},
                               MakeAt("funcref"_su8, ElementType::Funcref)}},
           nullopt,
           {},
           ElementListWithVars{ExternalKind::Function,
                               VarList{
                                   MakeAt("0"_su8, Var{Index{0}}),
                                   MakeAt("1"_su8, Var{Index{1}}),
                                   MakeAt("2"_su8, Var{Index{2}}),
                               }}},
     "(table funcref (elem 0 1 2))"_su8);
}

TEST_F(TextReadTest, Table_DuplicateName) {
  context.table_names.NewBound("$t"_sv);

  Fail(ReadTable, {{7, "Variable $t is already bound to index 0"}},
       "(table $t 0 funcref)"_su8);
}

TEST_F(TextReadTest, TableInlineImport) {
  // Inline import.
  OK(ReadTable,
     Table{
         TableDesc{
             {},
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         MakeAt("(import \"m\" \"n\")"_su8,
                InlineImport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                             MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})}),
         {},
         {}},
     "(table (import \"m\" \"n\") 0 funcref)"_su8);

  // Everything for Table import.
  OK(ReadTable,
     Table{
         TableDesc{
             MakeAt("$t"_su8, "$t"_sv),
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         MakeAt("(import \"a\" \"b\")"_su8,
                InlineImport{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                             MakeAt("\"b\""_su8, Text{"\"b\""_sv, 1})}),
         InlineExportList{
             MakeAt("(export \"m\")"_su8,
                    InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})},
         {}},
     "(table $t (export \"m\") (import \"a\" \"b\") 0 funcref)"_su8);
}

TEST_F(TextReadTest, Table_bulk_memory) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadTable,
       {
           {{21, "Expected Rpar, got Lpar"}},
           {{21, "Expected Rpar, got Lpar"}},
       },
       "(table funcref (elem (nop)))"_su8);

  context.features.enable_bulk_memory();

  // Inline element var list.
  OK(ReadTable,
     Table{TableDesc{{},
                     TableType{Limits{u32{2}, u32{2}},
                               MakeAt("funcref"_su8, ElementType::Funcref)}},
           nullopt,
           {},
           ElementListWithExpressions{
               MakeAt("funcref"_su8, ElementType::Funcref),
               ElementExpressionList{
                   ElementExpression{
                       MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
                   ElementExpression{
                       MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
               }}},
     "(table funcref (elem (nop) (nop)))"_su8);
}

TEST_F(TextReadTest, Memory) {
  // Simplest memory.
  OK(ReadMemory,
     Memory{MemoryDesc{{},
                       MakeAt("0"_su8,
                              MemoryType{MakeAt(
                                  "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
            nullopt,
            {},
            {}},
     "(memory 0)"_su8);

  // Name.
  OK(ReadMemory,
     Memory{MemoryDesc{MakeAt("$m"_su8, "$m"_sv),
                       MakeAt("0"_su8,
                              MemoryType{MakeAt(
                                  "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
            nullopt,
            {},
            {}},
     "(memory $m 0)"_su8);

  // Inline export.
  OK(ReadMemory,
     Memory{MemoryDesc{{},
                       MakeAt("0"_su8,
                              MemoryType{MakeAt(
                                  "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
            nullopt,
            InlineExportList{
                MakeAt("(export \"m\")"_su8,
                       InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})},
            {}},
     "(memory (export \"m\") 0)"_su8);

  // Name and inline export.
  OK(ReadMemory,
     Memory{MemoryDesc{MakeAt("$t"_su8, "$t"_sv),
                       MakeAt("0"_su8,
                              MemoryType{MakeAt(
                                  "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
            nullopt,
            InlineExportList{
                MakeAt("(export \"m\")"_su8,
                       InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})},
            {}},
     "(memory $t (export \"m\") 0)"_su8);

  // Inline data segment.
  OK(ReadMemory,
     Memory{MemoryDesc{{}, MemoryType{Limits{u32{10}, u32{10}}}},
            nullopt,
            {},
            TextList{
                MakeAt("\"hello\""_su8, Text{"\"hello\""_sv, 5}),
                MakeAt("\"world\""_su8, Text{"\"world\""_sv, 5}),
            }},
     "(memory (data \"hello\" \"world\"))"_su8);
}

TEST_F(TextReadTest, Memory_DuplicateName) {
  context.memory_names.NewBound("$m"_sv);

  Fail(ReadMemory, {{8, "Variable $m is already bound to index 0"}},
       "(memory $m 0)"_su8);
}

TEST_F(TextReadTest, MemoryInlineImport) {
  // Inline import.
  OK(ReadMemory,
     Memory{MemoryDesc{{},
                       MakeAt("0"_su8,
                              MemoryType{MakeAt(
                                  "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
            MakeAt("(import \"m\" \"n\")"_su8,
                   InlineImport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                                MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})}),
            {},
            {}},
     "(memory (import \"m\" \"n\") 0)"_su8);

  // Everything for Memory import.
  OK(ReadMemory,
     Memory{MemoryDesc{MakeAt("$t"_su8, "$t"_sv),
                       MakeAt("0"_su8,
                              MemoryType{MakeAt(
                                  "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
            MakeAt("(import \"a\" \"b\")"_su8,
                   InlineImport{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                                MakeAt("\"b\""_su8, Text{"\"b\""_sv, 1})}),
            InlineExportList{
                MakeAt("(export \"m\")"_su8,
                       InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})},
            {}},
     "(memory $t (export \"m\") (import \"a\" \"b\") 0)"_su8);
}

TEST_F(TextReadTest, Global) {
  using I = Instruction;
  using O = Opcode;

  // Simplest global.
  OK(ReadGlobal,
     Global{GlobalDesc{
                {},
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})},
            InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
            nullopt,
            {}},
     "(global i32 nop)"_su8);

  // Name.
  OK(ReadGlobal,
     Global{GlobalDesc{
                MakeAt("$g"_su8, "$g"_sv),
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})},
            InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
            nullopt,
            {}},
     "(global $g i32 nop)"_su8);

  // Inline export.
  OK(ReadGlobal,
     Global{GlobalDesc{
                {},
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})},
            InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
            nullopt,
            InlineExportList{MakeAt(
                "(export \"m\")"_su8,
                InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(global (export \"m\") i32 nop)"_su8);

  // Name and inline export.
  OK(ReadGlobal,
     Global{GlobalDesc{
                MakeAt("$g2"_su8, "$g2"_sv),
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})},
            InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
            nullopt,
            InlineExportList{MakeAt(
                "(export \"m\")"_su8,
                InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(global $g2 (export \"m\") i32 nop)"_su8);
}

TEST_F(TextReadTest, Global_DuplicateName) {
  context.global_names.NewBound("$g"_sv);

  Fail(ReadGlobal, {{8, "Variable $g is already bound to index 0"}},
       "(global $g i32 (nop))"_su8);
}

TEST_F(TextReadTest, GlobalInlineImport) {
  // Inline import.
  OK(ReadGlobal,
     Global{GlobalDesc{
                {},
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})},
            {},
            MakeAt("(import \"m\" \"n\")"_su8,
                   InlineImport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                                MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})}),
            {}},
     "(global (import \"m\" \"n\") i32)"_su8);

  // Everything for Global import.
  OK(ReadGlobal,
     Global{GlobalDesc{
                MakeAt("$g"_su8, "$g"_sv),
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})},
            {},
            MakeAt("(import \"a\" \"b\")"_su8,
                   InlineImport{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                                MakeAt("\"b\""_su8, Text{"\"b\""_sv, 1})}),
            InlineExportList{MakeAt(
                "(export \"m\")"_su8,
                InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(global $g (export \"m\") (import \"a\" \"b\") i32)"_su8);
}

TEST_F(TextReadTest, Event) {
  Fail(ReadEvent, {{0, "Events not allowed"}}, "(event)"_su8);

  context.features.enable_exceptions();

  // Simplest event.
  OK(ReadEvent, Event{}, "(event)"_su8);

  // Name.
  OK(ReadEvent, Event{EventDesc{MakeAt("$e"_su8, "$e"_sv), {}}, {}, {}},
     "(event $e)"_su8);

  // Inline export.
  OK(ReadEvent,
     Event{EventDesc{}, nullopt,
           InlineExportList{
               MakeAt("(export \"m\")"_su8,
                      InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(event (export \"m\"))"_su8);

  // Name and inline export.
  OK(ReadEvent,
     Event{EventDesc{MakeAt("$e2"_su8, "$e2"_sv), {}}, nullopt,
           InlineExportList{
               MakeAt("(export \"m\")"_su8,
                      InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(event $e2 (export \"m\"))"_su8);
}

TEST_F(TextReadTest, Event_DuplicateName) {
  context.features.enable_exceptions();
  context.event_names.NewBound("$e"_sv);

  Fail(ReadEvent, {{7, "Variable $e is already bound to index 0"}},
       "(event $e)"_su8);
}

TEST_F(TextReadTest, EventInlineImport) {
  Fail(ReadEvent, {{0, "Events not allowed"}},
       "(event (import \"m\" \"n\"))"_su8);

  context.features.enable_exceptions();

  // Inline import.
  OK(ReadEvent,
     Event{EventDesc{},
           MakeAt("(import \"m\" \"n\")"_su8,
                  InlineImport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                               MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1})}),
           {}},
     "(event (import \"m\" \"n\"))"_su8);

  // Everything for event import.
  OK(ReadEvent,
     Event{EventDesc{MakeAt("$e"_su8, "$e"_sv), {}},
           MakeAt("(import \"a\" \"b\")"_su8,
                  InlineImport{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                               MakeAt("\"b\""_su8, Text{"\"b\""_sv, 1})}),
           InlineExportList{
               MakeAt("(export \"m\")"_su8,
                      InlineExport{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1})})}},
     "(event $e (export \"m\") (import \"a\" \"b\"))"_su8);
}

TEST_F(TextReadTest, Import) {
  // Function.
  OK(ReadImport,
     Import{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1}), FunctionDesc{}},
     "(import \"m\" \"n\" (func))"_su8);

  // Table.
  OK(ReadImport,
     Import{
         MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
         MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1}),
         TableDesc{
             nullopt,
             MakeAt("1 funcref"_su8,
                    TableType{MakeAt("1"_su8, Limits{MakeAt("1"_su8, u32{1})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})}},
     "(import \"m\" \"n\" (table 1 funcref))"_su8);

  // Memory.
  OK(ReadImport,
     Import{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1}),
            MemoryDesc{nullopt,
                       MakeAt("1"_su8,
                              MemoryType{MakeAt(
                                  "1"_su8, Limits{MakeAt("1"_su8, u32{1})})})}},
     "(import \"m\" \"n\" (memory 1))"_su8);

  // Global.
  OK(ReadImport,
     Import{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1}),
            GlobalDesc{
                nullopt,
                MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                             Mutability::Const})}},
     "(import \"m\" \"n\" (global i32))"_su8);
}

TEST_F(TextReadTest, Import_AfterNonImport) {
  context.seen_non_import = true;
  Fail(ReadImport,
       {{1, "Imports must occur before all non-import definitions"}},
       "(import \"m\" \"n\" (func))"_su8);
}

TEST_F(TextReadTest, Import_FunctionDeferType) {
  using VT = ValueType;
  using BVT = BoundValueType;

  FunctionTypeMap& ftm = context.function_type_map;

  ftm.Define(BoundFunctionType{{BVT{nullopt, VT::I32}}, {}});
  Read(ReadImport, "(import \"m\" \"n\" (func (param f32)))"_su8);
  ftm.Define(BoundFunctionType{{BVT{nullopt, VT::I64}}, {}});
  ftm.EndModule();

  ASSERT_EQ(3u, ftm.Size());
  EXPECT_EQ((FunctionType{{VT::I32}, {}}), ftm.Get(0));
  EXPECT_EQ((FunctionType{{VT::I64}, {}}), ftm.Get(1));

  // Implicitly defined after other explicitly defined types.
  EXPECT_EQ((FunctionType{{MakeAt("f32"_su8, VT::F32)}, {}}), ftm.Get(2));
}

TEST_F(TextReadTest, Import_exceptions) {
  Fail(ReadImport, {{17, "Events not allowed"}},
       "(import \"m\" \"n\" (event))"_su8);

  context.features.enable_exceptions();

  // Event.
  OK(ReadImport,
     Import{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1}), EventDesc{}},
     "(import \"m\" \"n\" (event))"_su8);
}

TEST_F(TextReadTest, Export) {
  // Function.
  OK(ReadExport,
     Export{MakeAt("func"_su8, ExternalKind::Function),
            MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("0"_su8, Var{Index{0}})},
     "(export \"m\" (func 0))"_su8);

  // Table.
  OK(ReadExport,
     Export{MakeAt("table"_su8, ExternalKind::Table),
            MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("0"_su8, Var{Index{0}})},
     "(export \"m\" (table 0))"_su8);

  // Memory.
  OK(ReadExport,
     Export{MakeAt("memory"_su8, ExternalKind::Memory),
            MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("0"_su8, Var{Index{0}})},
     "(export \"m\" (memory 0))"_su8);

  // Global.
  OK(ReadExport,
     Export{MakeAt("global"_su8, ExternalKind::Global),
            MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("0"_su8, Var{Index{0}})},
     "(export \"m\" (global 0))"_su8);
}

TEST_F(TextReadTest, Export_exceptions) {
  Fail(ReadExport, {{13, "Events not allowed"}},
       "(export \"m\" (event 0))"_su8);

  context.features.enable_exceptions();

  // Event.
  OK(ReadExport,
     Export{MakeAt("event"_su8, ExternalKind::Event),
            MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
            MakeAt("0"_su8, Var{Index{0}})},
     "(export \"m\" (event 0))"_su8);
}

TEST_F(TextReadTest, Start) {
  OK(ReadStart, Start{MakeAt("0"_su8, Var{Index{0}})}, "(start 0)"_su8);
}

TEST_F(TextReadTest, Start_Multiple) {
  context.seen_start = true;
  Fail(ReadStart, {{1, "Multiple start functions"}}, "(start 0)"_su8);
}

TEST_F(TextReadTest, ElementExpression) {
  using I = Instruction;
  using O = Opcode;

  context.features.enable_bulk_memory();

  // Item.
  OK(ReadElementExpression,
     ElementExpression{
         MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
     },
     "(item nop)"_su8);

  // Expression.
  OK(ReadElementExpression,
     ElementExpression{
         MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)}),
     },
     "(nop)"_su8);
}

TEST_F(TextReadTest, OffsetExpression) {
  using I = Instruction;
  using O = Opcode;

  // Expression.
  OK(ReadOffsetExpression,
     InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
     "(nop)"_su8);

  // Offset keyword.
  OK(ReadOffsetExpression,
     InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
     "(offset nop)"_su8);
}

TEST_F(TextReadTest, ElementExpressionList) {
  using I = Instruction;
  using O = Opcode;

  context.features.enable_bulk_memory();

  // Item list.
  OKVector(
      ReadElementExpressionList,
      ElementExpressionList{
          ElementExpression{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
          ElementExpression{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
      },
      "(item nop) (item nop)"_su8);

  // Expression list.
  OKVector(
      ReadElementExpressionList,
      ElementExpressionList{
          ElementExpression{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
          ElementExpression{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
      },
      "(nop) (nop)"_su8);
}

TEST_F(TextReadTest, TableUseOpt) {
  OK(ReadTableUseOpt, Var{Index{0}}, "(table 0)"_su8);
  OK(ReadTableUseOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, ElementSegment_MVP) {
  using I = Instruction;
  using O = Opcode;

  // No table var, empty var list.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{ExternalKind::Function, {}}},
     "(elem (nop))"_su8);

  // No table var, var list.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{ExternalKind::Function,
                             VarList{MakeAt("0"_su8, Var{Index{0}}),
                                     MakeAt("1"_su8, Var{Index{1}}),
                                     MakeAt("2"_su8, Var{Index{2}})}}},
     "(elem (nop) 0 1 2)"_su8);

  // Table var.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, MakeAt("0"_su8, Var{Index{0}}),
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{ExternalKind::Function, {}}},
     "(elem 0 (nop))"_su8);

  // Table var as Id.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, MakeAt("$t"_su8, Var{"$t"_sv}),
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{ExternalKind::Function, {}}},
     "(elem $t (nop))"_su8);
}

TEST_F(TextReadTest, ElementSegment_bulk_memory) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadElementSegment,
       {
           {{6, "Expected offset expression, got ValueType"}},
           {{6, "Expected Rpar, got ValueType"}},
       },
       "(elem funcref)"_su8);

  Fail(ReadElementSegment,
       {
           {{6, "Expected offset expression, got Func"}},
           {{6, "Expected Rpar, got Func"}},
       },
       "(elem func)"_su8);

  context.features.enable_bulk_memory();

  // Passive, w/ expression list.
  OK(ReadElementSegment,
     ElementSegment{nullopt, SegmentType::Passive,
                    ElementListWithExpressions{
                        MakeAt("funcref"_su8, ElementType::Funcref),
                        ElementExpressionList{
                            ElementExpression{MakeAt(
                                "nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
                            ElementExpression{MakeAt(
                                "nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
                        }}},
     "(elem funcref (nop) (nop))"_su8);

  // Passive, w/ var list.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, SegmentType::Passive,
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function),
                             VarList{
                                 MakeAt("0"_su8, Var{Index{0}}),
                                 MakeAt("$e"_su8, Var{"$e"_sv}),
                             }}},
     "(elem func 0 $e)"_su8);

  // Passive w/ name.
  OK(ReadElementSegment,
     ElementSegment{
         MakeAt("$e"_su8, "$e"_sv), SegmentType::Passive,
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function), {}}},
     "(elem $e func)"_su8);

  // Declared, w/ expression list.
  OK(ReadElementSegment,
     ElementSegment{nullopt, SegmentType::Declared,
                    ElementListWithExpressions{
                        MakeAt("funcref"_su8, ElementType::Funcref),
                        ElementExpressionList{
                            ElementExpression{MakeAt(
                                "nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
                            ElementExpression{MakeAt(
                                "nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
                        }}},
     "(elem declare funcref (nop) (nop))"_su8);

  // Declared, w/ var list.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, SegmentType::Declared,
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function),
                             VarList{
                                 MakeAt("0"_su8, Var{Index{0}}),
                                 MakeAt("$e"_su8, Var{"$e"_sv}),
                             }}},
     "(elem declare func 0 $e)"_su8);

  // Declared w/ name.
  OK(ReadElementSegment,
     ElementSegment{
         MakeAt("$e2"_su8, "$e2"_sv), SegmentType::Declared,
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function), {}}},
     "(elem $e2 declare func)"_su8);

  // Active legacy, empty
  OK(ReadElementSegment,
     ElementSegment{
         nullopt,
         nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         {}},
     "(elem (nop))"_su8);

  // Active legacy (i.e. no element type or external kind).
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{ExternalKind::Function,
                             VarList{
                                 MakeAt("0"_su8, Var{Index{0}}),
                                 MakeAt("$e"_su8, Var{"$e"_sv}),
                             }}},
     "(elem (nop) 0 $e)"_su8);

  // Active, w/ var list.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function),
                             VarList{
                                 MakeAt("0"_su8, Var{Index{0}}),
                                 MakeAt("$e"_su8, Var{"$e"_sv}),
                             }}},
     "(elem (nop) func 0 $e)"_su8);

  // Active, w/ expression list.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithExpressions{
             MakeAt("funcref"_su8, ElementType::Funcref),
             ElementExpressionList{
                 ElementExpression{
                     MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
                 ElementExpression{
                     MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
             }}},
     "(elem (nop) funcref (nop) (nop))"_su8);

  // Active w/ table use.
  OK(ReadElementSegment,
     ElementSegment{
         nullopt, MakeAt("(table 0)"_su8, Var{Index{0}}),
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function),
                             VarList{
                                 MakeAt("1"_su8, Var{Index{1}}),
                             }}},
     "(elem (table 0) (nop) func 1)"_su8);

  // Active w/ name.
  OK(ReadElementSegment,
     ElementSegment{
         MakeAt("$e3"_su8, "$e3"_sv), nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         ElementListWithVars{MakeAt("func"_su8, ExternalKind::Function), {}}},
     "(elem $e3 (nop) func)"_su8);
}

TEST_F(TextReadTest, ElementSegment_DuplicateName) {
  context.features.enable_bulk_memory();
  context.element_segment_names.NewBound("$e"_sv);

  Fail(ReadElementSegment, {{6, "Variable $e is already bound to index 0"}},
       "(elem $e func)"_su8);
}

TEST_F(TextReadTest, DataSegment_MVP) {
  using I = Instruction;
  using O = Opcode;

  // No memory var, empty text list.
  OK(ReadDataSegment,
     DataSegment{
         nullopt,
         nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         {}},
     "(data (nop))"_su8);

  // No memory var, text list.
  OK(ReadDataSegment,
     DataSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         TextList{MakeAt("\"hi\""_su8, Text{"\"hi\""_sv, 2})}},
     "(data (nop) \"hi\")"_su8);

  // Memory var.
  OK(ReadDataSegment,
     DataSegment{
         nullopt,
         MakeAt("0"_su8, Var{Index{0}}),
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         {}},
     "(data 0 (nop))"_su8);

  // Memory var as Id.
  OK(ReadDataSegment,
     DataSegment{
         nullopt,
         MakeAt("$m"_su8, Var{"$m"_sv}),
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         {}},
     "(data $m (nop))"_su8);
}

TEST_F(TextReadTest, DataSegment_bulk_memory) {
  using I = Instruction;
  using O = Opcode;

  Fail(ReadDataSegment, {{5, "Expected offset expression, got Rpar"}},
       "(data)"_su8);

  context.features.enable_bulk_memory();

  // Passive, w/ text list.
  OK(ReadDataSegment,
     DataSegment{nullopt,
                 TextList{
                     MakeAt("\"hi\""_su8, Text{"\"hi\""_sv, 2}),
                 }},
     "(data \"hi\")"_su8);

  // Passive w/ name.
  OK(ReadDataSegment, DataSegment{MakeAt("$d"_su8, "$d"_sv), {}},
     "(data $d)"_su8);

  // Active, w/ text list.
  OK(ReadDataSegment,
     DataSegment{
         nullopt, nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         TextList{
             MakeAt("\"hi\""_su8, Text{"\"hi\""_sv, 2}),
         }},
     "(data (nop) \"hi\")"_su8);

  // Active w/ memory use.
  OK(ReadDataSegment,
     DataSegment{
         nullopt, MakeAt("(memory 0)"_su8, Var{Index{0}}),
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         TextList{
             MakeAt("\"hi\""_su8, Text{"\"hi\""_sv, 2}),
         }},
     "(data (memory 0) (nop) \"hi\")"_su8);

  // Active w/ name.
  OK(ReadDataSegment,
     DataSegment{
         MakeAt("$d2"_su8, "$d2"_sv),
         nullopt,
         InstructionList{MakeAt("nop"_su8, I{MakeAt("nop"_su8, O::Nop)})},
         {}},
     "(data $d2 (nop))"_su8);
}

TEST_F(TextReadTest, DataSegment_DuplicateName) {
  context.features.enable_bulk_memory();
  context.data_segment_names.NewBound("$d"_sv);

  Fail(ReadDataSegment, {{6, "Variable $d is already bound to index 0"}},
       "(data $d)"_su8);
}

TEST_F(TextReadTest, ModuleItem) {
  // Type.
  OK(ReadModuleItem, ModuleItem{TypeEntry{nullopt, BoundFunctionType{}}},
     "(type (func))"_su8);

  // Import.
  OK(ReadModuleItem,
     ModuleItem{Import{MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
                       MakeAt("\"n\""_su8, Text{"\"n\""_sv, 1}),
                       FunctionDesc{}}},
     "(import \"m\" \"n\" (func))"_su8);

  // Func.
  OK(ReadModuleItem, ModuleItem{Function{}}, "(func)"_su8);

  // Table.
  OK(ReadModuleItem,
     ModuleItem{Table{
         TableDesc{
             nullopt,
             MakeAt("0 funcref"_su8,
                    TableType{MakeAt("0"_su8, Limits{MakeAt("0"_su8, u32{0})}),
                              MakeAt("funcref"_su8, ElementType::Funcref)})},
         nullopt,
         {},
         nullopt}},
     "(table 0 funcref)"_su8);

  // Memory.
  OK(ReadModuleItem,
     ModuleItem{Memory{
         MemoryDesc{
             nullopt,
             MakeAt("0"_su8, MemoryType{MakeAt(
                                 "0"_su8, Limits{MakeAt("0"_su8, u32{0})})})},
         nullopt,
         {},
         nullopt}},
     "(memory 0)"_su8);

  // Global.
  OK(ReadModuleItem,
     ModuleItem{Global{
         GlobalDesc{
             nullopt,
             MakeAt("i32"_su8, GlobalType{MakeAt("i32"_su8, ValueType::I32),
                                          Mutability::Const})},
         InstructionList{
             MakeAt("nop"_su8, Instruction{MakeAt("nop"_su8, Opcode::Nop)})},
         nullopt,
         {}}},
     "(global i32 (nop))"_su8);

  // Export.
  OK(ReadModuleItem,
     ModuleItem{Export{
         MakeAt("func"_su8, ExternalKind::Function),
         MakeAt("\"m\""_su8, Text{"\"m\""_sv, 1}),
         MakeAt("0"_su8, Var{Index{0}}),
     }},
     "(export \"m\" (func 0))"_su8);

  // Start.
  OK(ReadModuleItem, ModuleItem{Start{MakeAt("0"_su8, Var{Index{0}})}},
     "(start 0)"_su8);

  // Elem.
  OK(ReadModuleItem,
     ModuleItem{ElementSegment{
         nullopt,
         nullopt,
         InstructionList{
             MakeAt("nop"_su8, Instruction{MakeAt("nop"_su8, Opcode::Nop)})},
         {}}},
     "(elem (nop))"_su8);

  // Data.
  OK(ReadModuleItem,
     ModuleItem{DataSegment{
         nullopt,
         nullopt,
         InstructionList{
             MakeAt("nop"_su8, Instruction{MakeAt("nop"_su8, Opcode::Nop)})},
         {}}},
     "(data (nop))"_su8);
}

TEST_F(TextReadTest, ModuleItem_exceptions) {
  Fail(ReadModuleItem, {{0, "Events not allowed"}}, "(event)"_su8);

  context.features.enable_exceptions();

  // Event.
  OK(ReadModuleItem,
     ModuleItem{
         Event{EventDesc{nullopt, EventType{EventAttribute::Exception,
                                            FunctionTypeUse{nullopt, {}}}},
               nullopt,
               {}}},
     "(event)"_su8);
}

TEST_F(TextReadTest, Module) {
  OK(ReadModule,
     Module{
         MakeAt("(type (func))"_su8,
                ModuleItem{TypeEntry{nullopt, BoundFunctionType{}}}),
         MakeAt(
             "(func nop)"_su8,
             ModuleItem{Function{
                 FunctionDesc{},
                 {},
                 InstructionList{MakeAt(
                     "nop"_su8, Instruction{MakeAt("nop"_su8, Opcode::Nop)})},
                 nullopt,
                 {}

             }}),
         MakeAt("(start 0)"_su8,
                ModuleItem{Start{MakeAt("0"_su8, Var{Index{0}})}})},
     "(type (func)) (func nop) (start 0)"_su8);
}

TEST_F(TextReadTest, ModuleVarOpt) {
  OK(ReadModuleVarOpt, ModuleVar{"$m"_sv}, "$m"_su8);
  OK(ReadModuleVarOpt, nullopt, ""_su8);
}

TEST_F(TextReadTest, ScriptModule) {
  // Text module.
  OK(ReadScriptModule, ScriptModule{nullopt, ScriptModuleKind::Text, Module{}},
     "(module)"_su8);

  // Binary module.
  OK(ReadScriptModule,
     ScriptModule{nullopt, ScriptModuleKind::Binary,
                  TextList{MakeAt("\"\""_su8, Text{"\"\""_sv, 0})}},
     "(module binary \"\")"_su8);

  // Quote module.
  OK(ReadScriptModule,
     ScriptModule{nullopt, ScriptModuleKind::Quote,
                  TextList{MakeAt("\"\""_su8, Text{"\"\""_sv, 0})}},
     "(module quote \"\")"_su8);

  // Text module w/ Name.
  OK(ReadScriptModule,
     ScriptModule{MakeAt("$m"_su8, "$m"_sv), ScriptModuleKind::Text, Module{}},
     "(module $m)"_su8);

  // Binary module w/ Name.
  OK(ReadScriptModule,
     ScriptModule{MakeAt("$m"_su8, "$m"_sv), ScriptModuleKind::Binary,
                  TextList{MakeAt("\"\""_su8, Text{"\"\""_sv, 0})}},
     "(module $m binary \"\")"_su8);

  // Quote module w/ Name.
  OK(ReadScriptModule,
     ScriptModule{MakeAt("$m"_su8, "$m"_sv), ScriptModuleKind::Quote,
                  TextList{MakeAt("\"\""_su8, Text{"\"\""_sv, 0})}},
     "(module $m quote \"\")"_su8);
}

TEST_F(TextReadTest, Const) {
  // i32.const
  OK(ReadConst, Const{u32{0}}, "(i32.const 0)"_su8);

  // i64.const
  OK(ReadConst, Const{u64{0}}, "(i64.const 0)"_su8);

  // f32.const
  OK(ReadConst, Const{f32{0}}, "(f32.const 0)"_su8);

  // f64.const
  OK(ReadConst, Const{f64{0}}, "(f64.const 0)"_su8);
}

TEST_F(TextReadTest, Const_simd) {
  Fail(ReadConst, {{1, "Simd values not allowed"}},
       "(v128.const i32x4 0 0 0 0)"_su8);

  context.features.enable_simd();

  OK(ReadConst, Const{v128{}}, "(v128.const i32x4 0 0 0 0)"_su8);
}

TEST_F(TextReadTest, Const_reference_types) {
  Fail(ReadConst, {{1, "ref.null not allowed"}}, "(ref.null)"_su8);
  Fail(ReadConst, {{1, "ref.host not allowed"}}, "(ref.host 0)"_su8);

  context.features.enable_reference_types();

  OK(ReadConst, Const{RefNullConst{}}, "(ref.null)"_su8);
  OK(ReadConst, Const{RefHostConst{MakeAt("0"_su8, u32{0})}},
     "(ref.host 0)"_su8);
}

TEST_F(TextReadTest, ConstList) {
  OKVector(ReadConstList, ConstList{}, ""_su8);

  OKVector(ReadConstList,
           ConstList{
               MakeAt("(i32.const 0)"_su8, Const{u32{0}}),
               MakeAt("(i64.const 1)"_su8, Const{u64{1}}),
           },
           "(i32.const 0) (i64.const 1)"_su8);
}

TEST_F(TextReadTest, InvokeAction) {
  // Name.
  OK(ReadInvokeAction,
     InvokeAction{nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}},
     "(invoke \"a\")"_su8);

  // Module.
  OK(ReadInvokeAction,
     InvokeAction{MakeAt("$m"_su8, "$m"_sv),
                  MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                  {}},
     "(invoke $m \"a\")"_su8);

  // Const list.
  OK(ReadInvokeAction,
     InvokeAction{nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                  ConstList{MakeAt("(i32.const 0)"_su8, Const{u32{0}})}},
     "(invoke \"a\" (i32.const 0))"_su8);
}

TEST_F(TextReadTest, GetAction) {
  // Name.
  OK(ReadGetAction,
     GetAction{nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1})},
     "(get \"a\")"_su8);

  // Module.
  OK(ReadGetAction,
     GetAction{MakeAt("$m"_su8, "$m"_sv),
               MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1})},
     "(get $m \"a\")"_su8);
}

TEST_F(TextReadTest, Action) {
  // Get action.
  OK(ReadAction,
     Action{GetAction{nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1})}},
     "(get \"a\")"_su8);

  // Invoke action.
  OK(ReadAction,
     Action{
         InvokeAction{nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}},
     "(invoke \"a\")"_su8);
}

TEST_F(TextReadTest, ModuleAssertion) {
  OK(ReadModuleAssertion,
     ModuleAssertion{
         MakeAt("(module)"_su8,
                ScriptModule{nullopt, ScriptModuleKind::Text, Module{}}),
         MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})},
     "(module) \"msg\""_su8);
}

TEST_F(TextReadTest, ActionAssertion) {
  OK(ReadActionAssertion,
     ActionAssertion{
         MakeAt("(invoke \"a\")"_su8,
                Action{InvokeAction{
                    nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}}),
         MakeAt("\"msg\""_su8, Text{"\"msg\"", 3}),
     },
     "(invoke \"a\") \"msg\""_su8);
}

TEST_F(TextReadTest, FloatResult) {
  OK(ReadFloatResult<f32>, F32Result{f32{0}}, "0"_su8);
  OK(ReadFloatResult<f32>, F32Result{NanKind::Arithmetic},
     "nan:arithmetic"_su8);
  OK(ReadFloatResult<f32>, F32Result{NanKind::Canonical}, "nan:canonical"_su8);

  OK(ReadFloatResult<f64>, F64Result{f64{0}}, "0"_su8);
  OK(ReadFloatResult<f64>, F64Result{NanKind::Arithmetic},
     "nan:arithmetic"_su8);
  OK(ReadFloatResult<f64>, F64Result{NanKind::Canonical}, "nan:canonical"_su8);
}

TEST_F(TextReadTest, SimdFloatResult) {
  OK(ReadSimdFloatResult<f32, 4>,
     ReturnResult{F32x4Result{
         F32Result{f32{0}},
         F32Result{f32{0}},
         F32Result{f32{0}},
         F32Result{f32{0}},
     }},
     "0 0 0 0"_su8);

  OK(ReadSimdFloatResult<f32, 4>,
     ReturnResult{F32x4Result{
         F32Result{f32{0}},
         F32Result{NanKind::Arithmetic},
         F32Result{f32{0}},
         F32Result{NanKind::Canonical},
     }},
     "0 nan:arithmetic 0 nan:canonical"_su8);

  OK(ReadSimdFloatResult<f64, 2>,
     ReturnResult{F64x2Result{
         F64Result{f64{0}},
         F64Result{f64{0}},
     }},
     "0 0"_su8);

  OK(ReadSimdFloatResult<f64, 2>,
     ReturnResult{F64x2Result{
         F64Result{NanKind::Arithmetic},
         F64Result{f64{0}},
     }},
     "nan:arithmetic 0"_su8);
}

TEST_F(TextReadTest, ReturnResult) {
  OK(ReadReturnResult, ReturnResult{u32{0}}, "(i32.const 0)"_su8);

  OK(ReadReturnResult, ReturnResult{u64{0}}, "(i64.const 0)"_su8);

  OK(ReadReturnResult, ReturnResult{F32Result{f32{0}}}, "(f32.const 0)"_su8);
  OK(ReadReturnResult, ReturnResult{F32Result{NanKind::Arithmetic}},
     "(f32.const nan:arithmetic)"_su8);
  OK(ReadReturnResult, ReturnResult{F32Result{NanKind::Canonical}},
     "(f32.const nan:canonical)"_su8);

  OK(ReadReturnResult, ReturnResult{F64Result{f64{0}}}, "(f64.const 0)"_su8);
  OK(ReadReturnResult, ReturnResult{F64Result{NanKind::Arithmetic}},
     "(f64.const nan:arithmetic)"_su8);
  OK(ReadReturnResult, ReturnResult{F64Result{NanKind::Canonical}},
     "(f64.const nan:canonical)"_su8);
}

TEST_F(TextReadTest, ReturnResult_simd) {
  Fail(ReadConst, {{1, "Simd values not allowed"}},
       "(v128.const i32x4 0 0 0 0)"_su8);

  context.features.enable_simd();

  OK(ReadReturnResult, ReturnResult{v128{}},
     "(v128.const i8x16 0 0 0 0  0 0 0 0  0 0 0 0  0 0 0 0)"_su8);
  OK(ReadReturnResult, ReturnResult{v128{}},
     "(v128.const i16x8 0 0 0 0  0 0 0 0)"_su8);
  OK(ReadReturnResult, ReturnResult{v128{}},
     "(v128.const i32x4 0 0 0 0)"_su8);
  OK(ReadReturnResult, ReturnResult{v128{}}, "(v128.const i64x2 0 0)"_su8);
  OK(ReadReturnResult, ReturnResult{F32x4Result{}},
     "(v128.const f32x4 0 0 0 0)"_su8);
  OK(ReadReturnResult, ReturnResult{F64x2Result{}},
     "(v128.const f64x2 0 0)"_su8);

  OK(ReadReturnResult,
     ReturnResult{F32x4Result{
         F32Result{0},
         F32Result{NanKind::Arithmetic},
         F32Result{0},
         F32Result{NanKind::Canonical},
     }},
     "(v128.const f32x4 0 nan:arithmetic 0 nan:canonical)"_su8);

  OK(ReadReturnResult,
     ReturnResult{F64x2Result{
         F64Result{0},
         F64Result{NanKind::Arithmetic},
     }},
     "(v128.const f64x2 0 nan:arithmetic)"_su8);
}

TEST_F(TextReadTest, ReturnResult_reference_types) {
  Fail(ReadReturnResult, {{1, "ref.null not allowed"}}, "(ref.null)"_su8);
  Fail(ReadReturnResult, {{1, "ref.host not allowed"}}, "(ref.host 0)"_su8);
  Fail(ReadReturnResult, {{1, "ref.any not allowed"}}, "(ref.any)"_su8);
  Fail(ReadReturnResult, {{1, "ref.func not allowed"}}, "(ref.func)"_su8);

  context.features.enable_reference_types();

  OK(ReadReturnResult, ReturnResult{RefNullConst{}}, "(ref.null)"_su8);
  OK(ReadReturnResult, ReturnResult{RefHostConst{MakeAt("0"_su8, u32{0})}},
     "(ref.host 0)"_su8);
  OK(ReadReturnResult, ReturnResult{RefAnyResult{}}, "(ref.any)"_su8);
  OK(ReadReturnResult, ReturnResult{RefFuncResult{}}, "(ref.func)"_su8);
}

TEST_F(TextReadTest, ReturnResultList) {
  OK(ReadReturnResultList, ReturnResultList{}, ""_su8);

  OK(ReadReturnResultList,
     ReturnResultList{
         MakeAt("(i32.const 0)"_su8, ReturnResult{u32{0}}),
         MakeAt("(f32.const nan:canonical)"_su8,
                ReturnResult{F32Result{NanKind::Canonical}}),
     },
     "(i32.const 0) (f32.const nan:canonical)"_su8);
}

TEST_F(TextReadTest, ReturnAssertion) {
  OK(ReadReturnAssertion,
     ReturnAssertion{
         MakeAt("(invoke \"a\")"_su8,
                Action{InvokeAction{
                    nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}}),
         {}},
     "(invoke \"a\")"_su8);

  OK(ReadReturnAssertion,
     ReturnAssertion{
         MakeAt("(invoke \"a\" (i32.const 0))"_su8,
                Action{InvokeAction{
                    nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
                    ConstList{MakeAt("(i32.const 0)"_su8, Const{u32{0}})}}}),
         ReturnResultList{MakeAt("(i32.const 1)"_su8, ReturnResult{u32{1}})}},
     "(invoke \"a\" (i32.const 0)) (i32.const 1)"_su8);
}

TEST_F(TextReadTest, Assertion) {
  // assert_malformed
  OK(ReadAssertion,
     Assertion{AssertionKind::Malformed,
               ModuleAssertion{
                   MakeAt("(module)"_su8,
                          ScriptModule{nullopt, ScriptModuleKind::Text, {}}),
                   MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})}},
     "(assert_malformed (module) \"msg\")"_su8);

  // assert_invalid
  OK(ReadAssertion,
     Assertion{AssertionKind::Invalid,
               ModuleAssertion{
                   MakeAt("(module)"_su8,
                          ScriptModule{nullopt, ScriptModuleKind::Text, {}}),
                   MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})}},
     "(assert_invalid (module) \"msg\")"_su8);

  // assert_unlinkable
  OK(ReadAssertion,
     Assertion{AssertionKind::Unlinkable,
               ModuleAssertion{
                   MakeAt("(module)"_su8,
                          ScriptModule{nullopt, ScriptModuleKind::Text, {}}),
                   MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})}},
     "(assert_unlinkable (module) \"msg\")"_su8);

  // assert_trap (module)
  OK(ReadAssertion,
     Assertion{AssertionKind::ModuleTrap,
               ModuleAssertion{
                   MakeAt("(module)"_su8,
                          ScriptModule{nullopt, ScriptModuleKind::Text, {}}),
                   MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})}},
     "(assert_trap (module) \"msg\")"_su8);

  // assert_return
  OK(ReadAssertion,
     Assertion{
         AssertionKind::Return,
         ReturnAssertion{
             MakeAt(
                 "(invoke \"a\")"_su8,
                 Action{InvokeAction{
                     nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}}),
             {}}},
     "(assert_return (invoke \"a\"))"_su8);

  // assert_trap (action)
  OK(ReadAssertion,
     Assertion{
         AssertionKind::ActionTrap,
         ActionAssertion{
             MakeAt(
                 "(invoke \"a\")"_su8,
                 Action{InvokeAction{
                     nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}}),
             MakeAt("\"msg\""_su8, Text{"\"msg\""_sv, 3})}},
     "(assert_trap (invoke \"a\") \"msg\")"_su8);

  // assert_exhaustion
  OK(ReadAssertion,
     Assertion{
         AssertionKind::Exhaustion,
         ActionAssertion{
             MakeAt(
                 "(invoke \"a\")"_su8,
                 Action{InvokeAction{
                     nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}}),
             MakeAt("\"msg\""_su8, Text{"\"msg\""_sv, 3})}},
     "(assert_exhaustion (invoke \"a\") \"msg\")"_su8);
}

TEST_F(TextReadTest, Register) {
  OK(ReadRegister, Register{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), nullopt},
     "(register \"a\")"_su8);

  OK(ReadRegister,
     Register{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}),
              MakeAt("$m"_su8, "$m"_sv)},
     "(register \"a\" $m)"_su8);
}

TEST_F(TextReadTest, Command) {
  // Module.
  OK(ReadCommand, Command{ScriptModule{nullopt, ScriptModuleKind::Text, {}}},
     "(module)"_su8);

  // Action.
  OK(ReadCommand,
     Command{
         InvokeAction{nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}},
     "(invoke \"a\")"_su8);

  // Assertion.
  OK(ReadCommand,
     Command{Assertion{
         AssertionKind::Invalid,
         ModuleAssertion{
             MakeAt("(module)"_su8,
                    ScriptModule{nullopt, ScriptModuleKind::Text, {}}),
             MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})}}},
     "(assert_invalid (module) \"msg\")"_su8);

  // Register.
  OK(ReadCommand,
     Command{Register{MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), nullopt}},
     "(register \"a\")"_su8);
}

TEST_F(TextReadTest, Script) {
  OKVector(
      ReadScript,
      Script{
          MakeAt("(module)"_su8,
                 Command{ScriptModule{nullopt, ScriptModuleKind::Text, {}}}),
          MakeAt("(invoke \"a\")"_su8,
                 Command{InvokeAction{
                     nullopt, MakeAt("\"a\""_su8, Text{"\"a\""_sv, 1}), {}}}),
          MakeAt(
              "(assert_invalid (module) \"msg\")"_su8,
              Command{Assertion{
                  AssertionKind::Invalid,
                  ModuleAssertion{
                      MakeAt("(module)"_su8,
                             ScriptModule{nullopt, ScriptModuleKind::Text, {}}),
                      MakeAt("\"msg\""_su8, Text{"\"msg\"", 3})}}}),
      },
      "(module) (invoke \"a\") (assert_invalid (module) \"msg\")"_su8);
}
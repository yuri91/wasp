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

#include <filesystem>
#include <fstream>
#include <iostream>

#include "absl/strings/str_format.h"

#include "src/tools/argparser.h"
#include "src/tools/text_errors.h"
#include "wasp/base/buffer.h"
#include "wasp/base/errors.h"
#include "wasp/base/features.h"
#include "wasp/base/file.h"
#include "wasp/base/formatters.h"
#include "wasp/base/span.h"
#include "wasp/base/string_view.h"
#include "wasp/binary/encoding.h"
#include "wasp/binary/formatters.h"
#include "wasp/binary/types.h"
#include "wasp/binary/visitor.h"
#include "wasp/binary/write.h"
#include "wasp/convert/to_binary.h"
#include "wasp/text/desugar.h"
#include "wasp/text/read.h"
#include "wasp/text/read/read_ctx.h"
#include "wasp/text/read/tokenizer.h"
#include "wasp/text/resolve.h"
#include "wasp/text/types.h"
#include "wasp/valid/valid_ctx.h"
#include "wasp/valid/validate.h"

#include "wasp/text/formatters.h"
#include "wasp/text/write.h"

namespace fs = std::filesystem;

namespace wasp {
namespace tools {
namespace custom {

using absl::PrintF;
using absl::Format;

struct Options {
  Features features;
  bool validate = true;
  std::string output_filename;
};

struct Tool {
  explicit Tool(string_view filename, SpanU8 data, Options);

  enum class PrintChars { No, Yes };

  int Run();

  std::string filename;
  Options options;
  SpanU8 data;
};

int Main(span<const string_view> args) {
  string_view filename;
  Options options;

  ArgParser parser{"wasp custom"};
  parser
      .Add("--help", "print help and exit",
           [&]() { parser.PrintHelpAndExit(0); })
      .Add('o', "--output", "<filename>", "write DOT file output to <filename>",
           [&](string_view arg) { options.output_filename = arg; })
      .Add("--no-validate", "Don't validate before writing",
           [&]() { options.validate = false; })
      .AddFeatureFlags(options.features)
      .Add("<filename>", "input wasm file", [&](string_view arg) {
        if (filename.empty()) {
          filename = arg;
        } else {
          Format(&std::cerr, "Filename already given\n");
        }
      });
  parser.Parse(args);

  if (filename.empty()) {
    Format(&std::cerr, "No filenames given.\n");
    parser.PrintHelpAndExit(1);
  }

  auto optbuf = ReadFile(filename);
  if (!optbuf) {
    Format(&std::cerr, "Error reading file %s.\n", filename);
    return 1;
  }

  if (options.output_filename.empty()) {
    // Create an output filename from the input filename.
    options.output_filename =
        fs::path(filename).replace_extension(".wasm").string();
  }

  SpanU8 data{*optbuf};
  Tool tool{filename, data, options};
  return tool.Run();
}

Tool::Tool(string_view filename, SpanU8 data, Options options)
    : filename{filename}, options{options}, data{data} {}

enum class AnnotationOrdering {
  Before,
  Inside,
  After,
};
AnnotationOrdering OrderAnnotation(const SpanU8& elem, const SpanU8& annot) {
  if(elem.end() <= annot.begin())
    return AnnotationOrdering::After;
  if(annot.end() <= elem.begin())
    return AnnotationOrdering::Before;
  return AnnotationOrdering::Inside;
}

enum class SectionOrder {
  Before,
  After,
};
enum class SectionPosition {
  First,
  Type,
  Import,
  Function,
  Table,
  Memory,
  Global,
  Export,
  Start,
  Elem,
  Code,
  Data,
  DataCount,
  Last,
};

struct CustomAnnotation {
  std::string_view name;
  std::vector<std::string_view> data;
  SectionOrder order;
  SectionPosition pos;
  CustomAnnotation(std::string_view name, std::vector<std::string_view> data,
      SectionOrder order, SectionPosition pos)
    : name(name), data(std::move(data)), order(order), pos(pos)
  {
  }
};

std::vector<CustomAnnotation> ExtractCustomAnnotations(
    std::vector<std::vector<text::Token>>& annots, tools::TextErrors& errors) {

#define EXPECT_END() \
  if(tok_it != ann_it->end()) { \
    errors.OnError(tok_it->loc, concat("Expected end of annotation. Found token: ", *tok_it)); \
    return ret; \
  }
#define EXPECT_NOT_END() \
  if(tok_it == ann_it->end()) { \
    errors.OnError((--tok_it)->loc, "Unexpected end of annotation"); \
    return ret; \
  }
#define EXPECT_TOK(type_, msg) \
  EXPECT_NOT_END(); \
  if(tok_it->type != type_) { \
    errors.OnError(tok_it->loc, concat("Expected ", msg, ". Found token: ", *tok_it)); \
    return ret; \
  }

  std::vector<CustomAnnotation> ret;
  auto custom_it = std::partition(annots.begin(), annots.end(), [](const auto& ann){
    return ann.front().type != text::TokenType::LparAnn || ann.front().as_string_view() != "(@custom";
  });

  for (auto ann_it = custom_it; ann_it != annots.end(); ann_it++) {
    auto tok_it = ann_it->begin() + 1;
    EXPECT_TOK(text::TokenType::Text, "section name");
    std::string_view name = tok_it->text().text;
    tok_it++;
    EXPECT_NOT_END();
    SectionOrder order = SectionOrder::After;
    SectionPosition pos = SectionPosition::Last;
    if(tok_it->type == text::TokenType::Lpar) {
      tok_it++;
      EXPECT_TOK(text::TokenType::Reserved, "after/before clause");
      auto content = ToStringView(tok_it->loc);
      if(content == "after") {
        order = SectionOrder::After;
      } else if(content == "before") {
        order = SectionOrder::Before;
      } else {
        errors.OnError(tok_it->loc, concat("Expected after/before clause. Found token: ", *tok_it));
        return ret;
      }
      tok_it++;
      EXPECT_NOT_END();
      content = ToStringView(tok_it->loc);
      if(tok_it->type == text::TokenType::Reserved && content == "first") {
        pos = SectionPosition::First;
      } else if(tok_it->type == text::TokenType::Type) {
        pos = SectionPosition::Type;
      } else if(tok_it->type == text::TokenType::Import) {
        pos = SectionPosition::Import;
      } else if(tok_it->type == text::TokenType::Func) {
        pos = SectionPosition::Function;
      } else if(tok_it->type == text::TokenType::Table) {
        pos = SectionPosition::Table;
      } else if(tok_it->type == text::TokenType::Memory) {
        pos = SectionPosition::Memory;
      } else if(tok_it->type == text::TokenType::Global) {
        pos = SectionPosition::Global;
      } else if(tok_it->type == text::TokenType::Export) {
        pos = SectionPosition::Export;
      } else if(tok_it->type == text::TokenType::Start) {
        pos = SectionPosition::Start;
      } else if(tok_it->type == text::TokenType::Elem) {
        pos = SectionPosition::Elem;
      } else if(tok_it->type == text::TokenType::Reserved && content == "code") {
        pos = SectionPosition::Code;
      } else if(tok_it->type == text::TokenType::Data) {
        pos = SectionPosition::Data;
      } else if(tok_it->type == text::TokenType::Reserved && content == "datacount") {
        pos = SectionPosition::DataCount;
      } else if(tok_it->type == text::TokenType::Reserved && content == "last") {
        pos = SectionPosition::Last;
      } else {
        errors.OnError(tok_it->loc, concat("Expected section position. Found token: ", *tok_it));
        return ret;
      }
    }
    std::vector<std::string_view> data;
    tok_it++;
    EXPECT_TOK(text::TokenType::Rpar, "location clause end");
    tok_it++;
    EXPECT_NOT_END();
    for(; tok_it != ann_it->end()-1; tok_it++) {
      EXPECT_TOK(text::TokenType::Text, "section data");
      data.push_back(tok_it->text().text);
    }
    ret.emplace_back(name, std::move(data), order, pos);
    EXPECT_TOK(text::TokenType::Rpar, "right parenthesis");
    tok_it++;
    EXPECT_END();
  }
  annots.erase(custom_it, annots.end());
  return ret;

#undef EXPECT_END
#undef EXPECT_NOT_END
#undef EXPECT_TOK
}

int Tool::Run() {
  text::Tokenizer tokenizer{data};
  tools::TextErrors errors{filename, data};
  text::ReadCtx read_context{options.features, errors};
  auto text_module =
      ReadSingleModule(tokenizer, read_context).value_or(text::Module{});
  Expect(tokenizer, read_context, text::TokenType::Eof);

  Resolve(text_module, errors);
  Desugar(text_module);

  if (errors.HasError()) {
    errors.PrintTo(std::cerr);
    return 1;
  }

  auto customs = ExtractCustomAnnotations(tokenizer.annotations(), errors);

  if (errors.HasError()) {
    errors.PrintTo(std::cerr);
    return 1;
  }

  for (const auto& item : text_module) {
    Location loc;
    switch (item.kind()) {
      case text::ModuleItemKind::Function: {
        loc = item.function().loc();
        break;
      }
      case text::ModuleItemKind::DefinedType: {
        loc = item.defined_type().loc();
        break;
      }
      case text::ModuleItemKind::Import: {
        loc = item.import().loc();
        break;
      }
      case text::ModuleItemKind::Table: {
        loc = item.table().loc();
        break;
      }
      case text::ModuleItemKind::Memory: {
        loc = item.memory().loc();
        break;
      }
      case text::ModuleItemKind::Global: {
        loc = item.global().loc();
        break;
      }
      case text::ModuleItemKind::Export: {
        loc = item.export_().loc();
        break;
      }
      case text::ModuleItemKind::Start: {
        loc = item.start().loc();
        break;
      }
      case text::ModuleItemKind::ElementSegment: {
        loc = item.element_segment().loc();
        break;
      }
      case text::ModuleItemKind::DataSegment: {
        loc = item.data_segment().loc();
        break;
      }
      case text::ModuleItemKind::Tag: {
        loc = item.tag().loc();
        break;
      }
    }
  }

  //convert::BinCtx convert_context{options.features};
  //auto binary_module = convert::ToBinary(convert_context, text_module);

  //if (options.validate) {
  //  valid::ValidCtx validate_context{options.features, errors};
  //  Validate(validate_context, binary_module);

  //  if (errors.HasError()) {
  //    errors.PrintTo(std::cerr);
  //    return 1;
  //  }
  //}

  //Buffer buffer;
  //Write(binary_module, std::back_inserter(buffer));

  //std::ofstream fstream(options.output_filename,
  //                      std::ios_base::out | std::ios_base::binary);
  //if (!fstream) {
  //  Format(&std::cerr, "Unable to open file %s.\n", options.output_filename);
  //  return 1;
  //}

  //auto span = ToStringView(buffer);
  //fstream.write(span.data(), span.size());
  return 0;
}

}  // namespace custom
}  // namespace tools
}  // namespace wasp

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "tl_json_converter.h"

#include "td/tl/tl_simple.h"

#include "td/utils/buffer.h"
#include "td/utils/filesystem.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

template <class T>
void gen_to_json_constructor(StringBuilder &sb, const T *constructor, bool is_header) {
  sb << "void to_json(JsonValueScope &jv, "
     << "const td_api::" << tl::simple::gen_cpp_name(constructor->name) << " &object)";
  if (is_header) {
    sb << ";\n";
    return;
  }
  sb << " {\n";
  sb << "  auto jo = jv.enter_object();\n";
  sb << "  jo << ctie(\"@type\", \"" << tl::simple::gen_cpp_name(constructor->name) << "\");\n";
  for (auto &arg : constructor->args) {
    auto field = tl::simple::gen_cpp_field_name(arg.name);
    // TODO: or as null
    bool is_custom = arg.type->type == tl::simple::Type::Custom;

    if (is_custom) {
      sb << "  if (object." << field << ") {\n  ";
    }
    auto object = PSTRING() << "object." << tl::simple::gen_cpp_field_name(arg.name);
    if (arg.type->type == tl::simple::Type::Bytes) {
      object = PSTRING() << "base64_encode(" << object << ")";
    } else if (arg.type->type == tl::simple::Type::Int64) {
      object = PSTRING() << "JsonInt64{" << object << "}";
    } else if (arg.type->type == tl::simple::Type::Vector &&
               arg.type->vector_value_type->type == tl::simple::Type::Int64) {
      object = PSTRING() << "JsonVectorInt64{" << object << "}";
    }
    sb << "  jo << ctie(\"" << arg.name << "\", ToJson(" << object << "));\n";
    if (is_custom) {
      sb << "  }\n";
    }
  }
  sb << "}\n";
}

void gen_to_json(StringBuilder &sb, const tl::simple::Schema &schema, bool is_header) {
  for (auto *custom_type : schema.custom_types) {
    if (custom_type->constructors.size() > 1) {
      auto type_name = tl::simple::gen_cpp_name(custom_type->name);
      sb << "void to_json(JsonValueScope &jv, const td_api::" << type_name << " &object)";
      if (is_header) {
        sb << ";\n";
      } else {
        sb << " {\n"
           << "  td_api::downcast_call(const_cast<td_api::" << type_name
           << " &>(object), [&jv](const auto &object) { "
              "to_json(jv, object); });\n"
           << "}\n";
      }
    }
    for (auto *constructor : custom_type->constructors) {
      gen_to_json_constructor(sb, constructor, is_header);
    }
  }
  for (auto *function : schema.functions) {
    gen_to_json_constructor(sb, function, is_header);
  }
}

template <class T>
void gen_from_json_constructor(StringBuilder &sb, const T *constructor, bool is_header) {
  sb << "Status from_json(td_api::" << tl::simple::gen_cpp_name(constructor->name) << " &to, JsonObject &from)";
  if (is_header) {
    sb << ";\n";
  } else {
    sb << " {\n";
    for (auto &arg : constructor->args) {
      sb << "  {\n";
      sb << "    TRY_RESULT(value, get_json_object_field(from, \"" << tl::simple::gen_cpp_name(arg.name)
         << "\", JsonValue::Type::Null, true));\n";
      sb << "    if (value.type() != JsonValue::Type::Null) {\n";
      if (arg.type->type == tl::simple::Type::Bytes) {
        sb << "      TRY_STATUS(from_json_bytes(to." << tl::simple::gen_cpp_field_name(arg.name) << ", value));\n";
      } else {
        sb << "      TRY_STATUS(from_json(to." << tl::simple::gen_cpp_field_name(arg.name) << ", value));\n";
      }
      sb << "    }\n";
      sb << "  }\n";
    }
    sb << "  return Status::OK();\n";
    sb << "}\n";
  }
}

void gen_from_json(StringBuilder &sb, const tl::simple::Schema &schema, bool is_header) {
  for (auto *custom_type : schema.custom_types) {
    for (auto *constructor : custom_type->constructors) {
      gen_from_json_constructor(sb, constructor, is_header);
    }
  }
  for (auto *function : schema.functions) {
    gen_from_json_constructor(sb, function, is_header);
  }
}

using Vec = std::vector<std::pair<int32, std::string>>;
void gen_tl_constructor_from_string(StringBuilder &sb, Slice name, const Vec &vec, bool is_header) {
  sb << "Result<int32> tl_constructor_from_string(td_api::" << name << " *object, const std::string &str)";
  if (is_header) {
    sb << ";\n";
    return;
  }
  sb << " {\n";
  sb << "  static const std::unordered_map<Slice, int32, SliceHash> m = {\n";

  bool is_first = true;
  for (auto &p : vec) {
    if (is_first) {
      is_first = false;
    } else {
      sb << ",\n";
    }
    sb << "    {\"" << p.second << "\", " << p.first << "}";
  }
  sb << "\n  };\n";
  sb << "  auto it = m.find(str);\n";
  sb << "  if (it == m.end()) {\n"
     << "    return Status::Error(\"Unknown class\");\n"
     << "  }\n"
     << "  return it->second;\n";
  sb << "}\n";
}

void gen_tl_constructor_from_string(StringBuilder &sb, const tl::simple::Schema &schema, bool is_header) {
  Vec vec_for_nullary;
  for (auto *custom_type : schema.custom_types) {
    Vec vec;
    for (auto *constructor : custom_type->constructors) {
      vec.push_back(std::make_pair(constructor->id, constructor->name));
      vec_for_nullary.push_back(vec.back());
    }

    if (vec.size() > 1) {
      gen_tl_constructor_from_string(sb, tl::simple::gen_cpp_name(custom_type->name), vec, is_header);
    }
  }
  gen_tl_constructor_from_string(sb, "Object", vec_for_nullary, is_header);

  Vec vec_for_function;
  for (auto *function : schema.functions) {
    vec_for_function.push_back(std::make_pair(function->id, function->name));
  }
  gen_tl_constructor_from_string(sb, "Function", vec_for_function, is_header);
}

void gen_json_converter_file(const tl::simple::Schema &schema, const std::string &file_name_base, bool is_header) {
  auto file_name = is_header ? file_name_base + ".h" : file_name_base + ".cpp";
  file_name = "auto/" + file_name;
  auto old_file_content = [&] {
    auto r_content = read_file(file_name);
    if (r_content.is_error()) {
      return BufferSlice();
    }
    return r_content.move_as_ok();
  }();

  std::string buf(2000000, ' ');
  StringBuilder sb(buf);

  if (is_header) {
    sb << "#pragma once\n\n";

    sb << "#include \"td/telegram/td_api.h\"\n\n";

    sb << "#include \"td/utils/JsonBuilder.h\"\n";
    sb << "#include \"td/utils/Status.h\"\n\n";
  } else {
    sb << "#include \"" << file_name_base << ".h\"\n\n";

    sb << "#include \"td/telegram/td_api.h\"\n";
    sb << "#include \"td/telegram/td_api.hpp\"\n\n";

    sb << "#include \"td/tl/tl_json.h\"\n\n";

    sb << "#include \"td/utils/base64.h\"\n";
    sb << "#include \"td/utils/common.h\"\n";
    sb << "#include \"td/utils/Slice.h\"\n\n";

    sb << "#include <unordered_map>\n\n";
  }
  sb << "namespace td {\n";
  sb << "namespace td_api{\n";
  gen_tl_constructor_from_string(sb, schema, is_header);
  gen_from_json(sb, schema, is_header);
  gen_to_json(sb, schema, is_header);
  sb << "}  // namespace td_api\n";
  sb << "}  // namespace td\n";

  CHECK(!sb.is_error());
  buf.resize(sb.as_cslice().size());
#if TD_WINDOWS
  string new_file_content;
  for (auto c : buf) {
    if (c == '\n') {
      new_file_content += '\r';
    }
    new_file_content += c;
  }
#else
  auto new_file_content = std::move(buf);
#endif
  if (new_file_content != old_file_content.as_slice()) {
    write_file(file_name, new_file_content).ensure();
  }
}

void gen_json_converter(const tl::tl_config &config, const std::string &file_name) {
  tl::simple::Schema schema(config);
  gen_json_converter_file(schema, file_name, true);
  gen_json_converter_file(schema, file_name, false);
}

}  // namespace td

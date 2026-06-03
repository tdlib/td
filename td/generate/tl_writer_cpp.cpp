//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "tl_writer_cpp.h"

#include <cassert>
#include <charconv>

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_TL_WRITER_CPP_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_TL_WRITER_CPP_MSAN_ACTIVE 1
#endif
#ifndef TD_TL_WRITER_CPP_MSAN_ACTIVE
#define TD_TL_WRITER_CPP_MSAN_ACTIVE 0
#endif

namespace {

template <class T>
void unpoison_object_if_msan(const T &value) {
#if TD_TL_WRITER_CPP_MSAN_ACTIVE
  __msan_unpoison(const_cast<T *>(&value), sizeof(value));
#else
  (void)value;
#endif
}

void unpoison_if_msan(const std::string &value) {
#if TD_TL_WRITER_CPP_MSAN_ACTIVE
  unpoison_object_if_msan(value);
  if (!value.empty()) {
    __msan_unpoison(const_cast<char *>(value.data()), value.size());
  }
  __msan_unpoison(const_cast<char *>(value.data() + value.size()), 1);
#else
  (void)value;
#endif
}

void unpoison_if_msan(const td::tl::tl_tree &value);
void unpoison_if_msan(const td::tl::tl_combinator &value);
void unpoison_if_msan(const std::vector<td::tl::tl_tree *> &values);
void unpoison_if_msan(const std::vector<td::tl::tl_combinator *> &values);
void unpoison_if_msan(const td::tl::arg &value);
void unpoison_if_msan(const std::vector<td::tl::arg> &values);
void unpoison_if_msan(const td::tl::tl_type &value);

void unpoison_if_msan(const td::tl::tl_type &value) {
  unpoison_object_if_msan(value);
  unpoison_if_msan(value.name);
}

void unpoison_if_msan(const td::tl::tl_combinator &value) {
  unpoison_object_if_msan(value);
  unpoison_if_msan(value.name);
  unpoison_if_msan(value.args);
  if (value.result != nullptr) {
    unpoison_if_msan(*value.result);
  }
}

void unpoison_if_msan(const td::tl::tl_tree &value) {
  unpoison_object_if_msan(value);
  switch (value.get_type()) {
    case td::tl::NODE_TYPE_TYPE: {
      const auto &type_tree = static_cast<const td::tl::tl_tree_type &>(value);
      unpoison_object_if_msan(type_tree);
      if (type_tree.type != nullptr) {
        unpoison_if_msan(*type_tree.type);
      }
      unpoison_if_msan(type_tree.children);
      return;
    }
    case td::tl::NODE_TYPE_ARRAY: {
      const auto &array_tree = static_cast<const td::tl::tl_tree_array &>(value);
      unpoison_object_if_msan(array_tree);
      if (array_tree.multiplicity != nullptr) {
        unpoison_if_msan(*array_tree.multiplicity);
      }
      unpoison_if_msan(array_tree.args);
      return;
    }
    case td::tl::NODE_TYPE_NAT_CONST:
      unpoison_object_if_msan(static_cast<const td::tl::tl_tree_nat_const &>(value));
      return;
    case td::tl::NODE_TYPE_VAR_TYPE:
      unpoison_object_if_msan(static_cast<const td::tl::tl_tree_var_type &>(value));
      return;
    case td::tl::NODE_TYPE_VAR_NUM:
      unpoison_object_if_msan(static_cast<const td::tl::tl_tree_var_num &>(value));
      return;
    default:
      assert(false && "unexpected tl_tree node type");
  }
}

void unpoison_if_msan(const std::vector<td::tl::tl_tree *> &values) {
#if TD_TL_WRITER_CPP_MSAN_ACTIVE
  unpoison_object_if_msan(values);
  if (!values.empty()) {
    __msan_unpoison(const_cast<td::tl::tl_tree **>(values.data()), values.size() * sizeof(values[0]));
  }
#else
  (void)values;
#endif
  for (const auto *value : values) {
    if (value != nullptr) {
      unpoison_if_msan(*value);
    }
  }
}

void unpoison_if_msan(const std::vector<td::tl::tl_combinator *> &values) {
#if TD_TL_WRITER_CPP_MSAN_ACTIVE
  unpoison_object_if_msan(values);
  if (!values.empty()) {
    __msan_unpoison(const_cast<td::tl::tl_combinator **>(values.data()), values.size() * sizeof(values[0]));
  }
#else
  (void)values;
#endif
  for (const auto *value : values) {
    if (value != nullptr) {
      unpoison_if_msan(*value);
    }
  }
}

void unpoison_if_msan(const td::tl::arg &value) {
  unpoison_object_if_msan(value);
  unpoison_if_msan(value.name);
  if (value.type != nullptr) {
    unpoison_if_msan(*value.type);
  }
}

void unpoison_if_msan(const std::vector<td::tl::arg> &values) {
#if TD_TL_WRITER_CPP_MSAN_ACTIVE
  unpoison_object_if_msan(values);
  if (!values.empty()) {
    __msan_unpoison(const_cast<td::tl::arg *>(values.data()), values.size() * sizeof(values[0]));
  }
#else
  (void)values;
#endif
  for (const auto &value : values) {
    unpoison_if_msan(value);
  }
}

void unpoison_if_msan(const td::tl::var_description &value) {
  unpoison_object_if_msan(value);
}

void unpoison_if_msan(const std::vector<td::tl::var_description> &values) {
#if TD_TL_WRITER_CPP_MSAN_ACTIVE
  unpoison_object_if_msan(values);
  if (!values.empty()) {
    __msan_unpoison(const_cast<td::tl::var_description *>(values.data()), values.size() * sizeof(values[0]));
  }
#else
  (void)values;
#endif
  for (const auto &value : values) {
    unpoison_if_msan(value);
  }
}

}  // namespace

namespace td {

std::string TD_TL_writer_cpp::gen_output_begin(const std::string &additional_imports) const {
  unpoison_if_msan(additional_imports);
  std::string ext_include_str;
  for (auto &it : ext_include) {
    unpoison_if_msan(it);
    ext_include_str += "#include " + it + "\n";
  }
  if (!ext_include_str.empty()) {
    ext_include_str += "\n";
  }

  std::string result;
  result += "#include \"";
  result += tl_name;
  result += ".h\"\n\n";
  result += ext_include_str;
  result +=
      "#include \"td/utils/common.h\"\n"
      "#include \"td/utils/format.h\"\n"
      "#include \"td/utils/logging.h\"\n"
      "#include \"td/utils/SliceBuilder.h\"\n"
      "#include \"td/utils/tl_parsers.h\"\n"
      "#include \"td/utils/tl_storers.h\"\n"
      "#include \"td/utils/TlStorerToString.h\"\n\n";
  result += additional_imports;
  result += "namespace td {\nnamespace ";
  result += tl_name;
  result += " {\n\n";
  return result;
}

std::string TD_TL_writer_cpp::gen_output_begin_once() const {
  return "std::string to_string(const BaseObject &value) {\n"
         "  TlStorerToString storer;\n"
         "  value.store(storer, \"\");\n"
         "  return storer.move_as_string();\n"
         "}\n";
}

std::string TD_TL_writer_cpp::gen_output_end() const {
  return "}  // namespace " + tl_name +
         "\n"
         "}  // namespace td\n";
}

std::string TD_TL_writer_cpp::gen_field_definition(const std::string &class_name, const std::string &type_name,
                                                   const std::string &field_name) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_vars(const tl::tl_combinator *t, const tl::tl_tree_type *result_type,
                                       std::vector<tl::var_description> &vars) const {
  unpoison_if_msan(*t);
  unpoison_if_msan(t->args);
  unpoison_if_msan(vars);
  for (std::size_t i = 0; i < vars.size(); i++) {
    vars[i].index = static_cast<int>(i);
    vars[i].is_stored = false;
    vars[i].is_type = false;
    vars[i].parameter_num = -1;
    vars[i].function_arg_num = -1;
  }

  if (result_type != nullptr) {
    unpoison_object_if_msan(*result_type);
    assert(result_type->children.empty());
  }

  for (std::size_t i = 0; i < t->args.size(); i++) {
    const tl::arg &a = t->args[i];

    int arg_type = a.type->get_type();
    if (arg_type == tl::NODE_TYPE_VAR_TYPE) {
      const tl::tl_tree_var_type *var_type = static_cast<const tl::tl_tree_var_type *>(a.type);
      assert(a.flags & tl::FLAG_EXCL);
      assert(var_type->var_num < static_cast<int>(vars.size()));
      assert(var_type->var_num >= 0);
      assert(!vars[var_type->var_num].is_type);
      vars[var_type->var_num].is_type = true;
      vars[var_type->var_num].function_arg_num = static_cast<int>(i);
    }
  }

  std::string res;
  for (std::size_t i = 0; i < vars.size(); i++) {
    if (!vars[i].is_type) {
      assert(vars[i].parameter_num == -1);
      assert(vars[i].function_arg_num == -1);
      assert(!vars[i].is_stored);
      res += "  ";
      res += gen_class_name("#");
      res += " ";
      res += gen_var_name(vars[i]);
      res += ";\n";
    }
  }
  return res;
}

std::string TD_TL_writer_cpp::gen_function_vars(const tl::tl_combinator *t,
                                                std::vector<tl::var_description> &vars) const {
  for (std::size_t i = 0; i < vars.size(); i++) {
    vars[i].index = static_cast<int>(i);
    vars[i].is_stored = false;
    vars[i].is_type = false;
    vars[i].parameter_num = -1;
    vars[i].function_arg_num = -1;
  }

  for (std::size_t i = 0; i < t->args.size(); i++) {
    const tl::arg &a = t->args[i];

    int arg_type = a.type->get_type();
    if (arg_type == tl::NODE_TYPE_VAR_TYPE) {
      const tl::tl_tree_var_type *var_type = static_cast<const tl::tl_tree_var_type *>(a.type);
      assert(a.flags & tl::FLAG_EXCL);
      assert(var_type->var_num >= 0);
      assert(!vars[var_type->var_num].is_type);
      vars[var_type->var_num].is_type = true;
      vars[var_type->var_num].function_arg_num = static_cast<int>(i);
    }
  }

  return "";
}

std::string TD_TL_writer_cpp::gen_uni(const tl::tl_tree_type *result_type, std::vector<tl::var_description> &vars,
                                      bool check_negative) const {
  assert(result_type->children.empty());
  return "";
}

std::string TD_TL_writer_cpp::gen_constructor_id_store_raw(const std::string &id) const {
  std::string result = "s.store_binary(";
  result += id;
  result += ");";
  return result;
}

std::string TD_TL_writer_cpp::gen_constructor_id_store(std::int32_t id, int storer_type) const {
  if (storer_type == 1) {
    return "";
  }
  std::string result = "  ";
  char id_buffer[32];
  auto [id_end, id_error] = std::to_chars(id_buffer, id_buffer + sizeof(id_buffer), id);
  assert(id_error == std::errc());
  std::string id_string(id_buffer, static_cast<std::size_t>(id_end - id_buffer));
  result += gen_constructor_id_store_raw(id_string);
  result += "\n";
  return result;
}

std::string TD_TL_writer_cpp::gen_fetch_class_name(const tl::tl_tree_type *tree_type) const {
  unpoison_object_if_msan(*tree_type);
  const tl::tl_type *t = tree_type->type;
  unpoison_object_if_msan(*t);
  const std::string &name = t->name;
  unpoison_if_msan(name);
  unpoison_if_msan(tree_type->children);

  if (name == "#" || name == "Int32") {
    return "TlFetchInt";
  }
  if (name == "Int53" || name == "Int64") {
    return "TlFetchLong";
  }
  if (name == "True" || name == "Bool" || name == "Int" || name == "Long" || name == "Double" || name == "Int128" ||
      name == "Int256" || name == "Int512") {
    std::string result = "TlFetch";
    result += name;
    return result;
  }
  if (name == "String") {
    return "TlFetchString<string>";
  }
  if (name == "Bytes") {
    return "TlFetchBytes<bytes>";
  }
  if (name == "SecureString") {
    return "TlFetchString<secure_string>";
  }
  if (name == "SecureBytes") {
    return "TlFetchBytes<secure_bytes>";
  }

  if (name == "Vector") {
    assert(t->arity == 1);
    assert(tree_type->children.size() == 1);
    assert(tree_type->children[0]->get_type() == tl::NODE_TYPE_TYPE);
    const tl::tl_tree_type *child = static_cast<const tl::tl_tree_type *>(tree_type->children[0]);

    std::string result = "TlFetchVector<";
    result += gen_full_fetch_class_name(child);
    result += ">";
    return result;
  }

  assert(!is_built_in_simple_type(name) && !is_built_in_complex_type(name));
  for (std::size_t i = 0; i < tree_type->children.size(); i++) {
    assert(tree_type->children[i]->get_type() == tl::NODE_TYPE_NAT_CONST);
  }

  assert(tree_type->children.empty());
  std::string result = "TlFetchObject<";
  result += gen_main_class_name(t);
  result += ">";
  return result;
}

std::string TD_TL_writer_cpp::gen_full_fetch_class_name(const tl::tl_tree_type *tree_type) const {
  unpoison_object_if_msan(*tree_type);
  const tl::tl_type *t = tree_type->type;
  unpoison_object_if_msan(*t);
  const std::string &name = t->name;
  unpoison_if_msan(name);
  unpoison_if_msan(t->constructors);

  assert(!(t->flags & tl::FLAG_DEFAULT_CONSTRUCTOR));  // Not supported yet

  std::int32_t expected_constructor_id = 0;
  if (tree_type->flags & tl::FLAG_BARE) {
    assert(is_type_bare(t));
  } else {
    if (is_type_bare(t)) {
      for (std::size_t i = 0; i < t->constructors_num; i++) {
        if (is_built_in_complex_type(name) || is_combinator_supported(t->constructors[i])) {
          assert(expected_constructor_id == 0);
          expected_constructor_id = t->constructors[i]->id;
          assert(expected_constructor_id != 0);
        }
      }
    }
  }
  if (expected_constructor_id == 0) {
    return gen_fetch_class_name(tree_type);
  }
  std::string result = "TlFetchBoxed<";
  result += gen_fetch_class_name(tree_type);
  result += ", ";
  char constructor_id_buffer[32];
  auto [constructor_id_end, constructor_id_error] = std::to_chars(
      constructor_id_buffer, constructor_id_buffer + sizeof(constructor_id_buffer), expected_constructor_id);
  assert(constructor_id_error == std::errc());
  result.append(constructor_id_buffer, static_cast<std::size_t>(constructor_id_end - constructor_id_buffer));
  result += ">";
  return result;
}

std::string TD_TL_writer_cpp::gen_type_fetch(const std::string &field_name, const tl::tl_tree_type *tree_type,
                                             const std::vector<tl::var_description> &vars, int parser_type) const {
  return gen_full_fetch_class_name(tree_type) + "::parse(p)";
}

std::string TD_TL_writer_cpp::gen_field_fetch(int field_num, const tl::arg &a, std::vector<tl::var_description> &vars,
                                              bool flat, int parser_type) const {
  assert(parser_type >= 0);
  unpoison_if_msan(a);
  unpoison_if_msan(vars);
  std::string field_name = parser_type == 0 ? (field_num == 0 ? ": " : ", ") : "res->";
  field_name += gen_field_name(a.name);

  if (a.type->get_type() == tl::NODE_TYPE_VAR_TYPE) {
    assert(parser_type == 1);

    const tl::tl_tree_var_type *t = static_cast<const tl::tl_tree_var_type *>(a.type);
    assert(a.flags == tl::FLAG_EXCL);

    assert(a.var_num == -1);
    assert(a.exist_var_num == -1);

    assert(t->var_num >= 0);
    assert(vars[t->var_num].is_type);
    assert(!vars[t->var_num].is_stored);
    vars[t->var_num].is_stored = true;

    std::string result = "  ";
    result += field_name;
    result += " = ";
    result += gen_base_function_class_name();
    result += "::fetch(p);\n";
    return result;
  }

  assert(!(a.flags & tl::FLAG_EXCL));
  assert(!(a.flags & tl::FLAG_OPT_VAR));

  if (a.exist_var_num != -1) {
    assert(0 <= a.exist_var_num && a.exist_var_num < static_cast<int>(vars.size()));
    assert(vars[a.exist_var_num].is_stored);

    if (a.var_num == -1 && parser_type != 0) {
      assert(a.type->get_type() == tl::NODE_TYPE_TYPE);
      const tl::tl_tree_type *tree_type = static_cast<tl::tl_tree_type *>(a.type);
      if (tree_type->flags & tl::FLAG_BARE && tree_type->type->name == "True") {
        assert(is_type_bare(tree_type->type));
        std::string true_bit_mask = int_to_string(1 << a.exist_var_bit);
        unpoison_if_msan(true_bit_mask);
        std::string result = "  ";
        result += field_name;
        result += " = (";
        result += gen_var_name(vars[a.exist_var_num]);
        result += " & ";
        result += true_bit_mask;
        result += ") != 0;\n";
        return result;
      }
    }
  }

  std::string res = "  ";
  if (a.exist_var_num != -1) {
    std::string exist_var_bit_mask = int_to_string(1 << a.exist_var_bit);
    unpoison_if_msan(exist_var_bit_mask);
    res += "if (";
    res += gen_var_name(vars[a.exist_var_num]);
    res += " & ";
    res += exist_var_bit_mask;
    res += ") { ";
  }

  if (flat) {
    //    TODO
    //    return gen_field_fetch(const tl::arg &a, std::vector<tl::var_description> &vars, int num, bool flat);
  }

  bool store_to_var_num = false;
  if (a.var_num >= 0) {
    assert(a.type->get_type() == tl::NODE_TYPE_TYPE);
    assert(static_cast<const tl::tl_tree_type *>(a.type)->type->id == tl::ID_VAR_NUM);
    assert(0 <= a.var_num && a.var_num < static_cast<int>(vars.size()));
    if (!vars[a.var_num].is_stored) {
      res += "if ((";
      res += gen_var_name(vars[a.var_num]);
      res += " = ";
      store_to_var_num = true;
    } else {
      assert(false);
    }
    vars[a.var_num].is_stored = true;
  }

  res += field_name;
  res += parser_type == 0 ? "(" : " = ";

  assert(a.type->get_type() == tl::NODE_TYPE_TYPE);
  const tl::tl_tree_type *tree_type = static_cast<tl::tl_tree_type *>(a.type);
  res += gen_type_fetch(field_name, tree_type, vars, parser_type);
  if (store_to_var_num) {
    res += ") < 0) { FAIL(\"Variable of type # can't be negative\"); }";
  } else {
    res += (parser_type == 0 ? ")" : ";");
  }

  if (a.exist_var_num >= 0) {
    res += " }";
    if (store_to_var_num) {
      res += " else { ";
      res += gen_var_name(vars[a.var_num]);
      res += " = 0; }";
    }
  }
  res += "\n";
  return res;
}

std::string TD_TL_writer_cpp::gen_var_type_fetch(const tl::arg &a) const {
  assert(false);
  return "";
}

std::string TD_TL_writer_cpp::get_pretty_field_name(std::string field_name) const {
  unpoison_if_msan(field_name);
  if (!field_name.empty() && field_name[0] == '_') {
    return "";
  }
  auto equals_pos = field_name.find('=');
  if (equals_pos != std::string::npos && equals_pos + 3 < field_name.size()) {
    field_name = field_name.substr(equals_pos + 2);
    auto bar_pos = field_name.find('|');
    if (bar_pos != std::string::npos && bar_pos >= 2 && field_name[bar_pos - 1] == ' ') {
      field_name = field_name.substr(0, bar_pos - 1);
    } else if (field_name.back() == ')') {
      field_name.pop_back();
    }
  }
  while (!field_name.empty() && field_name.back() == '_') {
    field_name.pop_back();
  }
  return field_name;
}

std::string TD_TL_writer_cpp::get_pretty_class_name(std::string class_name) const {
  unpoison_if_msan(class_name);
  if (tl_name != "mtproto_api") {
    for (std::size_t i = 0; i < class_name.size(); i++) {
      if (class_name[i] == '_') {
        class_name[i] = '.';
      }
    }
  }
  return class_name;
}

std::string TD_TL_writer_cpp::gen_vector_store(const std::string &field_name, const tl::tl_tree_type *t,
                                               const std::vector<tl::var_description> &vars, int storer_type) const {
  unpoison_if_msan(field_name);
  std::string num = !field_name.empty() && field_name[0] == '_' ? "2" : "";
  std::string result = "{ s.store_vector_begin(\"";
  result += get_pretty_field_name(field_name);
  result += "\", ";
  result += field_name;
  result += ".size()); for (const auto &_value";
  result += num;
  result += " : ";
  result += field_name;
  result += ") { ";
  result += gen_type_store("_value" + num, t, vars, storer_type);
  result += " } s.store_class_end(); }";
  return result;
}

std::string TD_TL_writer_cpp::gen_store_class_name(const tl::tl_tree_type *tree_type) const {
  unpoison_object_if_msan(*tree_type);
  const tl::tl_type *t = tree_type->type;
  unpoison_object_if_msan(*t);
  const std::string &name = t->name;
  unpoison_if_msan(name);
  unpoison_if_msan(tree_type->children);

  if (name == "#" || name == "Int" || name == "Long" || name == "Int32" || name == "Int53" || name == "Int64" ||
      name == "Double" || name == "Int128" || name == "Int256" || name == "Int512") {
    return "TlStoreBinary";
  }
  if (name == "Bool") {
    return "TlStoreBool";
  }
  if (name == "True") {
    assert(false);
    return "";
  }
  if (name == "String" || name == "Bytes" || name == "SecureString" || name == "SecureBytes") {
    return "TlStoreString";
  }

  if (name == "Vector") {
    assert(t->arity == 1);
    assert(tree_type->children.size() == 1);
    assert(tree_type->children[0]->get_type() == tl::NODE_TYPE_TYPE);
    const tl::tl_tree_type *child = static_cast<const tl::tl_tree_type *>(tree_type->children[0]);

    std::string result = "TlStoreVector<";
    result += gen_full_store_class_name(child);
    result += ">";
    return result;
  }

  assert(!is_built_in_simple_type(name) && !is_built_in_complex_type(name));
  for (std::size_t i = 0; i < tree_type->children.size(); i++) {
    assert(tree_type->children[i]->get_type() == tl::NODE_TYPE_NAT_CONST);
  }

  assert(tree_type->children.empty());
  return "TlStoreObject";
}

std::string TD_TL_writer_cpp::gen_full_store_class_name(const tl::tl_tree_type *tree_type) const {
  unpoison_object_if_msan(*tree_type);
  const tl::tl_type *t = tree_type->type;
  unpoison_object_if_msan(*t);
  const std::string &name = t->name;
  unpoison_if_msan(name);
  unpoison_if_msan(t->constructors);

  assert(!(t->flags & tl::FLAG_DEFAULT_CONSTRUCTOR));  // Not supported yet

  auto append_constructor_id = [](std::string &result, std::int32_t constructor_id) {
    char constructor_id_buffer[32];
    auto [constructor_id_end, constructor_id_error] =
        std::to_chars(constructor_id_buffer, constructor_id_buffer + sizeof(constructor_id_buffer), constructor_id);
    assert(constructor_id_error == std::errc());
    result.append(constructor_id_buffer, static_cast<std::size_t>(constructor_id_end - constructor_id_buffer));
  };

  if ((tree_type->flags & tl::FLAG_BARE) != 0 || name == "#" || name == "Bool") {
    return gen_store_class_name(tree_type);
  }

  if (is_built_in_complex_type(name)) {
    std::string result = "TlStoreBoxed<";
    result += gen_store_class_name(tree_type);
    result += ", ";
    append_constructor_id(result, t->constructors[0]->id);
    result += ">";
    return result;
  }

  if (!is_type_bare(t)) {
    std::string result = "TlStoreBoxedUnknown<";
    result += gen_store_class_name(tree_type);
    result += ">";
    return result;
  }

  for (std::size_t i = 0; i < t->constructors_num; i++) {
    if (is_combinator_supported(t->constructors[i])) {
      std::string result = "TlStoreBoxed<";
      result += gen_store_class_name(tree_type);
      result += ", ";
      append_constructor_id(result, t->constructors[i]->id);
      result += ">";
      return result;
    }
  }

  assert(false);
  return "";
}

std::string TD_TL_writer_cpp::gen_type_store(const std::string &field_name, const tl::tl_tree_type *tree_type,
                                             const std::vector<tl::var_description> &vars, int storer_type) const {
  if (storer_type == 0) {
    return gen_full_store_class_name(tree_type) + "::store(" + field_name + ", s);";
  }

  const tl::tl_type *t = tree_type->type;
  const std::string &name = t->name;

  assert(!(t->flags & tl::FLAG_DEFAULT_CONSTRUCTOR));  // Not supported yet

  if (name == "#" || name == "Int" || name == "Long" || name == "Int32" || name == "Int53" || name == "Int64" ||
      name == "Double" || name == "Bool" || name == "String" || name == "SecureString" || name == "Int128" ||
      name == "Int256" || name == "Int512") {
    return "s.store_field(\"" + get_pretty_field_name(field_name) + "\", " + field_name + ");";
  } else if (name == "True") {
    // currently nothing to do
    return "";
  } else if (name == "Bytes" || name == "SecureBytes") {
    return "s.store_bytes_field(\"" + get_pretty_field_name(field_name) + "\", " + field_name + ");";
  } else if (name == "Vector") {
    const tl::tl_tree_type *child = static_cast<const tl::tl_tree_type *>(tree_type->children[0]);
    return gen_vector_store(field_name, child, vars, storer_type);
  } else {
    assert(tree_type->children.empty());
    return "s.store_object_field(\"" + get_pretty_field_name(field_name) + "\", static_cast<const BaseObject *>(" +
           field_name + ".get()));";
  }
}

std::string TD_TL_writer_cpp::gen_field_store(const tl::arg &a, const std::vector<tl::arg> &args,
                                              std::vector<tl::var_description> &vars, bool flat,
                                              int storer_type) const {
  std::string field_name = gen_field_name(a.name);
  std::string res = storer_type == 1 ? "    " : "  ";

  if (a.type->get_type() == tl::NODE_TYPE_VAR_TYPE) {
    const tl::tl_tree_var_type *t = static_cast<const tl::tl_tree_var_type *>(a.type);
    assert(a.flags == tl::FLAG_EXCL);

    assert(a.var_num == -1);
    assert(a.exist_var_num == -1);

    assert(t->var_num >= 0);
    assert(!vars[t->var_num].is_stored);
    vars[t->var_num].is_stored = true;
    assert(vars[t->var_num].is_type);

    return res + field_name + "->store(s);\n";
  }

  assert(!(a.flags & tl::FLAG_EXCL));

  if (a.flags & tl::FLAG_OPT_VAR) {
    assert(false);
    assert(a.exist_var_num == -1);
    assert(0 <= a.var_num && a.var_num < static_cast<int>(vars.size()));

    assert(a.type->get_type() == tl::NODE_TYPE_TYPE);
    assert(static_cast<const tl::tl_tree_type *>(a.type)->type->id == tl::ID_VAR_NUM);
    assert(vars[a.var_num].is_stored);
    assert(!vars[a.var_num].is_type);
    return "";
  }

  if (a.exist_var_num >= 0) {
    assert(a.exist_var_num < static_cast<int>(vars.size()));
    assert(vars[a.exist_var_num].is_stored);

    if (a.var_num == -1 && a.type->get_type() == tl::NODE_TYPE_TYPE) {
      const tl::tl_tree_type *tree_type = static_cast<tl::tl_tree_type *>(a.type);
      if (tree_type->type->name == "True") {
        if (storer_type == 1) {
          std::string exist_var_name = gen_var_name(vars[a.exist_var_num]);
          std::string exist_var_bit_mask = int_to_string(1 << a.exist_var_bit);
          std::string pretty_field_name = get_pretty_field_name(field_name);
          unpoison_if_msan(exist_var_name);
          unpoison_if_msan(exist_var_bit_mask);
          unpoison_if_msan(pretty_field_name);

          std::string result = "    if (";
          result += exist_var_name;
          result += " & ";
          result += exist_var_bit_mask;
          result += ") { s.store_field(\"";
          result += pretty_field_name;
          result += "\", true); }\n";
          unpoison_if_msan(result);
          return result;
        } else {
          return "";
        }
      }
    }

    std::string exist_condition_var_name = gen_var_name(vars[a.exist_var_num]);
    std::string exist_condition_bit_mask = int_to_string(1 << a.exist_var_bit);
    unpoison_if_msan(exist_condition_var_name);
    unpoison_if_msan(exist_condition_bit_mask);

    res += "if (";
    res += exist_condition_var_name;
    res += " & ";
    res += exist_condition_bit_mask;
    res += ") { ";
  }

  if (flat) {
    //    TODO
    //    return gen_field_store(const tl::arg &a, const std::vector<tl::arg> &args, std::vector<tl::var_description> &vars, bool flat, int storer_type);
  }

  if (a.var_num >= 0) {
    assert(a.type->get_type() == tl::NODE_TYPE_TYPE);
    assert(static_cast<const tl::tl_tree_type *>(a.type)->type->id == tl::ID_VAR_NUM);
    assert(a.var_num < static_cast<int>(vars.size()));
    if (!vars[a.var_num].is_stored) {
      std::string reassigned_field_name = "(";
      reassigned_field_name += gen_var_name(vars[a.var_num]);
      reassigned_field_name += " = ";
      reassigned_field_name += field_name;
      field_name = std::move(reassigned_field_name);
      for (const tl::arg &other_arg : args) {
        if (other_arg.exist_var_num != a.var_num || other_arg.type->get_type() != tl::NODE_TYPE_TYPE ||
            (other_arg.flags & tl::FLAG_OPT_VAR) != 0) {
          continue;
        }
        const tl::tl_tree_type *tree_type = static_cast<tl::tl_tree_type *>(other_arg.type);
        if (tree_type->type->name == "True") {
          std::string other_arg_field_name = gen_field_name(other_arg.name);
          std::string other_arg_exist_var_bit = int_to_string(other_arg.exist_var_bit);
          unpoison_if_msan(other_arg_exist_var_bit);
          field_name += " | (";
          field_name += other_arg_field_name;
          field_name += " << ";
          field_name += other_arg_exist_var_bit;
          field_name += ")";
        }
      }
      field_name += ")";
      vars[a.var_num].is_stored = true;
    } else {
      assert(false);  // need to check value of stored var
      field_name = gen_var_name(vars[a.var_num]);
    }
  }

  assert(a.type->get_type() == tl::NODE_TYPE_TYPE);
  const tl::tl_tree_type *tree_type = static_cast<tl::tl_tree_type *>(a.type);
  res += gen_type_store(field_name, tree_type, vars, storer_type);
  if (a.exist_var_num >= 0) {
    res += " }";
  }
  res += "\n";
  return res;
}

std::string TD_TL_writer_cpp::gen_forward_class_declaration(const std::string &class_name, bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_class_begin(const std::string &class_name, const std::string &base_class_name,
                                              bool is_proxy, const tl::tl_tree *result) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_class_end() const {
  return "";
}

std::string TD_TL_writer_cpp::gen_class_alias(const std::string &class_name, const std::string &alias_name) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_get_id(const std::string &class_name, std::int32_t id, bool is_proxy) const {
  if (is_proxy) {
    return "";
  }
  return "\nconst std::int32_t " + class_name + "::ID;\n";
}

std::string TD_TL_writer_cpp::gen_function_result_type(const tl::tl_tree *result) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_fetch_function_begin(const std::string &parser_name, const std::string &class_name,
                                                       const std::string &parent_class_name, int arity, int field_count,
                                                       std::vector<tl::var_description> &vars, int parser_type) const {
  unpoison_if_msan(parser_name);
  unpoison_if_msan(class_name);
  unpoison_if_msan(parent_class_name);
  unpoison_if_msan(vars);
  current_fetch_class_name_ = class_name;
  current_fetch_case_class_names_.clear();

  for (std::size_t i = 0; i < vars.size(); i++) {
    assert(!vars[i].is_stored);
  }

  std::string fetched_type = "object_ptr<" + class_name + "> ";
  std::string returned_type = "object_ptr<" + parent_class_name + "> ";
  assert(arity == 0);

  if (parser_type == 0) {
    std::string result;
    result += "\n";
    result += returned_type;
    result += class_name;
    result += "::fetch(";
    result += parser_name;
    result += " &p) {\n";
    result += "  return make_tl_object<";
    result += class_name;
    result += ">(";
    if (field_count == 0) {
      result += ");\n";
    } else {
      result += "p);\n";
      result += "}\n\n";
      result += class_name;
      result += "::";
      result += class_name;
      result += "(";
      result += parser_name;
      result += " &p)\n";
    }
    return result;
  }

  std::string result;
  result += "\n";
  result += returned_type;
  result += class_name;
  result += "::fetch(";
  result += parser_name;
  result += " &p) {\n";
  result += "#define FAIL(error) p.set_error(error); return nullptr;\n";
  if (parser_type != -1) {
    result += "  ";
    result += fetched_type;
    result += "res = make_tl_object<";
    result += class_name;
    result += ">();\n";
  }
  return result;
}

std::string TD_TL_writer_cpp::gen_fetch_function_end(bool has_parent, int field_count,
                                                     const std::vector<tl::var_description> &vars,
                                                     int parser_type) const {
  for (std::size_t i = 0; i < vars.size(); i++) {
    assert(vars[i].is_stored);
  }

  if (parser_type == 0) {
    if (field_count == 0) {
      return "}\n";
    }
    return "{}\n";
  }

  if (parser_type == -1) {
    return "#undef FAIL\n"
           "}\n";
  }

  return "  if (p.get_error()) { FAIL(\"\"); }\n"
         "  return " +
         std::string(has_parent ? "std::move(res)" : "res") +
         ";\n"
         "#undef FAIL\n"
         "}\n";
}

std::string TD_TL_writer_cpp::gen_fetch_function_result_begin(const std::string &parser_name,
                                                              const std::string &class_name,
                                                              const tl::tl_tree *result) const {
  return "\n" + class_name + "::ReturnType " + class_name + "::fetch_result(" + parser_name +
         " &p) {\n"
         "#define FAIL(error) p.set_error(error); return ReturnType()\n"
         "  return ";
}

std::string TD_TL_writer_cpp::gen_fetch_function_result_end() const {
  return ";\n"
         "#undef FAIL\n"
         "}\n";
}

std::string TD_TL_writer_cpp::gen_fetch_function_result_any_begin(const std::string &parser_name,
                                                                  const std::string &class_name, bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_fetch_function_result_any_end(bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_cpp::gen_store_function_begin(const std::string &storer_name, const std::string &class_name,
                                                       int arity, std::vector<tl::var_description> &vars,
                                                       int storer_type) const {
  unpoison_if_msan(storer_name);
  unpoison_if_msan(class_name);
  unpoison_if_msan(vars);
  for (std::size_t i = 0; i < vars.size(); i++) {
    vars[i].is_stored = false;
  }

  if (storer_type == -1) {
    return "";
  }

  assert(arity == 0);
  std::string result = "\nvoid ";
  result += class_name;
  result += "::store(";
  result += storer_name;
  result += " &s";
  if (storer_type > 0) {
    result += ", const char *field_name";
  }
  result += ") const {\n";
  if (storer_type <= 0) {
    result += "  (void)sizeof(s);\n";
  } else {
    result +=
        "  if (!LOG_IS_STRIPPED(ERROR)) {\n"
        "    s.store_class_begin(field_name, \"";
    result += get_pretty_class_name(class_name);
    result += "\");\n";
  }
  return result;
}

std::string TD_TL_writer_cpp::gen_store_function_end(const std::vector<tl::var_description> &vars,
                                                     int storer_type) const {
  for (std::size_t i = 0; i < vars.size(); i++) {
    assert(vars[i].is_stored);
  }

  if (storer_type == -1) {
    return "";
  }

  return (storer_type <= 0 ? std::string()
                           : "    s.store_class_end();\n"
                             "  }\n") +
         "}\n";
}

std::string TD_TL_writer_cpp::gen_fetch_switch_begin() const {
  if (tl_name == "secret_api" && current_fetch_class_name_ == "Object") {
    return "  int constructor = p.fetch_int();\n";
  }

  return "  int constructor = p.fetch_int();\n"
         "  switch (constructor) {\n";
}

std::string TD_TL_writer_cpp::gen_fetch_switch_case(const tl::tl_combinator *t, int arity) const {
  assert(arity == 0);
  unpoison_if_msan(*t);
  unpoison_if_msan(t->name);
  const auto class_name = gen_class_name(t->name);
  if (tl_name == "secret_api" && current_fetch_class_name_ == "Object") {
    current_fetch_case_class_names_.push_back(class_name);
    return "";
  }

  std::string result = "    case ";
  result += class_name;
  result +=
      "::ID:\n"
      "      return ";
  result += class_name;
  result += "::fetch(p);\n";
  return result;
}

std::string TD_TL_writer_cpp::gen_fetch_switch_end() const {
  if (tl_name == "secret_api" && current_fetch_class_name_ == "Object") {
    std::string result =
        "  using ObjectFetchFunction = object_ptr<Object> (*)(TlParser &);\n"
        "  struct ObjectDispatchEntry {\n"
        "    int constructor;\n"
        "    ObjectFetchFunction fetch;\n"
        "  };\n"
        "  static const ObjectDispatchEntry kDispatchTable[] = {\n";

    for (const auto &class_name : current_fetch_case_class_names_) {
      result += "    {" + class_name + "::ID, [](TlParser &p) -> object_ptr<Object> { return " + class_name +
                "::fetch(p); }},\n";
    }

    result +=
        "  };\n"
        "  for (const auto &entry : kDispatchTable) {\n"
        "    if (entry.constructor == constructor) {\n"
        "      return entry.fetch(p);\n"
        "    }\n"
        "  }\n"
        "  FAIL(PSTRING() << \"Unknown constructor found \" << format::as_hex(constructor));\n";
    return result;
  }

  return "    default:\n"
         "      FAIL(PSTRING() << \"Unknown constructor found \" << format::as_hex(constructor));\n"
         "  }\n";
}

std::string TD_TL_writer_cpp::gen_constructor_begin(int field_count, const std::string &class_name,
                                                    bool is_default) const {
  return "\n" + class_name + "::" + class_name + "(";
}

std::string TD_TL_writer_cpp::gen_constructor_field_init(int field_num, const std::string &class_name, const tl::arg &a,
                                                         bool is_default) const {
  unpoison_if_msan(class_name);
  std::string field_type = gen_field_type(a);
  if (field_type.empty()) {
    return "";
  }
  std::string move_begin;
  std::string move_end;
  if ((field_type == "bytes" || field_type == "secure_bytes" || field_type.compare(0, 5, "array") == 0 ||
       field_type.compare(0, 10, "object_ptr") == 0) &&
      !is_default) {
    move_begin = "std::move(";
    move_end = ")";
  }

  std::string result = field_num == 0 ? ")\n  : " : "  , ";
  std::string field_name = gen_field_name(a.name);
  result += field_name;
  result += "(";
  result += move_begin;
  if (!is_default) {
    result += field_name;
  }
  result += move_end;
  result += ")\n";
  return result;
}

std::string TD_TL_writer_cpp::gen_constructor_end(const tl::tl_combinator *t, int field_count, bool is_default) const {
  if (field_count == 0) {
    return ") {\n"
           "}\n";
  }
  return "{}\n";
}

}  // namespace td

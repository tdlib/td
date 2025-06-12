//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "tl_writer_cpp.h"

#include <cassert>

namespace td {

std::string TD_TL_writer_cpp::gen_output_begin(const std::string &additional_imports) const {
  std::string ext_include_str;
  for (auto &it : ext_include) {
    ext_include_str += "#include " + it + "\n";
  }
  if (!ext_include_str.empty()) {
    ext_include_str += "\n";
  }

  return "#include \"" + tl_name + ".h\"\n\n" + ext_include_str +
         "#include \"td/utils/common.h\"\n"
         "#include \"td/utils/format.h\"\n"
         "#include \"td/utils/logging.h\"\n"
         "#include \"td/utils/SliceBuilder.h\"\n"
         "#include \"td/utils/tl_parsers.h\"\n"
         "#include \"td/utils/tl_storers.h\"\n"
         "#include \"td/utils/TlStorerToString.h\"\n\n" +
         additional_imports +
         "namespace td {\n"
         "namespace " +
         tl_name + " {\n\n";
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
  for (std::size_t i = 0; i < vars.size(); i++) {
    vars[i].index = static_cast<int>(i);
    vars[i].is_stored = false;
    vars[i].is_type = false;
    vars[i].parameter_num = -1;
    vars[i].function_arg_num = -1;
  }

  if (result_type != nullptr) {
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
      res += "  " + gen_class_name("#") + " " + gen_var_name(vars[i]) + ";\n";
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
  return "s.store_binary(" + id + ");";
}

std::string TD_TL_writer_cpp::gen_constructor_id_store(std::int32_t id, int storer_type) const {
  if (storer_type == 1) {
    return "";
  }
  return "  " + gen_constructor_id_store_raw(int_to_string(id)) + "\n";
}

std::string TD_TL_writer_cpp::gen_fetch_class_name(const tl::tl_tree_type *tree_type) const {
  const tl::tl_type *t = tree_type->type;
  const std::string &name = t->name;

  if (name == "#" || name == "Int32") {
    return "TlFetchInt";
  }
  if (name == "Int53" || name == "Int64") {
    return "TlFetchLong";
  }
  if (name == "True" || name == "Bool" || name == "Int" || name == "Long" || name == "Double" || name == "Int128" ||
      name == "Int256" || name == "Int512") {
    return "TlFetch" + name;
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

    return "TlFetchVector<" + gen_full_fetch_class_name(child) + ">";
  }

  assert(!is_built_in_simple_type(name) && !is_built_in_complex_type(name));
  for (std::size_t i = 0; i < tree_type->children.size(); i++) {
    assert(tree_type->children[i]->get_type() == tl::NODE_TYPE_NAT_CONST);
  }

  assert(tree_type->children.empty());
  return "TlFetchObject<" + gen_main_class_name(t) + ">";
}

std::string TD_TL_writer_cpp::gen_full_fetch_class_name(const tl::tl_tree_type *tree_type) const {
  const tl::tl_type *t = tree_type->type;
  const std::string &name = t->name;

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
  return "TlFetchBoxed<" + gen_fetch_class_name(tree_type) + ", " + int_to_string(expected_constructor_id) + ">";
}

std::string TD_TL_writer_cpp::gen_type_fetch(const std::string &field_name, const tl::tl_tree_type *tree_type,
                                             const std::vector<tl::var_description> &vars, int parser_type) const {
  return gen_full_fetch_class_name(tree_type) + "::parse(p)";
}

std::string TD_TL_writer_cpp::gen_field_fetch(int field_num, const tl::arg &a, std::vector<tl::var_description> &vars,
                                              bool flat, int parser_type) const {
  assert(parser_type >= 0);
  std::string field_name = (parser_type == 0 ? (field_num == 0 ? ": " : ", ") : "res->") + gen_field_name(a.name);

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

    return "  " + field_name + " = " + gen_base_function_class_name() + "::fetch(p);\n";
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
        return "  " + field_name + " = (" + gen_var_name(vars[a.exist_var_num]) + " & " +
               int_to_string(1 << a.exist_var_bit) + ") != 0;\n";
      }
    }
  }

  std::string res = "  ";
  if (a.exist_var_num != -1) {
    res += "if (" + gen_var_name(vars[a.exist_var_num]) + " & " + int_to_string(1 << a.exist_var_bit) + ") { ";
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
      res += "if ((" + gen_var_name(vars[a.var_num]) + " = ";
      store_to_var_num = true;
    } else {
      assert(false);
    }
    vars[a.var_num].is_stored = true;
  }

  res += field_name + (parser_type == 0 ? "(" : " = ");

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
      res += " else { " + gen_var_name(vars[a.var_num]) + " = 0; }";
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
  std::string num = !field_name.empty() && field_name[0] == '_' ? "2" : "";
  return "{ s.store_vector_begin(\"" + get_pretty_field_name(field_name) + "\", " + field_name +
         ".size()); for (const auto &_value" + num + " : " + field_name + ") { " +
         gen_type_store("_value" + num, t, vars, storer_type) + " } s.store_class_end(); }";
}

std::string TD_TL_writer_cpp::gen_store_class_name(const tl::tl_tree_type *tree_type) const {
  const tl::tl_type *t = tree_type->type;
  const std::string &name = t->name;

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

    return "TlStoreVector<" + gen_full_store_class_name(child) + ">";
  }

  assert(!is_built_in_simple_type(name) && !is_built_in_complex_type(name));
  for (std::size_t i = 0; i < tree_type->children.size(); i++) {
    assert(tree_type->children[i]->get_type() == tl::NODE_TYPE_NAT_CONST);
  }

  assert(tree_type->children.empty());
  return "TlStoreObject";
}

std::string TD_TL_writer_cpp::gen_full_store_class_name(const tl::tl_tree_type *tree_type) const {
  const tl::tl_type *t = tree_type->type;

  assert(!(t->flags & tl::FLAG_DEFAULT_CONSTRUCTOR));  // Not supported yet

  if ((tree_type->flags & tl::FLAG_BARE) != 0 || t->name == "#" || t->name == "Bool") {
    return gen_store_class_name(tree_type);
  }

  if (is_built_in_complex_type(t->name)) {
    return "TlStoreBoxed<" + gen_store_class_name(tree_type) + ", " + int_to_string(t->constructors[0]->id) + ">";
  }

  if (!is_type_bare(t)) {
    return "TlStoreBoxedUnknown<" + gen_store_class_name(tree_type) + ">";
  }

  for (std::size_t i = 0; i < t->constructors_num; i++) {
    if (is_combinator_supported(t->constructors[i])) {
      return "TlStoreBoxed<" + gen_store_class_name(tree_type) + ", " + int_to_string(t->constructors[i]->id) + ">";
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
          return "    if (" + gen_var_name(vars[a.exist_var_num]) + " & " + int_to_string(1 << a.exist_var_bit) +
                 ") { s.store_field(\"" + get_pretty_field_name(field_name) + "\", true); }\n";
        } else {
          return "";
        }
      }
    }

    res += "if (" + gen_var_name(vars[a.exist_var_num]) + " & " + int_to_string(1 << a.exist_var_bit) + ") { ";
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
      field_name = "(" + gen_var_name(vars[a.var_num]) + " = " + field_name;
      for (const tl::arg &other_arg : args) {
        if (other_arg.exist_var_num != a.var_num || other_arg.type->get_type() != tl::NODE_TYPE_TYPE ||
            (other_arg.flags & tl::FLAG_OPT_VAR) != 0) {
          continue;
        }
        const tl::tl_tree_type *tree_type = static_cast<tl::tl_tree_type *>(other_arg.type);
        if (tree_type->type->name == "True") {
          field_name += " | (" + gen_field_name(other_arg.name) + " << " + int_to_string(other_arg.exist_var_bit) + ")";
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
  for (std::size_t i = 0; i < vars.size(); i++) {
    assert(!vars[i].is_stored);
  }

  std::string fetched_type = "object_ptr<" + class_name + "> ";
  std::string returned_type = "object_ptr<" + parent_class_name + "> ";
  assert(arity == 0);

  if (parser_type == 0) {
    std::string result = "\n" + returned_type + class_name + "::fetch(" + parser_name +
                         " &p) {\n"
                         "  return make_tl_object<" +
                         class_name + ">(";
    if (field_count == 0) {
      result += ");\n";
    } else {
      result +=
          "p);\n"
          "}\n\n" +
          class_name + "::" + class_name + "(" + parser_name + " &p)\n";
    }
    return result;
  }

  return "\n" + returned_type + class_name + "::fetch(" + parser_name +
         " &p) {\n"
         "#define FAIL(error) p.set_error(error); return nullptr;\n" +
         (parser_type == -1 ? "" : "  " + fetched_type + "res = make_tl_object<" + class_name + ">();\n");
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
  for (std::size_t i = 0; i < vars.size(); i++) {
    vars[i].is_stored = false;
  }

  if (storer_type == -1) {
    return "";
  }

  assert(arity == 0);
  return "\n"
         "void " +
         class_name + "::store(" + storer_name + " &s" +
         std::string(storer_type <= 0 ? "" : ", const char *field_name") + ") const {\n" +
         (storer_type <= 0 ? "  (void)sizeof(s);\n"
                           : "  if (!LOG_IS_STRIPPED(ERROR)) {\n"
                             "    s.store_class_begin(field_name, \"" +
                                 get_pretty_class_name(class_name) + "\");\n");
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
  return "  int constructor = p.fetch_int();\n"
         "  switch (constructor) {\n";
}

std::string TD_TL_writer_cpp::gen_fetch_switch_case(const tl::tl_combinator *t, int arity) const {
  assert(arity == 0);
  return "    case " + gen_class_name(t->name) +
         "::ID:\n"
         "      return " +
         gen_class_name(t->name) + "::fetch(p);\n";
}

std::string TD_TL_writer_cpp::gen_fetch_switch_end() const {
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

  return (field_num == 0 ? ")\n  : " : "  , ") + gen_field_name(a.name) + "(" + move_begin +
         (is_default ? "" : gen_field_name(a.name)) + move_end + ")\n";
}

std::string TD_TL_writer_cpp::gen_constructor_end(const tl::tl_combinator *t, int field_count, bool is_default) const {
  if (field_count == 0) {
    return ") {\n"
           "}\n";
  }
  return "{}\n";
}

}  // namespace td

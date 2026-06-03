//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "tl_writer_hpp.h"

#include <cassert>

namespace td {

bool TD_TL_writer_hpp::is_documentation_generated() const {
#ifdef DISABLE_HPP_DOCUMENTATION
  return false;
#else
  return true;
#endif
}

int TD_TL_writer_hpp::get_additional_function_type(const std::string &additional_function_name) const {
  assert(additional_function_name == "downcast_call");
  return 2;
}

std::vector<std::string> TD_TL_writer_hpp::get_additional_functions() const {
  return {std::string{"downcast_call"}};
}

std::string TD_TL_writer_hpp::gen_base_type_class_name(int arity) const {
  assert(arity == 0);
  return "Object";
}

std::string TD_TL_writer_hpp::gen_base_tl_class_name() const {
  return "BaseObject";
}

std::string TD_TL_writer_hpp::gen_output_begin(const std::string &additional_imports) const {
  return "#pragma once\n"
         "\n"
#ifndef DISABLE_HPP_DOCUMENTATION
         "/**\n"
         " * \\file\n"
         " * Contains downcast_call methods for calling a function object on downcasted to\n"
         " * the most derived class TDLib API object.\n"
         " */\n"
#endif
         "#include \"" +
         tl_name +
         ".h\"\n"
         "#include <type_traits>\n"
         "\n"
         "namespace td {\n"
         "namespace " +
         tl_name + " {\n\n";
}

std::string TD_TL_writer_hpp::gen_output_begin_once() const {
  return "template <class Type>\n"
         "struct downcast_call_tag {\n"
         "  using is_downcast_call_tag = void;\n"
         "  using type = Type;\n"
         "};\n\n"
         "template <class T, class = void>\n"
         "struct downcast_call_target {\n"
         "  using type = std::decay_t<T>;\n"
         "};\n\n"
         "template <class T>\n"
         "struct downcast_call_target<T, std::void_t<typename std::decay_t<T>::is_downcast_call_tag,\n"
         "                                            typename std::decay_t<T>::type>> {\n"
         "  using type = typename std::decay_t<T>::type;\n"
         "};\n\n"
         "template <class T>\n"
         "using downcast_call_target_t = typename downcast_call_target<T>::type;\n\n"
         "template <class Base, class T>\n"
         "bool downcast_call(const Base &obj, const T &func) {\n"
         "  return downcast_call(const_cast<Base &>(obj), [&](auto &value) { func(std::as_const(value)); });\n"
         "}\n\n";
}

std::string TD_TL_writer_hpp::gen_output_end() const {
  return "}  // namespace " + tl_name +
         "\n"
         "}  // namespace td\n";
}

std::string TD_TL_writer_hpp::gen_field_definition(const std::string &class_name, const std::string &type_name,
                                                   const std::string &field_name) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_vars(const tl::tl_combinator *t, const tl::tl_tree_type *result_type,
                                       std::vector<tl::var_description> &vars) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_function_vars(const tl::tl_combinator *t,
                                                std::vector<tl::var_description> &vars) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_uni(const tl::tl_tree_type *result_type, std::vector<tl::var_description> &vars,
                                      bool check_negative) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_constructor_id_store(std::int32_t id, int storer_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_field_fetch(int field_num, const tl::arg &a, std::vector<tl::var_description> &vars,
                                              bool flat, int parser_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_field_store(const tl::arg &a, const std::vector<tl::arg> &args,
                                              std::vector<tl::var_description> &vars, bool flat,
                                              int storer_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_type_fetch(const std::string &field_name, const tl::tl_tree_type *tree_type,
                                             const std::vector<tl::var_description> &vars, int parser_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_type_store(const std::string &field_name, const tl::tl_tree_type *tree_type,
                                             const std::vector<tl::var_description> &vars, int storer_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_var_type_fetch(const tl::arg &a) const {
  assert(false);
  return "";
}

std::string TD_TL_writer_hpp::gen_forward_class_declaration(const std::string &class_name, bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_class_begin(const std::string &class_name, const std::string &base_class_name,
                                              bool is_proxy, const tl::tl_tree *result) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_class_end() const {
  return "";
}

std::string TD_TL_writer_hpp::gen_class_alias(const std::string &class_name, const std::string &alias_name) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_get_id(const std::string &class_name, std::int32_t id, bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_function_result_type(const tl::tl_tree *result) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_function_begin(const std::string &parser_name, const std::string &class_name,
                                                       const std::string &parent_class_name, int arity, int field_count,
                                                       std::vector<tl::var_description> &vars, int parser_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_function_end(bool has_parent, int field_count,
                                                     const std::vector<tl::var_description> &vars,
                                                     int parser_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_function_result_begin(const std::string &parser_name,
                                                              const std::string &class_name,
                                                              const tl::tl_tree *result) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_function_result_end() const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_function_result_any_begin(const std::string &parser_name,
                                                                  const std::string &class_name, bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_function_result_any_end(bool is_proxy) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_store_function_begin(const std::string &storer_name, const std::string &class_name,
                                                       int arity, std::vector<tl::var_description> &vars,
                                                       int storer_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_store_function_end(const std::vector<tl::var_description> &vars,
                                                     int storer_type) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_switch_begin() const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_switch_case(const tl::tl_combinator *t, int arity) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_fetch_switch_end() const {
  return "";
}

std::string TD_TL_writer_hpp::gen_additional_function(const std::string &function_name, const tl::tl_combinator *t,
                                                      bool is_function) const {
  assert(function_name == "downcast_call");
  return "";
}

std::string TD_TL_writer_hpp::gen_additional_proxy_function_begin(const std::string &function_name,
                                                                  const tl::tl_type *type,
                                                                  const std::string &class_name, int arity,
                                                                  bool is_function) const {
  assert(function_name == "downcast_call");
  return
#ifndef DISABLE_HPP_DOCUMENTATION
      "/**\n"
      " * Calls the specified function object with the given object downcasted to its most derived type.\n"
      " * \\param[in] obj Object to pass as an argument to the function object.\n"
      " * \\param[in] func Function object to which the object will be passed.\n"
      " * \\returns Whether function object call has happened. Should always return true for correct parameters.\n"
      " */\n"
#endif
      "template <class T, bool AllowTag>\n"
      "bool downcast_call_impl(int32 constructor, " +
      class_name +
      " *obj, const T &func);\n\n"
      "template <class T>\n"
      "bool downcast_call(" +
      class_name +
      " &obj, const T &func) {\n"
      "  return downcast_call_impl<T, false>(obj.get_id(), &obj, func);\n"
      "}\n\n"
      "template <class T>\n"
      "bool downcast_construct_call(int32 constructor, " +
      class_name +
      " *obj, const T &func) {\n"
      "  return downcast_call_impl<T, true>(constructor, obj, func);\n"
      "}\n\n"
      "template <class T, bool AllowTag>\n"
      "bool downcast_call_impl(int32 constructor, " +
      class_name +
      " *obj, const T &func) {  //-V2008\n"
      "  switch (constructor) {\n";
}

std::string TD_TL_writer_hpp::gen_additional_proxy_function_case(const std::string &function_name,
                                                                 const tl::tl_type *type, const std::string &class_name,
                                                                 int arity) const {
  assert(function_name == "downcast_call");
  assert(false);
  return "";
}

std::string TD_TL_writer_hpp::gen_additional_proxy_function_case(const std::string &function_name,
                                                                 const tl::tl_type *type, const tl::tl_combinator *t,
                                                                 int arity, bool is_function) const {
  assert(function_name == "downcast_call");
  const auto concrete_class_name = gen_class_name(t->name);

  std::string result = "    case ";
  result += concrete_class_name;
  result +=
      "::ID:\n"
      "      if constexpr (AllowTag) {\n"
      "        downcast_call_tag<";
  result += concrete_class_name;
  result +=
      "> type_tag;\n"
      "        func(type_tag);\n"
      "      } else {\n"
      "        func(static_cast<";
  result += concrete_class_name;
  result +=
      " &>(*obj));\n"
      "      }\n"
      "      return true;\n";
  return result;
}

std::string TD_TL_writer_hpp::gen_additional_proxy_function_end(const std::string &function_name,
                                                                const tl::tl_type *type, bool is_function) const {
  assert(function_name == "downcast_call");
  return "    default:\n"
         "      return false;\n"
         "  }\n"
         "}\n\n";
}

std::string TD_TL_writer_hpp::gen_constructor_begin(int field_count, const std::string &class_name,
                                                    bool is_default) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_constructor_parameter(int field_num, const std::string &class_name, const tl::arg &a,
                                                        bool is_default) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_constructor_field_init(int field_num, const std::string &class_name, const tl::arg &a,
                                                         bool is_default) const {
  return "";
}

std::string TD_TL_writer_hpp::gen_constructor_end(const tl::tl_combinator *t, int field_count, bool is_default) const {
  return "";
}

}  // namespace td

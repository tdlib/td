//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/tl/tl_config.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <utility>

#if defined(__has_include)
#if __has_include(<sanitizer/msan_interface.h>)
#include <sanitizer/msan_interface.h>
#define TD_TL_HAS_MSAN_INTERFACE 1
#else
#define TD_TL_HAS_MSAN_INTERFACE 0
#endif
#else
#define TD_TL_HAS_MSAN_INTERFACE 0
#endif

#if TD_TL_HAS_MSAN_INTERFACE
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#define TD_TL_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#define TD_TL_MSAN_ACTIVE 1
#endif
#ifndef TD_TL_MSAN_ACTIVE
#define TD_TL_MSAN_ACTIVE 0
#endif
#else
#define TD_TL_MSAN_ACTIVE 0
#endif

namespace td::tl {

template <class T, class... Args>
T *allocate_zeroed(Args &&...args) {
  auto *storage = static_cast<unsigned char *>(::operator new(sizeof(T)));
  std::memset(storage, 0, sizeof(T));
  return new (storage) T(std::forward<Args>(args)...);
}

template <class T>
void destroy_zeroed(T *value) {
  if (value == nullptr) {
    return;
  }
  std::destroy_at(value);
  ::operator delete(value);
}

template <class T>
void unpoison_if_msan(T &value) {
#if TD_TL_MSAN_ACTIVE
  __msan_unpoison(&value, sizeof(value));
#else
  (void)value;
#endif
}

void unpoison_if_msan(std::string &value) {
#if TD_TL_MSAN_ACTIVE
  __msan_unpoison(&value, sizeof(value));
  if (!value.empty()) {
    __msan_unpoison(value.data(), value.size());
  }
  __msan_unpoison(value.data() + value.size(), 1);
#else
  (void)value;
#endif
}

template <class T>
void unpoison_vector_data_if_msan(std::vector<T> &values) {
#if TD_TL_MSAN_ACTIVE
  __msan_unpoison(&values, sizeof(values));
  if (!values.empty()) {
    __msan_unpoison(values.data(), values.size() * sizeof(values[0]));
  }
#else
  (void)values;
#endif
}

void unpoison_tree_graph(tl_tree *tree, std::set<const tl_type *> &visited_types,
                         std::set<const tl_combinator *> &visited_combinators,
                         std::set<const tl_tree *> &visited_trees);

void unpoison_arg_graph(arg &value, std::set<const tl_type *> &visited_types,
                        std::set<const tl_combinator *> &visited_combinators,
                        std::set<const tl_tree *> &visited_trees) {
  unpoison_if_msan(value);
  unpoison_if_msan(value.name);
  if (value.type != nullptr) {
    unpoison_tree_graph(value.type, visited_types, visited_combinators, visited_trees);
  }
}

void unpoison_args_graph(std::vector<arg> &values, std::set<const tl_type *> &visited_types,
                         std::set<const tl_combinator *> &visited_combinators,
                         std::set<const tl_tree *> &visited_trees) {
  unpoison_vector_data_if_msan(values);
  for (auto &value : values) {
    unpoison_arg_graph(value, visited_types, visited_combinators, visited_trees);
  }
}

void unpoison_combinator_graph(tl_combinator *combinator, std::set<const tl_type *> &visited_types,
                               std::set<const tl_combinator *> &visited_combinators,
                               std::set<const tl_tree *> &visited_trees) {
  if (combinator == nullptr || !visited_combinators.insert(combinator).second) {
    return;
  }

  unpoison_if_msan(*combinator);
  unpoison_if_msan(combinator->name);
  unpoison_args_graph(combinator->args, visited_types, visited_combinators, visited_trees);
  if (combinator->result != nullptr) {
    unpoison_tree_graph(combinator->result, visited_types, visited_combinators, visited_trees);
  }
}

void unpoison_type_graph(tl_type *type, std::set<const tl_type *> &visited_types,
                         std::set<const tl_combinator *> &visited_combinators,
                         std::set<const tl_tree *> &visited_trees) {
  if (type == nullptr || !visited_types.insert(type).second) {
    return;
  }

  unpoison_if_msan(*type);
  unpoison_if_msan(type->name);
  unpoison_vector_data_if_msan(type->constructors);
  for (auto *constructor : type->constructors) {
    unpoison_combinator_graph(constructor, visited_types, visited_combinators, visited_trees);
  }
}

void unpoison_tree_graph(tl_tree *tree, std::set<const tl_type *> &visited_types,
                         std::set<const tl_combinator *> &visited_combinators,
                         std::set<const tl_tree *> &visited_trees) {
  if (tree == nullptr || !visited_trees.insert(tree).second) {
    return;
  }

  unpoison_if_msan(*tree);
  switch (tree->get_type()) {
    case NODE_TYPE_TYPE: {
      auto *type_tree = static_cast<tl_tree_type *>(tree);
      unpoison_if_msan(*type_tree);
      unpoison_vector_data_if_msan(type_tree->children);
      unpoison_type_graph(type_tree->type, visited_types, visited_combinators, visited_trees);
      for (auto *child : type_tree->children) {
        unpoison_tree_graph(child, visited_types, visited_combinators, visited_trees);
      }
      break;
    }
    case NODE_TYPE_ARRAY: {
      auto *array_tree = static_cast<tl_tree_array *>(tree);
      unpoison_if_msan(*array_tree);
      unpoison_tree_graph(array_tree->multiplicity, visited_types, visited_combinators, visited_trees);
      unpoison_args_graph(array_tree->args, visited_types, visited_combinators, visited_trees);
      break;
    }
    case NODE_TYPE_NAT_CONST:
      unpoison_if_msan(*static_cast<tl_tree_nat_const *>(tree));
      break;
    case NODE_TYPE_VAR_TYPE:
      unpoison_if_msan(*static_cast<tl_tree_var_type *>(tree));
      break;
    case NODE_TYPE_VAR_NUM:
      unpoison_if_msan(*static_cast<tl_tree_var_num *>(tree));
      break;
    default:
      assert(false && "unexpected tl_tree node type");
  }
}

void unpoison_config_graph(tl_config &config) {
  std::set<const tl_type *> visited_types;
  std::set<const tl_combinator *> visited_combinators;
  std::set<const tl_tree *> visited_trees;

  for (std::size_t type_num = 0; type_num < config.get_type_count(); type_num++) {
    unpoison_type_graph(config.get_type_by_num(type_num), visited_types, visited_combinators, visited_trees);
  }
  for (std::size_t function_num = 0; function_num < config.get_function_count(); function_num++) {
    unpoison_combinator_graph(config.get_function_by_num(function_num), visited_types, visited_combinators,
                              visited_trees);
  }
}

const std::int32_t TLS_SCHEMA_V2 = 0x3a2f9be2;
const std::int32_t TLS_SCHEMA_V3 = 0xe4a8604b;
const std::int32_t TLS_SCHEMA_V4 = 0x90ac88d7;
const std::int32_t TLS_TYPE = 0x12eb4386;
const std::int32_t TLS_COMBINATOR = 0x5c0a1ed5;
const std::int32_t TLS_COMBINATOR_LEFT_BUILTIN = 0xcd211f63;
const std::int32_t TLS_COMBINATOR_LEFT = 0x4c12c6d9;
const std::int32_t TLS_COMBINATOR_RIGHT_V2 = 0x2c064372;
const std::int32_t TLS_ARG_V2 = 0x29dfe61b;

const std::int32_t TLS_EXPR_NAT = 0xdcb49bd8;
const std::int32_t TLS_EXPR_TYPE = 0xecc9da78;

const std::int32_t TLS_NAT_CONST_OLD = 0xdcb49bd8;
const std::int32_t TLS_NAT_CONST = 0x8ce940b1;
const std::int32_t TLS_NAT_VAR = 0x4e8a14f0;
const std::int32_t TLS_TYPE_VAR = 0x0142ceae;
const std::int32_t TLS_ARRAY = 0xd9fb20de;
const std::int32_t TLS_TYPE_EXPR = 0xc1863d08;

tl_config::~tl_config() {
  clear();
}

tl_config::tl_config(tl_config &&other) noexcept {
  *this = std::move(other);
}

tl_config &tl_config::operator=(tl_config &&other) noexcept {
  if (this != &other) {
    clear();

    types = std::move(other.types);
    id_to_type = std::move(other.id_to_type);
    name_to_type = std::move(other.name_to_type);

    functions = std::move(other.functions);
    id_to_function = std::move(other.id_to_function);
    name_to_function = std::move(other.name_to_function);

    other.types.clear();
    other.id_to_type.clear();
    other.name_to_type.clear();

    other.functions.clear();
    other.id_to_function.clear();
    other.name_to_function.clear();
  }
  return *this;
}

void tl_config::clear() {
  for (auto *type : types) {
    destroy_zeroed(type);
  }
  for (auto *function : functions) {
    destroy_zeroed(function);
  }

  types.clear();
  functions.clear();

  id_to_type.clear();
  name_to_type.clear();
  id_to_function.clear();
  name_to_function.clear();
}

void tl_config::add_type(tl_type *type) {
  types.push_back(type);
  id_to_type[type->id] = type;
  // Rebuild the key from the initialized character range only, avoiding
  // sanitizer-visible reads of inactive string storage.
  name_to_type.insert_or_assign(std::string(type->name.data(), type->name.size()), type);
}

tl_type *tl_config::get_type(std::int32_t type_id) const {
  std::map<std::int32_t, tl_type *>::const_iterator it = id_to_type.find(type_id);
  assert(it != id_to_type.end());
  return it->second;
}

tl_type *tl_config::get_type(const std::string &type_name) {
  return name_to_type[type_name];
}

void tl_config::add_function(tl_combinator *function) {
  functions.push_back(function);
  id_to_function[function->id] = function;
  name_to_function.insert_or_assign(std::string(function->name.data(), function->name.size()), function);
}

tl_combinator *tl_config::get_function(std::int32_t function_id) {
  return id_to_function[function_id];
}

tl_combinator *tl_config::get_function(const std::string &function_name) {
  return name_to_function[function_name];
}

std::size_t tl_config::get_type_count() const {
  return types.size();
}

tl_type *tl_config::get_type_by_num(std::size_t num) const {
  return types[num];
}

std::size_t tl_config::get_function_count() const {
  return functions.size();
}

tl_combinator *tl_config::get_function_by_num(std::size_t num) const {
  return functions[num];
}

std::int32_t tl_config_parser::try_parse_int() {
  return try_parse(p.fetch_int());
}

std::int64_t tl_config_parser::try_parse_long() {
  return try_parse(p.fetch_long());
}

std::string tl_config_parser::try_parse_string() {
  std::string result = p.fetch_string();
  if (p.get_error() != nullptr) {
    std::fprintf(stderr, "Wrong TL-scheme specified: %s at %d\n", p.get_error(), static_cast<int>(p.get_error_pos()));
    std::abort();
  }

  return result;
}

template <class T>
T tl_config_parser::try_parse(const T &res) const {
  if (p.get_error() != nullptr) {
    std::fprintf(stderr, "Wrong TL-scheme specified: %s at %d\n", p.get_error(), static_cast<int>(p.get_error_pos()));
    std::abort();
  }

  return res;
}

int tl_config_parser::get_schema_version(std::int32_t version_id) {
  if (version_id == TLS_SCHEMA_V4) {
    return 4;
  }
  if (version_id == TLS_SCHEMA_V3) {
    return 3;
  }
  if (version_id == TLS_SCHEMA_V2) {
    return 2;
  }
  return -1;
}

tl_tree *tl_config_parser::read_num_const() {
  auto num = static_cast<int>(try_parse_int());

  return new tl_tree_nat_const(FLAG_NOVAR, num);
}

tl_tree *tl_config_parser::read_num_var(int *var_count) {
  std::int32_t diff = try_parse_int();
  auto var_num = static_cast<int>(try_parse_int());

  if (var_num >= *var_count) {
    *var_count = var_num + 1;
  }

  return new tl_tree_var_num(0, var_num, diff);
}

tl_tree *tl_config_parser::read_type_var(int *var_count) {
  auto var_num = static_cast<int>(try_parse_int());
  std::int32_t flags = try_parse_int();

  if (var_num >= *var_count) {
    *var_count = var_num + 1;
  }
  assert(!(flags & (FLAG_NOVAR | FLAG_BARE)));

  return new tl_tree_var_type(flags, var_num);
}

tl_tree *tl_config_parser::read_array(int *var_count) {
  std::int32_t flags = FLAG_NOVAR;
  tl_tree *multiplicity = read_nat_expr(var_count);

  auto *tree = new tl_tree_array(flags, multiplicity, read_args(var_count));

  for (const auto &arg_entry : tree->args) {
    if (!(arg_entry.flags & FLAG_NOVAR)) {
      tree->flags &= ~FLAG_NOVAR;
    }
  }
  return tree;
}

tl_tree *tl_config_parser::read_type(int *var_count) {
  tl_type *type = config.get_type(try_parse_int());
  assert(type != nullptr);
  std::int32_t flags = try_parse_int() | FLAG_NOVAR;
  auto arity = static_cast<int>(try_parse_int());
  assert(type->arity == arity);

  auto *tree = new tl_tree_type(flags, type, arity);
  for (std::int32_t i = 0; i < arity; i++) {
    tl_tree *child = read_expr(var_count);

    tree->children[i] = child;
    if (!(child->flags & FLAG_NOVAR)) {
      tree->flags &= ~FLAG_NOVAR;
    }
  }
  return tree;
}

tl_tree *tl_config_parser::read_type_expr(int *var_count) {
  std::int32_t tree_type = try_parse_int();
  switch (tree_type) {
    case TLS_TYPE_VAR:
      return read_type_var(var_count);
    case TLS_TYPE_EXPR:
      return read_type(var_count);
    case TLS_ARRAY:
      return read_array(var_count);
    default:
      std::fprintf(stderr, "tree_type = %d\n", static_cast<int>(tree_type));
      std::abort();
  }
}

tl_tree *tl_config_parser::read_nat_expr(int *var_count) {
  std::int32_t tree_type = try_parse_int();
  switch (tree_type) {
    case TLS_NAT_CONST_OLD:
    case TLS_NAT_CONST:
      return read_num_const();
    case TLS_NAT_VAR:
      return read_num_var(var_count);
    default:
      std::fprintf(stderr, "tree_type = %d\n", static_cast<int>(tree_type));
      std::abort();
  }
}

tl_tree *tl_config_parser::read_expr(int *var_count) {
  std::int32_t tree_type = try_parse_int();
  switch (tree_type) {
    case TLS_EXPR_NAT:
      return read_nat_expr(var_count);
    case TLS_EXPR_TYPE:
      return read_type_expr(var_count);
    default:
      std::fprintf(stderr, "tree_type = %d\n", static_cast<int>(tree_type));
      std::abort();
  }
}

std::vector<arg> tl_config_parser::read_args(int *var_count) {
  const int schema_flag_opt_field = 2 << static_cast<int>(schema_version >= 3);
  const int schema_flag_has_vars = schema_flag_opt_field ^ 6;

  auto args_num = static_cast<std::size_t>(try_parse_int());
  std::vector<arg> args(args_num);
  for (std::size_t i = 0; i < args_num; i++) {
    if (std::int32_t arg_v = try_parse_int(); arg_v != TLS_ARG_V2) {
      std::fprintf(stderr, "Wrong tls_arg magic %d\n", static_cast<int>(arg_v));
      std::abort();
    }

    std::string parsed_arg_name = try_parse_string();
    unpoison_if_msan(parsed_arg_name);
    arg cur_arg(std::move(parsed_arg_name));
    unpoison_if_msan(cur_arg.name);
    cur_arg.flags = try_parse_int();

    bool is_optional = false;
    if (cur_arg.flags & schema_flag_opt_field) {
      cur_arg.flags &= ~schema_flag_opt_field;
      is_optional = true;
    }
    if (cur_arg.flags & schema_flag_has_vars) {
      cur_arg.flags &= ~schema_flag_has_vars;
      cur_arg.var_num = static_cast<int>(try_parse_int());
    } else {
      cur_arg.var_num = -1;
    }

    if (cur_arg.var_num >= *var_count) {
      *var_count = cur_arg.var_num + 1;
    }
    if (is_optional) {
      cur_arg.exist_var_num = static_cast<int>(try_parse_int());
      cur_arg.exist_var_bit = static_cast<int>(try_parse_int());
    } else {
      cur_arg.exist_var_num = -1;
      cur_arg.exist_var_bit = 0;
    }
    cur_arg.type = read_type_expr(var_count);
    if (cur_arg.type->flags & FLAG_NOVAR) {
      cur_arg.flags |= FLAG_NOVAR;
    }

    args[i] = cur_arg;
    unpoison_if_msan(args[i]);
  }
  unpoison_if_msan(args);
  return args;
}

tl_combinator *tl_config_parser::read_combinator() {
  if (std::int32_t t = try_parse_int(); t != TLS_COMBINATOR) {
    std::fprintf(stderr, "Wrong tls_combinator magic %d\n", static_cast<int>(t));
    std::abort();
  }

  const auto combinator_id = try_parse_int();
  std::string parsed_combinator_name = try_parse_string();
  unpoison_if_msan(parsed_combinator_name);
  auto *combinator = allocate_zeroed<tl_combinator>(combinator_id, std::move(parsed_combinator_name));
  unpoison_if_msan(combinator->name);
  combinator->type_id = try_parse_int();
  combinator->var_count = 0;

  if (std::int32_t left_type = try_parse_int(); left_type == TLS_COMBINATOR_LEFT) {
    combinator->args = read_args(&combinator->var_count);
  } else {
    if (left_type != TLS_COMBINATOR_LEFT_BUILTIN) {
      std::fprintf(stderr, "Wrong tls_combinator_left magic %d\n", static_cast<int>(left_type));
      std::abort();
    }
  }

  if (std::int32_t right_ver = try_parse_int(); right_ver != TLS_COMBINATOR_RIGHT_V2) {
    std::fprintf(stderr, "Wrong tls_combinator_right magic %d\n", static_cast<int>(right_ver));
    std::abort();
  }
  combinator->result = read_type_expr(&combinator->var_count);
  unpoison_if_msan(*combinator);

  return combinator;
}

tl_type *tl_config_parser::read_type() {
  if (std::int32_t t = try_parse_int(); t != TLS_TYPE) {
    std::fprintf(stderr, "Wrong tls_type magic %d\n", t);
    std::abort();
  }

  const auto type_id = try_parse_int();
  std::string parsed_type_name = try_parse_string();
  unpoison_if_msan(parsed_type_name);
  auto *type = allocate_zeroed<tl_type>(type_id, std::move(parsed_type_name));
  unpoison_if_msan(type->name);
  type->constructors_num = static_cast<std::size_t>(try_parse_int());
  type->constructors.reserve(type->constructors_num);
  type->flags = try_parse_int();
  type->flags &= ~(1 | 8 | 16 | 1024);
  if (type->flags != 0) {
    std::fprintf(stderr, "Type %s has non-zero flags: %d\n", type->name.c_str(), static_cast<int>(type->flags));
  }
  type->arity = static_cast<int>(try_parse_int());

  try_parse_long();  // unused
  unpoison_if_msan(*type);
  return type;
}

tl_config tl_config_parser::parse_config() {
  schema_version = get_schema_version(try_parse_int());
  if (schema_version < 2) {
    std::fprintf(stderr, "Unsupported tl-schema version %d\n", static_cast<int>(schema_version));
    std::abort();
  }

  try_parse_int();  // date
  try_parse_int();  // version

  std::int32_t types_n = try_parse_int();
  std::size_t constructors_total = 0;
  for (std::int32_t i = 0; i < types_n; i++) {
    tl_type *type = read_type();
    config.add_type(type);
    constructors_total += type->constructors_num;
  }

  std::int32_t constructors_n = try_parse_int();
  assert(static_cast<std::size_t>(constructors_n) == constructors_total);
  (void)constructors_total;
  for (std::int32_t i = 0; i < constructors_n; i++) {
    tl_combinator *constructor = read_combinator();
    config.get_type(constructor->type_id)->add_constructor(constructor);
  }

  std::int32_t functions_n = try_parse_int();
  for (std::int32_t i = 0; i < functions_n; i++) {
    config.add_function(read_combinator());
  }
  p.fetch_end();
  try_parse(0);
  unpoison_config_graph(config);

  return std::move(config);
}

}  // namespace td::tl

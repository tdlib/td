// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace td::tl {

const int NODE_TYPE_TYPE = 1;
const int NODE_TYPE_NAT_CONST = 2;
const int NODE_TYPE_VAR_TYPE = 3;
const int NODE_TYPE_VAR_NUM = 4;
const int NODE_TYPE_ARRAY = 5;

const std::int32_t ID_VAR_NUM = 0x70659eff;
const std::int32_t ID_VAR_TYPE = 0x2cecf817;
const std::int32_t ID_INT = 0xa8509bda;
const std::int32_t ID_LONG = 0x22076cba;
const std::int32_t ID_DOUBLE = 0x2210c154;
const std::int32_t ID_STRING = 0xb5286e24;
const std::int32_t ID_VECTOR = 0x1cb5c415;
const std::int32_t ID_DICTIONARY = 0x1f4c618f;
const std::int32_t ID_MAYBE_TRUE = 0x3f9c8ef8;
const std::int32_t ID_MAYBE_FALSE = 0x27930a7b;
const std::int32_t ID_BOOL_FALSE = 0xbc799737;
const std::int32_t ID_BOOL_TRUE = 0x997275b5;

const std::int32_t FLAG_OPT_VAR = (1 << 17);
const std::int32_t FLAG_EXCL = (1 << 18);
const std::int32_t FLAG_NOVAR = (1 << 21);
const std::int32_t FLAG_DEFAULT_CONSTRUCTOR = (1 << 25);
const std::int32_t FLAG_BARE = (1 << 0);
const std::int32_t FLAG_COMPLEX = (1 << 1);
const std::int32_t FLAGS_MASK = ((1 << 16) - 1);

class tl_combinator;

class tl_tree;

class tl_type {
 public:
  tl_type() = default;
  tl_type(std::int32_t id, std::string name) : id(id), name(std::move(name)) {
  }

  std::int32_t id = 0;
  std::string name;
  int arity = 0;
  std::int32_t flags = 0;
  int simple_constructors = 0;
  std::size_t constructors_num = 0;
  std::vector<tl_combinator *> constructors;

  ~tl_type();

  void add_constructor(tl_combinator *new_constructor);
};

class arg {
 public:
  arg() = default;
  explicit arg(std::string name) : name(std::move(name)) {
  }

  std::string name;
  std::int32_t flags = 0;
  int var_num = -1;
  int exist_var_num = -1;
  int exist_var_bit = 0;
  tl_tree *type = nullptr;
};

class tl_combinator {
 public:
  tl_combinator() = default;
  tl_combinator(std::int32_t id, std::string name) : id(id), name(std::move(name)) {
  }

  std::int32_t id = 0;
  std::string name;
  int var_count = 0;
  std::int32_t type_id = 0;
  std::vector<arg> args;
  tl_tree *result = nullptr;

  ~tl_combinator();
};

class tl_tree {
 public:
  std::int32_t flags;

  explicit tl_tree(std::int32_t flags) : flags(flags) {
  }

  virtual int get_type() const = 0;

  virtual ~tl_tree() = default;
};

class tl_tree_type : public tl_tree {
 public:
  tl_type *type;
  std::vector<tl_tree *> children;

  tl_tree_type(std::int32_t flags, tl_type *type, int child_count) : tl_tree(flags), type(type), children(child_count) {
  }

  int get_type() const override {
    return NODE_TYPE_TYPE;
  }

  ~tl_tree_type() override;
};

class tl_tree_nat_const : public tl_tree {
 public:
  int num;

  tl_tree_nat_const(std::int32_t flags, int num) : tl_tree(flags), num(num) {
  }

  int get_type() const override {
    return NODE_TYPE_NAT_CONST;
  }
};

class tl_tree_var_type : public tl_tree {
 public:
  int var_num;

  tl_tree_var_type(std::int32_t flags, int var_num) : tl_tree(flags), var_num(var_num) {
  }

  int get_type() const override {
    return NODE_TYPE_VAR_TYPE;
  }
};

class tl_tree_var_num : public tl_tree {
 public:
  int var_num;
  int diff;

  tl_tree_var_num(std::int32_t flags, int var_num, int diff) : tl_tree(flags), var_num(var_num), diff(diff) {
  }

  int get_type() const override {
    return NODE_TYPE_VAR_NUM;
  }
};

class tl_tree_array : public tl_tree {
 public:
  tl_tree *multiplicity;
  std::vector<arg> args;

  tl_tree_array(std::int32_t flags, tl_tree *multiplicity, const std::vector<arg> &a)
      : tl_tree(flags), multiplicity(multiplicity), args(a) {
  }

  int get_type() const override {
    return NODE_TYPE_ARRAY;
  }

  ~tl_tree_array() override;
};

}  // namespace td::tl

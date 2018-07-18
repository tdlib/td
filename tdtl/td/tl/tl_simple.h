//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/tl/tl_config.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace td {
namespace tl {
namespace simple {
// TL type is

std::string gen_cpp_name(std::string name) {
  for (std::size_t i = 0; i < name.size(); i++) {
    if ((name[i] < '0' || '9' < name[i]) && (name[i] < 'a' || 'z' < name[i]) && (name[i] < 'A' || 'Z' < name[i])) {
      name[i] = '_';
    }
  }
  assert(name.size() > 0);
  assert(name[name.size() - 1] != '_');
  return name;
}

std::string gen_cpp_field_name(std::string name) {
  return gen_cpp_name(name) + "_";
}

struct CustomType;
struct Type {
  enum { Int32, Int53, Int64, Double, String, Bytes, Vector, Bool, Custom } type;

  // type == Custom
  bool is_bare{false};
  const CustomType *custom{nullptr};

  // type == Vector
  const Type *vector_value_type{nullptr};
};

struct Arg {
  const Type *type;
  std::string name;
};

struct Constructor {
  std::string name;
  std::int32_t id;
  std::vector<Arg> args;
  const CustomType *type;
};

struct CustomType {
  std::string name;
  std::vector<const Constructor *> constructors;
};

struct Function {
  std::string name;
  std::int32_t id;
  std::vector<Arg> args;
  const Type *type;
};

class Schema {
 public:
  explicit Schema(const tl_config &config) {
    config_ = &config;
    for (std::size_t type_num = 0, type_count = config.get_type_count(); type_num < type_count; type_num++) {
      auto *from_type = config.get_type_by_num(type_num);
      if (from_type->name == "Vector") {
        continue;
      }
      auto *type = get_type(from_type);
      if (type->type == Type::Custom) {
        custom_types.push_back(type->custom);
      }
    }
    for (std::size_t function_num = 0, function_count = config.get_function_count(); function_num < function_count;
         function_num++) {
      auto *from_function = config.get_function_by_num(function_num);
      functions.push_back(get_function(from_function));
    }
  }

  std::vector<const CustomType *> custom_types;
  std::vector<const Function *> functions;

 private:
  std::vector<std::unique_ptr<Function>> functions_;
  std::vector<std::unique_ptr<Constructor>> constructors_;
  std::vector<std::unique_ptr<CustomType>> custom_types_;
  std::vector<std::unique_ptr<Type>> types_;

  const tl_config *config_{nullptr};
  std::map<std::int32_t, Type *> type_by_id;
  std::map<std::int32_t, Constructor *> constructor_by_id;
  std::map<std::int32_t, Function *> function_by_id;

  const Type *get_type(const tl_type *from_type) {
    auto &type = type_by_id[from_type->id];
    if (!type) {
      types_.push_back(std::make_unique<Type>());
      type = types_.back().get();

      if (from_type->name == "Int32") {
        type->type = Type::Int32;
      } else if (from_type->name == "Int53") {
        type->type = Type::Int53;
      } else if (from_type->name == "Int64") {
        type->type = Type::Int64;
      } else if (from_type->name == "Double") {
        type->type = Type::Double;
      } else if (from_type->name == "String") {
        type->type = Type::String;
      } else if (from_type->name == "Bytes") {
        type->type = Type::Bytes;
      } else if (from_type->name == "Bool") {
        type->type = Type::Bool;
      } else if (from_type->name == "Vector") {
        assert(false);  // unreachable
      } else {
        type->type = Type::Custom;
        custom_types_.push_back(std::make_unique<CustomType>());
        auto *custom_type = custom_types_.back().get();
        type->custom = custom_type;
        custom_type->name = from_type->name;
        for (auto *constructor : from_type->constructors) {
          custom_type->constructors.push_back(get_constructor(constructor));
        }
      }
    }
    return type;
  }
  const CustomType *get_custom_type(const tl_type *from_type) {
    auto *type = get_type(from_type);
    assert(type->type == Type::Custom);
    return type->custom;
  }

  const Constructor *get_constructor(const tl_combinator *from) {
    auto &constructor = constructor_by_id[from->id];
    if (!constructor) {
      constructors_.push_back(std::make_unique<Constructor>());
      constructor = constructors_.back().get();
      constructor->id = from->id;
      constructor->name = from->name;
      constructor->type = get_custom_type(config_->get_type(from->type_id));
      for (auto &from_arg : from->args) {
        Arg arg;
        arg.name = from_arg.name;
        arg.type = get_type(from_arg.type);
        constructor->args.push_back(std::move(arg));
      }
    }
    return constructor;
  }
  const Function *get_function(const tl_combinator *from) {
    auto &function = function_by_id[from->id];
    if (!function) {
      functions_.push_back(std::make_unique<Function>());
      function = functions_.back().get();
      function->id = from->id;
      function->name = from->name;
      function->type = get_type(config_->get_type(from->type_id));
      for (auto &from_arg : from->args) {
        Arg arg;
        arg.name = from_arg.name;
        arg.type = get_type(from_arg.type);
        function->args.push_back(std::move(arg));
      }
    }
    return function;
  }
  const Type *get_type(const tl_tree *tree) {
    assert(tree->get_type() == NODE_TYPE_TYPE);
    auto *type_tree = static_cast<const tl_tree_type *>(tree);
    if (type_tree->type->name == "Vector") {
      assert(type_tree->children.size() == 1);
      types_.push_back(std::make_unique<Type>());
      auto *type = types_.back().get();
      type->type = Type::Vector;
      type->vector_value_type = get_type(type_tree->children[0]);
      return type;
    } else {
      assert(type_tree->children.empty());
      return get_type(type_tree->type);
    }
  }
};

}  // namespace simple
}  // namespace tl
}  // namespace td

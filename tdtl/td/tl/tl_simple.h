//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/tl/tl_config.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <iostream>

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_TL_SIMPLE_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_TL_SIMPLE_MSAN_ACTIVE 1
#endif
#ifndef TD_TL_SIMPLE_MSAN_ACTIVE
#define TD_TL_SIMPLE_MSAN_ACTIVE 0
#endif

namespace td {
namespace tl {
namespace simple {

template <class T>
inline void unpoison_object_if_msan(const T &value) {
#if TD_TL_SIMPLE_MSAN_ACTIVE
  __msan_unpoison(const_cast<T *>(&value), sizeof(value));
#else
  (void)value;
#endif
}

inline void unpoison_if_msan(const std::string &value) {
#if TD_TL_SIMPLE_MSAN_ACTIVE
  unpoison_object_if_msan(value);
  if (!value.empty()) {
    __msan_unpoison(const_cast<char *>(value.data()), value.size());
  }
  __msan_unpoison(const_cast<char *>(value.data() + value.size()), 1);
#else
  (void)value;
#endif
}

template <class T>
inline void unpoison_vector_data_if_msan(const std::vector<T> &values) {
#if TD_TL_SIMPLE_MSAN_ACTIVE
  unpoison_object_if_msan(values);
  if (!values.empty()) {
    __msan_unpoison(const_cast<T *>(values.data()), values.size() * sizeof(values[0]));
  }
#else
  (void)values;
#endif
}

inline void unpoison_if_msan(const tl_type &value) {
  unpoison_object_if_msan(value);
  unpoison_if_msan(value.name);
  unpoison_vector_data_if_msan(value.constructors);
}

inline void unpoison_if_msan(const tl_combinator &value) {
  unpoison_object_if_msan(value);
  unpoison_if_msan(value.name);
  unpoison_vector_data_if_msan(value.args);
}

inline std::string gen_cpp_name(std::string name) {
  for (auto &c : name) {
    if ((c < '0' || '9' < c) && (c < 'a' || 'z' < c) && (c < 'A' || 'Z' < c)) {
      c = '_';
    }
  }
  assert(!name.empty());
  assert(name[name.size() - 1] != '_');
  return name;
}

inline std::string gen_cpp_field_name(std::string name) {
  return gen_cpp_name(name) + '_';
}

struct CustomType;
struct Type {
  enum { Int32, Int53, Int64, Double, String, Bytes, Vector, Bool, Custom } type = Int32;

  // type == Custom
  bool is_bare{false};
  const CustomType *custom{nullptr};

  // type == Vector
  const Type *vector_value_type{nullptr};
};

struct Arg {
  const Type *type{nullptr};
  std::string name;
};

struct Constructor {
  std::string name;
  std::int32_t id{0};
  std::vector<Arg> args;
  const CustomType *type{nullptr};
};

struct CustomType {
  std::string name;
  std::vector<const Constructor *> constructors;

  mutable bool is_result_{false};
  mutable bool is_query_{false};
};

struct Function {
  std::string name;
  std::int32_t id{0};
  std::vector<Arg> args;
  const Type *type{nullptr};
};

class Schema {
 public:
  explicit Schema(const tl_config &config) {
    config_ = &config;
    for (std::size_t type_num = 0, type_count = config.get_type_count(); type_num < type_count; type_num++) {
      auto *from_type = config.get_type_by_num(type_num);
      unpoison_if_msan(*from_type);
      if (from_type->id == ID_VECTOR) {
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
    for (auto &function : functions_) {
      mark_result(function->type);
      for (auto &arg : function->args) {
        mark_query(arg.type);
      }
    }

    //for (auto custom_type : custom_types) {
    //std::cerr << custom_type->name;
    //if (custom_type->is_result_) {
    //std::cerr << " result";
    //}
    //if (custom_type->is_query_) {
    //std::cerr << " query";
    //}
    //std::cerr << std::endl;
    //}
  }

  std::vector<const CustomType *> custom_types;
  std::vector<const Function *> functions;

 private:
  std::vector<std::unique_ptr<Function>> functions_;
  std::vector<std::unique_ptr<Constructor>> constructors_;
  std::vector<std::unique_ptr<CustomType>> custom_types_;
  std::vector<std::unique_ptr<Type>> types_;

  const tl_config *config_{nullptr};
  std::vector<std::pair<std::int32_t, Type *>> type_by_id;
  std::vector<std::pair<std::int32_t, Constructor *>> constructor_by_id;
  std::vector<std::pair<std::int32_t, Function *>> function_by_id;

  void mark_result(const Type *type) {
    do_mark(type, true);
  }

  void mark_query(const Type *type) {
    do_mark(type, false);
  }

  void do_mark(const Type *type, bool is_result) {
    if (type->type == Type::Vector) {
      return do_mark(type->vector_value_type, is_result);
    }
    if (type->type != Type::Custom) {
      return;
    }
    auto *custom = type->custom;
    auto &was = is_result ? custom->is_result_ : custom->is_query_;
    if (was) {
      return;
    }
    was = true;
    for (auto constructor : custom->constructors) {
      for (auto &arg : constructor->args) {
        do_mark(arg.type, is_result);
      }
    }
  }

  const Type *get_type(const tl_type *from_type) {
    unpoison_if_msan(*from_type);
    unpoison_if_msan(from_type->name);
    for (const auto &[cached_type_id, cached_type] : type_by_id) {
      if (cached_type_id == from_type->id) {
        return cached_type;
      }
    }

    Type *type;
    {
      types_.push_back(std::make_unique<Type>());
      type = types_.back().get();
      type_by_id.emplace_back(from_type->id, type);

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
      } else if (from_type->id == ID_VECTOR) {
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
    unpoison_if_msan(*from);
    for (const auto &[cached_constructor_id, cached_constructor] : constructor_by_id) {
      if (cached_constructor_id == from->id) {
        return cached_constructor;
      }
    }

    Constructor *constructor;
    {
      constructors_.push_back(std::make_unique<Constructor>());
      constructor = constructors_.back().get();
      constructor_by_id.emplace_back(from->id, constructor);
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
    unpoison_if_msan(*from);
    for (const auto &[cached_function_id, cached_function] : function_by_id) {
      if (cached_function_id == from->id) {
        return cached_function;
      }
    }

    Function *function;
    {
      functions_.push_back(std::make_unique<Function>());
      function = functions_.back().get();
      function_by_id.emplace_back(from->id, function);
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
    unpoison_object_if_msan(*tree);
    assert(tree->get_type() == NODE_TYPE_TYPE);
    auto *type_tree = static_cast<const tl_tree_type *>(tree);
    unpoison_object_if_msan(*type_tree);
    unpoison_vector_data_if_msan(type_tree->children);
    unpoison_if_msan(*type_tree->type);
    if (type_tree->type->id == ID_VECTOR) {
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

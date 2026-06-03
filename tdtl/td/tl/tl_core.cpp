// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/tl/tl_core.h"

#include <cassert>
#include <memory>

namespace td::tl {

namespace {

template <class T>
void destroy_zeroed_tl_object(T *value) {
  if (value == nullptr) {
    return;
  }
  std::destroy_at(value);
  ::operator delete(value);
}

}  // namespace

void tl_type::add_constructor(tl_combinator *new_constructor) {
  constructors.push_back(new_constructor);

  assert(constructors.size() <= constructors_num);
}

tl_type::~tl_type() {
  for (auto *constructor : constructors) {
    destroy_zeroed_tl_object(constructor);
  }
}

tl_combinator::~tl_combinator() {
  for (auto &arg_entry : args) {
    delete arg_entry.type;
    arg_entry.type = nullptr;
  }
  delete result;
  result = nullptr;
}

tl_tree_type::~tl_tree_type() {
  for (auto *child : children) {
    delete child;
  }
}

tl_tree_array::~tl_tree_array() {
  delete multiplicity;
  multiplicity = nullptr;

  for (auto &arg_entry : args) {
    delete arg_entry.type;
    arg_entry.type = nullptr;
  }
}

}  // namespace td::tl

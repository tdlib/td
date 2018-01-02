//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {

template <class FunctionT>
struct member_function_class;

template <class ReturnType, class Type>
struct member_function_class<ReturnType Type::*> {
  using type = Type;
};

template <class FunctionT>
using member_function_class_t = typename member_function_class<FunctionT>::type;

}  // namespace td

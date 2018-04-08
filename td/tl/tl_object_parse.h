//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/tl/TlObject.h"

#include "td/utils/int_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace td {

template <class Func, std::int32_t constructor_id>
class TlFetchBoxed {
 public:
  template <class ParserT>
  static auto parse(ParserT &p) -> decltype(Func::parse(p)) {
    if (p.fetch_int() != constructor_id) {
      p.set_error("Wrong constructor found");
      return decltype(Func::parse(p))();
    }
    return Func::parse(p);
  }
};

class TlFetchTrue {
 public:
  template <class ParserT>
  static bool parse(ParserT &p) {
    return true;
  }
};

class TlFetchBool {
 public:
  template <class ParserT>
  static bool parse(ParserT &p) {
    constexpr std::int32_t ID_BOOL_FALSE = 0xbc799737;
    constexpr std::int32_t ID_BOOL_TRUE = 0x997275b5;

    std::int32_t c = p.fetch_int();
    if (c == ID_BOOL_TRUE) {
      return true;
    }
    if (c != ID_BOOL_FALSE) {
      p.set_error("Bool expected");
    }
    return false;
  }
};

class TlFetchInt {
 public:
  template <class ParserT>
  static std::int32_t parse(ParserT &p) {
    return p.fetch_int();
  }
};

class TlFetchLong {
 public:
  template <class ParserT>
  static std::int64_t parse(ParserT &p) {
    return p.fetch_long();
  }
};

class TlFetchDouble {
 public:
  template <class ParserT>
  static double parse(ParserT &p) {
    return p.fetch_double();
  }
};

class TlFetchInt128 {
 public:
  template <class ParserT>
  static UInt128 parse(ParserT &p) {
    return p.template fetch_binary<UInt128>();
  }
};

class TlFetchInt256 {
 public:
  template <class ParserT>
  static UInt256 parse(ParserT &p) {
    return p.template fetch_binary<UInt256>();
  }
};

template <class T>
class TlFetchString {
 public:
  template <class ParserT>
  static T parse(ParserT &p) {
    return p.template fetch_string<T>();
  }
};

template <class T>
class TlFetchBytes {
 public:
  template <class ParserT>
  static T parse(ParserT &p) {
    return p.template fetch_string<T>();
  }
};

template <class Func>
class TlFetchVector {
 public:
  template <class ParserT>
  static auto parse(ParserT &p) -> std::vector<decltype(Func::parse(p))> {
    const std::uint32_t multiplicity = p.fetch_int();
    std::vector<decltype(Func::parse(p))> v;
    if (p.get_left_len() < multiplicity) {
      p.set_error("Wrong vector length");
    } else {
      v.reserve(multiplicity);
      for (std::uint32_t i = 0; i < multiplicity; i++) {
        v.push_back(Func::parse(p));
      }
    }
    return v;
  }
};

template <class T>
class TlFetchObject {
 public:
  template <class ParserT>
  static tl_object_ptr<T> parse(ParserT &p) {
    return T::fetch(p);
  }
};

}  // namespace td

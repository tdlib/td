//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/SharedSlice.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/UInt.h"

namespace td {

class TlStorerToString {
  string result;
  size_t shift = 0;

  void store_field_begin(const char *name) {
    result.append(shift, ' ');
    if (name && name[0]) {
      result += name;
      result += " = ";
    }
  }

  void store_field_end() {
    result += '\n';
  }

  void store_long(int64 value) {
    result += (PSLICE() << value).c_str();
  }

  void store_binary(Slice data) {
    static const char *hex = "0123456789ABCDEF";

    result.append("{ ", 2);
    for (auto c : data) {
      unsigned char byte = c;
      result += hex[byte >> 4];
      result += hex[byte & 15];
      result += ' ';
    }
    result += '}';
  }

 public:
  TlStorerToString() = default;
  TlStorerToString(const TlStorerToString &other) = delete;
  TlStorerToString &operator=(const TlStorerToString &other) = delete;

  void store_field(const char *name, bool value) {
    store_field_begin(name);
    result += (value ? "true" : "false");
    store_field_end();
  }

  void store_field(const char *name, int32 value) {
    store_field(name, static_cast<int64>(value));
  }

  void store_field(const char *name, int64 value) {
    store_field_begin(name);
    store_long(value);
    store_field_end();
  }

  void store_field(const char *name, double value) {
    store_field_begin(name);
    result += (PSLICE() << value).c_str();
    store_field_end();
  }

  void store_field(const char *name, const char *value) {
    store_field_begin(name);
    result += value;
    store_field_end();
  }

  void store_field(const char *name, const string &value) {
    store_field_begin(name);
    result += '"';
    result += value;
    result += '"';
    store_field_end();
  }

  void store_field(const char *name, const SecureString &value) {
    store_field_begin(name);
    result.append("<secret>");
    store_field_end();
  }

  template <class T>
  void store_field(const char *name, const T &value) {
    store_field_begin(name);
    result.append(value.data(), value.size());
    store_field_end();
  }

  void store_bytes_field(const char *name, const SecureString &value) {
    store_field_begin(name);
    result.append("<secret>");
    store_field_end();
  }

  template <class BytesT>
  void store_bytes_field(const char *name, const BytesT &value) {
    static const char *hex = "0123456789ABCDEF";

    store_field_begin(name);
    result.append("bytes [");
    store_long(static_cast<int64>(value.size()));
    result.append("] { ");
    size_t len = min(static_cast<size_t>(64), value.size());
    for (size_t i = 0; i < len; i++) {
      int b = value[static_cast<int>(i)] & 0xff;
      result += hex[b >> 4];
      result += hex[b & 15];
      result += ' ';
    }
    if (len < value.size()) {
      result.append("...");
    }
    result += '}';
    store_field_end();
  }

  template <class ObjectT>
  void store_object_field(const char *name, const ObjectT *value) {
    if (value == nullptr) {
      store_field(name, "null");
    } else {
      value->store(*this, name);
    }
  }

  void store_field(const char *name, const UInt128 &value) {
    store_field_begin(name);
    store_binary(as_slice(value));
    store_field_end();
  }

  void store_field(const char *name, const UInt256 &value) {
    store_field_begin(name);
    store_binary(as_slice(value));
    store_field_end();
  }

  void store_vector_begin(const char *field_name, size_t vector_size) {
    store_field_begin(field_name);
    result += "vector[";
    result += (PSLICE() << vector_size).c_str();
    result += "] {\n";
    shift += 2;
  }

  void store_class_begin(const char *field_name, const char *class_name) {
    store_field_begin(field_name);
    result += class_name;
    result += " {\n";
    shift += 2;
  }

  void store_class_end() {
    CHECK(shift >= 2);
    shift -= 2;
    result.append(shift, ' ');
    result += "}\n";
  }

  string move_as_string() {
    return std::move(result);
  }
};

}  // namespace td

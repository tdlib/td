//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/SharedSlice.h"
#include "td/utils/Slice.h"
#include "td/utils/StorerBase.h"
#include "td/utils/UInt.h"

#include <cstring>

namespace td {

class TlStorerUnsafe {
  unsigned char *buf_;

 public:
  explicit TlStorerUnsafe(unsigned char *buf) : buf_(buf) {
  }

  TlStorerUnsafe(const TlStorerUnsafe &other) = delete;
  TlStorerUnsafe &operator=(const TlStorerUnsafe &other) = delete;

  template <class T>
  void store_binary(const T &x) {
    std::memcpy(buf_, &x, sizeof(T));
    buf_ += sizeof(T);
  }

  void store_int(int32 x) {
    store_binary<int32>(x);
  }

  void store_long(int64 x) {
    store_binary<int64>(x);
  }

  void store_slice(Slice slice) {
    std::memcpy(buf_, slice.begin(), slice.size());
    buf_ += slice.size();
  }
  void store_storer(const Storer &storer) {
    size_t size = storer.store(buf_);
    buf_ += size;
  }

  template <class T>
  void store_string(const T &str) {
    size_t len = str.size();
    if (len < 254) {
      *buf_++ = static_cast<unsigned char>(len);
      len++;
    } else if (len < (1 << 24)) {
      *buf_++ = static_cast<unsigned char>(254);
      *buf_++ = static_cast<unsigned char>(len & 255);
      *buf_++ = static_cast<unsigned char>((len >> 8) & 255);
      *buf_++ = static_cast<unsigned char>(len >> 16);
    } else if (static_cast<uint64>(len) < (static_cast<uint64>(1) << 32)) {
      *buf_++ = static_cast<unsigned char>(255);
      *buf_++ = static_cast<unsigned char>(len & 255);
      *buf_++ = static_cast<unsigned char>((len >> 8) & 255);
      *buf_++ = static_cast<unsigned char>((len >> 16) & 255);
      *buf_++ = static_cast<unsigned char>((len >> 24) & 255);
      *buf_++ = static_cast<unsigned char>(0);
      *buf_++ = static_cast<unsigned char>(0);
      *buf_++ = static_cast<unsigned char>(0);
    } else {
      LOG(FATAL) << "String size " << len << " is too big to be stored";
    }
    std::memcpy(buf_, str.data(), str.size());
    buf_ += str.size();

    switch (len & 3) {
      case 1:
        *buf_++ = 0;
        // fallthrough
      case 2:
        *buf_++ = 0;
        // fallthrough
      case 3:
        *buf_++ = 0;
    }
  }

  unsigned char *get_buf() const {
    return buf_;
  }
};

class TlStorerCalcLength {
  size_t length = 0;

 public:
  TlStorerCalcLength() = default;
  TlStorerCalcLength(const TlStorerCalcLength &other) = delete;
  TlStorerCalcLength &operator=(const TlStorerCalcLength &other) = delete;

  template <class T>
  void store_binary(const T &x) {
    length += sizeof(T);
  }

  void store_int(int32 x) {
    store_binary<int32>(x);
  }

  void store_long(int64 x) {
    store_binary<int64>(x);
  }

  void store_slice(Slice slice) {
    length += slice.size();
  }

  void store_storer(const Storer &storer) {
    length += storer.size();
  }

  template <class T>
  void store_string(const T &str) {
    size_t add = str.size();
    if (add < 254) {
      add += 1;
    } else if (add < (1 << 24)) {
      add += 4;
    } else {
      add += 8;
    }
    add = (add + 3) & -4;
    length += add;
  }

  size_t get_length() const {
    return length;
  }
};

class TlStorerToString {
  std::string result;
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

  std::string move_as_str() {
    return std::move(result);
  }
};

template <class T>
size_t tl_calc_length(const T &data) {
  TlStorerCalcLength storer_calc_length;
  data.store(storer_calc_length);
  return storer_calc_length.get_length();
}

template <class T>
size_t tl_store_unsafe(const T &data, unsigned char *dst) TD_WARN_UNUSED_RESULT;

template <class T>
size_t tl_store_unsafe(const T &data, unsigned char *dst) {
  TlStorerUnsafe storer_unsafe(dst);
  data.store(storer_unsafe);
  return static_cast<size_t>(storer_unsafe.get_buf() - dst);
}

}  // namespace td

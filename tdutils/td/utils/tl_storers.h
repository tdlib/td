//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/int_types.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/StorerBase.h"

#include <cstring>

namespace td {

class TlStorerUnsafe {
  char *buf;

 public:
  explicit TlStorerUnsafe(char *buf) : buf(buf) {
    CHECK(is_aligned_pointer<4>(buf));
  }

  TlStorerUnsafe(const TlStorerUnsafe &other) = delete;
  TlStorerUnsafe &operator=(const TlStorerUnsafe &other) = delete;

  template <class T>
  void store_binary(const T &x) {
    std::memcpy(buf, reinterpret_cast<const unsigned char *>(&x), sizeof(T));
    buf += sizeof(T);
  }

  void store_int(int32 x) {
    *reinterpret_cast<int32 *>(buf) = x;
    buf += sizeof(int32);
  }

  void store_long(int64 x) {
    store_binary<int64>(x);
  }

  void store_slice(Slice slice) {
    std::memcpy(buf, slice.begin(), slice.size());
    buf += slice.size();
  }
  void store_storer(const Storer &storer) {
    size_t size = storer.store(reinterpret_cast<unsigned char *>(buf));
    buf += size;
  }

  template <class T>
  void store_string(const T &str) {
    size_t len = str.size();
    if (len < 254) {
      *buf++ = static_cast<char>(len);
      len++;
    } else if (len < (1 << 24)) {
      *buf++ = static_cast<char>(static_cast<unsigned char>(254));
      *buf++ = static_cast<char>(len & 255);
      *buf++ = static_cast<char>((len >> 8) & 255);
      *buf++ = static_cast<char>(len >> 16);
    } else {
      LOG(FATAL) << "String size " << len << " is too big to be stored";
    }
    std::memcpy(buf, str.data(), str.size());
    buf += str.size();

    switch (len & 3) {
      case 1:
        *buf++ = '\0';
      // fallthrough
      case 2:
        *buf++ = '\0';
      // fallthrough
      case 3:
        *buf++ = '\0';
    }
  }

  char *get_buf() const {
    return buf;
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
    } else {
      add += 4;
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
  int shift = 0;

  void store_field_begin(const char *name) {
    for (int i = 0; i < shift; i++) {
      result += ' ';
    }
    if (name && name[0]) {
      result += name;
      result += " = ";
    }
  }

  void store_field_end() {
    result += "\n";
  }

  void store_long(int64 value) {
    result += (PSLICE() << value).c_str();
  }

  void store_binary(Slice data) {
    static const char *hex = "0123456789ABCDEF";

    result.append("{ ");
    for (auto c : data) {
      unsigned char byte = c;
      result += hex[byte >> 4];
      result += hex[byte & 15];
      result += ' ';
    }
    result.append("}");
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
    result.append(value.data(), value.size());
    result += '"';
    store_field_end();
  }

  template <class T>
  void store_field(const char *name, const T &value) {
    store_field_begin(name);
    result.append(value.data(), value.size());
    store_field_end();
  }

  template <class BytesT>
  void store_bytes_field(const char *name, const BytesT &value) {
    static const char *hex = "0123456789ABCDEF";

    store_field_begin(name);
    result.append("bytes { ");
    for (size_t i = 0; i < value.size(); i++) {
      int b = value[static_cast<int>(i)] & 0xff;
      result += hex[b >> 4];
      result += hex[b & 15];
      result += ' ';
    }
    result.append("}");
    store_field_end();
  }

  void store_field(const char *name, const UInt128 &value) {
    store_field_begin(name);
    store_binary(Slice(reinterpret_cast<const unsigned char *>(&value), sizeof(value)));
    store_field_end();
  }

  void store_field(const char *name, const UInt256 &value) {
    store_field_begin(name);
    store_binary(Slice(reinterpret_cast<const unsigned char *>(&value), sizeof(value)));
    store_field_end();
  }

  void store_class_begin(const char *field_name, const char *class_name) {
    store_field_begin(field_name);
    result += class_name;
    result += " {\n";
    shift += 2;
  }

  void store_class_end() {
    shift -= 2;
    for (int i = 0; i < shift; i++) {
      result += ' ';
    }
    result += "}\n";
    CHECK(shift >= 0);
  }

  std::string str() const {
    return result;
  }
};

template <class T>
size_t tl_calc_length(const T &data) {
  TlStorerCalcLength storer_calc_length;
  data.store(storer_calc_length);
  return storer_calc_length.get_length();
}

template <class T, class CharT>
size_t tl_store_unsafe(const T &data, CharT *dst) {
  char *start = reinterpret_cast<char *>(dst);
  TlStorerUnsafe storer_unsafe(start);
  data.store(storer_unsafe);
  return storer_unsafe.get_buf() - start;
}

}  // namespace td

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/utf8.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>

namespace td {

class TlParser {
  const unsigned char *data = nullptr;
  size_t data_len = 0;
  size_t left_len = 0;
  size_t error_pos = std::numeric_limits<size_t>::max();
  std::string error;

  unique_ptr<int32[]> data_buf;
  static constexpr size_t SMALL_DATA_ARRAY_SIZE = 6;
  std::array<int32, SMALL_DATA_ARRAY_SIZE> small_data_array;

  alignas(4) static const unsigned char empty_data[sizeof(UInt256)];

 public:
  explicit TlParser(Slice slice) {
    if (slice.size() % sizeof(int32) != 0) {
      set_error("Wrong length");
      return;
    }

    data_len = left_len = slice.size();
    if (is_aligned_pointer<4>(slice.begin())) {
      data = slice.ubegin();
    } else {
      int32 *buf;
      if (data_len <= small_data_array.size() * sizeof(int32)) {
        buf = &small_data_array[0];
      } else {
        LOG(ERROR) << "Unexpected big unaligned data pointer of length " << slice.size() << " at " << slice.begin();
        data_buf = make_unique<int32[]>(data_len / sizeof(int32));
        buf = data_buf.get();
      }
      std::memcpy(static_cast<void *>(buf), static_cast<const void *>(slice.begin()), slice.size());
      data = reinterpret_cast<unsigned char *>(buf);
    }
  }

  TlParser(const TlParser &other) = delete;
  TlParser &operator=(const TlParser &other) = delete;

  void set_error(const string &error_message);

  const char *get_error() const {
    if (error.empty()) {
      return nullptr;
    }
    return error.c_str();
  }

  size_t get_error_pos() const {
    return error_pos;
  }

  Status get_status() const {
    if (error.empty()) {
      return Status::OK();
    }
    return Status::Error(PSLICE() << error << " at " << error_pos);
  }

  void check_len(const size_t len) {
    if (unlikely(left_len < len)) {
      set_error("Not enough data to read");
    } else {
      left_len -= len;
    }
  }

  int32 fetch_int_unsafe() {
    int32 result = *reinterpret_cast<const int32 *>(data);
    data += sizeof(int32);
    return result;
  }

  int32 fetch_int() {
    check_len(sizeof(int32));
    return fetch_int_unsafe();
  }

  int64 fetch_long_unsafe() {
    int64 result;
    std::memcpy(reinterpret_cast<unsigned char *>(&result), data, sizeof(int64));
    data += sizeof(int64);
    return result;
  }

  int64 fetch_long() {
    check_len(sizeof(int64));
    return fetch_long_unsafe();
  }

  double fetch_double_unsafe() {
    double result;
    std::memcpy(reinterpret_cast<unsigned char *>(&result), data, sizeof(double));
    data += sizeof(double);
    return result;
  }

  double fetch_double() {
    check_len(sizeof(double));
    return fetch_double_unsafe();
  }

  template <class T>
  T fetch_binary_unsafe() {
    T result;
    std::memcpy(reinterpret_cast<unsigned char *>(&result), data, sizeof(T));
    data += sizeof(T);
    return result;
  }

  template <class T>
  T fetch_binary() {
    static_assert(sizeof(T) <= sizeof(empty_data), "too big fetch_binary");
    static_assert(sizeof(T) % sizeof(int32) == 0, "wrong call to fetch_binary");
    check_len(sizeof(T));
    return fetch_binary_unsafe<T>();
  }

  template <class T>
  T fetch_string() {
    check_len(sizeof(int32));
    size_t result_len = *data;
    const char *result_begin;
    size_t result_aligned_len;
    if (result_len < 254) {
      result_begin = reinterpret_cast<const char *>(data + 1);
      result_aligned_len = (result_len >> 2) << 2;
    } else if (result_len == 254) {
      result_len = data[1] + (data[2] << 8) + (data[3] << 16);
      result_begin = reinterpret_cast<const char *>(data + 4);
      result_aligned_len = ((result_len + 3) >> 2) << 2;
    } else {
      set_error("Can't fetch string, 255 found");
      return T();
    }
    check_len(result_aligned_len);
    data += result_aligned_len + sizeof(int32);
    return T(result_begin, result_len);
  }

  template <class T>
  T fetch_string_raw(const size_t size) {
    CHECK(size % sizeof(int32) == 0);
    check_len(size);
    const char *result = reinterpret_cast<const char *>(data);
    data += size;
    return T(result, size);
  }

  void fetch_end() {
    if (left_len) {
      set_error("Too much data to fetch");
    }
  }

  size_t get_left_len() const {
    return left_len;
  }
};

class TlBufferParser : public TlParser {
 public:
  explicit TlBufferParser(const BufferSlice *buffer_slice) : TlParser(buffer_slice->as_slice()), parent_(buffer_slice) {
  }
  template <class T>
  T fetch_string() {
    auto result = TlParser::fetch_string<T>();
    for (auto &c : result) {
      if (c == '\0') {
        c = ' ';
      }
    }
    if (check_utf8(result)) {
      return result;
    }
    CHECK(!result.empty());
    LOG(WARNING) << "Wrong UTF-8 string [[" << result << "]] in " << format::as_hex_dump<4>(parent_->as_slice());

    // trying to remove last character
    size_t new_size = result.size() - 1;
    while (new_size != 0 && !is_utf8_character_first_code_unit(static_cast<unsigned char>(result[new_size]))) {
      new_size--;
    }
    result.resize(new_size);
    if (check_utf8(result)) {
      return result;
    }

    return T();
  }
  template <class T>
  T fetch_string_raw(const size_t size) {
    return TlParser::fetch_string_raw<T>(size);
  }

 private:
  const BufferSlice *parent_;

  BufferSlice as_buffer_slice(Slice slice) {
    if (is_aligned_pointer<4>(slice.data())) {
      return parent_->from_slice(slice);
    }
    return BufferSlice(slice);
  }
};

template <>
inline BufferSlice TlBufferParser::fetch_string<BufferSlice>() {
  return as_buffer_slice(TlParser::fetch_string<Slice>());
}

template <>
inline BufferSlice TlBufferParser::fetch_string_raw<BufferSlice>(const size_t size) {
  return as_buffer_slice(TlParser::fetch_string_raw<Slice>(size));
}

}  // namespace td

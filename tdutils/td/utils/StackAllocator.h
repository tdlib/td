//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"

#include <array>
#include <cstdlib>

namespace td {

class StackAllocator {
  // TODO: alloc memory with mmap and unload unused pages
  // memory still can be corrupted, but it is better than explicit free function
  class Ptr {
   public:
    Ptr(char *ptr, size_t size) : slice_(ptr, size) {
    }
    Ptr(const Ptr &other) = delete;
    Ptr &operator=(const Ptr &other) = delete;
    Ptr(Ptr &&other) noexcept : slice_(other.slice_) {
      other.slice_ = MutableSlice();
    }
    Ptr &operator=(Ptr &&other) = delete;
    ~Ptr() {
      if (!slice_.empty()) {
        free_ptr(slice_.data(), slice_.size());
      }
    }

    MutableSlice as_slice() const {
      return slice_;
    }

   private:
    MutableSlice slice_;
  };

  struct Impl {
    static const size_t MEM_SIZE = 1024 * 1024;
    std::array<char, MEM_SIZE> mem;

    size_t pos{0};
    Ptr alloc(size_t size) {
      if (size == 0) {
        size = 8;
      } else {
        if (size > MEM_SIZE) {
          std::abort();  // too much memory requested
        }
        size = (size + 7) & -8;
      }
      char *res = mem.data() + pos;
      pos += size;
      if (pos > MEM_SIZE) {
        std::abort();  // memory is over
      }
      return Ptr(res, size);
    }
    void free_ptr(char *ptr, size_t size) {
      if (size > pos || ptr != mem.data() + (pos - size)) {
        std::abort();  // shouldn't happen
      }
      pos -= size;
    }
  };

  static Impl &impl();

  static void free_ptr(char *ptr, size_t size) {
    impl().free_ptr(ptr, size);
  }

 public:
  static Ptr alloc(size_t size) {
    return impl().alloc(size);
  }
};

}  // namespace td

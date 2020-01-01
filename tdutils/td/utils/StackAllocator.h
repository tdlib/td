//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/MovableValue.h"
#include "td/utils/Slice.h"

#include <array>
#include <cstdlib>
#include <memory>

namespace td {

class StackAllocator {
  class Deleter {
   public:
    void operator()(char *ptr) {
      free_ptr(ptr);
    }
  };

  // TODO: alloc memory with mmap and unload unused pages
  // memory still can be corrupted, but it is better than explicit free function
  // TODO: use pointer that can't be even copied
  using PtrImpl = std::unique_ptr<char, Deleter>;
  class Ptr {
   public:
    Ptr(char *ptr, size_t size) : ptr_(ptr), size_(size) {
    }

    MutableSlice as_slice() const {
      return MutableSlice(ptr_.get(), size_.get());
    }

   private:
    PtrImpl ptr_;
    MovableValue<size_t> size_;
  };

  static void free_ptr(char *ptr) {
    impl().free_ptr(ptr);
  }

  struct Impl {
    static const size_t MEM_SIZE = 1024 * 1024;
    std::array<char, MEM_SIZE> mem;

    size_t pos{0};
    char *alloc(size_t size) {
      if (size == 0) {
        size = 1;
      }
      char *res = mem.data() + pos;
      size = (size + 7) & -8;
      pos += size;
      if (pos > MEM_SIZE) {
        std::abort();  // memory is over
      }
      return res;
    }
    void free_ptr(char *ptr) {
      size_t new_pos = ptr - mem.data();
      if (new_pos >= pos) {
        std::abort();  // shouldn't happen
      }
      pos = new_pos;
    }
  };

  static Impl &impl();

 public:
  static Ptr alloc(size_t size) {
    return Ptr(impl().alloc(size), size);
  }
};

}  // namespace td

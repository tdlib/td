//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/buffer.h"

#include "td/utils/port/thread_local.h"

#include <cstddef>
#include <new>

// fixes https://bugs.llvm.org/show_bug.cgi?id=33723 for clang >= 3.6 + c++11 + libc++
#if TD_CLANG && _LIBCPP_VERSION
#define TD_OFFSETOF __builtin_offsetof
#else
#define TD_OFFSETOF offsetof
#endif

namespace td {

TD_THREAD_LOCAL BufferAllocator::BufferRawTls *BufferAllocator::buffer_raw_tls;  // static zero-initialized

std::atomic<size_t> BufferAllocator::buffer_mem;

size_t BufferAllocator::get_buffer_mem() {
  return buffer_mem;
}

BufferAllocator::WriterPtr BufferAllocator::create_writer(size_t size) {
  if (size < 512) {
    size = 512;
  }
  return create_writer_exact(size);
}

BufferAllocator::WriterPtr BufferAllocator::create_writer_exact(size_t size) {
  return WriterPtr(create_buffer_raw(size));
}

BufferAllocator::WriterPtr BufferAllocator::create_writer(size_t size, size_t prepend, size_t append) {
  auto ptr = create_writer(size + prepend + append);
  ptr->begin_ += prepend;
  ptr->end_ += prepend + size;
  return ptr;
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader(size_t size) {
  if (size < 512) {
    return create_reader_fast(size);
  }
  auto ptr = create_writer_exact(size);
  ptr->end_ += (size + 7) & -8;
  return create_reader(ptr);
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader_fast(size_t size) {
  size = (size + 7) & -8;

  init_thread_local<BufferRawTls>(buffer_raw_tls);

  auto buffer_raw = buffer_raw_tls->buffer_raw.get();
  if (buffer_raw == nullptr || buffer_raw->data_size_ - buffer_raw->end_.load(std::memory_order_relaxed) < size) {
    buffer_raw = create_buffer_raw(4096 * 4);
    buffer_raw_tls->buffer_raw = std::unique_ptr<BufferRaw, BufferAllocator::BufferRawDeleter>(buffer_raw);
  }
  buffer_raw->end_.fetch_add(size, std::memory_order_relaxed);
  buffer_raw->ref_cnt_.fetch_add(1, std::memory_order_acq_rel);
  return ReaderPtr(buffer_raw);
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader(const WriterPtr &raw) {
  raw->was_reader_ = true;
  raw->ref_cnt_.fetch_add(1, std::memory_order_acq_rel);
  return ReaderPtr(raw.get());
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader(const ReaderPtr &raw) {
  raw->ref_cnt_.fetch_add(1, std::memory_order_acq_rel);
  return ReaderPtr(raw.get());
}

void BufferAllocator::dec_ref_cnt(BufferRaw *ptr) {
  int left = ptr->ref_cnt_.fetch_sub(1, std::memory_order_acq_rel);
  if (left == 1) {
    auto buf_size = max(sizeof(BufferRaw), TD_OFFSETOF(BufferRaw, data_) + ptr->data_size_);
    buffer_mem -= buf_size;
    ptr->~BufferRaw();
    delete[] ptr;
  }
}

BufferRaw *BufferAllocator::create_buffer_raw(size_t size) {
  size = (size + 7) & -8;

  auto buf_size = TD_OFFSETOF(BufferRaw, data_) + size;
  if (buf_size < sizeof(BufferRaw)) {
    buf_size = sizeof(BufferRaw);
  }
  buffer_mem += buf_size;
  auto *buffer_raw = reinterpret_cast<BufferRaw *>(new char[buf_size]);
  new (buffer_raw) BufferRaw();
  buffer_raw->data_size_ = size;
  buffer_raw->begin_ = 0;
  buffer_raw->end_ = 0;

  buffer_raw->ref_cnt_.store(1, std::memory_order_relaxed);
  buffer_raw->has_writer_.store(true, std::memory_order_relaxed);
  buffer_raw->was_reader_ = false;
  return buffer_raw;
}
}  // namespace td

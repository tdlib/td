//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/utils/Status.h"
#include "td/utils/List.h"
#include "td/utils/Observer.h"
#include "td/utils/MovableValue.h"
#include "td/utils/SpinLock.h"
#include "td/utils/format.h"

#include "td/utils/port/detail/NativeFd.h"

#include <atomic>
namespace td {
class ObserverBase;

class PollFlags {
 public:
  using Raw = int32;
  bool can_read() const {
    return has_flags(Read());
  }
  bool can_write() const {
    return has_flags(Write());
  }
  bool can_close() const {
    return has_flags(Close());
  }
  bool has_pending_error() const {
    return has_flags(Error());
  }
  void remove_flags(PollFlags flags) {
    remove_flags(flags.raw());
  }
  bool add_flags(PollFlags flags) {
    auto old_flags = flags_;
    add_flags(flags.raw());
    return old_flags != flags_;
  }
  bool has_flags(PollFlags flags) const {
    return has_flags(flags.raw());
  }

  bool empty() const {
    return flags_ == 0;
  }
  Raw raw() const {
    return flags_;
  }
  static PollFlags from_raw(Raw raw) {
    return PollFlags(raw);
  }
  PollFlags() = default;

  bool operator==(const PollFlags &other) const {
    return flags_ == other.flags_;
  }
  bool operator!=(const PollFlags &other) const {
    return !(*this == other);
  }
  PollFlags operator|(const PollFlags other) const {
    return from_raw(raw() | other.raw());
  }

  static PollFlags Write() {
    return PollFlags(Flag::Write);
  }
  static PollFlags Error() {
    return PollFlags(Flag::Error);
  }
  static PollFlags Close() {
    return PollFlags(Flag::Close);
  }
  static PollFlags Read() {
    return PollFlags(Flag::Read);
  }
  static PollFlags ReadWrite() {
    return Read() | Write();
  }

 private:
  enum class Flag : Raw { Write = 0x001, Read = 0x002, Close = 0x004, Error = 0x008, None = 0 };
  Raw flags_{static_cast<Raw>(Flag::None)};

  explicit PollFlags(Raw raw) : flags_(raw) {
  }
  explicit PollFlags(Flag flag) : PollFlags(static_cast<Raw>(flag)) {
  }

  PollFlags &add_flags(Raw flags) {
    flags_ |= flags;
    return *this;
  }
  PollFlags &remove_flags(Raw flags) {
    flags_ &= ~flags;
    return *this;
  }
  bool has_flags(Raw flags) const {
    return (flags_ & flags) == flags;
  }
};

inline StringBuilder &operator<<(StringBuilder &sb, PollFlags flags) {
  sb << "[";
  if (flags.can_read()) {
    sb << "R";
  }
  if (flags.can_write()) {
    sb << "W";
  }
  if (flags.can_close()) {
    sb << "C";
  }
  if (flags.has_pending_error()) {
    sb << "E";
  }
  return sb << "]";
}

class PollFlagsSet {
 public:
  // write flags from any thread
  // this is the only function that should be called from other threads
  bool write_flags(PollFlags flags);

  bool write_flags_local(PollFlags flags);
  bool flush() const;

  PollFlags read_flags() const;
  PollFlags read_flags_local() const;
  void clear_flags(PollFlags flags);
  void clear();

 private:
  mutable std::atomic<PollFlags::Raw> to_write_{0};
  mutable PollFlags flags_;
};

class PollableFdInfo;
class PollableFdInfoUnlock {
 public:
  void operator()(PollableFdInfo *ptr);
};

class PollableFd;
class PollableFdRef {
 public:
  explicit PollableFdRef(ListNode *list_node) : list_node_(list_node) {
  }
  PollableFd lock();

 private:
  ListNode *list_node_;
};

class PollableFd {
 public:
  // Interface for kqueue, epoll and e.t.c.
  const NativeFd &native_fd() const;

  ListNode *release_as_list_node();
  PollableFdRef ref();
  static PollableFd from_list_node(ListNode *node);
  void add_flags(PollFlags flags);
  PollFlags get_flags_unsafe() const;

 private:
  std::unique_ptr<PollableFdInfo, PollableFdInfoUnlock> fd_info_;
  friend class PollableFdInfo;

  explicit PollableFd(std::unique_ptr<PollableFdInfo, PollableFdInfoUnlock> fd_info) : fd_info_(std::move(fd_info)) {
  }
};

inline PollableFd PollableFdRef::lock() {
  return PollableFd::from_list_node(list_node_);
}

class PollableFdInfo : private ListNode {
 public:
  PollableFdInfo() = default;
  PollableFd extract_pollable_fd(ObserverBase *observer) {
    VLOG(fd) << native_fd() << " extract pollable fd " << tag("observer", observer);
    CHECK(!empty());
    bool was_locked = lock_.test_and_set(std::memory_order_acquire);
    CHECK(!was_locked);
    set_observer(observer);
    return PollableFd{std::unique_ptr<PollableFdInfo, PollableFdInfoUnlock>{this}};
  }
  PollableFdRef get_pollable_fd_ref() {
    CHECK(!empty());
    bool was_locked = lock_.test_and_set(std::memory_order_acquire);
    CHECK(was_locked);
    return PollableFdRef{as_list_node()};
  }

  void add_flags(PollFlags flags) {
    flags_.write_flags_local(flags);
  }

  void clear_flags(PollFlags flags) {
    flags_.clear_flags(flags);
  }
  PollFlags get_flags() const {
    return flags_.read_flags();
  }
  PollFlags get_flags_local() const {
    return flags_.read_flags_local();
  }

  bool empty() const {
    return !fd_;
  }

  void set_native_fd(NativeFd new_native_fd) {
    if (fd_) {
      CHECK(!new_native_fd);
      bool was_locked = lock_.test_and_set(std::memory_order_acquire);
      CHECK(!was_locked);
      lock_.clear(std::memory_order_release);
    }

    fd_ = std::move(new_native_fd);
  }
  explicit PollableFdInfo(NativeFd native_fd) {
    set_native_fd(std::move(native_fd));
  }
  const NativeFd &native_fd() const {
    //CHECK(!empty());
    return fd_;
  }
  NativeFd move_as_native_fd() {
    return std::move(fd_);
  }

  ~PollableFdInfo() {
    VLOG(fd) << native_fd() << " destroy PollableFdInfo";
    bool was_locked = lock_.test_and_set(std::memory_order_acquire);
    CHECK(!was_locked);
  }

  void add_flags_from_poll(PollFlags flags) {
    VLOG(fd) << native_fd() << " add flags from poll " << flags;
    if (flags_.write_flags(flags)) {
      notify_observer();
    }
  }

 private:
  NativeFd fd_{};
  std::atomic_flag lock_{false};
  PollFlagsSet flags_;
#if TD_PORT_WINDOWS
  SpinLock observer_lock_;
#endif
  ObserverBase *observer_{nullptr};

  friend class PollableFd;
  friend class PollableFdInfoUnlock;

  void set_observer(ObserverBase *observer) {
#if TD_PORT_WINDOWS
    auto lock = observer_lock_.lock();
#endif
    CHECK(!observer_);
    observer_ = observer;
  }
  void clear_observer() {
#if TD_PORT_WINDOWS
    auto lock = observer_lock_.lock();
#endif
    observer_ = nullptr;
  }
  void notify_observer() {
#if TD_PORT_WINDOWS
    auto lock = observer_lock_.lock();
#endif
    VLOG(fd) << native_fd() << " notify " << tag("observer", observer_);
    if (observer_) {
      observer_->notify();
    }
  }

  void unlock() {
    lock_.clear(std::memory_order_release);
    as_list_node()->remove();
  }

  ListNode *as_list_node() {
    return static_cast<ListNode *>(this);
  }
  static PollableFdInfo *from_list_node(ListNode *list_node) {
    return static_cast<PollableFdInfo *>(list_node);
  }
};
inline void PollableFdInfoUnlock::operator()(PollableFdInfo *ptr) {
  ptr->unlock();
}

inline ListNode *PollableFd::release_as_list_node() {
  return fd_info_.release()->as_list_node();
}
inline PollableFdRef PollableFd::ref() {
  return PollableFdRef{fd_info_->as_list_node()};
}
inline PollableFd PollableFd::from_list_node(ListNode *node) {
  return PollableFd(std::unique_ptr<PollableFdInfo, PollableFdInfoUnlock>(PollableFdInfo::from_list_node(node)));
}

inline void PollableFd::add_flags(PollFlags flags) {
  fd_info_->add_flags_from_poll(flags);
}
inline PollFlags PollableFd::get_flags_unsafe() const {
  return fd_info_->get_flags_local();
}
inline const NativeFd &PollableFd::native_fd() const {
  return fd_info_->native_fd();
}
#if TD_PORT_POSIX
template <class F>
auto skip_eintr(F &&f) {
  decltype(f()) res;
  static_assert(std::is_integral<decltype(res)>::value, "integral type expected");
  do {
    errno = 0;  // just in case
    res = f();
  } while (res < 0 && errno == EINTR);
  return res;
}
template <class F>
auto skip_eintr_cstr(F &&f) {
  char *res;
  do {
    errno = 0;  // just in case
    res = f();
  } while (res == nullptr && errno == EINTR);
  return res;
}
#endif

template <class FdT>
bool can_read(const FdT &fd) {
  return fd.get_poll_info().get_flags().can_read() || fd.get_poll_info().get_flags().has_pending_error();
}

template <class FdT>
bool can_write(const FdT &fd) {
  return fd.get_poll_info().get_flags().can_write();
}

template <class FdT>
bool can_close(const FdT &fd) {
  return fd.get_poll_info().get_flags().can_close();
}

}  // namespace td

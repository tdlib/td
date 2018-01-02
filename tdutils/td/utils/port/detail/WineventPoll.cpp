//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/WineventPoll.h"

char disable_linker_warning_about_empty_file_wineventpoll_cpp TD_UNUSED;

#ifdef TD_POLL_WINEVENT

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/PollBase.h"
#include "td/utils/port/sleep.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {
namespace detail {

void WineventPoll::init() {
  clear();
}

void WineventPoll::clear() {
  fds_.clear();
}

void WineventPoll::subscribe(const Fd &fd, Fd::Flags flags) {
  for (auto &it : fds_) {
    if (it.fd_ref.get_key() == fd.get_key()) {
      it.flags = flags;
      return;
    }
  }
  fds_.push_back({fd.clone(), flags});
}

void WineventPoll::unsubscribe(const Fd &fd) {
  for (auto it = fds_.begin(); it != fds_.end(); ++it) {
    if (it->fd_ref.get_key() == fd.get_key()) {
      std::swap(*it, fds_.back());
      fds_.pop_back();
      return;
    }
  }
}

void WineventPoll::unsubscribe_before_close(const Fd &fd) {
  unsubscribe(fd);
}

void WineventPoll::run(int timeout_ms) {
  vector<std::pair<size_t, Fd::Flag>> events_desc;
  vector<HANDLE> events;
  for (size_t i = 0; i < fds_.size(); i++) {
    auto &fd_info = fds_[i];
    if (fd_info.flags & Fd::Flag::Write) {
      events_desc.emplace_back(i, Fd::Flag::Write);
      events.push_back(fd_info.fd_ref.get_write_event());
    }
    if (fd_info.flags & Fd::Flag::Read) {
      events_desc.emplace_back(i, Fd::Flag::Read);
      events.push_back(fd_info.fd_ref.get_read_event());
    }
  }
  if (events.empty()) {
    usleep_for(timeout_ms * 1000);
    return;
  }

  auto status = WaitForMultipleObjects(narrow_cast<DWORD>(events.size()), events.data(), false, timeout_ms);
  if (status == WAIT_FAILED) {
    auto error = OS_ERROR("WaitForMultipleObjects failed");
    LOG(FATAL) << events.size() << " " << timeout_ms << " " << error;
  }
  for (size_t i = 0; i < events.size(); i++) {
    if (WaitForSingleObject(events[i], 0) == WAIT_OBJECT_0) {
      auto &fd = fds_[events_desc[i].first].fd_ref;
      if (events_desc[i].second == Fd::Flag::Read) {
        fd.on_read_event();
      } else {
        fd.on_write_event();
      }
    }
  }
}

}  // namespace detail
}  // namespace td

#endif

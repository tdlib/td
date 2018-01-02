//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/binlog/Binlog.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"

#include <cstdio>
#include <map>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: binlog_dump <binlog_file_name>\n");
    return 1;
  }

  struct Info {
    std::size_t full_size = 0;
    std::size_t compressed_size = 0;
  };
  std::map<td::uint64, Info> info;

  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  td::Binlog binlog;
  binlog
      .init(argv[1],
            [&](auto &event) {
              info[0].compressed_size += event.raw_event_.size();
              info[event.type_].compressed_size += event.raw_event_.size();
            },
            td::DbKey::empty(), td::DbKey::empty(), -1,
            [&](auto &event) mutable {
              info[0].full_size += event.raw_event_.size();
              info[event.type_].full_size += event.raw_event_.size();
              LOG(PLAIN) << "LogEvent[" << td::tag("id", td::format::as_hex(event.id_)) << td::tag("type", event.type_)
                         << td::tag("flags", event.flags_) << td::tag("data", td::format::escaped(event.data_))
                         << "]\n";
            })
      .ensure();

  for (auto &it : info) {
    LOG(ERROR) << td::tag("handler", td::format::as_hex(it.first))
               << td::tag("full_size", td::format::as_size(it.second.full_size))
               << td::tag("compressed_size", td::format::as_size(it.second.compressed_size));
  }

  return 0;
}

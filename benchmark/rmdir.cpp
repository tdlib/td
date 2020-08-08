//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/logging.h"
#include "td/utils/port/path.h"
#include "td/utils/Slice.h"

int main(int argc, char *argv[]) {
  if (argc < 1) {
    return 1;
  }
  td::CSlice dir(argv[1]);
  int cnt = 0;
  auto status = td::walk_path(dir, [&](td::CSlice path, auto type) {
    if (type != td::WalkPath::Type::EnterDir) {
      cnt++;
      LOG(INFO) << path << " " << (type == td::WalkPath::Type::ExitDir);
    }
    //if (is_dir) {
    // td::rmdir(path);
    //} else {
    // td::unlink(path);
    //}
  });
  LOG(INFO) << status << ": " << cnt;
}

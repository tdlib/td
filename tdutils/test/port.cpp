//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

using namespace td;

TEST(Port, files) {
  CSlice main_dir = "test_dir";
  rmrf(main_dir).ignore();
  ASSERT_TRUE(FileFd::open(main_dir, FileFd::Write).is_error());
  ASSERT_TRUE(walk_path(main_dir, [](CSlice name, bool is_directory) { UNREACHABLE(); }).is_error());
  mkdir(main_dir).ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "A").ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "B").ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "B" << TD_DIR_SLASH << "D").ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "C").ensure();
  ASSERT_TRUE(FileFd::open(main_dir, FileFd::Write).is_error());
  std::string fd_path = PSTRING() << main_dir << TD_DIR_SLASH << "t.txt";
  std::string fd2_path = PSTRING() << main_dir << TD_DIR_SLASH << "C" << TD_DIR_SLASH << "t2.txt";

  auto fd = FileFd::open(fd_path, FileFd::Write | FileFd::CreateNew).move_as_ok();
  auto fd2 = FileFd::open(fd2_path, FileFd::Write | FileFd::CreateNew).move_as_ok();
  fd2.close();

  int cnt = 0;
  const int ITER_COUNT = 1000;
  for (int i = 0; i < ITER_COUNT; i++) {
    walk_path(main_dir,
              [&](CSlice name, bool is_directory) {
                if (!is_directory) {
                  ASSERT_TRUE(name == fd_path || name == fd2_path);
                }
                cnt++;
              })
        .ensure();
  }
  ASSERT_EQ(7 * ITER_COUNT, cnt);

  ASSERT_EQ(0u, fd.get_size());
  ASSERT_EQ(12u, fd.write("Hello world!").move_as_ok());
  ASSERT_EQ(4u, fd.pwrite("abcd", 1).move_as_ok());
  char buf[100];
  MutableSlice buf_slice(buf, sizeof(buf));
  ASSERT_TRUE(fd.pread(buf_slice.substr(0, 4), 2).is_error());
  fd.seek(11).ensure();
  ASSERT_EQ(2u, fd.write("?!").move_as_ok());

  ASSERT_TRUE(FileFd::open(main_dir, FileFd::Read | FileFd::CreateNew).is_error());
  fd = FileFd::open(fd_path, FileFd::Read | FileFd::Create).move_as_ok();
  ASSERT_EQ(13u, fd.get_size());
  ASSERT_EQ(4u, fd.pread(buf_slice.substr(0, 4), 1).move_as_ok());
  ASSERT_STREQ("abcd", buf_slice.substr(0, 4));

  fd.seek(0).ensure();
  ASSERT_EQ(13u, fd.read(buf_slice.substr(0, 13)).move_as_ok());
  ASSERT_STREQ("Habcd world?!", buf_slice.substr(0, 13));
}

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileLoaderUtils.h"

#include "td/telegram/files/FileLocation.h"
#include "td/telegram/Global.h"

#include "td/utils/common.h"
#include "td/utils/filesystem.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/Random.h"
#include "td/utils/StringBuilder.h"

namespace td {

namespace {
Result<std::pair<FileFd, string>> try_create_new_file(Result<CSlice> result_name) {
  TRY_RESULT(name, std::move(result_name));
  TRY_RESULT(fd, FileFd::open(name, FileFd::Read | FileFd::Write | FileFd::CreateNew, 0640));
  return std::make_pair(std::move(fd), name.str());
}

struct RandSuff {
  int len;
};
StringBuilder &operator<<(StringBuilder &sb, const RandSuff &) {
  for (int i = 0; i < 6; i++) {
    sb << format::hex_digit(Random::fast(0, 15));
  }
  return sb;
}
struct Ext {
  Slice ext;
};
StringBuilder &operator<<(StringBuilder &sb, const Ext &ext) {
  if (ext.ext.empty()) {
    return sb;
  }
  return sb << "." << ext.ext;
}
}  // namespace

Result<std::pair<FileFd, string>> open_temp_file(const FileType &file_type) {
  auto pmc = G()->td_db()->get_binlog_pmc();
  // TODO: CAS?
  int32 file_id = to_integer<int32>(pmc->get("tmp_file_id"));
  pmc->set("tmp_file_id", to_string(file_id + 1));

  auto temp_dir = get_files_temp_dir(file_type);
  auto res = try_create_new_file(PSLICE_SAFE() << temp_dir << file_id);
  if (res.is_error()) {
    res = try_create_new_file(PSLICE_SAFE() << temp_dir << file_id << "_" << RandSuff{6});
  }
  return res;
}

Result<string> create_from_temp(CSlice temp_path, CSlice dir, CSlice name) {
  decltype(try_create_new_file("path")) res;
  auto cleaned_name = clean_filename(name);

  PathView path_view(cleaned_name);
  auto stem = path_view.file_stem();
  auto ext = path_view.extension();
  if (!stem.empty() && !G()->parameters().ignore_file_names) {
    res = try_create_new_file(PSLICE_SAFE() << dir << stem << Ext{ext});
    for (int i = 0; res.is_error() && i < 10; i++) {
      res = try_create_new_file(PSLICE_SAFE() << dir << stem << "_(" << i << ")" << Ext{ext});
    }
    for (int i = 2; res.is_error() && i < 12; i++) {
      res = try_create_new_file(PSLICE_SAFE() << dir << stem << "_(" << RandSuff{i} << ")" << Ext{ext});
    }
  } else {
    auto pmc = G()->td_db()->get_binlog_pmc();
    int32 file_id = to_integer<int32>(pmc->get("perm_file_id"));
    pmc->set("perm_file_id", to_string(file_id + 1));
    res = try_create_new_file(PSLICE_SAFE() << dir << "file_" << file_id << Ext{ext});
    if (res.is_error()) {
      res = try_create_new_file(PSLICE_SAFE() << dir << "file_" << file_id << "_" << RandSuff{6} << Ext{ext});
    }
  }

  TRY_RESULT(tmp, std::move(res));
  tmp.first.close();
  auto perm_path = std::move(tmp.second);
  TRY_STATUS(rename(temp_path, perm_path));
  return perm_path;
}

const char *file_type_name[file_type_size] = {"thumbnails", "profile_photos", "photos",     "voice",
                                              "videos",     "documents",      "secret",     "temp",
                                              "stickers",   "music",          "animations", "secret_thumbnails",
                                              "wallpapers", "video_notes"};

string get_file_base_dir(const FileDirType &file_dir_type) {
  switch (file_dir_type) {
    case FileDirType::Secure:
      return G()->get_dir().str();
    case FileDirType::Common:
      return G()->get_files_dir().str();
    default:
      UNREACHABLE();
      return "";
  }
}

string get_files_base_dir(const FileType &file_type) {
  return get_file_base_dir(get_file_dir_type(file_type));
}
string get_files_temp_dir(const FileType &file_type) {
  return get_files_base_dir(file_type) + "temp" + TD_DIR_SLASH;
}
string get_files_dir(const FileType &file_type) {
  return get_files_base_dir(file_type) + file_type_name[static_cast<int32>(file_type)] + TD_DIR_SLASH;
}

}  // namespace td

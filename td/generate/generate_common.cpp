//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "tl_writer_cpp.h"
#include "tl_writer_h.h"
#include "tl_writer_hpp.h"
#include "tl_writer_jni_cpp.h"
#include "tl_writer_jni_h.h"

#include "td/tl/tl_config.h"
#include "td/tl/tl_generate.h"

#include <algorithm>
#include <string>
#include <vector>

static void generate_cpp(const std::string &directory, const std::string &tl_name, const std::string &string_type,
                         const std::string &bytes_type, const std::vector<std::string> &ext_cpp_includes,
                         const std::vector<std::string> &ext_h_includes) {
  std::string path = directory + "/" + tl_name;
  td::tl::tl_config config = td::tl::read_tl_config_from_file("scheme/" + tl_name + ".tlo");
  td::tl::write_tl_to_file(config, path + ".cpp",
                           td::TD_TL_writer_cpp(tl_name, string_type, bytes_type, ext_cpp_includes));
  td::tl::write_tl_to_file(config, path + ".h", td::TD_TL_writer_h(tl_name, string_type, bytes_type, ext_h_includes));
  td::tl::write_tl_to_file(config, path + ".hpp", td::TD_TL_writer_hpp(tl_name, string_type, bytes_type));
}

int main() {
  generate_cpp("auto/td/telegram", "telegram_api", "std::string", "BufferSlice",
               {"\"td/tl/tl_object_parse.h\"", "\"td/tl/tl_object_store.h\""}, {"\"td/utils/buffer.h\""});

  generate_cpp("auto/td/telegram", "secret_api", "std::string", "BufferSlice",
               {"\"td/tl/tl_object_parse.h\"", "\"td/tl/tl_object_store.h\""}, {"\"td/utils/buffer.h\""});

  generate_cpp("auto/td/mtproto", "mtproto_api", "Slice", "Slice",
               {"\"td/tl/tl_object_parse.h\"", "\"td/tl/tl_object_store.h\""}, {"\"td/utils/Slice.h\""});

#ifdef TD_API_JAVA_PACKAGE
  td::tl::tl_config config = td::tl::read_tl_config_from_file("scheme/td_api.tlo");
  std::string path = "auto/td/telegram/td_api";
  std::string package = TD_API_JAVA_PACKAGE;
  std::replace(package.begin(), package.end(), '/', '.');
  td::tl::write_tl_to_file(
      config, path + ".cpp",
      td::TD_TL_writer_jni_cpp("td_api", "std::string", "std::string", {"\"td/tl/tl_jni_object.h\""}, package));
  td::tl::write_tl_to_file(config, path + ".h",
                           td::TD_TL_writer_jni_h("td_api", "std::string", "std::string", {"<string>"}));
  td::tl::write_tl_to_file(config, path + ".hpp", td::TD_TL_writer_hpp("td_api", "std::string", "std::string"));
#else
  generate_cpp("auto/td/telegram", "td_api", "std::string", "std::string", {}, {"<string>"});
#endif
}

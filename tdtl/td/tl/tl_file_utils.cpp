//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/tl/tl_file_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_TL_FILE_UTILS_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_TL_FILE_UTILS_MSAN_ACTIVE 1
#endif
#ifndef TD_TL_FILE_UTILS_MSAN_ACTIVE
#define TD_TL_FILE_UTILS_MSAN_ACTIVE 0
#endif

namespace td {
namespace tl {

namespace {

template <class T>
void unpoison_object_if_msan(const T &value) {
#if TD_TL_FILE_UTILS_MSAN_ACTIVE
  __msan_unpoison(const_cast<T *>(&value), sizeof(value));
#else
  (void)value;
#endif
}

void unpoison_if_msan(const std::string &value) {
#if TD_TL_FILE_UTILS_MSAN_ACTIVE
  unpoison_object_if_msan(value);
  if (!value.empty()) {
    __msan_unpoison(const_cast<char *>(value.data()), value.size());
  }
  __msan_unpoison(const_cast<char *>(value.data() + value.size()), 1);
#else
  (void)value;
#endif
}

}  // namespace

std::string get_file_contents(const std::string &file_name) {
  unpoison_if_msan(file_name);
  std::string file_name_buffer(file_name.size() + 1, '\0');
  if (!file_name.empty()) {
    std::memcpy(file_name_buffer.data(), file_name.data(), file_name.size());
  }
  unpoison_if_msan(file_name_buffer);
  const char *file_name_cstr = file_name_buffer.data();

  FILE *f = std::fopen(file_name_cstr, "rb");
  if (f == NULL) {
    return std::string();
  }

  int fseek_res = std::fseek(f, 0, SEEK_END);
  if (fseek_res != 0) {
    std::fprintf(stderr, "Can't seek to the end of the file \"%s\"", file_name_cstr);
    std::abort();
  }
  long size_long = std::ftell(f);
  if (size_long < 0 || size_long >= (1 << 25)) {
    std::fprintf(stderr, "Wrong file \"%s\" has wrong size = %ld", file_name_cstr, size_long);
    std::abort();
  }
  std::size_t size = static_cast<std::size_t>(size_long);

  std::string result(size, ' ');
  if (size != 0) {
    std::rewind(f);
    std::size_t fread_res = std::fread(&result[0], size, 1, f);
    if (fread_res != 1) {
      std::fprintf(stderr, "Can't read file \"%s\"", file_name_cstr);
      std::abort();
    }
    unpoison_if_msan(result);
  }
  std::fclose(f);

  unpoison_if_msan(result);
  return result;
}

bool put_file_contents(const std::string &file_name, const std::string &contents, bool compare_documentation) {
  unpoison_if_msan(file_name);
  unpoison_if_msan(contents);
  std::string file_name_buffer(file_name.size() + 1, '\0');
  if (!file_name.empty()) {
    std::memcpy(file_name_buffer.data(), file_name.data(), file_name.size());
  }
  unpoison_if_msan(file_name_buffer);
  const char *file_name_cstr = file_name_buffer.data();

  std::string old_file_contents = get_file_contents(file_name);
  if (!compare_documentation) {
    old_file_contents = remove_documentation(old_file_contents);
  }
  unpoison_if_msan(old_file_contents);

  if (old_file_contents == contents) {
    return true;
  }

  std::fprintf(stderr, "Write file %s\n", file_name_cstr);

  FILE *f = std::fopen(file_name_cstr, "wb");
  if (f == NULL) {
    std::fprintf(stderr, "Can't open file \"%s\"\n", file_name_cstr);
    return false;
  }

  std::size_t fwrite_res = std::fwrite(contents.c_str(), contents.size(), 1, f);
  if (fwrite_res != 1) {
    std::fclose(f);
    return false;
  }
  if (std::fclose(f) != 0) {
    return false;
  }
  return true;
}

std::string remove_documentation(const std::string &str) {
  unpoison_if_msan(str);
  std::size_t line_begin = 0;
  std::string result(str.size(), '\0');
  std::size_t result_size = 0;
  bool inside_documentation = false;
  while (line_begin < str.size()) {
    std::size_t line_end = str.find('\n', line_begin);
    if (line_end == std::string::npos) {
      line_end = str.size() - 1;
    }
    std::string line = str.substr(line_begin, line_end - line_begin + 1);
    unpoison_if_msan(line);
    line_begin = line_end + 1;

    std::size_t pos = line.find_first_not_of(' ');
    bool has_doc_comment_prefix = pos != std::string::npos && pos + 2 < line.size() &&
                                  ((line[pos] == '/' && line[pos + 1] == '/' && line[pos + 2] == '/') ||
                                   (line[pos] == '/' && line[pos + 1] == '*' && line[pos + 2] == '*'));
    bool continues_doc_comment = pos != std::string::npos && inside_documentation && line[pos] == '*';
    bool ends_doc_comment =
        pos != std::string::npos && pos + 1 < line.size() && line[pos] == '*' && line[pos + 1] == '/';
    if (has_doc_comment_prefix || continues_doc_comment) {
      bool is_line_comment = pos != std::string::npos && pos + 2 < line.size() && line[pos] == '/' &&
                             line[pos + 1] == '/' && line[pos + 2] == '/';
      inside_documentation = !is_line_comment && !ends_doc_comment;
      continue;
    }

    inside_documentation = false;
    for (char c : line) {
      result[result_size++] = c;
    }
    unpoison_if_msan(result);
  }
  result.resize(result_size);
  unpoison_if_msan(result);
  return result;
}

}  // namespace tl
}  // namespace td

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/wstring_convert.h"

char disable_linker_warning_about_empty_file_wstring_convert_cpp TD_UNUSED;

#if TD_PORT_WINDOWS

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "td/utils/port/wstring_convert.h"

#include <codecvt>
#include <locale>
#include <utility>

namespace td {

namespace detail {
template <class Facet>
class UsableFacet : public Facet {
 public:
  template <class... Args>
  explicit UsableFacet(Args &&... args) : Facet(std::forward<Args>(args)...) {
  }
  ~UsableFacet() = default;
};
}  // namespace detail

Result<std::wstring> to_wstring(Slice slice) {
  // TODO(perf): optimize
  std::wstring_convert<detail::UsableFacet<std::codecvt_utf8_utf16<wchar_t>>> converter;
  auto res = converter.from_bytes(slice.begin(), slice.end());
  if (converter.converted() != slice.size()) {
    return Status::Error("Wrong encoding");
  }
  return res;
}

Result<string> from_wstring(const wchar_t *begin, size_t size) {
  std::wstring_convert<detail::UsableFacet<std::codecvt_utf8_utf16<wchar_t>>> converter;
  auto res = converter.to_bytes(begin, begin + size);
  if (converter.converted() != size) {
    return Status::Error("Wrong encoding");
  }
  return res;
}

Result<string> from_wstring(const std::wstring &str) {
  return from_wstring(str.data(), str.size());
}

Result<string> from_wstring(const wchar_t *begin) {
  return from_wstring(begin, wcslen(begin));
}

}  // namespace td

#endif

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Span.h"

namespace td {

struct ExportHtmlAsset {
  // the path relative to the export directory with '/' as the separator
  const char *path;
  const unsigned char *data;
  size_t size;
};

// the style sheet, the script and the images of exported HTML pages, embedded from td/generate/export_html
Span<ExportHtmlAsset> get_export_html_assets();

}  // namespace td

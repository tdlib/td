//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DocumentsManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void DocumentsManager::store_document(FileId file_id, T &storer) const {
  LOG(DEBUG) << "Store document " << file_id;
  auto it = documents_.find(file_id);
  CHECK(it != documents_.end());
  const Document *document = it->second.get();
  store(document->file_name, storer);
  store(document->mime_type, storer);
  store(document->thumbnail, storer);
  store(file_id, storer);
}

template <class T>
FileId DocumentsManager::parse_document(T &parser) {
  auto document = make_unique<Document>();
  parse(document->file_name, parser);
  parse(document->mime_type, parser);
  parse(document->thumbnail, parser);
  parse(document->file_id, parser);
  LOG(DEBUG) << "Parsed document " << document->file_id;
  return on_get_document(std::move(document), true);
}

}  // namespace td

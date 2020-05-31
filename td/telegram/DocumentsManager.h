//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/SecretInputMedia.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"

#include <unordered_map>
#include <utility>

namespace td {

class MultiPromiseActor;
class Td;

class DocumentsManager {
 public:
  explicit DocumentsManager(Td *td);

  class RemoteDocument {
   public:
    tl_object_ptr<telegram_api::document> document;
    // or
    tl_object_ptr<telegram_api::encryptedFile> secret_file;
    tl_object_ptr<secret_api::decryptedMessageMediaDocument> secret_document;
    // or
    tl_object_ptr<telegram_api::WebDocument> web_document;
    PhotoSize thumbnail;

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;

    RemoteDocument(tl_object_ptr<telegram_api::document> &&server_document)
        : document(std::move(server_document))
        , secret_file(nullptr)
        , secret_document(nullptr)
        , web_document(nullptr)
        , thumbnail()
        , attributes(std::move(document->attributes_)) {
    }

    RemoteDocument(tl_object_ptr<telegram_api::WebDocument> &&web_document, PhotoSize thumbnail,
                   vector<tl_object_ptr<telegram_api::DocumentAttribute>> &&attributes)
        : document(nullptr)
        , secret_file(nullptr)
        , secret_document(nullptr)
        , web_document(std::move(web_document))
        , thumbnail(std::move(thumbnail))
        , attributes(std::move(attributes)) {
    }

    RemoteDocument(tl_object_ptr<telegram_api::encryptedFile> &&secret_file,
                   tl_object_ptr<secret_api::decryptedMessageMediaDocument> &&secret_document,
                   vector<tl_object_ptr<telegram_api::DocumentAttribute>> &&attributes)
        : document(nullptr)
        , secret_file(std::move(secret_file))
        , secret_document(std::move(secret_document))
        , web_document(nullptr)
        , thumbnail()
        , attributes(std::move(attributes)) {
    }
  };

  tl_object_ptr<td_api::document> get_document_object(FileId file_id, PhotoFormat thumbnail_format);

  Document on_get_document(RemoteDocument remote_document, DialogId owner_dialog_id,
                           MultiPromiseActor *load_data_multipromise_ptr = nullptr,
                           Document::Type default_document_type = Document::Type::General, bool is_background = false,
                           bool is_pattern = false);

  void create_document(FileId file_id, string minithumbnail, PhotoSize thumbnail, string file_name, string mime_type,
                       bool replace);

  bool has_input_media(FileId file_id, FileId thumbnail_file_id, bool is_secret) const;

  SecretInputMedia get_secret_input_media(FileId document_file_id,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          const string &caption, BufferSlice thumbnail) const;

  tl_object_ptr<telegram_api::InputMedia> get_input_media(FileId file_id,
                                                          tl_object_ptr<telegram_api::InputFile> input_file,
                                                          tl_object_ptr<telegram_api::InputFile> input_thumbnail) const;

  FileId get_document_thumbnail_file_id(FileId file_id) const;

  void delete_document_thumbnail(FileId file_id);

  FileId dup_document(FileId new_id, FileId old_id);

  bool merge_documents(FileId new_id, FileId old_id, bool can_delete_old);

  template <class StorerT>
  void store_document(FileId file_id, StorerT &storer) const;

  template <class ParserT>
  FileId parse_document(ParserT &parser);

  string get_document_search_text(FileId file_id) const;

 private:
  class GeneralDocument {
   public:
    string file_name;
    string mime_type;
    string minithumbnail;
    PhotoSize thumbnail;
    FileId file_id;

    bool is_changed = true;
  };

  const GeneralDocument *get_document(FileId file_id) const;

  FileId on_get_document(unique_ptr<GeneralDocument> new_document, bool replace);

  Td *td_;
  std::unordered_map<FileId, unique_ptr<GeneralDocument>, FileIdHash> documents_;  // file_id -> GeneralDocument
};

}  // namespace td

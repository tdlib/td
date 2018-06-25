//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DocumentsManager.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/MimeType.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/utf8.h"

#include <limits>

namespace td {

DocumentsManager::DocumentsManager(Td *td) : td_(td) {
}

tl_object_ptr<td_api::document> DocumentsManager::get_document_object(FileId file_id) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  LOG(INFO) << "Return document " << file_id << " object";
  auto &document = documents_[file_id];
  CHECK(document != nullptr) << tag("file_id", file_id);
  document->is_changed = false;
  return make_tl_object<td_api::document>(document->file_name, document->mime_type,
                                          get_photo_size_object(td_->file_manager_.get(), &document->thumbnail),
                                          td_->file_manager_->get_file_object(file_id));
}

std::pair<DocumentsManager::DocumentType, FileId> DocumentsManager::on_get_document(
    RemoteDocument remote_document, DialogId owner_dialog_id, MultiPromiseActor *load_data_multipromise_ptr,
    DocumentType default_document_type) {
  tl_object_ptr<telegram_api::documentAttributeAnimated> animated;
  tl_object_ptr<telegram_api::documentAttributeVideo> video;
  tl_object_ptr<telegram_api::documentAttributeAudio> audio;
  tl_object_ptr<telegram_api::documentAttributeSticker> sticker;
  Dimensions dimensions;
  string file_name;
  bool has_stickers = false;
  int32 type_attributes = 0;
  for (auto &attribute : remote_document.attributes) {
    switch (attribute->get_id()) {
      case telegram_api::documentAttributeImageSize::ID: {
        auto image_size = move_tl_object_as<telegram_api::documentAttributeImageSize>(attribute);
        dimensions = get_dimensions(image_size->w_, image_size->h_);
        break;
      }
      case telegram_api::documentAttributeAnimated::ID:
        animated = move_tl_object_as<telegram_api::documentAttributeAnimated>(attribute);
        type_attributes++;
        break;
      case telegram_api::documentAttributeSticker::ID:
        sticker = move_tl_object_as<telegram_api::documentAttributeSticker>(attribute);
        type_attributes++;
        break;
      case telegram_api::documentAttributeVideo::ID:
        video = move_tl_object_as<telegram_api::documentAttributeVideo>(attribute);
        type_attributes++;
        break;
      case telegram_api::documentAttributeAudio::ID:
        audio = move_tl_object_as<telegram_api::documentAttributeAudio>(attribute);
        type_attributes++;
        break;
      case telegram_api::documentAttributeFilename::ID:
        file_name = std::move(static_cast<telegram_api::documentAttributeFilename *>(attribute.get())->file_name_);
        break;
      case telegram_api::documentAttributeHasStickers::ID:
        has_stickers = true;
        break;
      default:
        UNREACHABLE();
    }
  }
  int32 video_duration = 0;
  if (video != nullptr) {
    video_duration = video->duration_;
    if (dimensions.width == 0) {
      dimensions = get_dimensions(video->w_, video->h_);
    }

    if (animated != nullptr) {
      // video animation
      type_attributes--;
      video = nullptr;
    }
  }
  if (animated != nullptr && audio != nullptr) {
    // animation send as audio
    type_attributes--;
    audio = nullptr;
  }
  if (animated != nullptr && sticker != nullptr) {
    // animation send as sticker
    type_attributes--;
    sticker = nullptr;
  }

  auto document_type = default_document_type;
  FileType file_type = FileType::Document;
  Slice default_extension;
  bool supports_streaming = false;
  bool has_webp_thumbnail = false;
  if (type_attributes == 1 || default_document_type != DocumentType::General) {  // not a general document
    if (animated != nullptr || default_document_type == DocumentType::Animation) {
      document_type = DocumentType::Animation;
      file_type = FileType::Animation;
      default_extension = "mp4";
    } else if (audio != nullptr || default_document_type == DocumentType::Audio ||
               default_document_type == DocumentType::VoiceNote) {
      bool is_voice_note = default_document_type == DocumentType::VoiceNote;
      if (audio != nullptr) {
        is_voice_note = (audio->flags_ & telegram_api::documentAttributeAudio::Flags::VOICE_MASK) != 0;
      }
      if (is_voice_note) {
        document_type = DocumentType::VoiceNote;
        file_type = FileType::VoiceNote;
        default_extension = "oga";
        file_name.clear();
      } else {
        document_type = DocumentType::Audio;
        file_type = FileType::Audio;
        default_extension = "mp3";
      }
    } else if (sticker != nullptr || default_document_type == DocumentType::Sticker) {
      document_type = DocumentType::Sticker;
      file_type = FileType::Sticker;
      default_extension = "webp";
      owner_dialog_id = DialogId();
      file_name.clear();
      has_webp_thumbnail = td_->stickers_manager_->has_webp_thumbnail(sticker);
    } else if (video != nullptr || default_document_type == DocumentType::Video ||
               default_document_type == DocumentType::VideoNote) {
      bool is_video_note = default_document_type == DocumentType::VideoNote;
      if (video != nullptr) {
        is_video_note = (video->flags_ & telegram_api::documentAttributeVideo::ROUND_MESSAGE_MASK) != 0;
        if (!is_video_note) {
          supports_streaming = (video->flags_ & telegram_api::documentAttributeVideo::SUPPORTS_STREAMING_MASK) != 0;
        }
      }
      if (is_video_note) {
        document_type = DocumentType::VideoNote;
        file_type = FileType::VideoNote;
        file_name.clear();
      } else {
        document_type = DocumentType::Video;
        file_type = FileType::Video;
      }
      default_extension = "mp4";
    }
  } else if (type_attributes >= 2) {
    LOG(WARNING) << "Receive document with more than 1 type attribute: animated = " << to_string(animated)
                 << ", sticker = " << to_string(sticker) << ", video = " << to_string(video)
                 << ", audio = " << to_string(audio) << ", file_name = " << file_name << ", dimensions = " << dimensions
                 << ", has_stickers = " << has_stickers;
  }

  int64 id;
  int64 access_hash;
  int32 dc_id;
  int32 size;
  string mime_type;
  PhotoSize thumbnail;
  FileEncryptionKey encryption_key;
  bool is_web = false;
  bool is_web_no_proxy = false;
  string url;
  if (remote_document.document != nullptr) {
    auto document = std::move(remote_document.document);

    id = document->id_;
    access_hash = document->access_hash_;
    dc_id = document->dc_id_;
    size = document->size_;
    mime_type = std::move(document->mime_type_);

    if (document_type != DocumentType::VoiceNote) {
      thumbnail = get_photo_size(td_->file_manager_.get(), FileType::Thumbnail, 0, 0, owner_dialog_id,
                                 std::move(document->thumb_), has_webp_thumbnail);
    }
  } else if (remote_document.secret_file != nullptr) {
    CHECK(remote_document.secret_document != nullptr);
    auto file = std::move(remote_document.secret_file);
    auto document = std::move(remote_document.secret_document);

    id = file->id_;
    access_hash = file->access_hash_;
    dc_id = file->dc_id_;
    size = document->size_;
    mime_type = std::move(document->mime_type_);
    file_type = FileType::Encrypted;
    encryption_key = FileEncryptionKey{document->key_.as_slice(), document->iv_.as_slice()};
    if (encryption_key.empty()) {
      return {DocumentType::Unknown, FileId()};
    }

    if (document_type != DocumentType::VoiceNote) {
      thumbnail = get_thumbnail_photo_size(td_->file_manager_.get(), std::move(document->thumb_), owner_dialog_id,
                                           document->thumb_w_, document->thumb_h_);
    }
  } else {
    is_web = true;
    id = Random::fast(0, std::numeric_limits<int32>::max());
    dc_id = 0;
    access_hash = 0;
    thumbnail = std::move(remote_document.thumbnail);

    auto web_document_ptr = std::move(remote_document.web_document);
    switch (web_document_ptr->get_id()) {
      case telegram_api::webDocument::ID: {
        auto web_document = move_tl_object_as<telegram_api::webDocument>(web_document_ptr);
        auto r_http_url = parse_url(web_document->url_);
        if (r_http_url.is_error()) {
          LOG(ERROR) << "Can't parse URL " << web_document->url_;
          return {DocumentType::Unknown, FileId()};
        }
        auto http_url = r_http_url.move_as_ok();

        access_hash = web_document->access_hash_;
        url = http_url.get_url();
        file_name = get_url_query_file_name(http_url.query_);
        mime_type = std::move(web_document->mime_type_);
        size = web_document->size_;
        break;
      }
      case telegram_api::webDocumentNoProxy::ID: {
        is_web_no_proxy = true;
        auto web_document = move_tl_object_as<telegram_api::webDocumentNoProxy>(web_document_ptr);

        if (web_document->url_.find('.') == string::npos) {
          LOG(ERROR) << "Receive invalid URL " << web_document->url_;
          return {DocumentType::Unknown, FileId()};
        }

        url = std::move(web_document->url_);
        file_name = get_url_file_name(url);
        mime_type = std::move(web_document->mime_type_);
        size = web_document->size_;
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  LOG(DEBUG) << "Receive document with id = " << id << " of type " << static_cast<int32>(document_type);
  if (!is_web && !DcId::is_valid(dc_id)) {
    LOG(ERROR) << "Wrong dc_id = " << dc_id;
    return {DocumentType::Unknown, FileId()};
  }

  auto suggested_file_name = file_name;
  if (suggested_file_name.empty()) {
    suggested_file_name = to_string(static_cast<uint64>(id));
    auto extension = MimeType::to_extension(mime_type, default_extension);
    if (!extension.empty()) {
      suggested_file_name += '.';
      suggested_file_name += extension;
    }
  }

  FileId file_id;
  if (!is_web) {
    file_id = td_->file_manager_->register_remote(
        FullRemoteFileLocation(file_type, id, access_hash, DcId::internal(dc_id)), FileLocationSource::FromServer,
        owner_dialog_id, size, 0, suggested_file_name);
    if (!encryption_key.empty()) {
      td_->file_manager_->set_encryption_key(file_id, std::move(encryption_key));
    }
  } else if (!is_web_no_proxy) {
    file_id = td_->file_manager_->register_remote(FullRemoteFileLocation(file_type, url, access_hash),
                                                  FileLocationSource::FromServer, owner_dialog_id, 0, size, file_name);
  } else {
    auto r_file_id = td_->file_manager_->from_persistent_id(url, file_type);
    if (r_file_id.is_error()) {
      LOG(ERROR) << "Can't register URL: " << r_file_id.error();
      return {DocumentType::Unknown, FileId()};
    }
    file_id = r_file_id.move_as_ok();
  }

  if (dimensions.width != 0 && thumbnail.dimensions.width != 0) {
    if ((thumbnail.dimensions.width < thumbnail.dimensions.height && dimensions.width > dimensions.height) ||
        (thumbnail.dimensions.width > thumbnail.dimensions.height && dimensions.width < dimensions.height)) {
      // fix for wrong dimensions specified by the Android application
      std::swap(dimensions.width, dimensions.height);
    }
  }

  switch (document_type) {
    case DocumentType::Animation:
      // TODO use has_stickers
      td_->animations_manager_->create_animation(file_id, std::move(thumbnail), std::move(file_name),
                                                 std::move(mime_type), video_duration, dimensions, !is_web);
      break;
    case DocumentType::Audio: {
      int32 duration = 0;
      string title;
      string performer;
      if (audio != nullptr) {
        duration = audio->duration_;
        title = std::move(audio->title_);
        performer = std::move(audio->performer_);
      }
      td_->audios_manager_->create_audio(file_id, std::move(thumbnail), std::move(file_name), std::move(mime_type),
                                         duration, std::move(title), std::move(performer), !is_web);
      break;
    }
    case DocumentType::General:
      td_->documents_manager_->create_document(file_id, std::move(thumbnail), std::move(file_name),
                                               std::move(mime_type), !is_web);
      break;
    case DocumentType::Sticker:
      td_->stickers_manager_->create_sticker(file_id, std::move(thumbnail), dimensions, true, std::move(sticker),
                                             load_data_multipromise_ptr);
      break;
    case DocumentType::Video:
      td_->videos_manager_->create_video(file_id, std::move(thumbnail), has_stickers, vector<FileId>(),
                                         std::move(file_name), std::move(mime_type), video_duration, dimensions,
                                         supports_streaming, !is_web);
      break;
    case DocumentType::VideoNote:
      td_->video_notes_manager_->create_video_note(file_id, std::move(thumbnail), video_duration, dimensions, !is_web);
      break;
    case DocumentType::VoiceNote: {
      int32 duration = 0;
      string waveform;
      if (audio != nullptr) {
        duration = audio->duration_;
        waveform = audio->waveform_.as_slice().str();
      }
      td_->voice_notes_manager_->create_voice_note(file_id, std::move(mime_type), duration, std::move(waveform),
                                                   !is_web);
      break;
    }
    case DocumentType::Unknown:
    default:
      UNREACHABLE();
  }
  return {document_type, file_id};
}

FileId DocumentsManager::on_get_document(std::unique_ptr<Document> new_document, bool replace) {
  auto file_id = new_document->file_id;
  LOG(INFO) << "Receive document " << file_id;
  auto &d = documents_[new_document->file_id];
  if (d == nullptr) {
    d = std::move(new_document);
  } else if (replace) {
    CHECK(d->file_id == new_document->file_id);
    if (d->mime_type != new_document->mime_type) {
      LOG(DEBUG) << "Document " << file_id << " mime_type has changed";
      d->mime_type = new_document->mime_type;
      d->is_changed = true;
    }
    if (d->file_name != new_document->file_name) {
      LOG(DEBUG) << "Document " << file_id << " file_name has changed";
      d->file_name = new_document->file_name;
      d->is_changed = true;
    }
    if (d->thumbnail != new_document->thumbnail) {
      if (!d->thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Document " << file_id << " thumbnail has changed";
      } else {
        LOG(INFO) << "Document " << file_id << " thumbnail has changed from " << d->thumbnail << " to "
                  << new_document->thumbnail;
      }
      d->thumbnail = new_document->thumbnail;
      d->is_changed = true;
    }
  }

  return file_id;
}

void DocumentsManager::create_document(FileId file_id, PhotoSize thumbnail, string file_name, string mime_type,
                                       bool replace) {
  auto d = make_unique<Document>();
  d->file_id = file_id;
  d->file_name = std::move(file_name);
  d->mime_type = std::move(mime_type);
  d->thumbnail = std::move(thumbnail);
  on_get_document(std::move(d), replace);
}

const DocumentsManager::Document *DocumentsManager::get_document(FileId file_id) const {
  auto document = documents_.find(file_id);
  if (document == documents_.end()) {
    return nullptr;
  }

  CHECK(document->second->file_id == file_id);
  return document->second.get();
}

bool DocumentsManager::has_input_media(FileId file_id, FileId thumbnail_file_id, bool is_secret) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (is_secret) {
    if (!file_view.is_encrypted_secret() || file_view.encryption_key().empty() || !file_view.has_remote_location()) {
      return false;
    }

    return !thumbnail_file_id.is_valid();
  } else {
    if (file_view.is_encrypted()) {
      return false;
    }
    return file_view.has_remote_location() || file_view.has_url();
  }
}

SecretInputMedia DocumentsManager::get_secret_input_media(FileId document_file_id,
                                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                          const string &caption, BufferSlice thumbnail) const {
  const Document *document = get_document(document_file_id);
  CHECK(document != nullptr);
  auto file_view = td_->file_manager_->get_file_view(document_file_id);
  auto &encryption_key = file_view.encryption_key();
  if (!file_view.is_encrypted_secret() || encryption_key.empty()) {
    return SecretInputMedia{};
  }
  if (file_view.has_remote_location()) {
    input_file = file_view.remote_location().as_input_encrypted_file();
  }
  if (!input_file) {
    return SecretInputMedia{};
  }
  if (document->thumbnail.file_id.is_valid() && thumbnail.empty()) {
    return SecretInputMedia{};
  }
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  if (document->file_name.size()) {
    attributes.push_back(make_tl_object<secret_api::documentAttributeFilename>(document->file_name));
  }
  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaDocument>(
          std::move(thumbnail), document->thumbnail.dimensions.width, document->thumbnail.dimensions.height,
          document->mime_type, narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
          BufferSlice(encryption_key.iv_slice()), std::move(attributes), caption)};
}

tl_object_ptr<telegram_api::InputMedia> DocumentsManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return nullptr;
  }
  if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
    return make_tl_object<telegram_api::inputMediaDocument>(0, file_view.remote_location().as_input_document(), 0);
  }
  if (file_view.has_url()) {
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(0, file_view.url(), 0);
  }
  CHECK(!file_view.has_remote_location());

  const Document *document = get_document(file_id);
  CHECK(document != nullptr);

  if (input_file != nullptr) {
    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    if (document->file_name.size()) {
      attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(document->file_name));
    }
    int32 flags = 0;
    if (input_thumbnail != nullptr) {
      flags |= telegram_api::inputMediaUploadedDocument::THUMB_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), document->mime_type,
        std::move(attributes), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  }

  return nullptr;
}

FileId DocumentsManager::get_document_thumbnail_file_id(FileId file_id) const {
  auto document = get_document(file_id);
  CHECK(document != nullptr);
  return document->thumbnail.file_id;
}

void DocumentsManager::delete_document_thumbnail(FileId file_id) {
  auto &document = documents_[file_id];
  CHECK(document != nullptr);
  document->thumbnail = PhotoSize();
}

FileId DocumentsManager::dup_document(FileId new_id, FileId old_id) {
  const Document *old_document = get_document(old_id);
  CHECK(old_document != nullptr);
  auto &new_document = documents_[new_id];
  CHECK(!new_document);
  new_document = std::make_unique<Document>(*old_document);
  new_document->file_id = new_id;
  new_document->thumbnail.file_id = td_->file_manager_->dup_file_id(new_document->thumbnail.file_id);
  return new_id;
}

bool DocumentsManager::merge_documents(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge documents " << new_id << " and " << old_id;
  const Document *old_ = get_document(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = documents_.find(new_id);
  if (new_it == documents_.end()) {
    auto &old = documents_[old_id];
    old->is_changed = true;
    if (!can_delete_old) {
      dup_document(new_id, old_id);
    } else {
      old->file_id = new_id;
      documents_.emplace(new_id, std::move(old));
    }
  } else {
    Document *new_ = new_it->second.get();
    CHECK(new_ != nullptr);

    if (old_->thumbnail != new_->thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->thumbnail.file_id, old_->thumbnail.file_id));
    }

    new_->is_changed = true;
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    documents_.erase(old_id);
  }
  return true;
}

string DocumentsManager::get_document_search_text(FileId file_id) const {
  auto document = get_document(file_id);
  CHECK(document);
  if (document->file_name.size() > 32) {
    return document->file_name;
  }
  auto buf = StackAllocator::alloc(256);
  StringBuilder sb(buf.as_slice());
  auto stem = PathView(document->file_name).file_stem();
  sb << document->file_name;
  for (size_t i = 1; i + 1 < stem.size(); i++) {
    if (is_utf8_character_first_code_unit(stem[i])) {
      sb << " " << stem.substr(0, i);
    }
  }
  if (sb.is_error()) {
    return document->file_name;
  }
  return sb.as_cslice().str();
}

}  // namespace td

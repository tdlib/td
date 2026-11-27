//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/ExportRichMessage.h"
#include "td/telegram/ExportSettings.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class Td;

struct ExportStats {
  int32 file_count = 0;
  int64 total_size = 0;
};

struct ExportDialogInfo {
  enum class Type : int32 {
    Unknown,
    Self,
    Replies,
    VerifyCodes,
    Personal,
    Bot,
    PrivateGroup,
    PrivateSupergroup,
    PublicSupergroup,
    PrivateChannel,
    PublicChannel
  };
  DialogId dialog_id;
  Type type = Type::Unknown;
  string name;
  // the directory of the chat relative to the export directory, ending with '/'; empty if a single chat is exported
  string relative_path;
  bool is_left_channel = false;
  bool only_my_messages = false;
};

enum class ExportAbout : int32 { Telegram, Chats, LeftChats, Contacts, FrequentContacts, Sessions, WebSessions };

// returns the description of the exported data
Slice get_export_about_text(ExportAbout about);

// identifiers of messages of the basic group, from which the supergroup was migrated, are shifted by the value
constexpr int32 EXPORT_MIGRATED_MESSAGE_ID_SHIFT = -1000000000;

constexpr int64 EXPORT_NANOS_IN_GRAM = 1000000000;

struct ExportDateTime {
  int64 year;
  int64 month;
  int64 day;
  // seconds since the start of the day
  int64 seconds;
};

// returns the UTC offset of the date: utc_time_offset, moved by the change of the system time zone's offset between
// now and the date, so that each date gets its own daylight saving time
int32 get_export_utc_time_offset(int64 date, int32 utc_time_offset);

int64 get_export_local_time(int64 date, int32 utc_time_offset);

ExportDateTime get_export_date_time(int64 local_time);

// returns the date in the "YYYY-MM-DDTHH:MM:SS" format
string get_export_date_time_string(int64 local_time);

// returns the date in the "DD-MM-YYYY_HH-MM-SS" format, used in names of exported files
string get_export_file_date_time_string(int64 local_time);

int64 get_export_file_size(const td_api::file *file);

const td_api::photoSize *get_export_photo_size(const vector<td_api::object_ptr<td_api::photoSize>> &sizes);

// the maximum size of photos in HTML pages, in physical pixels
constexpr int32 EXPORT_HTML_PHOTO_SIZE = 520;

// the size of photos of service messages in HTML pages, in logical pixels
constexpr int32 EXPORT_HTML_SERVICE_PHOTO_SIZE = 60;

// the sizes of userpics in lists and of the user's own userpic in HTML pages, in logical pixels
constexpr int32 EXPORT_HTML_ENTRY_USERPIC_SIZE = 48;
constexpr int32 EXPORT_HTML_PERSONAL_USERPIC_SIZE = 90;

// the size of previews of stories in HTML pages, in logical pixels
constexpr int32 EXPORT_HTML_STORY_WIDTH = 45;
constexpr int32 EXPORT_HTML_STORY_HEIGHT = 80;

// returns the smallest size, which covers a box of the given size, or nullptr if it is the largest size
const td_api::photoSize *get_export_photo_thumbnail_size(const vector<td_api::object_ptr<td_api::photoSize>> &sizes,
                                                         int32 box_size);

// returns the text or the caption of the message
const td_api::formattedText *get_export_message_text(const td_api::MessageContent *content);

enum class ExportContentKind : int32 { Unsupported, Ordinary, Service };

ExportContentKind get_export_content_kind(const td_api::MessageContent *content);

int64 get_export_dialog_bare_id(DialogId dialog_id);

DialogId get_export_message_sender_dialog_id(const td_api::MessageSender *sender);

// returns 0 for messages, which aren't sent yet
int32 get_export_server_message_id(int64 message_id);

void get_export_giveaway_prize(const td_api::GiveawayPrize *prize, int32 &month_count, int64 &star_count);

struct ExportTextPart {
  Slice text;
  // nullptr for plain text
  const td_api::TextEntityType *type;
};

// a part per entity in their order, and the plain text between them; entities nested in another one are skipped
vector<ExportTextPart> get_export_text_parts(Slice text,
                                             const vector<td_api::object_ptr<td_api::textEntity>> *entities);

string format_export_money_amount(int64 amount, Slice currency);

string format_export_phone_number(const string &phone_number);

struct ExportMessage {
  td_api::object_ptr<td_api::message> message;
  // td_api has no content for cleared history, so such a message has messageUnsupported content
  bool is_history_cleared = false;
  // the photo, the document or the contact's vCard
  ExportFile file;
  ExportFile thumbnail;
  // non-null if the message has a rich message, whatever its content is
  unique_ptr<ExportRichMessage> rich_message;
};

// the files of custom emoji, which are used in texts of messages and in reactions
using ExportCustomEmojiFiles = FlatHashMap<CustomEmojiId, ExportFile, CustomEmojiIdHash>;

struct ExportUserpic {
  int32 date = 0;
  int64 size = 0;
  ExportFile file;
  // a smaller size of the photo, which HTML pages show instead of scaling the photo
  ExportFile thumbnail;
  // the size shown with the personal information; loaded only for the current profile photo
  ExportFile personal_thumbnail;
};

struct ExportStory {
  int32 date = 0;
  int32 expire_date = 0;
  bool is_pinned = false;
  td_api::object_ptr<td_api::formattedText> caption;
  int64 size = 0;
  ExportFile file;
  // the preview of the story, which is loaded only for HTML pages
  ExportFile thumbnail;
};

// emoji outside texts of messages aren't resolved, so their identifier is returned instead of the path;
// "(unavailable)" if the file couldn't be loaded, and an empty string if it is skipped by the settings
string get_export_custom_emoji_path(const ExportCustomEmojiFiles *custom_emoji_files, int64 custom_emoji_id);

struct ExportPersonalInfo {
  int64 user_id = 0;
  string first_name;
  string last_name;
  string phone_number;
  string username;
  string bio;
  // the color chosen by the user; -1 if none
  int32 color = -1;
};

struct ExportContact {
  string first_name;
  string last_name;
  string phone_number;
  int32 date = 0;
};

struct ExportTopPeer {
  DialogId dialog_id;
  double rating = 0.0;
};

struct ExportContactList {
  // in the order of names
  vector<ExportContact> contacts;
  vector<ExportTopPeer> correspondents;
  vector<ExportTopPeer> inline_bots;
  vector<ExportTopPeer> phone_calls;
};

struct ExportSession {
  int32 api_id = 0;
  string application_name;
  string application_version;
  string device_model;
  string platform;
  string system_version;
  string ip;
  string country;
  string region;
  int32 created = 0;
  int32 last_active = 0;
};

struct ExportWebSession {
  // empty if the bot is unknown
  string bot_username;
  string domain;
  string browser;
  string platform;
  string ip;
  string region;
  int32 created = 0;
  int32 last_active = 0;
};

struct ExportSessionList {
  vector<ExportSession> sessions;
  vector<ExportWebSession> web_sessions;
};

class ExportOutputFile {
 public:
  ExportOutputFile(string path, ExportStats *stats);

  // an empty write creates an empty file
  Status write(Slice data) TD_WARN_UNUSED_RESULT;

 private:
  string path_;
  FileFd fd_;
  ExportStats *stats_;
};

class ExportWriter {
 public:
  ExportWriter() = default;
  ExportWriter(const ExportWriter &) = delete;
  ExportWriter &operator=(const ExportWriter &) = delete;
  ExportWriter(ExportWriter &&) = delete;
  ExportWriter &operator=(ExportWriter &&) = delete;
  virtual ~ExportWriter() = default;

  virtual Status start(const ExportSettings &settings, ExportStats *stats) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_personal_info(const ExportPersonalInfo &info) TD_WARN_UNUSED_RESULT = 0;

  // the profile photos are received from the current one
  virtual Status write_userpics_start(int32 count) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_userpics_slice(const vector<ExportUserpic> &userpics) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_userpics_end() TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_stories_start(int32 count) TD_WARN_UNUSED_RESULT = 0;

  // skipped_count is the number of received stories, which can't be exported
  virtual Status write_stories_slice(const vector<ExportStory> &stories, int32 skipped_count) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_stories_end() TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_profile_audios_start(int32 count) TD_WARN_UNUSED_RESULT = 0;

  // the audios are messages without a chat, a sender and a date, numbered from 1
  virtual Status write_profile_audios_slice(const vector<ExportMessage> &audios) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_profile_audios_end() TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_contacts(const ExportContactList &contacts) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_sessions(const ExportSessionList &sessions) TD_WARN_UNUSED_RESULT = 0;

  // the file of other data is skipped if it couldn't be loaded
  virtual Status write_other_data(const ExportFile &file) TD_WARN_UNUSED_RESULT = 0;

  // the chats are written between write_dialogs_start and write_dialogs_end; left channels are written last
  virtual Status write_dialogs_start(int32 dialog_count) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_dialog_start(const ExportDialogInfo &dialog) TD_WARN_UNUSED_RESULT = 0;

  // messages of the basic group the supergroup was migrated from have is_migrated set and are written first
  virtual Status write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated,
                                       const ExportCustomEmojiFiles &custom_emoji_files) TD_WARN_UNUSED_RESULT = 0;

  // only_my_messages is false if access to the chat was lost after other messages were exported
  virtual Status write_dialog_end(bool only_my_messages) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_dialogs_end() TD_WARN_UNUSED_RESULT = 0;

  virtual Status finish() TD_WARN_UNUSED_RESULT = 0;
};

unique_ptr<ExportWriter> create_export_writer(Td *td, ExportFormat format);

}  // namespace td

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileDb.h"

#include "td/telegram/files/FileData.h"
#include "td/telegram/files/FileData.hpp"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileLocation.hpp"
#include "td/telegram/logevent/LogEvent.h"  // WithVersion
#include "td/telegram/Version.h"

#include "td/actor/actor.h"

#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueSafe.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

namespace td {

Status drop_file_db(SqliteDb &db, int32 version) {
  LOG(WARNING) << "Drop file_db " << tag("version", version) << tag("current_db_version", current_db_version());
  TRY_STATUS(SqliteKeyValue::drop(db, "files"));
  return Status::OK();
}

Status fix_file_remote_location_key_bug(SqliteDb &db);
Status init_file_db(SqliteDb &db, int32 version) {
  LOG(INFO) << "Init file database " << tag("version", version);

  // Check if database exists
  TRY_RESULT(has_table, db.has_table("files"));

  if (!has_table) {
    version = 0;
  } else if (version < static_cast<int32>(DbVersion::DialogDbCreated)) {
    TRY_STATUS(drop_file_db(db, version));
    version = 0;
  } else if (version < static_cast<int>(DbVersion::FixFileRemoteLocationKeyBug)) {
    TRY_STATUS(fix_file_remote_location_key_bug(db));
  }

  if (version == 0) {
    TRY_STATUS(SqliteKeyValue::init(db, "files"));
  }
  return Status::OK();
}

class FileDb : public FileDbInterface {
 public:
  class FileDbActor : public Actor {
   public:
    FileDbActor(FileDbId current_pmc_id, std::shared_ptr<SqliteKeyValueSafe> file_kv_safe)
        : current_pmc_id_(current_pmc_id), file_kv_safe_(std::move(file_kv_safe)) {
    }

    void close(Promise<> promise) {
      file_kv_safe_.reset();
      LOG(INFO) << "FileDb is closed";
      promise.set_value(Unit());
      stop();
    }

    void load_file_data(const string &key, Promise<FileData> promise) {
      promise.set_result(load_file_data_impl(actor_id(this), file_pmc(), key, current_pmc_id_));
    }

    void clear_file_data(FileDbId id, const string &remote_key, const string &local_key, const string &generate_key) {
      auto &pmc = file_pmc();
      pmc.begin_transaction().ensure();
      SCOPE_EXIT {
        pmc.commit_transaction().ensure();
      };

      if (id > current_pmc_id_) {
        pmc.set("file_id", to_string(id.get()));
        current_pmc_id_ = id;
      }

      pmc.erase(PSTRING() << "file" << id.get());
      LOG(DEBUG) << "ERASE " << format::as_hex_dump<4>(Slice(PSLICE() << "file" << id.get()));

      if (!remote_key.empty()) {
        pmc.erase(remote_key);
        LOG(DEBUG) << "ERASE remote " << format::as_hex_dump<4>(Slice(remote_key));
      }
      if (!local_key.empty()) {
        pmc.erase(local_key);
        LOG(DEBUG) << "ERASE local " << format::as_hex_dump<4>(Slice(local_key));
      }
      if (!generate_key.empty()) {
        pmc.erase(generate_key);
      }
    }
    void store_file_data(FileDbId id, const string &file_data, const string &remote_key, const string &local_key,
                         const string &generate_key) {
      auto &pmc = file_pmc();
      pmc.begin_transaction().ensure();
      SCOPE_EXIT {
        pmc.commit_transaction().ensure();
      };

      if (id > current_pmc_id_) {
        pmc.set("file_id", to_string(id.get()));
        current_pmc_id_ = id;
      }

      pmc.set(PSTRING() << "file" << id.get(), file_data);

      if (!remote_key.empty()) {
        pmc.set(remote_key, to_string(id.get()));
      }
      if (!local_key.empty()) {
        pmc.set(local_key, to_string(id.get()));
      }
      if (!generate_key.empty()) {
        pmc.set(generate_key, to_string(id.get()));
      }
    }
    void store_file_data_ref(FileDbId id, FileDbId new_id) {
      auto &pmc = file_pmc();
      pmc.begin_transaction().ensure();
      SCOPE_EXIT {
        pmc.commit_transaction().ensure();
      };

      if (id > current_pmc_id_) {
        pmc.set("file_id", to_string(id.get()));
        current_pmc_id_ = id;
      }

      do_store_file_data_ref(id, new_id);
    }

    void optimize_refs(const std::vector<FileDbId> ids, FileDbId main_id) {
      LOG(INFO) << "Optimize " << ids.size() << " ids in file database to " << main_id.get();
      auto &pmc = file_pmc();
      pmc.begin_transaction().ensure();
      SCOPE_EXIT {
        pmc.commit_transaction().ensure();
      };
      for (size_t i = 0; i + 1 < ids.size(); i++) {
        do_store_file_data_ref(ids[i], main_id);
      }
    }

   private:
    FileDbId current_pmc_id_;
    std::shared_ptr<SqliteKeyValueSafe> file_kv_safe_;

    SqliteKeyValue &file_pmc() {
      return file_kv_safe_->get();
    }

    void do_store_file_data_ref(FileDbId id, FileDbId new_id) {
      file_pmc().set(PSTRING() << "file" << id.get(), PSTRING() << "@@" << new_id.get());
    }
  };

  explicit FileDb(std::shared_ptr<SqliteKeyValueSafe> kv_safe, int scheduler_id = -1) {
    file_kv_safe_ = std::move(kv_safe);
    CHECK(file_kv_safe_);
    current_pmc_id_ = FileDbId(to_integer<uint64>(file_kv_safe_->get().get("file_id")));
    file_db_actor_ =
        create_actor_on_scheduler<FileDbActor>("FileDbActor", scheduler_id, current_pmc_id_, file_kv_safe_);
  }

  FileDbId create_pmc_id() override {
    current_pmc_id_ = FileDbId(current_pmc_id_.get() + 1);
    return current_pmc_id_;
  }

  void close(Promise<> promise) override {
    send_closure(std::move(file_db_actor_), &FileDbActor::close, std::move(promise));
  }

  void get_file_data_impl(string key, Promise<FileData> promise) override {
    send_closure(file_db_actor_, &FileDbActor::load_file_data, std::move(key), std::move(promise));
  }

  Result<FileData> get_file_data_sync_impl(string key) override {
    return load_file_data_impl(file_db_actor_.get(), file_kv_safe_->get(), key, current_pmc_id_);
  }

  void clear_file_data(FileDbId id, const FileData &file_data) override {
    string remote_key;
    if (file_data.remote_.type() == RemoteFileLocation::Type::Full) {
      remote_key = as_key(file_data.remote_.full());
    }
    string local_key;
    if (file_data.local_.type() == LocalFileLocation::Type::Full) {
      local_key = as_key(file_data.local_.full());
    }
    string generate_key;
    if (file_data.generate_ != nullptr) {
      generate_key = as_key(*file_data.generate_);
    }
    send_closure(file_db_actor_, &FileDbActor::clear_file_data, id, remote_key, local_key, generate_key);
  }
  void set_file_data(FileDbId id, const FileData &file_data, bool new_remote, bool new_local,
                     bool new_generate) override {
    string remote_key;
    if (file_data.remote_.type() == RemoteFileLocation::Type::Full && new_remote) {
      remote_key = as_key(file_data.remote_.full());
    }
    string local_key;
    if (file_data.local_.type() == LocalFileLocation::Type::Full && new_local) {
      local_key = as_key(file_data.local_.full());
    }
    string generate_key;
    if (file_data.generate_ != nullptr && new_generate) {
      generate_key = as_key(*file_data.generate_);
    }
    LOG(DEBUG) << "SAVE " << id.get() << " -> " << file_data << " "
               << tag("remote_key", format::as_hex_dump<4>(Slice(remote_key)))
               << tag("local_key", format::as_hex_dump<4>(Slice(local_key)))
               << tag("generate_key", format::as_hex_dump<4>(Slice(generate_key)));
    send_closure(file_db_actor_, &FileDbActor::store_file_data, id, serialize(file_data), remote_key, local_key,
                 generate_key);
  }

  void set_file_data_ref(FileDbId id, FileDbId new_id) override {
    send_closure(file_db_actor_, &FileDbActor::store_file_data_ref, id, new_id);
  }
  SqliteKeyValue &pmc() override {
    return file_kv_safe_->get();
  }

 private:
  ActorOwn<FileDbActor> file_db_actor_;
  FileDbId current_pmc_id_;
  std::shared_ptr<SqliteKeyValueSafe> file_kv_safe_;

  static Result<FileData> load_file_data_impl(ActorId<FileDbActor> file_db_actor_id, SqliteKeyValue &pmc,
                                              const string &key, FileDbId current_pmc_id) {
    //LOG(DEBUG) << "Load by key " << format::as_hex_dump<4>(Slice(key));
    TRY_RESULT(id, get_id(pmc, key));

    vector<FileDbId> ids;
    string data_str;
    int attempt_count = 0;
    while (true) {
      if (attempt_count > 100) {
        LOG(FATAL) << "Cycle in file database? current_pmc_id=" << current_pmc_id << " key=" << key
                   << " links=" << format::as_array(ids);
      }
      attempt_count++;

      data_str = pmc.get(PSTRING() << "file" << id.get());
      auto data_slice = Slice(data_str);

      if (data_slice.substr(0, 2) == "@@") {
        ids.push_back(id);

        id = FileDbId(to_integer<uint64>(data_slice.substr(2)));
      } else {
        break;
      }
    }
    if (ids.size() > 1) {
      send_closure(file_db_actor_id, &FileDbActor::optimize_refs, std::move(ids), id);
    }
    //LOG(DEBUG) << "By id " << id.get() << " found data " << format::as_hex_dump<4>(Slice(data_str));
    //LOG(INFO) << attempt_count;

    logevent::WithVersion<TlParser> parser(data_str);
    parser.set_version(static_cast<int32>(Version::Initial));
    FileData data;
    data.parse(parser, true);
    parser.fetch_end();
    auto status = parser.get_status();
    if (status.is_error()) {
      return std::move(status);
    }
    return std::move(data);
  }

  static Result<FileDbId> get_id(SqliteKeyValue &pmc, const string &key) TD_WARN_UNUSED_RESULT {
    auto id_str = pmc.get(key);
    //LOG(DEBUG) << "Found id " << id_str << " by key " << format::as_hex_dump<4>(Slice(key));
    if (id_str.empty()) {
      return Status::Error("There is no such a key in database");
    }
    return FileDbId(to_integer<uint64>(id_str));
  }
};

std::shared_ptr<FileDbInterface> create_file_db(std::shared_ptr<SqliteConnectionSafe> connection, int scheduler_id) {
  auto kv = std::make_shared<SqliteKeyValueSafe>("files", std::move(connection));
  return std::make_shared<FileDb>(std::move(kv), scheduler_id);
}

Status fix_file_remote_location_key_bug(SqliteDb &db) {
  static const int32 OLD_KEY_MAGIC = 0x64378433;
  SqliteKeyValue kv;
  kv.init_with_connection(db.clone(), "files").ensure();
  auto ptr = StackAllocator::alloc(4);
  MutableSlice prefix = ptr.as_slice();
  TlStorerUnsafe(prefix.ubegin()).store_int(OLD_KEY_MAGIC);
  kv.get_by_prefix(prefix, [&](Slice key, Slice value) {
    CHECK(TlParser(key).fetch_int() == OLD_KEY_MAGIC);
    auto remote_str = PSTRING() << key.substr(4, 4) << Slice("\0\0\0\0") << key.substr(8);
    FullRemoteFileLocation remote;
    logevent::WithVersion<TlParser> parser(remote_str);
    parser.set_version(static_cast<int32>(Version::Initial));
    parse(remote, parser);
    parser.fetch_end();
    auto status = parser.get_status();
    if (status.is_ok()) {
      kv.set(FileDbInterface::as_key(remote), value);
    }
    LOG(DEBUG) << "ERASE " << format::as_hex_dump<4>(Slice(key));
    kv.erase(key);
    return true;
  });
  return Status::OK();
}

}  // namespace td

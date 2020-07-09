//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/binlog/Binlog.h"

#include "td/db/DbKey.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/tl_parsers.h"

#include <cstdio>
#include <map>

struct Trie {
 public:
  Trie() {
    nodes_.resize(1);
  }

  void add(td::Slice value) {
    do_add(0, PSLICE() << value << '\0');
  }

  void dump() {
    if (nodes_[0].sum == 0) {  // division by zero
      return;
    }
    LOG(ERROR) << "TOTAL: " << nodes_[0].sum;
    do_dump(0);
  }

 private:
  struct FullNode {
    int next[256] = {};
    int sum = 0;
  };
  std::vector<FullNode> nodes_;
  std::string path_;
  void do_add(int id, td::Slice value) {
    nodes_[id].sum++;
    if (value.empty()) {
      return;
    }
    td::uint8 c = value[0];
    if (nodes_[id].next[c] == 0) {
      int next_id = static_cast<int>(nodes_.size());
      nodes_.emplace_back();
      nodes_[id].next[c] = next_id;
    }
    do_add(nodes_[id].next[c], value.substr(1));
  }
  void do_dump(int v) {
    bool flag = !path_.empty() && path_.back() == '\0';
    if (flag || nodes_[v].sum * 20 < nodes_[0].sum) {
      if (flag) {
        path_.pop_back();
      } else {
        path_.push_back('*');
      }
      LOG(ERROR) << nodes_[v].sum << " " << nodes_[v].sum * 100 / nodes_[0].sum << "% [" << td::format::escaped(path_)
                 << "]";
      if (flag) {
        path_.push_back('\0');
      } else {
        path_.pop_back();
      }
      return;
    }
    for (int c = 0; c < 256; c++) {
      auto next_id = nodes_[v].next[c];
      if (next_id == 0) {
        continue;
      }
      path_.append(1, (char)c);
      do_dump(next_id);
      path_.pop_back();
    }
  }
};

namespace td {
struct Event : public Storer {
  Event() = default;
  Event(Slice key, Slice value) : key(key), value(value) {
  }
  Slice key;
  Slice value;
  template <class StorerT>
  void store(StorerT &&storer) const {
    storer.store_string(key);
    storer.store_string(value);
  }

  template <class ParserT>
  void parse(ParserT &&parser) {
    key = parser.template fetch_string<Slice>();
    value = parser.template fetch_string<Slice>();
  }

  size_t size() const override {
    TlStorerCalcLength storer;
    store(storer);
    return storer.get_length();
  }
  size_t store(uint8 *ptr) const override {
    TlStorerUnsafe storer(ptr);
    store(storer);
    return static_cast<size_t>(storer.get_buf() - ptr);
  }
};
}  // namespace td
enum Magic { ConfigPmcMagic = 0x1f18, BinlogPmcMagic = 0x4327 };

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: binlog_dump <binlog_file_name>\n");
    return 1;
  }

  struct Info {
    std::size_t full_size = 0;
    std::size_t compressed_size = 0;
    Trie trie;
    Trie compressed_trie;
  };
  std::map<td::uint64, Info> info;

  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  td::Binlog binlog;
  binlog
      .init(
          argv[1],
          [&](auto &event) {
            info[0].compressed_size += event.raw_event_.size();
            info[event.type_].compressed_size += event.raw_event_.size();
            if (event.type_ == ConfigPmcMagic || event.type_ == BinlogPmcMagic) {
              td::Event kv_event;
              kv_event.parse(td::TlParser(event.data_));
              info[event.type_].compressed_trie.add(kv_event.key);
            }
          },
          td::DbKey::raw_key("cucumber"), td::DbKey::empty(), -1,
          [&](auto &event) mutable {
            info[0].full_size += event.raw_event_.size();
            info[event.type_].full_size += event.raw_event_.size();
            if (event.type_ == ConfigPmcMagic || event.type_ == BinlogPmcMagic) {
              td::Event kv_event;
              kv_event.parse(td::TlParser(event.data_));
              info[event.type_].trie.add(kv_event.key);
            }
            LOG(PLAIN) << "LogEvent[" << td::tag("id", td::format::as_hex(event.id_)) << td::tag("type", event.type_)
                       << td::tag("flags", event.flags_) << td::tag("size", event.data_.size())
                       << td::tag("data", td::format::escaped(event.data_)) << "]\n";
          })
      .ensure();

  for (auto &it : info) {
    LOG(ERROR) << td::tag("handler", td::format::as_hex(it.first))
               << td::tag("full_size", td::format::as_size(it.second.full_size))
               << td::tag("compressed_size", td::format::as_size(it.second.compressed_size));
    it.second.trie.dump();
    it.second.compressed_trie.dump();
  }

  return 0;
}

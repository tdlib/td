//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/telegram/net/DcId.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {
class DcOption {
  // do not forget to update PrintFlags
  enum Flags { IPv6 = 1, MediaOnly = 2, ObfuscatedTcpOnly = 4, Cdn = 8, Static = 16 };

  int32 flags = 0;
  DcId dc_id;
  IPAddress ip_address;

  struct PrintFlags {
    int32 flags;
  };

  bool is_ipv6() const {
    return (flags & Flags::IPv6) != 0;
  }

 public:
  DcOption() = default;

  DcOption(DcId dc_id, const IPAddress &ip_address)
      : flags(ip_address.is_ipv4() ? 0 : IPv6), dc_id(dc_id), ip_address(ip_address) {
  }

  explicit DcOption(const telegram_api::dcOption &option) {
    auto ip = option.ip_address_;
    auto port = option.port_;
    flags = 0;
    if (!DcId::is_valid(option.id_)) {
      dc_id = DcId::invalid();
      return;
    }

    if (option.cdn_) {
      dc_id = DcId::external(option.id_);
      flags |= Flags::Cdn;
    } else {
      dc_id = DcId::internal(option.id_);
    }
    if (option.ipv6_) {
      flags |= Flags::IPv6;
    }
    if (option.media_only_) {
      flags |= Flags::MediaOnly;
    }
    if (option.tcpo_only_) {
      flags |= Flags::ObfuscatedTcpOnly;
    }
    if (option.static_) {
      flags |= Flags::Static;
    }
    init_ip_address(ip, port);
  }

  DcOption(DcId new_dc_id, const telegram_api::ipPort &ip_port) {
    dc_id = new_dc_id;
    init_ip_address(IPAddress::ipv4_to_str(ip_port.ipv4_), ip_port.port_);
  }

  DcId get_dc_id() const {
    return dc_id;
  }

  const IPAddress &get_ip_address() const {
    return ip_address;
  }

  bool is_media_only() const {
    return (flags & Flags::MediaOnly) != 0;
  }

  bool is_obfuscated_tcp_only() const {
    return (flags & Flags::ObfuscatedTcpOnly) != 0;
  }

  bool is_static() const {
    return (flags & Flags::Static) != 0;
  }

  bool is_valid() const {
    return ip_address.is_valid() && dc_id.is_exact();
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(flags);
    storer.store_int(dc_id.get_raw_id());
    CHECK(ip_address.is_valid());
    storer.store_string(ip_address.get_ip_str());
    storer.store_int(ip_address.get_port());
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    flags = parser.fetch_int();
    auto raw_dc_id = parser.fetch_int();
    if (flags & Flags::Cdn) {
      dc_id = DcId::external(raw_dc_id);
    } else {
      dc_id = DcId::internal(raw_dc_id);
    }
    auto ip = parser.template fetch_string<std::string>();
    auto port = parser.fetch_int();
    init_ip_address(ip, port);
  }

  friend bool operator==(const DcOption &lhs, const DcOption &rhs);

  friend StringBuilder &operator<<(StringBuilder &sb, const DcOption::PrintFlags &flags);

  friend StringBuilder &operator<<(StringBuilder &sb, const DcOption &dc_option);

 private:
  void init_ip_address(CSlice ip, int32 port) {
    if (is_ipv6()) {
      ip_address.init_ipv6_port(ip, port).ignore();
    } else {
      ip_address.init_ipv4_port(ip, port).ignore();
    }
  }
};

inline bool operator==(const DcOption &lhs, const DcOption &rhs) {
  return lhs.dc_id == rhs.dc_id && lhs.ip_address == rhs.ip_address && lhs.flags == rhs.flags;
}

inline StringBuilder &operator<<(StringBuilder &sb, const DcOption::PrintFlags &flags) {
  if ((flags.flags & DcOption::Flags::ObfuscatedTcpOnly) != 0) {
    sb << "(ObfuscatedTcpOnly)";
  }
  if ((flags.flags & DcOption::Flags::MediaOnly) != 0) {
    sb << "(MediaOnly)";
  }
  if ((flags.flags & DcOption::Flags::IPv6) != 0) {
    sb << "(IPv6)";
  }
  if ((flags.flags & DcOption::Flags::Cdn) != 0) {
    sb << "(Cdn)";
  }
  if ((flags.flags & DcOption::Flags::Static) != 0) {
    sb << "(Static)";
  }
  return sb;
}

inline StringBuilder &operator<<(StringBuilder &sb, const DcOption &dc_option) {
  return sb << tag("DcOption", format::concat(dc_option.dc_id, tag("ip", dc_option.ip_address.get_ip_str()),
                                              tag("port", dc_option.ip_address.get_port()),
                                              tag("flags", DcOption::PrintFlags{dc_option.flags})));
}

class DcOptions {
 public:
  DcOptions() = default;
  explicit DcOptions(const std::vector<tl_object_ptr<telegram_api::dcOption>> &server_dc_options) {
    for (auto &dc_option : server_dc_options) {
      DcOption option(*dc_option);
      if (option.is_valid()) {
        dc_options.push_back(std::move(option));
      }
    }
  }
  explicit DcOptions(const telegram_api::help_configSimple &config_simple) {
    auto dc_id = DcId::is_valid(config_simple.dc_id_) ? DcId::internal(config_simple.dc_id_) : DcId();
    for (auto &ip_port : config_simple.ip_port_list_) {
      DcOption option(dc_id, *ip_port);
      if (option.is_valid()) {
        dc_options.push_back(std::move(option));
      }
    }
  }
  template <class StorerT>
  void store(StorerT &storer) const {
    ::td::store(dc_options, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    ::td::parse(dc_options, parser);
  }

  std::vector<DcOption> dc_options;
};
inline StringBuilder &operator<<(StringBuilder &sb, const DcOptions &dc_options) {
  return sb << "DcOptions" << format::as_array(dc_options.dc_options);
}
};  // namespace td

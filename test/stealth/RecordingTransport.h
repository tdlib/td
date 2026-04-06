//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/IStreamTransport.h"

#include <vector>

namespace td {
namespace mtproto {
namespace test {

class RecordingTransport final : public IStreamTransport {
 public:
  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) final {
    read_next_calls++;
    *quick_ack = last_quick_ack;
    if (next_read_message.empty()) {
      message->clear();
      return 0;
    }
    *message = next_read_message.clone();
    return next_read_message.size();
  }

  bool support_quick_ack() const final {
    return support_quick_ack_result;
  }

  void write(BufferWriter &&message, bool quick_ack) final {
    write_calls++;
    queued_hints.push_back(last_hint);
    written_quick_acks.push_back(quick_ack);
    written_payloads.push_back(message.as_buffer_slice().as_slice().str());
  }

  bool can_read() const final {
    return can_read_result;
  }

  bool can_write() const final {
    return can_write_result;
  }

  void init(ChainBufferReader *input, ChainBufferWriter *output) final {
    init_calls++;
    input_ = input;
    output_ = output;
  }

  size_t max_prepend_size() const final {
    return max_prepend_size_result;
  }

  size_t max_append_size() const final {
    return max_append_size_result;
  }

  TransportType get_type() const final {
    return type;
  }

  bool use_random_padding() const final {
    return use_random_padding_result;
  }

  void set_traffic_hint(stealth::TrafficHint hint) final {
    last_hint = hint;
  }

  void set_max_tls_record_size(int32 size) final {
    max_tls_record_sizes.push_back(size);
  }

  bool supports_tls_record_sizing() const final {
    return supports_tls_record_sizing_result;
  }

  bool support_quick_ack_result{true};
  bool can_read_result{true};
  bool can_write_result{true};
  bool use_random_padding_result{false};
  bool supports_tls_record_sizing_result{true};
  size_t max_prepend_size_result{17};
  size_t max_append_size_result{9};
  uint32 last_quick_ack{0};
  BufferSlice next_read_message{"read-path"};
  TransportType type{TransportType::ObfuscatedTcp, 0, ProxySecret()};
  ChainBufferReader *input_{nullptr};
  ChainBufferWriter *output_{nullptr};
  int read_next_calls{0};
  int init_calls{0};
  int write_calls{0};
  stealth::TrafficHint last_hint{stealth::TrafficHint::Unknown};
  std::vector<stealth::TrafficHint> queued_hints;
  std::vector<bool> written_quick_acks;
  std::vector<string> written_payloads;
  std::vector<int32> max_tls_record_sizes;
};

}  // namespace test
}  // namespace mtproto
}  // namespace td
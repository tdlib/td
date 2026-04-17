// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/TlsReaderByteFlow.h"
#include "td/mtproto/TransportType.h"

#include "td/utils/AesCtrByteFlow.h"
#include "td/utils/buffer.h"
#include "td/utils/ByteFlow.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/Status.h"
#include "td/utils/UInt.h"

namespace td {
namespace mtproto {
namespace tcp {

class IntermediateTransport {
 public:
  explicit IntermediateTransport(bool with_padding) : with_padding_(with_padding) {
  }

  // Writes a packet into message.
  // Returns 0 if everything is ok, and [expected_size] otherwise.
  // There is no sense to call this function when [stream->size > expected_size]
  //
  // (TCP is a stream-oriented protocol, so the input message is a stream, not a slice)
  size_t read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack);

  // Writes header inplace.
  void write_prepare_inplace(BufferWriter *message, bool quick_ack);

  void set_stealth_target_frame_size(size_t target) {
    stealth_target_frame_size_ = target;
  }

  // Writes first several bytes into output stream.
  void init_output_stream(ChainBufferWriter *stream);

  bool with_padding() const {
    return with_padding_;
  }

 private:
  bool with_padding_;
  size_t stealth_target_frame_size_{0};
};

class OldTransport final : public IStreamTransport {
 public:
  OldTransport() = default;
  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) final TD_WARN_UNUSED_RESULT {
    return impl_.read_from_stream(input_, message, quick_ack);
  }
  bool support_quick_ack() const final {
    return true;
  }
  void write(BufferWriter &&message, bool quick_ack) final {
    impl_.write_prepare_inplace(&message, quick_ack);
    output_->append(message.as_buffer_slice());
  }
  void init(ChainBufferReader *input, ChainBufferWriter *output) final {
    input_ = input;
    output_ = output;
    impl_.init_output_stream(output_);
  }
  bool can_read() const final {
    return true;
  }
  bool can_write() const final {
    return true;
  }

  size_t max_prepend_size() const final {
    return 4;
  }

  size_t max_append_size() const final {
    return 15;
  }

  TransportType get_type() const final {
    return TransportType{TransportType::Tcp, 0, ProxySecret()};
  }

  bool use_random_padding() const final {
    return false;
  }

 private:
  IntermediateTransport impl_{false};
  ChainBufferReader *input_{nullptr};
  ChainBufferWriter *output_{nullptr};
};

class ObfuscatedTransport final : public IStreamTransport {
 public:
  ObfuscatedTransport(int16 dc_id, ProxySecret secret)
      : dc_id_(dc_id), secret_(std::move(secret)), impl_(secret_.use_random_padding()) {
  }

  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) final TD_WARN_UNUSED_RESULT;

  bool support_quick_ack() const final {
    return true;
  }

  void write(BufferWriter &&message, bool quick_ack) final;

  void init(ChainBufferReader *input, ChainBufferWriter *output) final;

  bool can_read() const final {
    return true;
  }

  bool can_write() const final {
    return true;
  }

  size_t max_prepend_size() const final {
    size_t res = 4;
    if (secret_.emulate_tls()) {
      res += 5;
      if (is_first_tls_packet_) {
        res += 6;
      }
    }
    res += header_.size();
    if (res & 3) {
      res += 4 - (res & 3);
    }
    return res;
  }

  size_t max_append_size() const final {
    if (!secret_.emulate_tls() || stealth_record_padding_target_ <= 0) {
      return 15;
    }
    return static_cast<size_t>(std::max<int32>(15, stealth_record_padding_target_ - 4));
  }

  TransportType get_type() const final {
    return TransportType{TransportType::ObfuscatedTcp, dc_id_, secret_};
  }

  bool use_random_padding() const final {
    return secret_.use_random_padding();
  }

  void set_max_tls_record_size(int32 size) final {
    max_tls_packet_length_ = std::max<int32>(256, std::min<int32>(size, 16384));
  }

  void set_stealth_record_padding_target(int32 target_bytes) final {
    stealth_record_padding_target_ = std::max<int32>(0, std::min<int32>(target_bytes, max_tls_packet_length_));
    impl_.set_stealth_target_frame_size(static_cast<size_t>(stealth_record_padding_target_));
  }

  bool supports_tls_record_sizing() const final {
    return secret_.emulate_tls();
  }

  int32 tls_record_sizing_payload_overhead() const final {
    if (!secret_.emulate_tls()) {
      return 0;
    }
    auto overhead = static_cast<int32>(header_.size());
    if (is_first_tls_packet_) {
      overhead += 6;
    }
    return overhead;
  }

#if TD_HAVE_OPENSSL
  AesCtrState clone_input_cipher_state_for_tests() const {
    AesCtrState state;
    state.init(as_slice(input_key_), as_slice(input_iv_));
    return state;
  }
#endif

 private:
  int16 dc_id_;
  bool is_first_tls_packet_{true};
  ProxySecret secret_;
  std::string header_;
  IntermediateTransport impl_;
  TlsReaderByteFlow tls_reader_byte_flow_;
  AesCtrByteFlow aes_ctr_byte_flow_;
  ByteFlowSink byte_flow_sink_;
  ChainBufferReader *input_ = nullptr;
  UInt256 input_key_{};
  UInt128 input_iv_{};

  static constexpr int32 MAX_TLS_PACKET_LENGTH = 2878;
  int32 max_tls_packet_length_{MAX_TLS_PACKET_LENGTH};
  int32 stealth_record_padding_target_{0};

  // TODO: use ByteFlow?
  // One problem is that BufferedFd owns output_buffer_
  // The other problem is that first 56 bytes must be sent unencrypted.
  UInt256 output_key_{};
  AesCtrState output_state_;
  ChainBufferWriter *output_ = nullptr;

  void do_write_tls(BufferWriter &&message);
  void do_write_tls(BufferBuilder &&builder);
  void do_write_main(BufferWriter &&message);
  void do_write(BufferSlice &&message);
};

using Transport = ObfuscatedTransport;

}  // namespace tcp
}  // namespace mtproto
}  // namespace td

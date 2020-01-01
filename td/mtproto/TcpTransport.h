//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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

class ITransport {
  // Writes packet into message.
  // Returns 0 if everything is ok, and [expected_size] otherwise.
  // There is no sense to call this function when [stream->size > expected_size]
  //
  // (tpc is stream-base protocol. So the input message is a stream, not a slice)
  virtual size_t read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack) = 0;

  // Writes header inplace.
  virtual void write_prepare_inplace(BufferWriter *message, bool quick_ack) = 0;

  // Writes first several bytes into output stream.
  virtual void init_output_stream(ChainBufferWriter *stream) = 0;

  virtual bool support_quick_ack() const = 0;

 public:
  ITransport() = default;
  ITransport(const ITransport &) = delete;
  ITransport &operator=(const ITransport &) = delete;
  ITransport(ITransport &&) = delete;
  ITransport &operator=(ITransport &&) = delete;
  virtual ~ITransport() = default;
};

class AbridgedTransport : public ITransport {
 public:
  size_t read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack) override;
  void write_prepare_inplace(BufferWriter *message, bool quick_ack) override;
  void init_output_stream(ChainBufferWriter *stream) override;
  bool support_quick_ack() const override {
    return false;
  }
};

class IntermediateTransport : ITransport {
 public:
  explicit IntermediateTransport(bool with_padding) : with_padding_(with_padding) {
  }
  size_t read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack) override;
  void write_prepare_inplace(BufferWriter *message, bool quick_ack) override;
  void init_output_stream(ChainBufferWriter *stream) override;
  bool support_quick_ack() const override {
    return true;
  }
  bool with_padding() const {
    return with_padding_;
  }

 private:
  bool with_padding_;
};

using TransportImpl = IntermediateTransport;

class OldTransport : public IStreamTransport {
 public:
  OldTransport() = default;
  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) override TD_WARN_UNUSED_RESULT {
    return impl_.read_from_stream(input_, message, quick_ack);
  }
  bool support_quick_ack() const override {
    return impl_.support_quick_ack();
  }
  void write(BufferWriter &&message, bool quick_ack) override {
    impl_.write_prepare_inplace(&message, quick_ack);
    output_->append(message.as_buffer_slice());
  }
  void init(ChainBufferReader *input, ChainBufferWriter *output) override {
    input_ = input;
    output_ = output;
    impl_.init_output_stream(output_);
  }
  bool can_read() const override {
    return true;
  }
  bool can_write() const override {
    return true;
  }

  size_t max_prepend_size() const override {
    return 4;
  }

  size_t max_append_size() const override {
    return 15;
  }

  TransportType get_type() const override {
    return TransportType{TransportType::Tcp, 0, ProxySecret()};
  }

  bool use_random_padding() const override {
    return false;
  }

 private:
  TransportImpl impl_{false};
  ChainBufferReader *input_;
  ChainBufferWriter *output_;
};

class ObfuscatedTransport : public IStreamTransport {
 public:
  ObfuscatedTransport(int16 dc_id, const ProxySecret &secret)
      : dc_id_(dc_id), secret_(secret), impl_(secret_.use_random_padding()) {
  }

  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) override TD_WARN_UNUSED_RESULT;

  bool support_quick_ack() const override {
    return impl_.support_quick_ack();
  }

  void write(BufferWriter &&message, bool quick_ack) override;

  void init(ChainBufferReader *input, ChainBufferWriter *output) override;

  bool can_read() const override {
    return true;
  }

  bool can_write() const override {
    return true;
  }

  size_t max_prepend_size() const override {
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

  size_t max_append_size() const override {
    return 15;
  }

  TransportType get_type() const override {
    return TransportType{TransportType::ObfuscatedTcp, dc_id_, secret_};
  }
  bool use_random_padding() const override {
    return secret_.use_random_padding();
  }

 private:
  int16 dc_id_;
  bool is_first_tls_packet_{true};
  ProxySecret secret_;
  std::string header_;
  TransportImpl impl_;
  TlsReaderByteFlow tls_reader_byte_flow_;
  AesCtrByteFlow aes_ctr_byte_flow_;
  ByteFlowSink byte_flow_sink_;
  ChainBufferReader *input_ = nullptr;

  static constexpr int32 MAX_TLS_PACKET_LENGTH = 2878;

  // TODO: use ByteFlow?
  // One problem is that BufferedFd owns output_buffer_
  // The other problem is that first 56 bytes must be sent unencrypted.
  UInt256 output_key_;
  AesCtrState output_state_;
  ChainBufferWriter *output_;

  void do_write_tls(BufferWriter &&message);
  void do_write_tls(BufferBuilder &&builder);
  void do_write_main(BufferWriter &&message);
  void do_write(BufferSlice &&message);
};

using Transport = ObfuscatedTransport;

}  // namespace tcp
}  // namespace mtproto
}  // namespace td

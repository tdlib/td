//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/IStreamTransport.h"

#include "td/utils/AesCtrByteFlow.h"
#include "td/utils/buffer.h"
#include "td/utils/ByteFlow.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Status.h"

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
  size_t read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack) override;
  void write_prepare_inplace(BufferWriter *message, bool quick_ack) override;
  void init_output_stream(ChainBufferWriter *stream) override;
  bool support_quick_ack() const override {
    return true;
  }
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
  TransportType get_type() const override {
    return TransportType::Tcp;
  }

 private:
  TransportImpl impl_;
  ChainBufferReader *input_;
  ChainBufferWriter *output_;
};

class ObfuscatedTransport : public IStreamTransport {
 public:
  ObfuscatedTransport() = default;
  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) override TD_WARN_UNUSED_RESULT {
    aes_ctr_byte_flow_.wakeup();
    return impl_.read_from_stream(byte_flow_sink_.get_output(), message, quick_ack);
  }

  bool support_quick_ack() const override {
    return impl_.support_quick_ack();
  }

  void write(BufferWriter &&message, bool quick_ack) override {
    impl_.write_prepare_inplace(&message, quick_ack);
    auto slice = message.as_buffer_slice();
    output_state_.encrypt(slice.as_slice(), slice.as_slice());
    output_->append(std::move(slice));
  }

  void init(ChainBufferReader *input, ChainBufferWriter *output) override;

  bool can_read() const override {
    return true;
  }

  bool can_write() const override {
    return true;
  }

  size_t max_prepend_size() const override {
    return 4;
  }

  TransportType get_type() const override {
    return TransportType::ObfuscatedTcp;
  }

 private:
  TransportImpl impl_;
  AesCtrByteFlow aes_ctr_byte_flow_;
  ByteFlowSink byte_flow_sink_;
  ChainBufferReader *input_;

  // TODO: use ByteFlow?
  // One problem is that BufferedFd owns output_buffer_
  // The other problem is that first 56 bytes must be sent unencrypted.
  UInt256 output_key_;
  AesCtrState output_state_;
  ChainBufferWriter *output_;
};

using Transport = ObfuscatedTransport;

}  // namespace tcp
}  // namespace mtproto
}  // namespace td

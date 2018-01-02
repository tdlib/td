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
#include "td/utils/logging.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <algorithm>

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

  void init(ChainBufferReader *input, ChainBufferWriter *output) override {
    input_ = input;
    output_ = output;

    const size_t header_size = 64;
    string header(header_size, '\0');
    MutableSlice header_slice = header;
    int32 try_cnt = 0;
    while (true) {
      try_cnt++;
      CHECK(try_cnt < 10);
      Random::secure_bytes(header_slice.ubegin(), header.size());
      if (as<uint8>(header.data()) == 0xef) {
        continue;
      }
      auto first_int = as<uint32>(header.data());
      if (first_int == 0x44414548 || first_int == 0x54534f50 || first_int == 0x20544547 || first_int == 0x4954504f ||
          first_int == 0xeeeeeeee) {
        continue;
      }
      auto second_int = as<uint32>(header.data() + sizeof(uint32));
      if (second_int == 0) {
        continue;
      }
      break;
    }
    // TODO: It is actually IntermediateTransport::init_output_stream, so it will work only with
    // TransportImpl==IntermediateTransport
    as<uint32>(header_slice.begin() + 56) = 0xeeeeeeee;

    string rheader = header;
    std::reverse(rheader.begin(), rheader.end());
    aes_ctr_byte_flow_.init(as<UInt256>(rheader.data() + 8), as<UInt128>(rheader.data() + 8 + 32));
    aes_ctr_byte_flow_.set_input(input_);
    aes_ctr_byte_flow_ >> byte_flow_sink_;

    output_key_ = as<UInt256>(header.data() + 8);
    output_state_.init(output_key_, as<UInt128>(header.data() + 8 + 32));
    output_->append(header_slice.substr(0, 56));
    output_state_.encrypt(header_slice, header_slice);
    output_->append(header_slice.substr(56, 8));
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

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/TcpTransport.h"

#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"

#include <algorithm>

namespace td {
namespace mtproto {
namespace tcp {
size_t IntermediateTransport::read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack) {
  CHECK(message);
  size_t stream_size = stream->size();
  size_t header_size = 4;
  if (stream->size() < header_size) {
    return header_size;
  }
  uint32 data_size;
  auto it = stream->clone();
  it.advance(header_size, MutableSlice(reinterpret_cast<uint8 *>(&data_size), sizeof(data_size)));
  if (data_size & (1u << 31)) {
    if (quick_ack) {
      *quick_ack = data_size;
    }
    stream->advance(header_size);
    return 0;
  }

  size_t total_size = data_size + header_size;
  if (stream_size < total_size) {
    // optimization
    // stream->make_solid(total_size);
    return total_size;
  }

  stream->advance(header_size);
  *message = stream->cut_head(data_size).move_as_buffer_slice();
  return 0;
}

void IntermediateTransport::write_prepare_inplace(BufferWriter *message, bool quick_ack) {
  size_t size = message->size();
  CHECK(size % 4 == 0);
  CHECK(size < (1 << 24));
  if (quick_ack) {
    size |= static_cast<size_t>(1) << 31;
  }

  size_t prepend_size = 4;
  MutableSlice prepend = message->prepare_prepend();
  CHECK(prepend.size() >= prepend_size);
  message->confirm_prepend(prepend_size);

  size_t append_size = 0;
  if (with_padding()) {
    append_size = static_cast<uint32>(Random::secure_int32()) % 16;
    MutableSlice append = message->prepare_append().truncate(append_size);
    CHECK(append.size() == append_size);
    Random::secure_bytes(append);
    message->confirm_append(append.size());
  }

  as<uint32>(message->as_slice().begin()) = static_cast<uint32>(size + append_size);
}

void IntermediateTransport::init_output_stream(ChainBufferWriter *stream) {
  const uint32 magic = with_padding() ? 0xdddddddd : 0xeeeeeeee;
  stream->append(Slice(reinterpret_cast<const char *>(&magic), 4));
}

size_t AbridgedTransport::read_from_stream(ChainBufferReader *stream, BufferSlice *message, uint32 *quick_ack) {
  if (stream->empty()) {
    return 1;
  }
  uint8 byte = 0;
  stream->clone().advance(1, MutableSlice(&byte, 1));
  size_t header_size;
  uint32 data_size;
  if (byte < 0x7f) {
    header_size = 1;
    data_size = byte * 4u;
  } else {
    if (stream->size() < 4) {
      return 4;
    }
    header_size = 4;
    stream->clone().advance(4, MutableSlice(reinterpret_cast<char *>(&data_size), sizeof(data_size)));
    data_size >>= 8;
    data_size = data_size * 4;
  }

  size_t total_size = header_size + data_size;
  if (stream->size() < total_size) {
    // optimization
    // stream->make_solid(total_size);
    return total_size;
  }

  stream->advance(header_size);
  *message = stream->cut_head(data_size).move_as_buffer_slice();
  return 0;
}

void AbridgedTransport::write_prepare_inplace(BufferWriter *message, bool quick_ack) {
  CHECK(!quick_ack);
  size_t size = message->size() / 4;
  CHECK(size % 4 == 0);
  CHECK(size < 1 << 24);

  size_t prepend_size = size >= 0x7f ? 4 : 1;

  MutableSlice prepend = message->prepare_prepend();
  CHECK(prepend.size() >= prepend_size);
  message->confirm_prepend(prepend_size);

  MutableSlice data = message->as_slice();
  if (size >= 0x7f) {
    uint32 size_encoded = 0x7f + (static_cast<uint32>(size) << 8);
    as<uint32>(data.begin()) = size_encoded;
  } else {
    as<uint8>(data.begin()) = static_cast<uint8>(size);
  }
}

void AbridgedTransport::init_output_stream(ChainBufferWriter *stream) {
  const uint8 magic = 0xef;
  stream->append(Slice(&magic, 1));
}

void ObfuscatedTransport::init(ChainBufferReader *input, ChainBufferWriter *output) {
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
        first_int == 0xdddddddd || first_int == 0xeeeeeeee) {
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
  as<uint32>(header_slice.begin() + 56) = impl_.with_padding() ? 0xdddddddd : 0xeeeeeeee;
  if (dc_id_ != 0) {
    as<int16>(header_slice.begin() + 60) = dc_id_;
  }

  string rheader = header;
  std::reverse(rheader.begin(), rheader.end());
  auto key = as<UInt256>(rheader.data() + 8);
  if (secret_.size() == 17) {
    secret_ = secret_.substr(1);
  }
  auto fix_key = [&](UInt256 &key) {
    if (secret_.size() == 16) {
      Sha256State state;
      sha256_init(&state);
      sha256_update(as_slice(key), &state);
      sha256_update(secret_, &state);
      sha256_final(&state, as_slice(key));
    }
  };
  fix_key(key);
  aes_ctr_byte_flow_.init(key, as<UInt128>(rheader.data() + 8 + 32));
  aes_ctr_byte_flow_.set_input(input_);
  aes_ctr_byte_flow_ >> byte_flow_sink_;

  output_key_ = as<UInt256>(header.data() + 8);
  fix_key(output_key_);
  output_state_.init(output_key_, as<UInt128>(header.data() + 8 + 32));
  output_->append(header_slice.substr(0, 56));
  output_state_.encrypt(header_slice, header_slice);
  output_->append(header_slice.substr(56, 8));
}

}  // namespace tcp
}  // namespace mtproto
}  // namespace td

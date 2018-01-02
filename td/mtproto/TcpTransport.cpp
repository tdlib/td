//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/TcpTransport.h"

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

  as<uint32>(message->as_slice().begin()) = static_cast<uint32>(size);
}

void IntermediateTransport::init_output_stream(ChainBufferWriter *stream) {
  const uint32 magic = 0xeeeeeeee;
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
}  // namespace tcp
}  // namespace mtproto
}  // namespace td

// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/TlsReaderByteFlow.h"

#include "td/utils/ByteFlow.h"
#include "td/utils/Status.h"
#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

TEST(TlsReaderByteFlowAdversarial, OversizedRecordLengthFailsClosedImmediately) {
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  td::ByteFlowSource source(&input);
  td::mtproto::TlsReaderByteFlow reader;
  td::ByteFlowSink sink;
  source >> reader >> sink;

  input_writer.append(td::BufferSlice("\x17\x03\x03\xff\xff", 5));
  source.wakeup();

  ASSERT_TRUE(sink.is_ready());
  ASSERT_TRUE(sink.status().is_error());
}

TEST(TlsReaderByteFlowAdversarial, ValidApplicationDataRecordStillPassesThrough) {
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  td::ByteFlowSource source(&input);
  td::mtproto::TlsReaderByteFlow reader;
  td::ByteFlowSink sink;
  source >> reader >> sink;

  input_writer.append(td::BufferSlice("\x17\x03\x03\x00\x04test", 9));
  source.wakeup();

  sink.get_output()->sync_with_writer();
  ASSERT_EQ("test", sink.get_output()->move_as_buffer_slice().as_slice().str());
}

}  // namespace

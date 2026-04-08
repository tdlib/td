//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/TransportType.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {

class IStreamTransport {
 public:
  IStreamTransport() = default;
  IStreamTransport(const IStreamTransport &) = delete;
  IStreamTransport &operator=(const IStreamTransport &) = delete;
  virtual ~IStreamTransport() = default;
  virtual Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) = 0;
  virtual bool support_quick_ack() const = 0;
  virtual void write(BufferWriter &&message, bool quick_ack) = 0;
  virtual bool can_read() const = 0;
  virtual bool can_write() const = 0;
  virtual void init(ChainBufferReader *input, ChainBufferWriter *output) = 0;
  virtual size_t max_prepend_size() const = 0;
  virtual size_t max_append_size() const = 0;
  virtual TransportType get_type() const = 0;
  virtual bool use_random_padding() const = 0;
  virtual void pre_flush_write(double now) {
  }
  virtual double get_shaping_wakeup() const {
    return 0.0;
  }
  virtual void set_traffic_hint(stealth::TrafficHint hint) {
  }
  virtual void set_max_tls_record_size(int32 size) {
  }
  virtual bool supports_tls_record_sizing() const {
    return false;
  }
  virtual int32 tls_record_sizing_payload_overhead() const {
    return 0;
  }
  virtual size_t traffic_bulk_threshold_bytes() const {
    return 8192;
  }
};

using StreamTransportFactoryForTests = unique_ptr<IStreamTransport> (*)(TransportType type);

unique_ptr<IStreamTransport> create_transport(TransportType type);
StreamTransportFactoryForTests set_transport_factory_for_tests(StreamTransportFactoryForTests factory);

}  // namespace mtproto
}  // namespace td

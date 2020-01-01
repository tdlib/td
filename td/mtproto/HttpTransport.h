//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/TransportType.h"

#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"

#include "td/utils/buffer.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {
namespace http {

class Transport : public IStreamTransport {
 public:
  explicit Transport(string secret) : secret_(std::move(secret)) {
  }

  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) override TD_WARN_UNUSED_RESULT;
  bool support_quick_ack() const override {
    return false;
  }
  void write(BufferWriter &&message, bool quick_ack) override;
  bool can_read() const override;
  bool can_write() const override;
  void init(ChainBufferReader *input, ChainBufferWriter *output) override {
    reader_.init(input);
    output_ = output;
  }

  size_t max_prepend_size() const override;
  size_t max_append_size() const override;
  TransportType get_type() const override {
    return {TransportType::Http, 0, ProxySecret::from_raw(secret_)};
  }
  bool use_random_padding() const override;

 private:
  string secret_;
  HttpReader reader_;
  HttpQuery http_query_;
  ChainBufferWriter *output_;
  enum { Write, Read } turn_ = Write;
};

}  // namespace http
}  // namespace mtproto
}  // namespace td

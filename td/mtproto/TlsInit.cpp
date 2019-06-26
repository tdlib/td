//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/TlsInit.h"

#include "td/utils/as.h"
#include "td/utils/crypto.h"
#include "td/utils/Random.h"
#include "td/utils/Span.h"

namespace td {
void Grease::init(MutableSlice res) {
  Random::secure_bytes(res);
  for (auto &c : res) {
    c = (c & 0xF0) + 0x0A;
  }
  for (size_t i = 1; i < res.size(); i += 2) {
    if (res[i] == res[i - 1]) {
      res[i] ^= 0x10;
    }
  }
}

class TlsHello {
 public:
  struct Op {
    enum class Type { String, Random, Zero, Domain, Grease, BeginScope, EndScope };
    Type type;
    int length;
    int seed;
    std::string data;

    static Op string(Slice str) {
      Op res;
      res.type = Type::String;
      res.data = str.str();
      return res;
    }
    static Op random(int length) {
      Op res;
      res.type = Type::Random;
      res.length = length;
      return res;
    }
    static Op zero(int length) {
      Op res;
      res.type = Type::Zero;
      res.length = length;
      return res;
    }
    static Op domain() {
      Op res;
      res.type = Type::Domain;
      return res;
    }
    static Op grease(int seed) {
      Op res;
      res.type = Type::Grease;
      res.seed = seed;
      return res;
    }
    static Op begin_scope() {
      Op res;
      res.type = Type::BeginScope;
      return res;
    }
    static Op end_scope() {
      Op res;
      res.type = Type::EndScope;
      return res;
    }
  };

  static const TlsHello &get_default() {
    static TlsHello result = [] {
      TlsHello res;
      res.ops_ = {
          Op::string("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"),
          Op::zero(32),
          Op::string("\x20"),
          Op::random(32),
          Op::string("\x00\x22"),
          Op::grease(0),
          Op::string("\x13\x01\x13\x02\x13\x03\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30\xcc\xa9\xcc\xa8\xc0\x13\xc0\x14\x00\x9c"
                     "\x00\x9d\x00\x2f\x00\x35\x00\x0a\x01\x00\x01\x91"),
          Op::grease(2),
          Op::string("\x00\x00\x00\x00"),
          Op::begin_scope(),
          Op::begin_scope(),
          Op::string("\x00"),
          Op::begin_scope(),
          Op::domain(),
          Op::end_scope(),
          Op::end_scope(),
          Op::end_scope(),
          Op::string("\x00\x17\x00\x00\xff\x01\x00\x01\x00\x00\x0a\x00\x0a\x00\x08"),
          Op::grease(4),
          Op::string(
              "\x00\x1d\x00\x17\x00\x18\x00\x0b\x00\x02\x01\x00\x00\x23\x00\x00\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08"
              "\x68\x74\x74\x70\x2f\x31\x2e\x31\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x0d\x00\x14\x00\x12\x04\x03\x08"
              "\x04\x04\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06\x01\x02\x01\x00\x12\x00\x00\x00\x33\x00\x2b\x00\x29"),
          Op::grease(4),
          Op::string("\x00\x01\x00\x00\x1d\x00\x20"),
          Op::random(32),
          Op::string("\x00\x2d\x00\x02\x01\x01\x00\x2b\x00\x0b\x0a"),
          Op::grease(6),
          Op::string("\x03\x04\x03\x03\x03\x02\x03\x01\x00\x1b\x00\x03\x02\x00\x02"),
          Op::grease(3),
          Op::string("\x00\x01\x00\x00\x15")};
      return res;
    }();
    return result;
  }
  Span<Op> get_ops() const {
    return ops_;
  }

 private:
  std::vector<Op> ops_;
};

class TlsHelloContext {
 public:
  explicit TlsHelloContext(std::string domain) {
    Grease::init(MutableSlice(grease_.data(), grease_.size()));
    domain_ = std::move(domain);
  }
  char get_grease(size_t i) const {
    CHECK(i < grease_.size());
    return grease_[i];
  }
  size_t grease_size() const {
    return grease_.size();
  }
  Slice get_domain() const {
    return domain_;
  }

 private:
  constexpr static size_t MAX_GREASE = 8;
  std::array<char, MAX_GREASE> grease_;
  std::string domain_;
};

class TlsHelloCalcLength {
 public:
  void do_op(const TlsHello::Op &op, const TlsHelloContext *context) {
    if (status_.is_error()) {
      return;
    }
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        size_ += op.data.size();
        break;
      case Type::Random:
        if (op.length <= 0 || op.length > 1024) {
          return on_error(Status::Error("Invalid random length"));
        }
        size_ += op.length;
        break;
      case Type::Zero:
        if (op.length <= 0 || op.length > 1024) {
          return on_error(Status::Error("Invalid zero length"));
        }
        size_ += op.length;
        break;
      case Type::Domain:
        CHECK(context);
        size_ += context->get_domain().size();
        break;
      case Type::Grease:
        CHECK(context);
        if (op.seed < 0 || static_cast<size_t>(op.seed) >= context->grease_size()) {
          return on_error(Status::Error("Invalid grease seed"));
        }
        size_ += 2;
        break;
      case Type::BeginScope:
        size_ += 2;
        scope_offset_.push_back(size_);
        break;
      case Type::EndScope: {
        if (scope_offset_.empty()) {
          return on_error(Status::Error("Unbalanced scopes"));
        }
        auto begin_offset = scope_offset_.back();
        scope_offset_.pop_back();
        auto end_offset = size_;
        auto size = end_offset - begin_offset;
        if (size >= (1 << 14)) {
          return on_error(Status::Error("Scope is too big"));
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  Result<size_t> finish() {
    if (size_ > 515) {
      on_error(Status::Error("Too long for zero padding"));
    }
    if (size_ < 11 + 32) {
      on_error(Status::Error("Too small for hash"));
    }
    int zero_pad = 515 - static_cast<int>(size_);
    using Op = TlsHello::Op;
    do_op(Op::begin_scope(), nullptr);
    do_op(Op::zero(zero_pad), nullptr);
    do_op(Op::end_scope(), nullptr);
    if (!scope_offset_.empty()) {
      on_error(Status::Error("Unbalanced scopes"));
    }
    TRY_STATUS(std::move(status_));
    return size_;
  }

 private:
  size_t size_{0};
  Status status_;
  std::vector<size_t> scope_offset_;

  void on_error(Status error) {
    if (status_.is_ok()) {
      status_ = std::move(error);
    }
  }
};

class TlsHelloStore {
 public:
  TlsHelloStore(MutableSlice dest) : data_(dest), dest_(dest) {
  }
  void do_op(const TlsHello::Op &op, TlsHelloContext *context) {
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        dest_.copy_from(op.data);
        dest_.remove_prefix(op.data.size());
        break;
      case Type::Random:
        Random::secure_bytes(dest_.substr(0, op.length));
        dest_.remove_prefix(op.length);
        break;
      case Type::Zero:
        std::memset(dest_.begin(), 0, op.length);
        dest_.remove_prefix(op.length);
        break;
      case Type::Domain: {
        CHECK(context);
        auto domain = context->get_domain();
        dest_.copy_from(domain);
        dest_.remove_prefix(domain.size());
        break;
      }
      case Type::Grease: {
        CHECK(context)
        auto grease = context->get_grease(op.seed);
        dest_[0] = grease;
        dest_[1] = grease;
        dest_.remove_prefix(2);
        break;
      }
      case Type::BeginScope:
        scope_offset_.push_back(get_offset());
        dest_.remove_prefix(2);
        break;
      case Type::EndScope: {
        CHECK(!scope_offset_.empty());
        auto begin_offset = scope_offset_.back();
        scope_offset_.pop_back();
        auto end_offset = get_offset();
        size_t size = end_offset - begin_offset - 2;
        CHECK(size < (1 << 14));
        data_[begin_offset] = static_cast<char>((size >> 8) & 0xff);
        data_[begin_offset + 1] = static_cast<char>(size & 0xff);
        break;
      }
    }
  }

  void finish(int32 unix_time) {
    int zero_pad = 515 - static_cast<int>(get_offset());
    using Op = TlsHello::Op;
    do_op(Op::begin_scope(), nullptr);
    do_op(Op::zero(zero_pad), nullptr);
    do_op(Op::end_scope(), nullptr);

    auto tmp = sha256(data_);
    auto hash_dest = data_.substr(11);
    hash_dest.copy_from(tmp);
    int32 old = as<int32>(hash_dest.substr(28).data());
    as<int32>(hash_dest.substr(28).data()) = old ^ unix_time;
    CHECK(dest_.empty());
  }

 private:
  MutableSlice data_;
  MutableSlice dest_;
  std::vector<size_t> scope_offset_;
  size_t get_offset() {
    return data_.size() - dest_.size();
  }
};

class TlsObfusaction {
 public:
  static std::string generate_header(std::string domain, int32 unix_time) {
    auto &hello = TlsHello::get_default();
    TlsHelloContext context(domain);
    TlsHelloCalcLength calc_length;
    for (auto &op : hello.get_ops()) {
      calc_length.do_op(op, &context);
    }
    auto length = calc_length.finish().move_as_ok();
    std::string data(length, 0);
    TlsHelloStore storer(data);
    for (auto &op : hello.get_ops()) {
      storer.do_op(op, &context);
    }
    storer.finish(0);
    return data;
  }
};

void TlsInit::send_hello() {
  auto hello = TlsObfusaction::generate_header(username_, 0);
  fd_.output_buffer().append(hello);
  state_ = State::WaitHelloResponse;
}

Status TlsInit::wait_hello_response() {
  //[
  auto it = fd_.input_buffer().clone();
  //S "\x16\x03\x03"
  {
    Slice first = "\x16\x03\x03";
    std::string got_first(first.size(), 0);
    if (it.size() < first.size()) {
      return td::Status::OK();
    }
    it.advance(first.size(), got_first);
    if (first != got_first) {
      return Status::Error("First part of response to hello is invalid");
    }
  }

  //[
  {
    if (it.size() < 2) {
      return td::Status::OK();
    }
    uint8 tmp[2];
    it.advance(2, MutableSlice(tmp, 2));
    size_t skip_size = (tmp[0] << 8) + tmp[1];
    if (it.size() < skip_size) {
      return td::Status::OK();
    }
    it.advance(skip_size);
  }

  //S "\x14\x03\x03\x00\x01\x01\x17\x03\x03"
  {
    Slice first = "\x14\x03\x03\x00\x01\x01\x17\x03\x03";
    std::string got_first(first.size(), 0);
    if (it.size() < first.size()) {
      return td::Status::OK();
    }
    it.advance(first.size(), got_first);
    if (first != got_first) {
      return Status::Error("Second part of response to hello is invalid");
    }
  }

  //[
  {
    if (it.size() < 2) {
      return td::Status::OK();
    }
    uint8 tmp[2];
    it.advance(2, MutableSlice(tmp, 2));
    size_t skip_size = (tmp[0] << 8) + tmp[1];
    if (it.size() < skip_size) {
      return td::Status::OK();
    }
    it.advance(skip_size);
  }
  fd_.input_buffer() = std::move(it);

  stop();
  return td::Status::OK();
}

Status TlsInit::loop_impl() {
  switch (state_) {
    case State::SendHello:
      send_hello();
      break;
    case State::WaitHelloResponse:
      TRY_STATUS(wait_hello_response());
      break;
  }
  return Status::OK();
}
}  // namespace td

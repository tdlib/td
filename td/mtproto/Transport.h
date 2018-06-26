//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/utils.h"

#include "td/utils/common.h"
#include "td/utils/int_types.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <tuple>

namespace td {
namespace mtproto {
class AuthKey;

#pragma pack(push, 4)
#if TD_MSVC
#pragma warning(push)
#pragma warning(disable : 4200)
#endif

struct CryptoHeader {
  uint64 auth_key_id;
  UInt128 message_key;

  // encrypted part
  uint64 salt;
  uint64 session_id;

  // It is weird to generate message_id and seq_no while writing a packet.
  //
  // uint64 message_id;
  // uint32 seq_no;
  // uint32 message_data_length;
  uint8 data[0];  // use compiler extension

  static size_t encrypted_header_size() {
    return sizeof(salt) + sizeof(session_id);
  }

  uint8 *encrypt_begin() {
    return reinterpret_cast<uint8 *>(&salt);
  }

  const uint8 *encrypt_begin() const {
    return reinterpret_cast<const uint8 *>(&salt);
  }

  CryptoHeader() = delete;
  CryptoHeader(const CryptoHeader &) = delete;
  CryptoHeader(CryptoHeader &&) = delete;
  CryptoHeader &operator=(const CryptoHeader &) = delete;
  CryptoHeader &operator=(CryptoHeader &&) = delete;
  ~CryptoHeader() = delete;
};

struct CryptoPrefix {
  uint64 message_id;
  uint32 seq_no;
  uint32 message_data_length;
};

struct EndToEndHeader {
  uint64 auth_key_id;
  UInt128 message_key;

  // encrypted part
  // uint32 message_data_length;
  uint8 data[0];  // use compiler extension

  static size_t encrypted_header_size() {
    return 0;
  }

  uint8 *encrypt_begin() {
    return reinterpret_cast<uint8 *>(&data);
  }

  const uint8 *encrypt_begin() const {
    return reinterpret_cast<const uint8 *>(&data);
  }

  EndToEndHeader() = delete;
  EndToEndHeader(const EndToEndHeader &) = delete;
  EndToEndHeader(EndToEndHeader &&) = delete;
  EndToEndHeader &operator=(const EndToEndHeader &) = delete;
  EndToEndHeader &operator=(EndToEndHeader &&) = delete;
  ~EndToEndHeader() = delete;
};

struct EndToEndPrefix {
  uint32 message_data_length;
};

struct NoCryptoHeader {
  uint64 auth_key_id;

  // message_id is removed from CryptoHeader. Should be removed from here too.
  //
  // int64 message_id;
  // uint32 message_data_length;
  uint8 data[0];  // use compiler extension

  NoCryptoHeader() = delete;
  NoCryptoHeader(const NoCryptoHeader &) = delete;
  NoCryptoHeader(NoCryptoHeader &&) = delete;
  NoCryptoHeader &operator=(const NoCryptoHeader &) = delete;
  NoCryptoHeader &operator=(NoCryptoHeader &&) = delete;
  ~NoCryptoHeader() = delete;
};

#if TD_MSVC
#pragma warning(pop)
#endif
#pragma pack(pop)

struct PacketInfo {
  enum { Common, EndToEnd } type = Common;
  uint64 auth_key_id;
  uint32 message_ack;
  UInt128 message_key;

  uint64 salt;
  uint64 session_id;

  uint64 message_id;
  int32 seq_no;
  int32 version = 1;
  bool no_crypto_flag;
  bool is_creator = false;
};

class Transport {
 public:
  class ReadResult {
   public:
    enum Type { Packet, Nop, Error, Quickack };

    static ReadResult make_nop() {
      return {};
    }
    static ReadResult make_error(int32 error_code) {
      ReadResult res;
      res.type_ = Error;
      res.error_code_ = error_code;
      return res;
    }
    static ReadResult make_packet(MutableSlice packet) {
      CHECK(!packet.empty());
      ReadResult res;
      res.type_ = Packet;
      res.packet_ = packet;
      return res;
    }
    static ReadResult make_quick_ack(uint32 quick_ack) {
      ReadResult res;
      res.type_ = Quickack;
      res.quick_ack_ = quick_ack;
      return res;
    }

    Type type() const {
      return type_;
    }

    MutableSlice packet() const {
      CHECK(type_ == Packet);
      return packet_;
    }
    uint32 quick_ack() const {
      CHECK(type_ == Quickack);
      return quick_ack_;
    }
    int32 error() const {
      CHECK(type_ == Error);
      return error_code_;
    }

   private:
    Type type_ = Nop;
    MutableSlice packet_;
    int32 error_code_;
    uint32 quick_ack_;
  };

  static Result<uint64> read_auth_key_id(Slice message);

  // Reads mtproto packet from [message] and saves into [data].
  // If message is encrypted, [auth_key] is used.
  // Decryption and unpacking is made inplace, so [data] will be subslice of [message].
  // Returns size of mtproto packet.
  // If dest.size() >= size, the packet is also written into [dest].
  // If auth_key is nonempty, encryption will be used.
  static Result<ReadResult> read(MutableSlice message, const AuthKey &auth_key, PacketInfo *info) TD_WARN_UNUSED_RESULT;

  static size_t write(const Storer &storer, const AuthKey &auth_key, PacketInfo *info,
                      MutableSlice dest = MutableSlice());

 private:
  template <class HeaderT>
  static std::tuple<uint32, UInt128> calc_message_ack_and_key(const HeaderT &head, size_t data_size);

  static std::tuple<uint32, UInt128> calc_message_key2(const AuthKey &auth_key, int X, Slice to_encrypt);

  template <class HeaderT>
  static size_t calc_crypto_size(size_t data_size);

  template <class HeaderT>
  static size_t calc_crypto_size2(size_t data_size);

  static size_t calc_no_crypto_size(size_t data_size);

  static Status read_no_crypto(MutableSlice message, PacketInfo *info, MutableSlice *data) TD_WARN_UNUSED_RESULT;

  static Status read_crypto(MutableSlice message, const AuthKey &auth_key, PacketInfo *info,
                            MutableSlice *data) TD_WARN_UNUSED_RESULT;
  static Status read_e2e_crypto(MutableSlice message, const AuthKey &auth_key, PacketInfo *info,
                                MutableSlice *data) TD_WARN_UNUSED_RESULT;
  template <class HeaderT, class PrefixT>
  static Status read_crypto_impl(int X, MutableSlice message, const AuthKey &auth_key, HeaderT **header_ptr,
                                 PrefixT **prefix_ptr, MutableSlice *data, PacketInfo *info) TD_WARN_UNUSED_RESULT;

  static size_t write_no_crypto(const Storer &storer, PacketInfo *info, MutableSlice dest);

  static size_t write_crypto(const Storer &storer, const AuthKey &auth_key, PacketInfo *info, MutableSlice dest);
  static size_t write_e2e_crypto(const Storer &storer, const AuthKey &auth_key, PacketInfo *info, MutableSlice dest);
  template <class HeaderT>
  static void write_crypto_impl(int X, const Storer &storer, const AuthKey &auth_key, PacketInfo *info, HeaderT *header,
                                size_t data_size);
};

}  // namespace mtproto
}  // namespace td

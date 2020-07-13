//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/RawConnection.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/Transport.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"
#include "td/utils/StorerBase.h"

#include <utility>

namespace td {
namespace mtproto {

void RawConnection::send_crypto(const Storer &storer, int64 session_id, int64 salt, const AuthKey &auth_key,
                                uint64 quick_ack_token) {
  PacketInfo info;
  info.version = 2;
  info.no_crypto_flag = false;
  info.salt = salt;
  info.session_id = session_id;
  info.use_random_padding = transport_->use_random_padding();

  auto packet = BufferWriter{Transport::write(storer, auth_key, &info), transport_->max_prepend_size(),
                             transport_->max_append_size()};
  Transport::write(storer, auth_key, &info, packet.as_slice());

  bool use_quick_ack = false;
  if (quick_ack_token != 0 && transport_->support_quick_ack()) {
    auto tmp = quick_ack_to_token_.emplace(info.message_ack, quick_ack_token);
    if (tmp.second) {
      use_quick_ack = true;
    } else {
      LOG(ERROR) << "Quick ack " << info.message_ack << " collision";
    }
  }

  transport_->write(std::move(packet), use_quick_ack);
}

uint64 RawConnection::send_no_crypto(const Storer &storer) {
  PacketInfo info;

  info.no_crypto_flag = true;
  auto packet = BufferWriter{Transport::write(storer, AuthKey(), &info), transport_->max_prepend_size(),
                             transport_->max_append_size()};
  Transport::write(storer, AuthKey(), &info, packet.as_slice());
  LOG(INFO) << "Send handshake packet: " << format::as_hex_dump<4>(packet.as_slice());
  transport_->write(std::move(packet), false);
  return info.message_id;
}

Status RawConnection::flush_read(const AuthKey &auth_key, Callback &callback) {
  auto r = socket_fd_.flush_read();
  if (r.is_ok()) {
    if (stats_callback_) {
      stats_callback_->on_read(r.ok());
    }
    callback.on_read(r.ok());
  }
  while (transport_->can_read()) {
    BufferSlice packet;
    uint32 quick_ack = 0;
    TRY_RESULT(wait_size, transport_->read_next(&packet, &quick_ack));
    if (!is_aligned_pointer<4>(packet.as_slice().ubegin())) {
      BufferSlice new_packet(packet.size());
      new_packet.as_slice().copy_from(packet.as_slice());
      packet = std::move(new_packet);
    }
    LOG_CHECK(is_aligned_pointer<4>(packet.as_slice().ubegin()))
        << packet.as_slice().ubegin() << ' ' << packet.size() << ' ' << wait_size;
    if (wait_size != 0) {
      constexpr size_t MAX_PACKET_SIZE = (1 << 22) + 1024;
      if (wait_size > MAX_PACKET_SIZE) {
        return Status::Error(PSLICE() << "Expected packet size is too big: " << wait_size);
      }
      break;
    }

    if (quick_ack != 0) {
      TRY_STATUS(on_quick_ack(quick_ack, callback));
      continue;
    }

    PacketInfo info;
    info.version = 2;

    TRY_RESULT(read_result, Transport::read(packet.as_slice(), auth_key, &info));
    switch (read_result.type()) {
      case Transport::ReadResult::Quickack: {
        TRY_STATUS(on_quick_ack(read_result.quick_ack(), callback));
        break;
      }
      case Transport::ReadResult::Error: {
        TRY_STATUS(on_read_mtproto_error(read_result.error()));
        break;
      }
      case Transport::ReadResult::Packet: {
        // If a packet was successfully decrypted, then it is ok to assume that the connection is alive
        if (!auth_key.empty()) {
          if (stats_callback_) {
            stats_callback_->on_pong();
          }
        }

        TRY_STATUS(callback.on_raw_packet(info, packet.from_slice(read_result.packet())));
        break;
      }
      case Transport::ReadResult::Nop:
        break;
      default:
        UNREACHABLE();
    }
  }

  TRY_STATUS(std::move(r));
  return Status::OK();
}

Status RawConnection::on_read_mtproto_error(int32 error_code) {
  if (error_code == -429) {
    if (stats_callback_) {
      stats_callback_->on_mtproto_error();
    }
    return Status::Error(500, PSLICE() << "MTProto error: " << error_code);
  }
  if (error_code == -404) {
    return Status::Error(-404, PSLICE() << "MTProto error: " << error_code);
  }
  return Status::Error(PSLICE() << "MTProto error: " << error_code);
}

Status RawConnection::on_quick_ack(uint32 quick_ack, Callback &callback) {
  auto it = quick_ack_to_token_.find(quick_ack);
  if (it == quick_ack_to_token_.end()) {
    LOG(WARNING) << Status::Error(PSLICE() << "Unknown quick_ack " << quick_ack);
    return Status::OK();
    // TODO: return Status::Error(PSLICE() << "Unknown quick_ack " << quick_ack);
  }
  auto token = it->second;
  quick_ack_to_token_.erase(it);
  callback.on_quick_ack(token).ignore();
  return Status::OK();
}

Status RawConnection::flush_write() {
  TRY_RESULT(size, socket_fd_.flush_write());
  if (size > 0 && stats_callback_) {
    stats_callback_->on_write(size);
  }
  return Status::OK();
}

}  // namespace mtproto
}  // namespace td

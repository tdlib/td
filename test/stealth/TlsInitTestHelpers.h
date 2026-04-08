// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/crypto.h"
#include "td/utils/port/SocketFd.h"

#if TD_PORT_POSIX
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace td {
namespace mtproto {
namespace test {

struct SocketPair final {
  SocketFd client;
  SocketFd peer;
};

inline Result<SocketPair> create_socket_pair() {
#if TD_PORT_POSIX
  int fds[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return OS_ERROR("Failed to create socketpair");
  }

  TRY_RESULT(client, SocketFd::from_native_fd(NativeFd(fds[0])));
  TRY_RESULT(peer, SocketFd::from_native_fd(NativeFd(fds[1])));
  return SocketPair{std::move(client), std::move(peer)};
#else
  return Status::Error("socketpair is unsupported on this platform");
#endif
}

inline Status write_all(SocketFd &socket_fd, Slice data) {
  while (!data.empty()) {
    TRY_RESULT(written, socket_fd.write(data));
    data.remove_prefix(written);
  }
  return Status::OK();
}

inline Result<string> read_exact(SocketFd &socket_fd, size_t size) {
  string result(size, '\0');
  MutableSlice dest(result);
  while (!dest.empty()) {
    TRY_RESULT(read_size, socket_fd.read(dest));
    if (read_size == 0) {
      return Status::Error("Unexpected EOF while reading socketpair data");
    }
    dest.remove_prefix(read_size);
  }
  return result;
}

inline void append_u16_be(string &out, uint16 value) {
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

inline string make_tls_init_response(Slice secret, Slice hello_rand, Slice first_prefix, Slice second_prefix,
                                     size_t first_payload_len = 40, size_t second_payload_len = 16) {
  string response;
  response.reserve(first_prefix.size() + second_prefix.size() + first_payload_len + second_payload_len + 4);
  response += first_prefix.str();
  append_u16_be(response, static_cast<uint16>(first_payload_len));
  response.append(first_payload_len, static_cast<char>(0x42));
  response += second_prefix.str();
  append_u16_be(response, static_cast<uint16>(second_payload_len));
  response.append(second_payload_len, static_cast<char>(0x24));

  CHECK(response.size() >= 43);

  string response_for_hmac = response;
  auto response_rand_slice = MutableSlice(response_for_hmac).substr(11, 32);
  std::fill(response_rand_slice.begin(), response_rand_slice.end(), '\0');

  string hash_dest(32, '\0');
  string hmac_input = hello_rand.str();
  hmac_input += response_for_hmac;
  hmac_sha256(secret, hmac_input, hash_dest);
  MutableSlice(response).substr(11, 32).copy_from(hash_dest);
  return response;
}

}  // namespace test
}  // namespace mtproto
}  // namespace td
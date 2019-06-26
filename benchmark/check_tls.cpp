//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/detail/Iocp.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/sleep.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

td::Status test_tls(const td::string &url) {
  td::IPAddress address;
  TRY_STATUS(address.init_host_port(url, 443));
  TRY_RESULT(socket, td::SocketFd::open(address));

  td::string request;

  auto add_string = [&](td::Slice data) { request.append(data.data(), data.size()); };
  auto add_random = [&](size_t length) {
    while (length-- > 0) {
      request += static_cast<char>(td::Random::secure_int32());
    }
  };
  auto add_length = [&](size_t length) {
    request += static_cast<char>(length / 256);
    request += static_cast<char>(length % 256);
  };

  const size_t MAX_GREASE = 7;
  char greases[MAX_GREASE];
  td::Random::secure_bytes(td::MutableSlice{greases, MAX_GREASE});
  for (size_t i = 0; i < MAX_GREASE; i++) {
    greases[i] = static_cast<char>((greases[i] & 0xF0) + 0x0A);
  }
  for (size_t i = 1; i < MAX_GREASE; i += 2) {
    if (greases[i] == greases[i - 1]) {
      greases[i] = static_cast<char>(0x10 ^ greases[i]);
    }
  }
  auto add_grease = [&](size_t num) {
    auto c = greases[num];
    request += c;
    request += c;
  };

  add_string("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03");
  add_random(32);
  add_string("\x20");
  add_random(32);
  add_string("\x00\x22");
  add_grease(0);
  add_string(
      "\x13\x01\x13\x02\x13\x03\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30\xcc\xa9\xcc\xa8\xc0\x13\xc0\x14\x00\x9c\x00\x9d\x00"
      "\x2f\x00\x35\x00\x0a\x01\x00\x01\x91");
  add_grease(2);
  add_string("\x00\x00\x00\x00");
  add_length(url.size() + 5);
  add_length(url.size() + 3);
  add_string("\x00");
  add_length(url.size());
  add_string(url);
  add_string("\x00\x17\x00\x00\xff\x01\x00\x01\x00\x00\x0a\x00\x0a\x00\x08");
  add_grease(4);
  add_string(
      "\x00\x1d\x00\x17\x00\x18\x00\x0b\x00\x02\x01\x00\x00\x23\x00\x00\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68"
      "\x74\x74\x70\x2f\x31\x2e\x31\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x0d\x00\x14\x00\x12\x04\x03\x08\x04\x04"
      "\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06\x01\x02\x01\x00\x12\x00\x00\x00\x33\x00\x2b\x00\x29");
  add_grease(4);
  add_string("\x00\x01\x00\x00\x1d\x00\x20");
  add_random(32);
  add_string("\x00\x2d\x00\x02\x01\x01\x00\x2b\x00\x0b\x0a");
  add_grease(6);
  add_string("\x03\x04\x03\x03\x03\x02\x03\x01\x00\x1b\x00\x03\x02\x00\x02");
  add_grease(3);
  add_string("\x00\x01\x00\x00\x15");
  auto padding = 515 - static_cast<int>(request.size());
  CHECK(padding >= 0);
  add_length(padding);
  request.resize(517);

  // LOG(ERROR) << td::format::as_hex_dump<0>(td::Slice(request));

  socket.write(request);

  auto end_time = td::Time::now() + 3;
  td::string result;
  size_t pos = 0;
  size_t server_hello_length = 0;
  size_t encrypted_application_data_length = 0;
  while (td::Time::now() < end_time) {
    char buf[1];
    TRY_RESULT(res, socket.read(td::MutableSlice{buf, sizeof(buf)}));
    if (res > 0) {
      auto read_length = [&] {
        CHECK(result.size() >= 2 + pos);
        pos += 2;
        return static_cast<unsigned char>(result[pos - 2]) * 256 + static_cast<unsigned char>(result[pos - 1]);
      };

      result += td::Slice(buf, res).str();
      while (true) {
#define CHECK_LENGTH(length)            \
  if (pos + (length) > result.size()) { \
    break;                              \
  }
#define EXPECT_STR(pos, str, error)                       \
  if (!begins_with(td::Slice(result).substr(pos), str)) { \
    return td::Status::Error(error);                      \
  }

        if (pos == 0) {
          CHECK_LENGTH(3);
          EXPECT_STR(0, "\x16\x03\x03", "Non-TLS response or TLS <= 1.1");
          pos += 3;
        }
        if (pos == 3) {
          CHECK_LENGTH(2);
          server_hello_length = read_length();
          if (server_hello_length <= 39) {
            return td::Status::Error("Receive too short server hello");
          }
        }
        if (server_hello_length > 0) {
          if (pos == 5) {
            CHECK_LENGTH(server_hello_length);
            pos += server_hello_length;

            EXPECT_STR(5, "\x02\x00", "Non-TLS response 2");
            EXPECT_STR(9, "\x03\x03", "Non-TLS response 3");

            auto random_id = td::Slice(result.c_str() + 11, 32);
            if (random_id ==
                "\xcf\x21\xad\x74\xe5\x9a\x61\x11\xbe\x1d\x8c\x02\x1e\x65\xb8\x91\xc2\xa2\x11\x16\x7a\xbb\x8c\x5e\x07"
                "\x9e\x09\xe2\xc8\xa8\x33\x9c") {
              return td::Status::Error("TLS 1.3 servers returning HelloRetryRequest are not supprted");
            }
            if (result[43] == '\x00') {
              return td::Status::Error("TLS <= 1.2: empty session_id");
            }
            EXPECT_STR(43, "\x20", "Non-TLS response 4");
            if (server_hello_length <= 75) {
              return td::Status::Error("Receive too short server hello 2");
            }
            EXPECT_STR(44, request.substr(44, 32), "TLS <= 1.2: expected mirrored session_id");
            EXPECT_STR(76, "\x13\x01\x00", "TLS <= 1.2: expected x25519 as a chosen cipher");
            EXPECT_STR(79, "\x00\x2e", "Support only TLS 1.3 extension list of size 47");
          }
          if (pos == 5 + server_hello_length) {
            CHECK_LENGTH(9);
            EXPECT_STR(pos, "\x14\x03\x03\x00\x01\x01", "Expected dummy ChangeCipherSpec");
            EXPECT_STR(pos + 6, "\x17\x03\x03", "Expected encrypted application data");
            pos += 9;
          }
          if (pos == 14 + server_hello_length) {
            CHECK_LENGTH(2);
            encrypted_application_data_length = read_length();
            if (encrypted_application_data_length == 0) {
              return td::Status::Error("Receive empty encrypted application data");
            }
          }
          if (encrypted_application_data_length > 0) {
            CHECK_LENGTH(encrypted_application_data_length);
            pos += encrypted_application_data_length;
            return td::Status::OK();
          }
        }
      }
    } else {
      td::usleep_for(10000);
    }
  }

  // LOG(ERROR) << url << ":" << td::format::as_hex_dump<0>(td::Slice(result));
  return td::Status::Error("Failed to get response in 3 seconds");
}

int main(int argc, char *argv[]) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(WARNING));

#if TD_PORT_WINDOWS
  auto iocp = td::make_unique<td::detail::Iocp>();
  iocp->init();
  auto iocp_thread = td::thread([&iocp] { iocp->loop(); });
  td::detail::Iocp::Guard iocp_guard(iocp.get());
#endif

  td::vector<td::string> urls;
  for (int i = 1; i < argc; i++) {
    urls.emplace_back(argv[i]);
  }
  for (auto &url : urls) {
    LOG(ERROR) << url << ": " << test_tls(url);
  }

#if TD_PORT_WINDOWS
  iocp->interrupt_loop();
  iocp_thread.join();
#endif
}

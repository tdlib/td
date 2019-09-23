//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Client.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <cstdlib>

static void usage() {
  td::TsCerr() << "Tests specified MTProto-proxy; exits with code 0 on success.\n";
  td::TsCerr() << "Usage:\n";
  td::TsCerr() << "check_proxy [-v<N>] [-h] server:port:secret\n";
  td::TsCerr() << "  -v<N>\tSet verbosity level to N\n";
  td::TsCerr() << "  -h/--help\tDisplay this information\n";
  td::TsCerr() << "  -d/--dc-id\tIdentifier of a datacenter, to which try to connect (default is 2)\n";
  td::TsCerr() << "  -t/--timeout\tMaximum overall timeout for the request (default is 10 seconds)\n";
  std::exit(2);
}

int main(int argc, char **argv) {
  int new_verbosity_level = VERBOSITY_NAME(FATAL);
  td::string server;
  td::int32 port = 0;
  td::string secret;

  td::int32 dc_id = 2;
  double timeout = 10.0;

  for (int i = 1; i < argc; i++) {
    td::string arg(argv[i]);
    if (arg.substr(0, 2) == "-v") {
      if (arg.size() == 2 && i + 1 < argc && argv[i + 1][0] != '-') {
        arg = argv[++i];
      } else {
        arg = arg.substr(2);
      }
      int new_verbosity = 1;
      while (arg[0] == 'v') {
        new_verbosity++;
        arg = arg.substr(1);
      }
      if (!arg.empty()) {
        new_verbosity += td::to_integer<int>(arg) - (new_verbosity == 1);
      }
      new_verbosity_level = VERBOSITY_NAME(FATAL) + new_verbosity;
    } else if (arg == "-t" || arg == "--timeout") {
      if (i + 1 == argc) {
        td::TsCerr() << "Value is required after " << arg;
        usage();
      }
      timeout = td::to_double(std::string(argv[++i]));
    } else if (arg == "-d" || arg == "--dc_id") {
      if (i + 1 == argc) {
        td::TsCerr() << "Value is required after " << arg;
        usage();
      }
      dc_id = td::to_integer<td::int32>(std::string(argv[++i]));
    } else if (arg[0] == '-') {
      usage();
    } else {
      auto secret_pos = arg.rfind(':');
      if (secret_pos == td::string::npos) {
        td::TsCerr() << (PSLICE() << "Error: failed to find proxy port and secret in \"" << arg << "\"\n");
        usage();
      }
      secret = arg.substr(secret_pos + 1);
      auto port_pos = arg.substr(0, secret_pos).rfind(':');
      if (port_pos == td::string::npos) {
        td::TsCerr() << (PSLICE() << "Error: failed to find proxy secret in \"" << arg << "\"\n");
        usage();
      }
      auto r_port = td::to_integer_safe<td::int32>(arg.substr(port_pos + 1, secret_pos - port_pos - 1));
      if (r_port.is_error()) {
        td::TsCerr() << (PSLICE() << "Error: failed to parse proxy port in \"" << arg << "\"\n");
        usage();
      }
      port = r_port.move_as_ok();
      server = arg.substr(0, port_pos);
    }
  }

  if (server.empty() || port <= 0 || port > 65536 || secret.empty()) {
    td::TsCerr() << "Error: proxy address to check is not specified\n";
    usage();
  }

  SET_VERBOSITY_LEVEL(new_verbosity_level);

  td::Client client;
  client.send({1, td::td_api::make_object<td::td_api::testProxy>(
                      server, port, td::td_api::make_object<td::td_api::proxyTypeMtproto>(secret), dc_id, timeout)});
  while (true) {
    auto response = client.receive(100.0);
    if (response.id == 1) {
      if (response.object->get_id() == td::td_api::error::ID) {
        LOG(ERROR) << to_string(response.object);
        return 1;
      } else {
        LOG(ERROR) << "Success!";
        return 0;
      }
    }
  }
}

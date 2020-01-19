//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/OptionsParser.h"

#if TD_HAVE_GETOPT
#include "getopt.h"
#endif

#if !TD_WINDOWS
#include <getopt.h>
#include <unistd.h>
#endif

namespace td {

void OptionsParser::set_description(std::string description) {
  description_ = std::move(description);
}

void OptionsParser::add_option(Option::Type type, char short_key, Slice long_key, Slice description,
                               std::function<Status(Slice)> callback) {
  options_.push_back(Option{type, short_key, long_key.str(), description.str(), std::move(callback)});
}

void OptionsParser::add_option(char short_key, Slice long_key, Slice description,
                               std::function<Status(Slice)> callback) {
  add_option(Option::Type::Arg, short_key, long_key, description, std::move(callback));
}

void OptionsParser::add_option(char short_key, Slice long_key, Slice description,
                               std::function<Status(void)> callback) {
  // Ouch. There must be some better way
  add_option(Option::Type::NoArg, short_key, long_key, description,
             std::bind([](std::function<Status(void)> &func, Slice) { return func(); }, std::move(callback),
                       std::placeholders::_1));
}

Result<int> OptionsParser::run(int argc, char *argv[]) {
#if TD_HAVE_GETOPT
  char buff[1024];
  StringBuilder sb(MutableSlice{buff, sizeof(buff)});
  for (auto &opt : options_) {
    CHECK(opt.type != Option::Type::OptionalArg);
    sb << opt.short_key;
    if (opt.type == Option::Type::Arg) {
      sb << ":";
    }
  }
  if (sb.is_error()) {
    return Status::Error("Can't parse options");
  }
  CSlice short_options = sb.as_cslice();

  vector<option> long_options;
  for (auto &opt : options_) {
    if (opt.long_key.empty()) {
      continue;
    }
    option o;
    o.flag = nullptr;
    o.val = opt.short_key;
    o.has_arg = opt.type == Option::Type::Arg ? required_argument : no_argument;
    o.name = opt.long_key.c_str();
    long_options.push_back(o);
  }
  long_options.push_back({nullptr, 0, nullptr, 0});

  while (true) {
    int opt_i = getopt_long(argc, argv, short_options.c_str(), &long_options[0], nullptr);
    if (opt_i == ':') {
      return Status::Error("Missing argument");
    }
    if (opt_i == '?') {
      return Status::Error("Unrecognized option");
    }
    if (opt_i == -1) {
      break;
    }
    bool found = false;
    for (auto &opt : options_) {
      if (opt.short_key == opt_i) {
        Slice arg;
        if (opt.type == Option::Type::Arg) {
          arg = Slice(optarg);
        }
        auto status = opt.arg_callback(arg);
        if (status.is_error()) {
          return std::move(status);
        }
        found = true;
        break;
      }
    }
    if (!found) {
      return Status::Error("Unknown argument");
    }
  }
  return optind;
#else
  return -1;
#endif
}

StringBuilder &operator<<(StringBuilder &sb, const OptionsParser &o) {
  sb << o.description_ << "\n";
  for (auto &opt : o.options_) {
    sb << "-" << opt.short_key;
    if (!opt.long_key.empty()) {
      sb << "|--" << opt.long_key;
    }
    if (opt.type == OptionsParser::Option::Type::OptionalArg) {
      sb << "[";
    }
    if (opt.type != OptionsParser::Option::Type::NoArg) {
      sb << "<arg>";
    }
    if (opt.type == OptionsParser::Option::Type::OptionalArg) {
      sb << "]";
    }
    sb << "\t" << opt.description;
    sb << "\n";
  }
  return sb;
}

}  // namespace td

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/OptionParser.h"

#include "td/utils/misc.h"

#include <cstring>
#include <unordered_map>

namespace td {

void OptionParser::set_description(string description) {
  description_ = std::move(description);
}

void OptionParser::add_option(Option::Type type, char short_key, Slice long_key, Slice description,
                              std::function<Status(Slice)> callback) {
  options_.push_back(Option{type, short_key, long_key.str(), description.str(), std::move(callback)});
}

void OptionParser::add_option(char short_key, Slice long_key, Slice description,
                              std::function<Status(Slice)> callback) {
  add_option(Option::Type::Arg, short_key, long_key, description, std::move(callback));
}

void OptionParser::add_option(char short_key, Slice long_key, Slice description, std::function<Status(void)> callback) {
  // Ouch. There must be some better way
  add_option(Option::Type::NoArg, short_key, long_key, description,
             std::bind([](std::function<Status(void)> &func, Slice) { return func(); }, std::move(callback),
                       std::placeholders::_1));
}

Result<vector<char *>> OptionParser::run(int argc, char *argv[]) {
  std::unordered_map<char, const Option *> short_options;
  std::unordered_map<string, const Option *> long_options;
  for (auto &opt : options_) {
    if (opt.short_key != '\0') {
      short_options[opt.short_key] = &opt;
    }
    if (!opt.long_key.empty()) {
      long_options[opt.long_key] = &opt;
    }
  }

  vector<char *> non_options;
  for (int arg_pos = 1; arg_pos < argc; arg_pos++) {
    const char *arg = argv[arg_pos];
    if (arg[0] != '-' || arg[1] == '\0') {
      non_options.push_back(argv[arg_pos]);
      continue;
    }
    if (arg[1] == '-' && arg[2] == '\0') {
      // "--"; after it everything is non-option
      while (++arg_pos < argc) {
        non_options.push_back(argv[arg_pos]);
      }
      break;
    }

    if (arg[1] == '-') {
      // long option
      Slice long_arg(arg + 2, std::strlen(arg + 2));
      Slice param;
      auto equal_pos = long_arg.find('=');
      bool has_equal = equal_pos != Slice::npos;
      if (has_equal) {
        param = long_arg.substr(equal_pos + 1);
        long_arg = long_arg.substr(0, equal_pos);
      }

      auto it = long_options.find(long_arg.str());
      if (it == long_options.end()) {
        return Status::Error(PSLICE() << "Option " << long_arg << " was unrecognized");
      }

      auto option = it->second;
      switch (option->type) {
        case Option::Type::NoArg:
          if (has_equal) {
            return Status::Error(PSLICE() << "Option " << long_arg << " must not have argument");
          }
          break;
        case Option::Type::Arg:
          if (!has_equal) {
            if (++arg_pos == argc) {
              return Status::Error(PSLICE() << "Option " << long_arg << " must have argument");
            }
            param = Slice(argv[arg_pos], std::strlen(argv[arg_pos]));
          }
          break;
        default:
          UNREACHABLE();
      }

      TRY_STATUS(option->arg_callback(param));
      continue;
    }

    for (size_t opt_pos = 1; arg[opt_pos] != '\0'; opt_pos++) {
      auto it = short_options.find(arg[opt_pos]);
      if (it == short_options.end()) {
        return Status::Error(PSLICE() << "Option " << arg[opt_pos] << " was unrecognized");
      }

      auto option = it->second;
      Slice param;
      switch (option->type) {
        case Option::Type::NoArg:
          // nothing to do
          break;
        case Option::Type::Arg:
          if (arg[opt_pos + 1] == '\0') {
            if (++arg_pos == argc) {
              return Status::Error(PSLICE() << "Option " << arg[opt_pos] << " must have argument");
            }
            param = Slice(argv[arg_pos], std::strlen(argv[arg_pos]));
          } else {
            param = Slice(arg + opt_pos + 1, std::strlen(arg + opt_pos + 1));
            opt_pos += param.size();
          }
          break;
        default:
          UNREACHABLE();
      }

      TRY_STATUS(option->arg_callback(param));
    }
  }

  return std::move(non_options);
}

StringBuilder &operator<<(StringBuilder &sb, const OptionParser &o) {
  sb << o.description_ << "\n";
  for (auto &opt : o.options_) {
    sb << "-" << opt.short_key;
    if (!opt.long_key.empty()) {
      sb << "|--" << opt.long_key;
    }
    if (opt.type != OptionParser::Option::Type::NoArg) {
      sb << "<arg>";
    }
    sb << "\t" << opt.description;
    sb << "\n";
  }
  return sb;
}

}  // namespace td

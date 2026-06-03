// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"

#include <clocale>
#include <cstdlib>

#if !TD_WINDOWS
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace to_double_locale_adversarial {

td::string render_to_double(td::Slice input, int precision = 6) {
  return PSTRING() << td::StringBuilder::FixedDouble(td::to_double(input), precision);
}

#if !TD_WINDOWS
class ScopedEnvVar final {
 public:
  ScopedEnvVar(const char *name, td::Slice value) : name_(name) {
    if (const auto *old_value = std::getenv(name_)) {
      has_old_value_ = true;
      old_value_ = old_value;
    }
    new_value_ = value.str();
    CHECK(::setenv(name_, new_value_.c_str(), 1) == 0);
  }

  ~ScopedEnvVar() {
    if (has_old_value_) {
      CHECK(::setenv(name_, old_value_.c_str(), 1) == 0);
    } else {
      CHECK(::unsetenv(name_) == 0);
    }
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;
  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;
  ScopedEnvVar(ScopedEnvVar &&) = delete;
  ScopedEnvVar &operator=(ScopedEnvVar &&) = delete;

 private:
  const char *name_;
  bool has_old_value_ = false;
  td::string new_value_;
  td::string old_value_;
};

class ScopedNumericLocale final {
 public:
  ScopedNumericLocale() {
    if (const auto *current_locale = std::setlocale(LC_NUMERIC, nullptr)) {
      locale_name_ = current_locale;
    } else {
      locale_name_ = "C";
    }
  }

  ~ScopedNumericLocale() {
    CHECK(std::setlocale(LC_NUMERIC, locale_name_.c_str()) != nullptr);
  }

  ScopedNumericLocale(const ScopedNumericLocale &) = delete;
  ScopedNumericLocale &operator=(const ScopedNumericLocale &) = delete;
  ScopedNumericLocale(ScopedNumericLocale &&) = delete;
  ScopedNumericLocale &operator=(ScopedNumericLocale &&) = delete;

 private:
  td::string locale_name_;
};

class ScopedTempDir final {
 public:
  explicit ScopedTempDir(td::string path) : path_(std::move(path)) {
  }

  ~ScopedTempDir() {
    td::rmrf(path_).ignore();
  }

  ScopedTempDir(const ScopedTempDir &) = delete;
  ScopedTempDir &operator=(const ScopedTempDir &) = delete;
  ScopedTempDir(ScopedTempDir &&) = delete;
  ScopedTempDir &operator=(ScopedTempDir &&) = delete;

  const td::string &path() const {
    return path_;
  }

 private:
  td::string path_;
};

int run_localedef(const td::string &output_path) {
  const auto child_pid = ::fork();
  CHECK(child_pid >= 0);

  if (child_pid == 0) {
    ::execlp("localedef", "localedef", "-i", "de_DE", "-f", "UTF-8", output_path.c_str(), nullptr);
    _exit(127);
  }

  int status = 0;
  CHECK(::waitpid(child_pid, &status, 0) == child_pid);
  return status;
}
#endif

}  // namespace to_double_locale_adversarial

TEST(ToDoubleLocaleAdversarial, parses_dot_decimal_independent_of_process_numeric_locale) {
#if TD_WINDOWS
  SUCCEED();
#else
  auto locale_root = td::mkdtemp(td::get_temporary_dir(), "to-double-locale").move_as_ok();
  to_double_locale_adversarial::ScopedTempDir temp_dir(locale_root);

  const auto locale_output = PSTRING() << temp_dir.path() << "/de_DE.UTF-8";
  const auto status = to_double_locale_adversarial::run_localedef(locale_output);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));

  to_double_locale_adversarial::ScopedEnvVar locale_path("LOCPATH", temp_dir.path());
  to_double_locale_adversarial::ScopedNumericLocale numeric_locale;
  ASSERT_TRUE(std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr);

  ASSERT_EQ("1.500000", to_double_locale_adversarial::render_to_double("1.5"));
#endif
}
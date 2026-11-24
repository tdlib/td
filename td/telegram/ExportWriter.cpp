//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportWriter.h"

#include "td/telegram/ExportJsonWriter.h"

#include "td/utils/logging.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/port/config.h"
#include "td/utils/port/path.h"
#include "td/utils/SliceBuilder.h"

#include <ctime>

namespace td {

static int64 floor_div(int64 a, int64 b) {
  return a >= 0 ? a / b : (a - b + 1) / b;
}

static int32 get_system_utc_time_offset(int64 date) {
  auto time = static_cast<std::time_t>(date);
  std::tm local_time{};
  std::tm utc_time{};
#if TD_PORT_WINDOWS
  auto is_converted = localtime_s(&local_time, &time) == 0 && gmtime_s(&utc_time, &time) == 0;
#else
  auto is_converted = localtime_r(&time, &local_time) != nullptr && gmtime_r(&time, &utc_time) != nullptr;
#endif
  if (!is_converted) {
    // dates before 1970 can't be converted on Windows
    return Clocks::tz_offset();
  }
  int32 offset = ((local_time.tm_hour - utc_time.tm_hour) * 60 + local_time.tm_min - utc_time.tm_min) * 60 +
                 local_time.tm_sec - utc_time.tm_sec;
  // the local date differs from the UTC one by at most a day
  auto day_difference = local_time.tm_year != utc_time.tm_year ? local_time.tm_year - utc_time.tm_year
                                                               : local_time.tm_yday - utc_time.tm_yday;
  if (day_difference > 0) {
    offset += 86400;
  } else if (day_difference < 0) {
    offset -= 86400;
  }
  return offset;
}

int32 get_export_utc_time_offset(int64 date, int32 utc_time_offset) {
  // the option "utc_time_offset" is Clocks::tz_offset(), which is calculated once, unless the application changed it
  if (utc_time_offset == Clocks::tz_offset()) {
    return get_system_utc_time_offset(date);
  }
  // the application keeps the option current, so it is compared with the current offset of the system time zone
  auto now = static_cast<int64>(Clocks::system());
  return utc_time_offset + get_system_utc_time_offset(date) - get_system_utc_time_offset(now);
}

int64 get_export_local_time(int64 date, int32 utc_time_offset) {
  return date + get_export_utc_time_offset(date, utc_time_offset);
}

namespace {
struct ExportDateTime {
  int64 year;
  int64 month;
  int64 day;
  int64 seconds;
};
}  // namespace

static ExportDateTime get_export_date_time(int64 local_time) {
  auto days = floor_div(local_time, 86400);
  auto seconds = local_time - days * 86400;

  days += 719468;
  auto era = floor_div(days, 146097);
  auto day_of_era = days - era * 146097;
  auto year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  auto year = year_of_era + era * 400;
  auto day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  auto shifted_month = (5 * day_of_year + 2) / 153;
  auto day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
  auto month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  if (month <= 2) {
    year++;
  }
  return {year, month, day, seconds};
}

static void append_export_year(string &result, int64 year) {
  if (0 <= year && year <= 9999) {
    result += static_cast<char>('0' + year / 1000);
    result += static_cast<char>('0' + year / 100 % 10);
    result += static_cast<char>('0' + year / 10 % 10);
    result += static_cast<char>('0' + year % 10);
  } else {
    result += to_string(year);
  }
}

static void append_export_two_digits(string &result, int64 value) {
  result += static_cast<char>('0' + value / 10);
  result += static_cast<char>('0' + value % 10);
}

string get_export_date_time_string(int64 local_time) {
  auto date_time = get_export_date_time(local_time);
  string result;
  append_export_year(result, date_time.year);
  result += '-';
  append_export_two_digits(result, date_time.month);
  result += '-';
  append_export_two_digits(result, date_time.day);
  result += 'T';
  append_export_two_digits(result, date_time.seconds / 3600);
  result += ':';
  append_export_two_digits(result, date_time.seconds / 60 % 60);
  result += ':';
  append_export_two_digits(result, date_time.seconds % 60);
  return result;
}

string get_export_file_date_time_string(int64 local_time) {
  auto date_time = get_export_date_time(local_time);
  string result;
  append_export_two_digits(result, date_time.day);
  result += '-';
  append_export_two_digits(result, date_time.month);
  result += '-';
  append_export_year(result, date_time.year);
  result += '_';
  append_export_two_digits(result, date_time.seconds / 3600);
  result += '-';
  append_export_two_digits(result, date_time.seconds / 60 % 60);
  result += '-';
  append_export_two_digits(result, date_time.seconds % 60);
  return result;
}

int64 get_export_file_size(const td_api::file *file) {
  if (file == nullptr) {
    return 0;
  }
  return file->size_ != 0 ? file->size_ : file->expected_size_;
}

const td_api::photoSize *get_export_photo_size(const vector<td_api::object_ptr<td_api::photoSize>> &sizes) {
  const td_api::photoSize *result = nullptr;
  int64 max_area = 0;
  for (const auto &size : sizes) {
    if (size == nullptr) {
      continue;
    }
    auto area = static_cast<int64>(size->width_) * size->height_;
    if (area > max_area) {
      result = size.get();
      max_area = area;
    }
  }
  return result;
}

ExportOutputFile::ExportOutputFile(string path, ExportStats *stats) : path_(std::move(path)), stats_(stats) {
}

Status ExportOutputFile::write(Slice data) {
  if (fd_.empty()) {
    TRY_STATUS(mkpath(path_));
    auto r_fd = FileFd::open(path_, FileFd::Write | FileFd::Create | FileFd::Truncate);
    if (r_fd.is_error()) {
      return Status::Error(PSLICE() << "Failed to create file \"" << path_ << "\": " << r_fd.error().message());
    }
    fd_ = r_fd.move_as_ok();
    stats_->file_count++;
  }
  auto r_size = fd_.write(data);
  if (r_size.is_error() || r_size.ok() != data.size()) {
    fd_.close();
    return Status::Error(PSLICE() << "Failed to write to file \"" << path_ << '"');
  }
  stats_->total_size += static_cast<int64>(data.size());
  return Status::OK();
}

unique_ptr<ExportWriter> create_export_writer(Td *td, ExportFormat format) {
  switch (format) {
    case ExportFormat::Json:
      return make_unique<ExportJsonWriter>(td);
    case ExportFormat::Html:
    case ExportFormat::HtmlAndJson:
      return nullptr;
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/FormattedDate.h"

namespace td {

FormattedDate::FormattedDate(int32 date, bool relative, bool short_time, bool long_time, bool short_date,
                             bool long_date, bool day_of_week)
    : date_(date) {
  if (date_ <= 0) {
    LOG(ERROR) << "Receive wrong date " << date_;
  }
  if (relative) {
    date_flags_ = DateFlags::Relative;
  } else {
    if (short_time) {
      date_flags_ |= DateFlags::ShortTime;
    } else if (long_time) {
      date_flags_ |= DateFlags::LongTime;
    }
    if (short_date) {
      date_flags_ |= DateFlags::ShortDate;
    } else if (long_date) {
      date_flags_ |= DateFlags::LongDate;
    }
    if (day_of_week) {
      date_flags_ |= DateFlags::DayOfWeek;
    }
  }
}

FormattedDate::FormattedDate(int32 date, int32 date_flags) : date_(date), date_flags_(date_flags) {
}

Result<FormattedDate> FormattedDate::get_formatted_date(
    int32 date, const td_api::object_ptr<td_api::DateTimeFormattingType> &type_ptr) {
  if (date <= 0) {
    return Status::Error(400, "Invalid date specified");
  }
  int32 date_flags = 0;
  if (type_ptr != nullptr) {
    switch (type_ptr->get_id()) {
      case td_api::dateTimeFormattingTypeRelative::ID:
        date_flags = DateFlags::Relative;
        break;
      case td_api::dateTimeFormattingTypeAbsolute::ID: {
        auto type = static_cast<const td_api::dateTimeFormattingTypeAbsolute *>(type_ptr.get());
        if (type->time_precision_ != nullptr) {
          switch (type->time_precision_->get_id()) {
            case td_api::dateTimePartPrecisionNone::ID:
              break;
            case td_api::dateTimePartPrecisionShort::ID:
              date_flags |= DateFlags::ShortTime;
              break;
            case td_api::dateTimePartPrecisionLong::ID:
              date_flags |= DateFlags::LongTime;
              break;
            default:
              UNREACHABLE();
          }
        }
        if (type->date_precision_ != nullptr) {
          switch (type->date_precision_->get_id()) {
            case td_api::dateTimePartPrecisionNone::ID:
              break;
            case td_api::dateTimePartPrecisionShort::ID:
              date_flags |= DateFlags::ShortDate;
              break;
            case td_api::dateTimePartPrecisionLong::ID:
              date_flags |= DateFlags::LongDate;
              break;
            default:
              UNREACHABLE();
          }
        }
        if (type->show_day_of_week_) {
          date_flags |= DateFlags::DayOfWeek;
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  FormattedDate result;
  result.date_ = date;
  result.date_flags_ = date_flags;
  return std::move(result);
}

Result<FormattedDate> FormattedDate::get_formatted_date(int32 date, const string &date_format) {
  TRY_RESULT(date_flags, get_date_flags(date_format));
  FormattedDate result;
  result.date_ = date;
  result.date_flags_ = date_flags;
  return std::move(result);
}

Result<int32> FormattedDate::get_date_flags(const string &date_format) {
  if (date_format == "r" || date_format == "R") {
    return DateFlags::Relative;
  }
  int32 date_flags = 0;
  for (auto c : date_format) {
    switch (c) {
      case 't':
        date_flags |= DateFlags::ShortTime;
        break;
      case 'T':
        date_flags |= DateFlags::LongTime;
        break;
      case 'd':
        date_flags |= DateFlags::ShortDate;
        break;
      case 'D':
        date_flags |= DateFlags::LongDate;
        break;
      case 'w':
      case 'W':
        date_flags |= DateFlags::DayOfWeek;
        break;
      default:
        return Status::Error(400, "Invalid date format used");
    }
  }
  return std::move(date_flags);
}

td_api::object_ptr<td_api::DateTimeFormattingType> FormattedDate::get_date_time_formatting_type_object() const {
  if (date_flags_ == 0) {
    return nullptr;
  }
  if ((date_flags_ & DateFlags::Relative) != 0) {
    return td_api::make_object<td_api::dateTimeFormattingTypeRelative>();
  }
  td_api::object_ptr<td_api::DateTimePartPrecision> time_precision;
  if ((date_flags_ & DateFlags::ShortTime) != 0) {
    time_precision = td_api::make_object<td_api::dateTimePartPrecisionShort>();
  } else if ((date_flags_ & DateFlags::LongTime) != 0) {
    time_precision = td_api::make_object<td_api::dateTimePartPrecisionLong>();
  } else {
    time_precision = td_api::make_object<td_api::dateTimePartPrecisionNone>();
  }

  td_api::object_ptr<td_api::DateTimePartPrecision> date_precision;
  if ((date_flags_ & DateFlags::ShortDate) != 0) {
    date_precision = td_api::make_object<td_api::dateTimePartPrecisionShort>();
  } else if ((date_flags_ & DateFlags::LongDate) != 0) {
    date_precision = td_api::make_object<td_api::dateTimePartPrecisionLong>();
  } else {
    date_precision = td_api::make_object<td_api::dateTimePartPrecisionNone>();
  }
  auto show_day_of_week = (date_flags_ & DateFlags::DayOfWeek) != 0;
  return td_api::make_object<td_api::dateTimeFormattingTypeAbsolute>(std::move(time_precision),
                                                                     std::move(date_precision), show_day_of_week);
}

telegram_api::object_ptr<telegram_api::messageEntityFormattedDate>
FormattedDate::get_input_message_entity_formatted_date(int32 offset, int32 length) const {
  return telegram_api::make_object<telegram_api::messageEntityFormattedDate>(
      0, (date_flags_ & DateFlags::Relative) != 0, (date_flags_ & DateFlags::ShortTime) != 0,
      (date_flags_ & DateFlags::LongTime) != 0, (date_flags_ & DateFlags::ShortDate) != 0,
      (date_flags_ & DateFlags::LongDate) != 0, (date_flags_ & DateFlags::DayOfWeek) != 0, offset, length, date_);
}

telegram_api::object_ptr<telegram_api::textDate> FormattedDate::get_input_text_date(
    telegram_api::object_ptr<telegram_api::RichText> &&text) const {
  return telegram_api::make_object<telegram_api::textDate>(
      0, (date_flags_ & DateFlags::Relative) != 0, (date_flags_ & DateFlags::ShortTime) != 0,
      (date_flags_ & DateFlags::LongTime) != 0, (date_flags_ & DateFlags::ShortDate) != 0,
      (date_flags_ & DateFlags::LongDate) != 0, (date_flags_ & DateFlags::DayOfWeek) != 0, std::move(text), date_);
}

bool operator==(const FormattedDate &lhs, const FormattedDate &rhs) {
  return lhs.date_ == rhs.date_ && lhs.date_flags_ == rhs.date_flags_;
}

bool operator!=(const FormattedDate &lhs, const FormattedDate &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const FormattedDate &date) {
  return string_builder << "date " << date.date_ << " with flags " << date.date_flags_;
}

}  // namespace td

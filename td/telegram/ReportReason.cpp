//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReportReason.h"

#include "td/telegram/misc.h"

namespace td {

Result<ReportReason> ReportReason::get_report_reason(td_api::object_ptr<td_api::ChatReportReason> reason,
                                                     string &&message) {
  if (!clean_input_string(message)) {
    return Status::Error(400, "Report text must be encoded in UTF-8");
  }
  if (reason == nullptr) {
    return Status::Error(400, "Reason must be non-empty");
  }

  auto type = [&] {
    switch (reason->get_id()) {
      case td_api::chatReportReasonSpam::ID:
        return ReportReason::Type::Spam;
      case td_api::chatReportReasonViolence::ID:
        return ReportReason::Type::Violence;
      case td_api::chatReportReasonPornography::ID:
        return ReportReason::Type::Pornography;
      case td_api::chatReportReasonChildAbuse::ID:
        return ReportReason::Type::ChildAbuse;
      case td_api::chatReportReasonCopyright::ID:
        return ReportReason::Type::Copyright;
      case td_api::chatReportReasonUnrelatedLocation::ID:
        return ReportReason::Type::UnrelatedLocation;
      case td_api::chatReportReasonFake::ID:
        return ReportReason::Type::Fake;
      case td_api::chatReportReasonCustom::ID:
        return ReportReason::Type::Custom;
      default:
        UNREACHABLE();
        return ReportReason::Type::Custom;
    }
  }();
  return ReportReason(type, std::move(message));
}

tl_object_ptr<telegram_api::ReportReason> ReportReason::get_input_report_reason() const {
  switch (type_) {
    case ReportReason::Type::Spam:
      return make_tl_object<telegram_api::inputReportReasonSpam>();
    case ReportReason::Type::Violence:
      return make_tl_object<telegram_api::inputReportReasonViolence>();
    case ReportReason::Type::Pornography:
      return make_tl_object<telegram_api::inputReportReasonPornography>();
    case ReportReason::Type::ChildAbuse:
      return make_tl_object<telegram_api::inputReportReasonChildAbuse>();
    case ReportReason::Type::Copyright:
      return make_tl_object<telegram_api::inputReportReasonCopyright>();
    case ReportReason::Type::UnrelatedLocation:
      return make_tl_object<telegram_api::inputReportReasonGeoIrrelevant>();
    case ReportReason::Type::Fake:
      return make_tl_object<telegram_api::inputReportReasonFake>();
    case ReportReason::Type::Custom:
      return make_tl_object<telegram_api::inputReportReasonOther>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReportReason &report_reason) {
  string_builder << "ReportReason";
  switch (report_reason.type_) {
    case ReportReason::Type::Spam:
      return string_builder << "Spam";
    case ReportReason::Type::Violence:
      return string_builder << "Violence";
    case ReportReason::Type::Pornography:
      return string_builder << "Pornography";
    case ReportReason::Type::ChildAbuse:
      return string_builder << "ChildAbuse";
    case ReportReason::Type::Copyright:
      return string_builder << "Copyright";
    case ReportReason::Type::UnrelatedLocation:
      return string_builder << "UnrelatedLocation";
    case ReportReason::Type::Fake:
      return string_builder << "Fake";
    case ReportReason::Type::Custom:
      return string_builder << "Custom";
    default:
      UNREACHABLE();
  }
  return string_builder;
}

}  // namespace td

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TimeZoneManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"

namespace td {

class GetTimezonesListQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::help_TimezonesList>> promise_;

 public:
  explicit GetTimezonesListQuery(Promise<telegram_api::object_ptr<telegram_api::help_TimezonesList>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int32 hash) {
    send_query(G()->net_query_creator().create(telegram_api::help_getTimezonesList(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getTimezonesList>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "GetTimezonesListQuery returned " << status;
    }
    promise_.set_error(std::move(status));
  }
};

TimeZoneManager::TimeZone::TimeZone(string &&id, string &&name, int32 utc_offset)
    : id_(std::move(id)), name_(std::move(name)), utc_offset_(utc_offset) {
}

td_api::object_ptr<td_api::timeZone> TimeZoneManager::TimeZone::get_time_zone_object() const {
  return td_api::make_object<td_api::timeZone>(id_, name_, utc_offset_);
}

bool operator==(const TimeZoneManager::TimeZone &lhs, const TimeZoneManager::TimeZone &rhs) {
  return lhs.id_ == rhs.id_ && lhs.name_ == rhs.name_ && lhs.utc_offset_ == rhs.utc_offset_;
}

bool operator!=(const TimeZoneManager::TimeZone &lhs, const TimeZoneManager::TimeZone &rhs) {
  return !(lhs == rhs);
}

td_api::object_ptr<td_api::timeZones> TimeZoneManager::TimeZoneList::get_time_zones_object() const {
  return td_api::make_object<td_api::timeZones>(
      transform(time_zones_, [](const TimeZone &time_zone) { return time_zone.get_time_zone_object(); }));
}

TimeZoneManager::TimeZoneManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

TimeZoneManager::~TimeZoneManager() = default;

void TimeZoneManager::tear_down() {
  parent_.reset();
}

void TimeZoneManager::get_time_zones(Promise<td_api::object_ptr<td_api::timeZones>> &&promise) {
  if (time_zones_.hash_ != 0) {
    return promise.set_value(time_zones_.get_time_zones_object());
  }
  reload_time_zones(std::move(promise));
}

void TimeZoneManager::reload_time_zones(Promise<td_api::object_ptr<td_api::timeZones>> &&promise) {
  get_time_zones_queries_.push_back(std::move(promise));
  if (get_time_zones_queries_.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::help_TimezonesList>> &&r_time_zones) {
          send_closure(actor_id, &TimeZoneManager::on_get_time_zones, std::move(r_time_zones));
        });
    td_->create_handler<GetTimezonesListQuery>(std::move(query_promise))->send(time_zones_.hash_);
  }
}

void TimeZoneManager::on_get_time_zones(
    Result<telegram_api::object_ptr<telegram_api::help_TimezonesList>> &&r_time_zones) {
  G()->ignore_result_if_closing(r_time_zones);
  if (r_time_zones.is_error()) {
    return fail_promises(get_time_zones_queries_, r_time_zones.move_as_error());
  }

  auto time_zones_ptr = r_time_zones.move_as_ok();
  switch (time_zones_ptr->get_id()) {
    case telegram_api::help_timezonesListNotModified::ID:
      break;
    case telegram_api::help_timezonesList::ID: {
      auto zone_list = telegram_api::move_object_as<telegram_api::help_timezonesList>(time_zones_ptr);
      vector<TimeZone> time_zones;
      for (auto &time_zone : zone_list->timezones_) {
        time_zones.emplace_back(std::move(time_zone->id_), std::move(time_zone->name_), time_zone->utc_offset_);
      }
      if (time_zones_.time_zones_ != time_zones || time_zones_.hash_ != zone_list->hash_) {
        time_zones_.time_zones_ = std::move(time_zones);
        time_zones_.hash_ = zone_list->hash_;
      }
      break;
    }
    default:
      UNREACHABLE();
  }
  auto promises = std::move(get_time_zones_queries_);
  reset_to_empty(get_time_zones_queries_);
  for (auto &promise : promises) {
    if (promise) {
      promise.set_value(time_zones_.get_time_zones_object());
    }
  }
}

}  // namespace td

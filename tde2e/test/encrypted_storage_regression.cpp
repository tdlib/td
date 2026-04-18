// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define private public
#include "td/e2e/EncryptedStorage.h"
#undef private

#include "td/utils/tests.h"

using namespace tde2e_core;
namespace api = tde2e_api;

namespace {

Value make_name_value(std::string first_name, std::string last_name, td::uint32 timestamp) {
  Value value;
  value.o_name = api::Entry<api::Name>{api::Entry<api::Name>::Server, timestamp,
                                       api::Name{std::move(first_name), std::move(last_name)}};
  return value;
}

api::Entry<api::PhoneNumber> make_phone_entry(std::string phone_number, td::uint32 timestamp) {
  return api::Entry<api::PhoneNumber>{api::Entry<api::PhoneNumber>::Server, timestamp,
                                      api::PhoneNumber{std::move(phone_number)}};
}

api::Entry<api::Name> make_name_entry(std::string first_name, std::string last_name, td::uint32 timestamp) {
  return api::Entry<api::Name>{api::Entry<api::Name>::Server, timestamp,
                               api::Name{std::move(first_name), std::move(last_name)}};
}

void assert_name_equals(const Value &value, const char *first_name, const char *last_name) {
  ASSERT_TRUE(value.o_name.has_value());
  ASSERT_EQ(value.o_name->value.first_name, first_name);
  ASSERT_EQ(value.o_name->value.last_name, last_name);
}

void assert_phone_equals(const Value &value, const char *phone_number) {
  ASSERT_TRUE(value.o_phone_number.has_value());
  ASSERT_EQ(value.o_phone_number->value.phone_number, phone_number);
}

}  // namespace

TEST(EncryptedStorageRegression, RewriteDoesNotDropExistingValue) {
  auto owner_pk = PrivateKey::generate().move_as_ok();
  auto r_storage = EncryptedStorage::create("", owner_pk);
  ASSERT_TRUE(r_storage.is_ok());
  auto storage = r_storage.move_as_ok();

  auto contact_pk = PrivateKey::generate().move_as_ok();
  auto contact_public_key = contact_pk.to_public_key().to_u256();
  Key key{contact_public_key};

  auto first_value = make_name_value("Alpha", "One", 1);
  auto second_value = make_name_value("Bravo", "Two", 2);

  storage.sync_entry(key, first_value, true);
  ASSERT_TRUE(storage.partial_key_value_[key].has_value());
  assert_name_equals(storage.partial_key_value_[key].value(), "Alpha", "One");

  storage.sync_entry(key, second_value, true);
  ASSERT_TRUE(storage.partial_key_value_[key].has_value());
  assert_name_equals(storage.partial_key_value_[key].value(), "Bravo", "Two");
}

TEST(EncryptedStorageRegression, KnownValueUpdateComputesOptimisticStateImmediately) {
  auto owner_pk = PrivateKey::generate().move_as_ok();
  auto r_storage = EncryptedStorage::create("", owner_pk);
  ASSERT_TRUE(r_storage.is_ok());
  auto storage = r_storage.move_as_ok();

  auto contact_pk = PrivateKey::generate().move_as_ok();
  Key key{contact_pk.to_public_key().to_u256()};

  storage.sync_entry(key, make_name_value("Known", "Base", 1), true);

  auto update_id = storage.update(key, to_update(make_phone_entry("+10000000001", 3)));
  ASSERT_TRUE(update_id.is_ok());

  auto update_it = storage.updates_.find(key);
  ASSERT_TRUE(update_it != storage.updates_.end());
  ASSERT_TRUE(update_it->second.o_new_value.has_value());
  assert_name_equals(*update_it->second.o_new_value, "Known", "Base");
  assert_phone_equals(*update_it->second.o_new_value, "+10000000001");
}

TEST(EncryptedStorageRegression, RewriteRefreshesDerivedValueForPendingUpdate) {
  auto owner_pk = PrivateKey::generate().move_as_ok();
  auto r_storage = EncryptedStorage::create("", owner_pk);
  ASSERT_TRUE(r_storage.is_ok());
  auto storage = r_storage.move_as_ok();

  auto contact_pk = PrivateKey::generate().move_as_ok();
  Key key{contact_pk.to_public_key().to_u256()};

  storage.sync_entry(key, make_name_value("First", "Proof", 1), true);
  auto update_id = storage.update(key, to_update(make_phone_entry("+10000000002", 4)));
  ASSERT_TRUE(update_id.is_ok());

  storage.sync_entry(key, make_name_value("Second", "Proof", 5), true);

  auto update_it = storage.updates_.find(key);
  ASSERT_TRUE(update_it != storage.updates_.end());
  ASSERT_TRUE(update_it->second.o_new_value.has_value());
  assert_name_equals(*update_it->second.o_new_value, "Second", "Proof");
  assert_phone_equals(*update_it->second.o_new_value, "+10000000002");
}

TEST(EncryptedStorageRegression, EntryEqualityDetectsDifferentNamePayloads) {
  auto left = make_name_entry("Alpha", "One", 1);
  auto right = make_name_entry("Bravo", "Two", 1);

  ASSERT_FALSE(left == right);
}

TEST(EncryptedStorageRegression, UpdateEqualityDetectsDifferentNames) {
  auto left = to_update(make_name_entry("Alpha", "One", 1));
  auto right = to_update(make_name_entry("Bravo", "Two", 1));

  ASSERT_FALSE(left == right);
}

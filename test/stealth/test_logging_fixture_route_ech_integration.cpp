// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/filesystem.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/tests.h"

#include <fstream>

namespace {

using td::logging_hardening::test::repo_path;

td::JsonValue read_import_manifest_root(td::string &content_storage) {
  auto manifest_path = repo_path("test/analysis/fixtures/imported/import_manifest.json");
  auto read_result = td::read_file_str(manifest_path);
  CHECK(read_result.is_ok());
  content_storage = read_result.move_as_ok();

  auto json_result = td::json_decode(td::MutableSlice(content_storage));
  CHECK(json_result.is_ok());
  auto root = json_result.move_as_ok();

  CHECK(root.type() == td::JsonValue::Type::Object);
  return root;
}

bool repo_file_exists(td::Slice relative_path) {
  std::ifstream input(repo_path(relative_path), std::ios::binary);
  return input.good();
}

TEST(LoggingFixtureRouteEchIntegration, ImportedManifestRetainsRealTrafficPairingContract) {
  td::string content_storage;
  auto root = read_import_manifest_root(content_storage);
  auto entries_field = root.get_object().extract_required_field("entries", td::JsonValue::Type::Array);
  ASSERT_TRUE(entries_field.is_ok());

  auto entries_value = entries_field.move_as_ok();
  auto &entries = entries_value.get_array();
  ASSERT_TRUE(!entries.empty());

  for (auto &entry_value : entries) {
    ASSERT_TRUE(entry_value.type() == td::JsonValue::Type::Object);
    auto &entry = entry_value.get_object();

    auto artifacts_field = entry.extract_required_field("artifacts", td::JsonValue::Type::Object);
    ASSERT_TRUE(artifacts_field.is_ok());
    auto artifacts_value = artifacts_field.move_as_ok();
    auto &artifacts = artifacts_value.get_object();

    auto clienthello = artifacts.get_optional_string_field("clienthello");
    auto serverhello = artifacts.get_optional_string_field("serverhello");
    auto capture_path = entry.get_optional_string_field("capture_path");

    ASSERT_TRUE(clienthello.is_ok());
    ASSERT_TRUE(serverhello.is_ok());
    ASSERT_TRUE(capture_path.is_ok());
    ASSERT_TRUE(!clienthello.ok().empty());
    ASSERT_TRUE(!serverhello.ok().empty());
    ASSERT_TRUE(!capture_path.ok().empty());

    ASSERT_TRUE(capture_path.ok().find("docs/Samples/Traffic dumps/") == 0);
    ASSERT_TRUE(repo_file_exists(capture_path.ok()));
    ASSERT_TRUE(repo_file_exists(clienthello.ok()));
    ASSERT_TRUE(repo_file_exists(serverhello.ok()));
  }
}

TEST(LoggingFixtureRouteEchIntegration, ImportedManifestHasNonRuRouteModeCoverage) {
  td::string content_storage;
  auto root = read_import_manifest_root(content_storage);
  auto entries_field = root.get_object().extract_required_field("entries", td::JsonValue::Type::Array);
  ASSERT_TRUE(entries_field.is_ok());

  auto entries_value = entries_field.move_as_ok();
  bool has_non_ru = false;
  for (auto &entry_value : entries_value.get_array()) {
    if (entry_value.type() != td::JsonValue::Type::Object) {
      continue;
    }
    auto &entry = entry_value.get_object();
    auto route_mode = entry.get_optional_string_field("route_mode");
    if (route_mode.is_ok() && route_mode.ok() == "non_ru_egress") {
      has_non_ru = true;
      break;
    }
  }
  ASSERT_TRUE(has_non_ru);
}

}  // namespace

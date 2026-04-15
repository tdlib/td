// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"
#include "td/mtproto/ConfigWindowTable.h"
#include "td/mtproto/PacketAlignmentSeeds.h"
#include "td/telegram/ReferenceTable.h"
#include "td/telegram/SessionBlendTable.h"
#include "td/telegram/net/ConfigCacheSeeds.h"

#include "td/net/RouteWindowTable.h"
#include "td/net/SessionTicketSeeds.h"

#include "td/utils/CatalogWeightTable.h"
#include "td/utils/HashIndexSeeds.h"
#include "td/utils/Slice.h"
#include "td/utils/UInt.h"
#include "td/utils/as.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <array>

namespace {

template <size_t N>
void append_bytes(td::string &target, const unsigned char (&bytes)[N]) {
  target.append(reinterpret_cast<const char *>(bytes), N);
}

td::uint64 load_uint64_le(td::Slice slice) {
  CHECK(slice.size() == 8);
  td::uint64 result = 0;
  for (size_t i = 0; i < 8; i++) {
    result |= static_cast<td::uint64>(static_cast<unsigned char>(slice[i])) << (i * 8);
  }
  return result;
}

td::string assemble_key_material() {
  td::string key_material;
  key_material.reserve(128);
  append_bytes(key_material, td::vault_detail::kHashIndexSeeds);
  append_bytes(key_material, td::vault_detail::kSessionTicketSeeds);
  append_bytes(key_material, td::vault_detail::kPacketAlignmentSeeds);
  append_bytes(key_material, td::vault_detail::kConfigCacheSeeds);
  return key_material;
}

td::UInt256 derive_mask(td::Slice label) {
  auto key_material = assemble_key_material();
  td::UInt256 mask;
  td::hmac_sha256(label, td::Slice(key_material), td::as_mutable_slice(mask));
  return mask;
}

td::uint64 unmask_with(td::uint64 masked_value, td::Slice label, size_t offset) {
  auto mask = derive_mask(label);
  return masked_value ^ load_uint64_le(td::as_slice(mask).substr(offset, 8));
}

bool equals_any_known_slot(td::uint64 candidate) {
  auto as_int = static_cast<td::int64>(candidate);
  return as_int == td::ReferenceTable::slot_value(td::mtproto::BlobRole::Primary) ||
         as_int == td::ReferenceTable::slot_value(td::mtproto::BlobRole::Secondary) ||
         as_int == td::ReferenceTable::slot_value(td::mtproto::BlobRole::Auxiliary);
}

constexpr const char *kLabelCatalog = "table_mix_v1_gamma";
constexpr const char *kLabelWindow = "table_mix_v1_delta";
constexpr const char *kLabelShared = "table_mix_v1_sigma";
constexpr const char *kLabelConfig = "table_mix_v1_theta";

TEST(LabelDomainSeparation, CorrectLabelReproducesCatalogPrimarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kCatalogWeightPrimary), td::Slice(kLabelCatalog), 0);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Primary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CorrectLabelReproducesCatalogSecondarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kCatalogWeightSecondary), td::Slice(kLabelCatalog), 8);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Secondary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CorrectLabelReproducesWindowPrimarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kRouteWindowPrimary), td::Slice(kLabelWindow), 0);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Primary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CorrectLabelReproducesWindowSecondarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kRouteWindowSecondary), td::Slice(kLabelWindow), 8);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Secondary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CorrectLabelReproducesSharedPrimarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kSessionBlendPrimary), td::Slice(kLabelShared), 0);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Primary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CorrectLabelReproducesSharedSecondarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kSessionBlendSecondary), td::Slice(kLabelShared), 8);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Secondary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CorrectLabelReproducesConfigAuxiliarySlot) {
  auto recovered =
      unmask_with(static_cast<td::uint64>(td::vault_detail::kConfigWindowAuxiliary), td::Slice(kLabelConfig), 16);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Auxiliary), static_cast<td::int64>(recovered));
}

TEST(LabelDomainSeparation, CatalogMaskedValueUnderForeignLabelsDoesNotMatchAnySlot) {
  const std::array<const char *, 3> foreign_labels{{kLabelWindow, kLabelShared, kLabelConfig}};
  for (auto foreign_label : foreign_labels) {
    auto candidate_primary =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kCatalogWeightPrimary), td::Slice(foreign_label), 0);
    ASSERT_TRUE(!equals_any_known_slot(candidate_primary));
    auto candidate_secondary =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kCatalogWeightSecondary), td::Slice(foreign_label), 8);
    ASSERT_TRUE(!equals_any_known_slot(candidate_secondary));
  }
}

TEST(LabelDomainSeparation, WindowMaskedValueUnderForeignLabelsDoesNotMatchAnySlot) {
  const std::array<const char *, 3> foreign_labels{{kLabelCatalog, kLabelShared, kLabelConfig}};
  for (auto foreign_label : foreign_labels) {
    auto candidate_primary =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kRouteWindowPrimary), td::Slice(foreign_label), 0);
    ASSERT_TRUE(!equals_any_known_slot(candidate_primary));
    auto candidate_secondary =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kRouteWindowSecondary), td::Slice(foreign_label), 8);
    ASSERT_TRUE(!equals_any_known_slot(candidate_secondary));
  }
}

TEST(LabelDomainSeparation, SharedMaskedValueUnderForeignLabelsDoesNotMatchAnySlot) {
  const std::array<const char *, 3> foreign_labels{{kLabelCatalog, kLabelWindow, kLabelConfig}};
  for (auto foreign_label : foreign_labels) {
    auto candidate_primary =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kSessionBlendPrimary), td::Slice(foreign_label), 0);
    ASSERT_TRUE(!equals_any_known_slot(candidate_primary));
    auto candidate_secondary =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kSessionBlendSecondary), td::Slice(foreign_label), 8);
    ASSERT_TRUE(!equals_any_known_slot(candidate_secondary));
  }
}

TEST(LabelDomainSeparation, ConfigMaskedValueUnderForeignLabelsDoesNotMatchAnySlot) {
  const std::array<const char *, 3> foreign_labels{{kLabelCatalog, kLabelWindow, kLabelShared}};
  for (auto foreign_label : foreign_labels) {
    auto candidate =
        unmask_with(static_cast<td::uint64>(td::vault_detail::kConfigWindowAuxiliary), td::Slice(foreign_label), 16);
    ASSERT_TRUE(!equals_any_known_slot(candidate));
  }
}

}  // namespace

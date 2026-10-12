//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonWalletOwnershipProof.h"

#include "td/utils/Ed25519.h"

namespace td {

Result<TonWalletOwnershipProof> TonWalletOwnershipProof::get_ton_wallet_ownership_proof(
    td_api::object_ptr<td_api::tonWalletOwnershipProof> &&proof) {
  if (proof == nullptr) {
    return Status::Error(400, "Proof must be non-empty");
  }
  if (proof->public_key_.size() != Ed25519::PrivateKey::LENGTH) {
    return Status::Error(400, "Invalid public key specified");
  }
  if (proof->signature_.size() != 64u) {
    return Status::Error(400, "Invalid signature specified");
  }
  TonWalletOwnershipProof result;
  result.public_key_ = BufferSlice(proof->public_key_);
  result.proof_ =
      telegram_api::make_object<telegram_api::walletOwnershipProof>(proof->timestamp_, BufferSlice(proof->signature_));
  return std::move(result);
}

}  // namespace td

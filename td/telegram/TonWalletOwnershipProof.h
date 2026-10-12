//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

struct TonWalletOwnershipProof {
  BufferSlice public_key_;
  telegram_api::object_ptr<telegram_api::walletOwnershipProof> proof_;

  static Result<TonWalletOwnershipProof> get_ton_wallet_ownership_proof(
      td_api::object_ptr<td_api::tonWalletOwnershipProof> &&proof);
};

}  // namespace td

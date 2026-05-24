// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include <cstdint>
#include <utility>

namespace td {

enum class TextCompositionTonesUpdateDispatchResult : std::uint8_t {
  SkippedForBotSession,
  ReloadRequestedAndPromiseCompleted,
};

template <class ReloadAiComposeTones, class CompletePromise>
TextCompositionTonesUpdateDispatchResult dispatch_ai_compose_tones_update(
    bool is_bot_session, ReloadAiComposeTones &&reload_ai_compose_tones, CompletePromise &&complete_promise) {
  if (is_bot_session) {
    std::forward<CompletePromise>(complete_promise)();
    return TextCompositionTonesUpdateDispatchResult::SkippedForBotSession;
  }

  std::forward<ReloadAiComposeTones>(reload_ai_compose_tones)();
  std::forward<CompletePromise>(complete_promise)();
  return TextCompositionTonesUpdateDispatchResult::ReloadRequestedAndPromiseCompleted;
}

}  // namespace td
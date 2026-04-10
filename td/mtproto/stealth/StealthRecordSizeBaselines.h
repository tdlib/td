// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/StealthConfig.h"

namespace td {
namespace mtproto {
namespace stealth {
namespace baselines {

// Capture-aligned browser-envelope bins used by PR-S0 statistical tests and runtime wiring.
constexpr RecordSizeBin kGreetingRecord1[] = {{80, 95, 4}, {96, 1018, 2}, {1019, 1500, 1}};
constexpr RecordSizeBin kGreetingRecord2[] = {{80, 229, 3}, {230, 515, 3}, {516, 1453, 3}, {1454, 1500, 1}};
constexpr RecordSizeBin kGreetingRecord3[] = {{80, 972, 4}, {973, 1500, 2}};
constexpr RecordSizeBin kGreetingRecord4[] = {{80, 83, 2}, {84, 963, 6}, {964, 1453, 1}, {1454, 1500, 1}};
constexpr RecordSizeBin kGreetingRecord5[] = {{80, 963, 6}, {964, 1453, 1}, {1454, 1500, 1}};

constexpr RecordSizeBin kActiveBrowsingBins[] = {
    {200, 494, 2}, {495, 1255, 3}, {1256, 2941, 3}, {2942, 5394, 2}, {5395, 8192, 1},
};

constexpr RecordSizeBin kBulkTransferBins[] = {
    {8192, 13062, 1},
    {13063, 16401, 3},
    {16402, 16408, 4},
};

constexpr RecordSizeBin kIdleChaffBins[] = {
    {50, 98, 3},
    {99, 341, 4},
    {342, 544, 2},
    {545, 800, 1},
};

constexpr int32 kSmallRecordThreshold = 200;
constexpr double kSmallRecordMaxFraction = 0.408;

}  // namespace baselines
}  // namespace stealth
}  // namespace mtproto
}  // namespace td
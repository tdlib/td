// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Workstream E classifier gate (Tier 3 shape): evaluate whether a
// simple interpretable model can separate REAL vs GENERATED samples
// inside the same family/lane. We use LOOCV over real samples and
// synthesize 1,000 generated samples per fold. The gate fails on gross
// leaks (point AUC > 0.80) and also reports bootstrap 95% CI.

#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/WireClassifierFeatures.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr size_t kGeneratedPerFold = 1000;
constexpr size_t kGeneratedTrainPerFold = 500;
constexpr size_t kGeneratedEvalPerFold = kGeneratedPerFold - kGeneratedTrainPerFold;
constexpr size_t kBootstrapResamples = 512;
constexpr double kTier3GrossLeakAucThreshold = 0.80;
constexpr double kLower95CiLeakThreshold = 0.60;

struct FamilyLaneCase final {
  Slice family_id;
  Slice route_lane;
  BrowserProfile profile;
  EchMode ech_mode;
};

constexpr FamilyLaneCase kCase{Slice("chromium_linux_desktop"), Slice("non_ru_egress"), BrowserProfile::Chrome133,
                               EchMode::Rfc9180Outer};

using wire_classifier::FeatureMask;
using wire_classifier::SampleFeatures;

double compute_auc(const vector<double> &scores, const vector<uint8> &labels) {
  CHECK(scores.size() == labels.size());
  if (scores.empty()) {
    return 0.5;
  }

  size_t positives = 0;
  for (auto label : labels) {
    positives += (label == 1 ? 1 : 0);
  }
  const size_t negatives = labels.size() - positives;
  if (positives == 0 || negatives == 0) {
    return 0.5;
  }

  vector<std::pair<double, uint8>> rows;
  rows.reserve(scores.size());
  for (size_t i = 0; i < scores.size(); i++) {
    rows.emplace_back(scores[i], labels[i]);
  }
  std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

  double rank_sum_positive = 0.0;
  size_t index = 0;
  while (index < rows.size()) {
    size_t end = index + 1;
    while (end < rows.size() && rows[end].first == rows[index].first) {
      end++;
    }
    const double average_rank = 0.5 * static_cast<double>(index + 1 + end);
    for (size_t i = index; i < end; i++) {
      if (rows[i].second == 1) {
        rank_sum_positive += average_rank;
      }
    }
    index = end;
  }

  const double positives_d = static_cast<double>(positives);
  const double negatives_d = static_cast<double>(negatives);
  const double u_stat = rank_sum_positive - (positives_d * (positives_d + 1.0) * 0.5);
  return u_stat / (positives_d * negatives_d);
}

struct AucSummary final {
  double point_auc{0.5};
  double ci_low{0.5};
  double ci_high{0.5};
};

std::array<double, wire_classifier::kFeatureCount> compute_feature_means(const vector<SampleFeatures> &samples,
                                                                         const FeatureMask &mask) {
  std::array<double, wire_classifier::kFeatureCount> means{};
  if (samples.empty()) {
    return means;
  }
  for (const auto &sample : samples) {
    auto v = wire_classifier::to_vector(sample, mask);
    for (size_t i = 0; i < means.size(); i++) {
      means[i] += v[i];
    }
  }
  for (double &value : means) {
    value /= static_cast<double>(samples.size());
  }
  return means;
}

std::array<double, wire_classifier::kFeatureCount> compute_feature_vars(
    const vector<SampleFeatures> &samples, const std::array<double, wire_classifier::kFeatureCount> &means,
    const FeatureMask &mask) {
  std::array<double, wire_classifier::kFeatureCount> vars{};
  if (samples.size() <= 1) {
    return vars;
  }
  for (const auto &sample : samples) {
    auto v = wire_classifier::to_vector(sample, mask);
    for (size_t i = 0; i < vars.size(); i++) {
      const double diff = v[i] - means[i];
      vars[i] += diff * diff;
    }
  }
  const double denom = static_cast<double>(samples.size() - 1);
  for (double &value : vars) {
    value /= denom;
  }
  return vars;
}

struct LinearModel final {
  std::array<double, wire_classifier::kFeatureCount> weights{};
  FeatureMask mask;
  double bias{0.0};

  double score(const SampleFeatures &sample) const {
    const auto v = wire_classifier::to_vector(sample, mask);
    double out = bias;
    for (size_t i = 0; i < weights.size(); i++) {
      out += weights[i] * v[i];
    }
    return out;
  }
};

LinearModel fit_interpretable_linear_model(const vector<SampleFeatures> &real_train,
                                           const vector<SampleFeatures> &generated_train, const FeatureMask &mask) {
  constexpr double kEps = 1e-9;

  const auto real_means = compute_feature_means(real_train, mask);
  const auto generated_means = compute_feature_means(generated_train, mask);
  const auto real_vars = compute_feature_vars(real_train, real_means, mask);
  const auto generated_vars = compute_feature_vars(generated_train, generated_means, mask);

  LinearModel model;
  model.mask = mask;
  std::array<double, wire_classifier::kFeatureCount> midpoint{};
  for (size_t i = 0; i < model.weights.size(); i++) {
    const double pooled_var = real_vars[i] + generated_vars[i] + kEps;
    model.weights[i] = (real_means[i] - generated_means[i]) / pooled_var;
    midpoint[i] = 0.5 * (real_means[i] + generated_means[i]);
  }

  model.bias = 0.0;
  for (size_t i = 0; i < model.weights.size(); i++) {
    model.bias -= model.weights[i] * midpoint[i];
  }

  return model;
}

vector<SampleFeatures> collect_generated_samples(BrowserProfile profile, EchMode ech_mode, size_t sample_count,
                                                 uint64 seed_salt) {
  vector<SampleFeatures> out;
  out.reserve(sample_count);
  for (size_t i = 0; i < sample_count; i++) {
    MockRng rng((static_cast<uint64>(i) * 1315423911ULL) ^ seed_salt);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret",
                                                   kUnixTime + static_cast<int32>(i), profile, ech_mode, rng);
    out.push_back(wire_classifier::extract_generated_features(wire));
  }
  return out;
}

vector<SampleFeatures> collect_real_samples_from_baseline(const baselines::FamilyLaneBaseline &baseline) {
  vector<SampleFeatures> out;
  const size_t sample_count = td::max(static_cast<size_t>(15), baseline.authoritative_sample_count);
  out.reserve(sample_count);
  for (size_t i = 0; i < sample_count; i++) {
    out.push_back(wire_classifier::make_real_features_from_baseline_summary(baseline, i));
  }
  return out;
}

AucSummary bootstrap_auc_ci(const vector<double> &scores, const vector<uint8> &labels, size_t resamples) {
  AucSummary summary;
  summary.point_auc = compute_auc(scores, labels);

  std::mt19937_64 prng(0xA1B2C3D4E5F60789ULL);
  std::uniform_int_distribution<size_t> pick(0, scores.size() - 1);

  vector<double> auc_samples;
  auc_samples.reserve(resamples);
  vector<double> sampled_scores(scores.size());
  vector<uint8> sampled_labels(labels.size());

  while (auc_samples.size() < resamples) {
    size_t positives = 0;
    size_t negatives = 0;
    for (size_t i = 0; i < scores.size(); i++) {
      const size_t idx = pick(prng);
      sampled_scores[i] = scores[idx];
      sampled_labels[i] = labels[idx];
      if (sampled_labels[i] == 1) {
        positives++;
      } else {
        negatives++;
      }
    }
    if (positives == 0 || negatives == 0) {
      continue;
    }
    auc_samples.push_back(compute_auc(sampled_scores, sampled_labels));
  }

  std::sort(auc_samples.begin(), auc_samples.end());
  const size_t low_idx = static_cast<size_t>(std::floor(0.025 * static_cast<double>(auc_samples.size() - 1)));
  const size_t high_idx = static_cast<size_t>(std::floor(0.975 * static_cast<double>(auc_samples.size() - 1)));
  summary.ci_low = auc_samples[low_idx];
  summary.ci_high = auc_samples[high_idx];
  return summary;
}

AucSummary run_loocv_real_vs_generated_gate(const FamilyLaneCase &cfg) {
  const auto *baseline = baselines::get_baseline(cfg.family_id, cfg.route_lane);
  CHECK(baseline != nullptr);
  CHECK(baseline->authoritative_sample_count >= 15u);
  CHECK(!baseline->set_catalog.observed_wire_lengths.empty());
  CHECK(!baseline->set_catalog.observed_extension_order_templates.empty());

  const auto real_samples = collect_real_samples_from_baseline(*baseline);
  const auto mask = wire_classifier::classifier_feature_mask_for_baseline(*baseline);

  vector<double> scores;
  vector<uint8> labels;
  scores.reserve(real_samples.size() * (1 + kGeneratedEvalPerFold));
  labels.reserve(scores.capacity());

  for (size_t fold = 0; fold < real_samples.size(); fold++) {
    vector<SampleFeatures> real_train;
    real_train.reserve(real_samples.size() - 1);
    for (size_t i = 0; i < real_samples.size(); i++) {
      if (i != fold) {
        real_train.push_back(real_samples[i]);
      }
    }

    const auto generated = collect_generated_samples(cfg.profile, cfg.ech_mode, kGeneratedPerFold,
                                                     0x5D7E3A11ULL + static_cast<uint64>(fold) * 0x9E3779B97F4A7C15ULL);
    CHECK(generated.size() == kGeneratedPerFold);

    vector<SampleFeatures> generated_train;
    generated_train.reserve(kGeneratedTrainPerFold);
    for (size_t i = 0; i < kGeneratedTrainPerFold; i++) {
      generated_train.push_back(generated[i]);
    }

    const auto model = fit_interpretable_linear_model(real_train, generated_train, mask);

    scores.push_back(model.score(real_samples[fold]));
    labels.push_back(1);

    for (size_t i = kGeneratedTrainPerFold; i < generated.size(); i++) {
      scores.push_back(model.score(generated[i]));
      labels.push_back(0);
    }
  }

  return bootstrap_auc_ci(scores, labels, kBootstrapResamples);
}

TEST(TLS_WirePatternDistinguisherContract, Tier3LoocvRealVsGeneratedAucGateDoesNotDetectGrossLeak) {
  const auto summary = run_loocv_real_vs_generated_gate(kCase);

  std::fprintf(stderr, "[TLS_WirePatternDistinguisherContract] point_auc=%.4f ci95=[%.4f, %.4f]\n", summary.point_auc,
               summary.ci_low, summary.ci_high);

  ASSERT_TRUE(std::isfinite(summary.point_auc));
  ASSERT_TRUE(std::isfinite(summary.ci_low));
  ASSERT_TRUE(std::isfinite(summary.ci_high));
  ASSERT_TRUE(summary.ci_low <= summary.point_auc + 1e-12);
  ASSERT_TRUE(summary.point_auc <= summary.ci_high + 1e-12);

  // Tier-3 LOOCV gross-leak detector per plan Section 12.
  ASSERT_TRUE(summary.point_auc <= kTier3GrossLeakAucThreshold);
  // Secondary confidence bound retained for release reporting continuity.
  ASSERT_TRUE(summary.ci_low <= kLower95CiLeakThreshold);
}

TEST(TLS_WirePatternDistinguisherContractMetrics, AucIsOneForPerfectSeparation) {
  vector<double> scores = {0.9, 0.8, 0.7, 0.2, 0.1, 0.0};
  vector<uint8> labels = {1, 1, 1, 0, 0, 0};
  ASSERT_TRUE(std::fabs(1.0 - compute_auc(scores, labels)) < 1e-12);
}

TEST(TLS_WirePatternDistinguisherContractMetrics, AucIsHalfForConstantScores) {
  vector<double> scores = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  vector<uint8> labels = {1, 1, 1, 0, 0, 0};
  ASSERT_TRUE(std::fabs(0.5 - compute_auc(scores, labels)) < 1e-12);
}

TEST(TLS_WirePatternDistinguisherContractMetrics, BootstrapIntervalContainsPointEstimate) {
  vector<double> scores = {0.91, 0.87, 0.81, 0.77, 0.73, 0.69, 0.61, 0.55,
                           0.49, 0.43, 0.39, 0.31, 0.27, 0.19, 0.13, 0.07};
  vector<uint8> labels = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};

  const auto summary = bootstrap_auc_ci(scores, labels, 256);
  ASSERT_TRUE(summary.ci_low <= summary.point_auc + 1e-12);
  ASSERT_TRUE(summary.point_auc <= summary.ci_high + 1e-12);
}

}  // namespace

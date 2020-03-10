//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/PublicRsaKeyShared.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <algorithm>

namespace td {

PublicRsaKeyShared::PublicRsaKeyShared(DcId dc_id, bool is_test) : dc_id_(dc_id) {
  if (!dc_id_.is_empty()) {
    return;
  }
  auto add_pem = [this](CSlice pem) {
    auto r_rsa = RSA::from_pem_public_key(pem);
    LOG_CHECK(r_rsa.is_ok()) << r_rsa.error() << " " << pem;

    if (r_rsa.is_ok()) {
      this->add_rsa(r_rsa.move_as_ok());
    }
  };

  if (is_test) {
    add_pem(
        "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIIBCgKCAQEAr4v4wxMDXIaMOh8bayF/NyoYdpcysn5EbjTIOZC0RkgzsRj3SGlu\n"
        "52QSz+ysO41dQAjpFLgxPVJoOlxXokaOq827IfW0bGCm0doT5hxtedu9UCQKbE8j\n"
        "lDOk+kWMXHPZFJKWRgKgTu9hcB3y3Vk+JFfLpq3d5ZB48B4bcwrRQnzkx5GhWOFX\n"
        "x73ZgjO93eoQ2b/lDyXxK4B4IS+hZhjzezPZTI5upTRbs5ljlApsddsHrKk6jJNj\n"
        "8Ygs/ps8e6ct82jLXbnndC9s8HjEvDvBPH9IPjv5JUlmHMBFZ5vFQIfbpo0u0+1P\n"
        "n6bkEi5o7/ifoyVv2pAZTRwppTz0EuXD8QIDAQAB\n"
        "-----END RSA PUBLIC KEY-----");
    return;
  }

  //old_key
  add_pem(
      "-----BEGIN RSA PUBLIC KEY-----\n"
      "MIIBCgKCAQEAwVACPi9w23mF3tBkdZz+zwrzKOaaQdr01vAbU4E1pvkfj4sqDsm6\n"
      "lyDONS789sVoD/xCS9Y0hkkC3gtL1tSfTlgCMOOul9lcixlEKzwKENj1Yz/s7daS\n"
      "an9tqw3bfUV/nqgbhGX81v/+7RFAEd+RwFnK7a+XYl9sluzHRyVVaTTveB2GazTw\n"
      "Efzk2DWgkBluml8OREmvfraX3bkHZJTKX4EQSjBbbdJ2ZXIsRrYOXfaA+xayEGB+\n"
      "8hdlLmAjbCVfaigxX0CDqWeR1yFL9kwd9P0NsZRPsmoqVwMbMu7mStFai6aIhc3n\n"
      "Slv8kg9qv1m6XHVQY3PnEw+QQtqSIXklHwIDAQAB\n"
      "-----END RSA PUBLIC KEY-----");

  // a35e0b92d00f9b61c351ce30526cb855649b12a35e01fe39b5b315e81b515779  key1.pub
  add_pem(
      "-----BEGIN RSA PUBLIC KEY-----\n"
      "MIIBCgKCAQEAruw2yP/BCcsJliRoW5eB\n"
      "VBVle9dtjJw+OYED160Wybum9SXtBBLXriwt4rROd9csv0t0OHCaTmRqBcQ0J8fx\n"
      "hN6/cpR1GWgOZRUAiQxoMnlt0R93LCX/j1dnVa/gVbCjdSxpbrfY2g2L4frzjJvd\n"
      "l84Kd9ORYjDEAyFnEA7dD556OptgLQQ2e2iVNq8NZLYTzLp5YpOdO1doK+ttrltg\n"
      "gTCy5SrKeLoCPPbOgGsdxJxyz5KKcZnSLj16yE5HvJQn0CNpRdENvRUXe6tBP78O\n"
      "39oJ8BTHp9oIjd6XWXAsp2CvK45Ol8wFXGF710w9lwCGNbmNxNYhtIkdqfsEcwR5\n"
      "JwIDAQAB\n"
      "-----END RSA PUBLIC KEY-----\n");

  // f1c346bd6de0c3365658e0740de42372e51262099d47ee097c3ff1e238ebf985  key2.pub
  add_pem(
      "-----BEGIN RSA PUBLIC KEY-----\n"
      "MIIBCgKCAQEAvfLHfYH2r9R70w8prHbl\n"
      "Wt/nDkh+XkgpflqQVcnAfSuTtO05lNPspQmL8Y2XjVT4t8cT6xAkdgfmmvnvRPOO\n"
      "KPi0OfJXoRVylFzAQG/j83u5K3kRLbae7fLccVhKZhY46lvsueI1hQdLgNV9n1cQ\n"
      "3TDS2pQOCtovG4eDl9wacrXOJTG2990VjgnIKNA0UMoP+KF03qzryqIt3oTvZq03\n"
      "DyWdGK+AZjgBLaDKSnC6qD2cFY81UryRWOab8zKkWAnhw2kFpcqhI0jdV5QaSCEx\n"
      "vnsjVaX0Y1N0870931/5Jb9ICe4nweZ9kSDF/gip3kWLG0o8XQpChDfyvsqB9OLV\n"
      "/wIDAQAB\n"
      "-----END RSA PUBLIC KEY-----\n");

  // 129e129a464a2b515f579fd568f5579e8a6ea2832a362b07f282a7c271acfead  key3.pub
  add_pem(
      "-----BEGIN RSA PUBLIC KEY-----\n"
      "MIIBCgKCAQEAs/ditzm+mPND6xkhzwFI\n"
      "z6J/968CtkcSE/7Z2qAJiXbmZ3UDJPGrzqTDHkO30R8VeRM/Kz2f4nR05GIFiITl\n"
      "4bEjvpy7xqRDspJcCFIOcyXm8abVDhF+th6knSU0yLtNKuQVP6voMrnt9MV1X92L\n"
      "GZQLgdHZbPQz0Z5qIpaKhdyA8DEvWWvSUwwc+yi1/gGaybwlzZwqXYoPOhwMebzK\n"
      "Uk0xW14htcJrRrq+PXXQbRzTMynseCoPIoke0dtCodbA3qQxQovE16q9zz4Otv2k\n"
      "4j63cz53J+mhkVWAeWxVGI0lltJmWtEYK6er8VqqWot3nqmWMXogrgRLggv/Nbbo\n"
      "oQIDAQAB\n"
      "-----END RSA PUBLIC KEY-----\n");

  // f9e47d59fbe0fa338ac8c5085201a0dd58dfd88f44abb16756ee5e9d50d52949  key4.pub
  add_pem(
      "-----BEGIN RSA PUBLIC KEY-----\n"
      "MIIBCgKCAQEAvmpxVY7ld/8DAjz6F6q0\n"
      "5shjg8/4p6047bn6/m8yPy1RBsvIyvuDuGnP/RzPEhzXQ9UJ5Ynmh2XJZgHoE9xb\n"
      "nfxL5BXHplJhMtADXKM9bWB11PU1Eioc3+AXBB8QiNFBn2XI5UkO5hPhbb9mJpjA\n"
      "9Uhw8EdfqJP8QetVsI/xrCEbwEXe0xvifRLJbY08/Gp66KpQvy7g8w7VB8wlgePe\n"
      "xW3pT13Ap6vuC+mQuJPyiHvSxjEKHgqePji9NP3tJUFQjcECqcm0yV7/2d0t/pbC\n"
      "m+ZH1sadZspQCEPPrtbkQBlvHb4OLiIWPGHKSMeRFvp3IWcmdJqXahxLCUS1Eh6M\n"
      "AQIDAQAB\n"
      "-----END RSA PUBLIC KEY-----\n");
}

void PublicRsaKeyShared::add_rsa(RSA rsa) {
  auto lock = rw_mutex_.lock_write();
  auto fingerprint = rsa.get_fingerprint();
  auto *has_rsa = get_rsa_locked(fingerprint);
  if (has_rsa) {
    return;
  }
  options_.push_back(RsaOption{fingerprint, std::move(rsa)});
}

Result<std::pair<RSA, int64>> PublicRsaKeyShared::get_rsa(const vector<int64> &fingerprints) {
  auto lock = rw_mutex_.lock_read();
  for (auto fingerprint : fingerprints) {
    auto *rsa = get_rsa_locked(fingerprint);
    if (rsa) {
      return std::make_pair(rsa->clone(), fingerprint);
    }
  }
  return Status::Error(PSLICE() << "Unknown fingerprints " << format::as_array(fingerprints));
}

void PublicRsaKeyShared::drop_keys() {
  if (dc_id_.is_empty()) {
    return;
  }
  auto lock = rw_mutex_.lock_write();
  options_.clear();
}

bool PublicRsaKeyShared::has_keys() {
  auto lock = rw_mutex_.lock_read();
  return !options_.empty();
}

void PublicRsaKeyShared::add_listener(unique_ptr<Listener> listener) {
  if (listener->notify()) {
    auto lock = rw_mutex_.lock_write();
    listeners_.push_back(std::move(listener));
  }
}

RSA *PublicRsaKeyShared::get_rsa_locked(int64 fingerprint) {
  auto it = std::find_if(options_.begin(), options_.end(),
                         [&](const auto &value) { return value.fingerprint == fingerprint; });
  if (it == options_.end()) {
    return nullptr;
  }
  return &it->rsa;
}

void PublicRsaKeyShared::notify() {
  auto lock = rw_mutex_.lock_read();
  td::remove_if(listeners_, [&](auto &listener) { return !listener->notify(); });
}

}  // namespace td

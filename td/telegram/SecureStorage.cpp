//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureStorage.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"

namespace td {
namespace secure_storage {

// Helpers
Result<ValueHash> ValueHash::create(Slice data) {
  UInt256 hash;
  if (data.size() != ::td::as_slice(hash).size()) {
    return Status::Error(PSLICE() << "Wrong hash size " << data.size());
  }
  ::td::as_slice(hash).copy_from(data);
  return ValueHash{hash};
}

static AesCbcState calc_aes_cbc_state_hash(Slice hash) {
  CHECK(hash.size() == 64);
  UInt256 key;
  as_slice(key).copy_from(hash.substr(0, 32));
  UInt128 iv;
  as_slice(iv).copy_from(hash.substr(32, 16));
  LOG(INFO) << "End AES CBC state calculation";
  return AesCbcState{key, iv};
}

AesCbcState calc_aes_cbc_state_pbkdf2(Slice secret, Slice salt) {
  LOG(INFO) << "Begin AES CBC state calculation";
  UInt<512> hash;
  auto hash_slice = as_slice(hash);
  pbkdf2_sha512(secret, salt, 100000, hash_slice);
  return calc_aes_cbc_state_hash(hash_slice);
}

AesCbcState calc_aes_cbc_state_sha512(Slice seed) {
  LOG(INFO) << "Begin AES CBC state calculation";
  UInt<512> hash;
  auto hash_slice = as_slice(hash);
  sha512(seed, hash_slice);
  return calc_aes_cbc_state_hash(hash_slice);
}

template <class F>
static Status data_view_for_each(DataView &data, F &&f) {
  const int64 step = 128 << 10;
  for (int64 i = 0, size = data.size(); i < size; i += step) {
    TRY_RESULT(bytes, data.pread(i, min(step, size - i)));
    TRY_STATUS(f(std::move(bytes)));
  }
  return Status::OK();
}

Result<ValueHash> calc_value_hash(DataView &data_view) {
  Sha256State state;
  sha256_init(&state);
  data_view_for_each(data_view, [&state](BufferSlice bytes) {
    sha256_update(bytes.as_slice(), &state);
    return Status::OK();
  });
  UInt256 res;
  sha256_final(&state, as_slice(res));
  return ValueHash{res};
}

ValueHash calc_value_hash(Slice data) {
  UInt256 res;
  sha256(data, as_slice(res));
  return ValueHash{res};
}

BufferSlice gen_random_prefix(int64 data_size) {
  BufferSlice buff(narrow_cast<size_t>(((32 + 15 + data_size) & -16) - data_size));
  Random::secure_bytes(buff.as_slice());
  buff.as_slice()[0] = narrow_cast<uint8>(buff.size());
  CHECK((buff.size() + data_size) % 16 == 0);
  return buff;
}

FileDataView::FileDataView(FileFd &fd, int64 size) : fd_(fd), size_(size) {
}

int64 FileDataView::size() const {
  return size_;
}

Result<BufferSlice> FileDataView::pread(int64 offset, int64 size) {
  auto slice = BufferSlice(narrow_cast<size_t>(size));
  TRY_RESULT(actual_size, fd_.pread(slice.as_slice(), offset));
  if (static_cast<int64>(actual_size) != size) {
    return Status::Error("Not enough data in file");
  }
  return std::move(slice);
}

BufferSliceDataView::BufferSliceDataView(BufferSlice buffer_slice) : buffer_slice_(std::move(buffer_slice)) {
}

int64 BufferSliceDataView::size() const {
  return narrow_cast<int64>(buffer_slice_.size());
}

Result<BufferSlice> BufferSliceDataView::pread(int64 offset, int64 size) {
  auto end_offset = size + offset;
  if (this->size() < end_offset) {
    return Status::Error("Not enough data in BufferSlice");
  }
  return BufferSlice(buffer_slice_.as_slice().substr(narrow_cast<size_t>(offset), narrow_cast<size_t>(size)));
}

ConcatDataView::ConcatDataView(DataView &left, DataView &right) : left_(left), right_(right) {
}

int64 ConcatDataView::size() const {
  return left_.size() + right_.size();
}

Result<BufferSlice> ConcatDataView::pread(int64 offset, int64 size) {
  auto end_offset = size + offset;
  if (this->size() < end_offset) {
    return Status::Error("Not enough data in ConcatDataView");
  }

  auto substr = [](DataView &slice, int64 offset, int64 size) -> Result<BufferSlice> {
    auto l = max(int64{0}, offset);
    auto r = min(slice.size(), offset + size);
    if (l >= r) {
      return BufferSlice();
    }
    return slice.pread(l, r - l);
  };

  TRY_RESULT(a, substr(left_, offset, size));
  TRY_RESULT(b, substr(right_, offset - left_.size(), size));

  if (a.empty()) {
    return std::move(b);
  }
  if (b.empty()) {
    return std::move(a);
  }

  BufferSlice res(a.size() + b.size());
  res.as_slice().copy_from(a.as_slice());
  res.as_slice().substr(a.size()).copy_from(b.as_slice());
  return std::move(res);
}

Password::Password(std::string password) : password_(std::move(password)) {
}

Slice Password::as_slice() const {
  return password_;
}

static uint8 secret_checksum(Slice secret) {
  uint32 sum = 0;
  for (uint8 c : secret) {
    sum += c;
  }
  return static_cast<uint8>((255 + 239 - sum % 255) % 255);
}

Result<Secret> Secret::create(Slice secret) {
  if (secret.size() != 32) {
    return Status::Error("wrong secret size");
  }
  uint32 checksum = secret_checksum(secret);
  if (checksum != 0) {
    return Status::Error(PSLICE() << "Wrong checksum " << checksum);
  }
  UInt256 res;
  td::as_slice(res).copy_from(secret);

  UInt256 secret_sha256;
  sha256(secret, ::td::as_slice(secret_sha256));
  auto hash = as<int64>(secret_sha256.raw);
  return Secret{res, hash};
}

Secret Secret::create_new() {
  UInt256 secret;
  auto secret_slice = td::as_slice(secret);
  Random::secure_bytes(secret_slice);
  auto checksum_diff = secret_checksum(secret_slice);
  uint8 new_byte = static_cast<uint8>((static_cast<uint32>(secret_slice.ubegin()[0]) + checksum_diff) % 255);
  secret_slice.ubegin()[0] = new_byte;
  return create(secret_slice).move_as_ok();
}

Slice Secret::as_slice() const {
  using td::as_slice;
  return as_slice(secret_);
}

int64 Secret::get_hash() const {
  return hash_;
}

Secret Secret::clone() const {
  return {secret_, hash_};
}

EncryptedSecret Secret::encrypt(Slice key, Slice salt, EnryptionAlgorithm algorithm) {
  auto aes_cbc_state = [&]() {
    switch (algorithm) {
      case EnryptionAlgorithm::Sha512:
        return calc_aes_cbc_state_sha512(PSLICE() << salt << key << salt);
      case EnryptionAlgorithm::Pbkdf2:
        return calc_aes_cbc_state_pbkdf2(key, salt);
      default:
        UNREACHABLE();
        return AesCbcState(UInt256(), UInt128());
    }
  }();

  UInt256 res;
  aes_cbc_state.encrypt(as_slice(), td::as_slice(res));
  return EncryptedSecret::create(td::as_slice(res)).move_as_ok();
}

Secret::Secret(UInt256 secret, int64 hash) : secret_(secret), hash_(hash) {
}

Result<EncryptedSecret> EncryptedSecret::create(Slice encrypted_secret) {
  if (encrypted_secret.size() != 32) {
    return Status::Error("Wrong encrypted secret size");
  }
  UInt256 res;
  td::as_slice(res).copy_from(encrypted_secret);
  return EncryptedSecret{res};
}

Result<Secret> EncryptedSecret::decrypt(Slice key, Slice salt, EnryptionAlgorithm algorithm) {
  auto aes_cbc_state = [&]() {
    switch (algorithm) {
      case EnryptionAlgorithm::Sha512:
        return calc_aes_cbc_state_sha512(PSLICE() << salt << key << salt);
      case EnryptionAlgorithm::Pbkdf2:
        return calc_aes_cbc_state_pbkdf2(key, salt);
      default:
        UNREACHABLE();
        return AesCbcState(UInt256(), UInt128());
    }
  }();

  UInt256 res;
  aes_cbc_state.decrypt(td::as_slice(encrypted_secret_), td::as_slice(res));
  return Secret::create(td::as_slice(res));
}

Slice EncryptedSecret::as_slice() const {
  return td::as_slice(encrypted_secret_);
}

EncryptedSecret::EncryptedSecret(UInt256 encrypted_secret) : encrypted_secret_(encrypted_secret) {
}

Decryptor::Decryptor(AesCbcState aes_cbc_state) : aes_cbc_state_(std::move(aes_cbc_state)) {
  sha256_init(&sha256_state_);
}

Result<BufferSlice> Decryptor::append(BufferSlice data) {
  if (data.empty()) {
    return BufferSlice();
  }
  if (data.size() % 16 != 0) {
    return Status::Error("Part size should be divisible by 16");
  }
  aes_cbc_state_.decrypt(data.as_slice(), data.as_slice());
  sha256_update(data.as_slice(), &sha256_state_);
  if (!skipped_prefix_) {
    to_skip_ = data.as_slice().ubegin()[0];
    size_t to_skip = min(to_skip_, data.size());
    if (to_skip_ > data.size()) {
      to_skip_ = 0;  // to fail final to_skip check
    }
    skipped_prefix_ = true;
    data = data.from_slice(data.as_slice().remove_prefix(to_skip));
  }
  return std::move(data);
}

Result<ValueHash> Decryptor::finish() {
  if (!skipped_prefix_) {
    return Status::Error("No data was given");
  }
  if (to_skip_ < 32) {
    return Status::Error("Too small random prefix");
  }
  UInt256 res;
  sha256_final(&sha256_state_, as_slice(res));
  return ValueHash{res};
}

Encryptor::Encryptor(AesCbcState aes_cbc_state, DataView &data_view)
    : aes_cbc_state_(std::move(aes_cbc_state)), data_view_(data_view) {
}

int64 Encryptor::size() const {
  return data_view_.size();
}

Result<BufferSlice> Encryptor::pread(int64 offset, int64 size) {
  if (offset != current_offset_) {
    return Status::Error("Arbitrary offset is not supported");
  }
  if (size % 16 != 0) {
    return Status::Error("Part size should be divisible by 16");
  }
  TRY_RESULT(part, data_view_.pread(offset, size));
  aes_cbc_state_.encrypt(part.as_slice(), part.as_slice());
  current_offset_ += size;
  return std::move(part);
}

Result<EncryptedValue> encrypt_value(const Secret &secret, Slice data) {
  BufferSliceDataView random_prefix_view{gen_random_prefix(data.size())};
  BufferSliceDataView data_view{BufferSlice(data)};
  ConcatDataView full_view{random_prefix_view, data_view};

  TRY_RESULT(hash, calc_value_hash(full_view));

  auto aes_cbc_state = calc_aes_cbc_state_sha512(PSLICE() << secret.as_slice() << hash.as_slice());
  Encryptor encryptor(aes_cbc_state, full_view);
  TRY_RESULT(encrypted_data, encryptor.pread(0, encryptor.size()));
  return EncryptedValue{std::move(encrypted_data), std::move(hash)};
}

Result<BufferSlice> decrypt_value(const Secret &secret, const ValueHash &hash, Slice data) {
  auto aes_cbc_state = calc_aes_cbc_state_sha512(PSLICE() << secret.as_slice() << hash.as_slice());
  Decryptor decryptor(aes_cbc_state);
  TRY_RESULT(decrypted_value, decryptor.append(BufferSlice(data)));
  TRY_RESULT(got_hash, decryptor.finish());
  if (got_hash.as_slice() != hash.as_slice()) {
    return Status::Error(PSLICE() << "Hash mismatch " << format::as_hex_dump<4>(got_hash.as_slice()) << " "
                                  << format::as_hex_dump<4>(hash.as_slice()));
  }
  return std::move(decrypted_value);
}

Result<ValueHash> encrypt_file(const Secret &secret, std::string src, std::string dest) {
  TRY_RESULT(src_file, FileFd::open(src, FileFd::Flags::Read));
  TRY_RESULT(dest_file, FileFd::open(dest, FileFd::Flags::Truncate | FileFd::Flags::Write | FileFd::Create));
  auto src_file_size = src_file.get_size();

  BufferSliceDataView random_prefix_view(gen_random_prefix(src_file_size));
  FileDataView data_view(src_file, src_file_size);
  ConcatDataView full_view(random_prefix_view, data_view);

  TRY_RESULT(hash, calc_value_hash(full_view));

  auto aes_cbc_state = calc_aes_cbc_state_sha512(PSLICE() << secret.as_slice() << hash.as_slice());
  Encryptor encryptor(aes_cbc_state, full_view);
  TRY_STATUS(
      data_view_for_each(encryptor, [&dest_file](BufferSlice bytes) { return dest_file.write(bytes.as_slice()); }));
  return std::move(hash);
}

Status decrypt_file(const Secret &secret, const ValueHash &hash, std::string src, std::string dest) {
  TRY_RESULT(src_file, FileFd::open(src, FileFd::Flags::Read));
  TRY_RESULT(dest_file, FileFd::open(dest, FileFd::Flags::Truncate | FileFd::Flags::Write | FileFd::Create));
  auto src_file_size = src_file.get_size();

  FileDataView src_file_view(src_file, src_file_size);

  auto aes_cbc_state = calc_aes_cbc_state_sha512(PSLICE() << secret.as_slice() << hash.as_slice());
  Decryptor decryptor(aes_cbc_state);
  TRY_STATUS(data_view_for_each(src_file_view, [&decryptor, &dest_file](BufferSlice bytes) {
    TRY_RESULT(decrypted_bytes, decryptor.append(std::move(bytes)));
    TRY_STATUS(dest_file.write(decrypted_bytes.as_slice()));
    return Status::OK();
  }));

  TRY_RESULT(got_hash, decryptor.finish());

  if (hash.as_slice() != got_hash.as_slice()) {
    return Status::Error("Hash mismatch");
  }

  return Status::OK();
}

}  // namespace secure_storage
}  // namespace td

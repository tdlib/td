//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureValue.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/utils/misc.h"
#include "td/utils/overloaded.h"

namespace td {

SecureValueType get_secure_value_type(tl_object_ptr<telegram_api::SecureValueType> &&secure_value_type) {
  CHECK(secure_value_type != nullptr);
  switch (secure_value_type->get_id()) {
    case telegram_api::secureValueTypePersonalDetails::ID:
      return SecureValueType::PersonalDetails;
    case telegram_api::secureValueTypePassport::ID:
      return SecureValueType::Passport;
    case telegram_api::secureValueTypeDriverLicense::ID:
      return SecureValueType::DriverLicense;
    case telegram_api::secureValueTypeIdentityCard::ID:
      return SecureValueType::IdentityCard;
    case telegram_api::secureValueTypeAddress::ID:
      return SecureValueType::Address;
    case telegram_api::secureValueTypeUtilityBill::ID:
      return SecureValueType::UtilityBill;
    case telegram_api::secureValueTypeBankStatement::ID:
      return SecureValueType::BankStatement;
    case telegram_api::secureValueTypeRentalAgreement::ID:
      return SecureValueType::RentalAgreement;
    case telegram_api::secureValueTypePhone::ID:
      return SecureValueType::PhoneNumber;
    case telegram_api::secureValueTypeEmail::ID:
      return SecureValueType::EmailAddress;
    default:
      UNREACHABLE();
      return SecureValueType::None;
  }
}

SecureValueType get_secure_value_type_td_api(tl_object_ptr<td_api::PassportDataType> &&passport_data_type) {
  CHECK(passport_data_type != nullptr);
  switch (passport_data_type->get_id()) {
    case td_api::passportDataTypePersonalDetails::ID:
      return SecureValueType::PersonalDetails;
    case td_api::passportDataTypePassport::ID:
      return SecureValueType::Passport;
    case td_api::passportDataTypeDriverLicense::ID:
      return SecureValueType::DriverLicense;
    case td_api::passportDataTypeIdentityCard::ID:
      return SecureValueType::IdentityCard;
    case td_api::passportDataTypeAddress::ID:
      return SecureValueType::Address;
    case td_api::passportDataTypeUtilityBill::ID:
      return SecureValueType::UtilityBill;
    case td_api::passportDataTypeBankStatement::ID:
      return SecureValueType::BankStatement;
    case td_api::passportDataTypeRentalAgreement::ID:
      return SecureValueType::RentalAgreement;
    case td_api::passportDataTypePhoneNumber::ID:
      return SecureValueType::PhoneNumber;
    case td_api::passportDataTypeEmailAddress::ID:
      return SecureValueType::EmailAddress;
    default:
      UNREACHABLE();
      return SecureValueType::None;
  }
}

vector<SecureValueType> get_secure_value_types(
    vector<tl_object_ptr<telegram_api::SecureValueType>> &&secure_value_types) {
  return transform(std::move(secure_value_types), get_secure_value_type);
}

td_api::object_ptr<td_api::PassportDataType> get_passport_data_type_object(SecureValueType type) {
  switch (type) {
    case SecureValueType::PersonalDetails:
      return td_api::make_object<td_api::passportDataTypePersonalDetails>();
    case SecureValueType::Passport:
      return td_api::make_object<td_api::passportDataTypePassport>();
    case SecureValueType::DriverLicense:
      return td_api::make_object<td_api::passportDataTypeDriverLicense>();
    case SecureValueType::IdentityCard:
      return td_api::make_object<td_api::passportDataTypeIdentityCard>();
    case SecureValueType::Address:
      return td_api::make_object<td_api::passportDataTypeAddress>();
    case SecureValueType::UtilityBill:
      return td_api::make_object<td_api::passportDataTypeUtilityBill>();
    case SecureValueType::BankStatement:
      return td_api::make_object<td_api::passportDataTypeBankStatement>();
    case SecureValueType::RentalAgreement:
      return td_api::make_object<td_api::passportDataTypeRentalAgreement>();
    case SecureValueType::PhoneNumber:
      return td_api::make_object<td_api::passportDataTypePhoneNumber>();
    case SecureValueType::EmailAddress:
      return td_api::make_object<td_api::passportDataTypeEmailAddress>();
    case SecureValueType::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<telegram_api::SecureValueType> get_secure_value_type_telegram_object(SecureValueType type) {
  switch (type) {
    case SecureValueType::PersonalDetails:
      return telegram_api::make_object<telegram_api::secureValueTypePersonalDetails>();
    case SecureValueType::Passport:
      return telegram_api::make_object<telegram_api::secureValueTypePassport>();
    case SecureValueType::DriverLicense:
      return telegram_api::make_object<telegram_api::secureValueTypeDriverLicense>();
    case SecureValueType::IdentityCard:
      return telegram_api::make_object<telegram_api::secureValueTypeIdentityCard>();
    case SecureValueType::Address:
      return telegram_api::make_object<telegram_api::secureValueTypeAddress>();
    case SecureValueType::UtilityBill:
      return telegram_api::make_object<telegram_api::secureValueTypeUtilityBill>();
    case SecureValueType::BankStatement:
      return telegram_api::make_object<telegram_api::secureValueTypeBankStatement>();
    case SecureValueType::RentalAgreement:
      return telegram_api::make_object<telegram_api::secureValueTypeRentalAgreement>();
    case SecureValueType::PhoneNumber:
      return telegram_api::make_object<telegram_api::secureValueTypePhone>();
    case SecureValueType::EmailAddress:
      return telegram_api::make_object<telegram_api::secureValueTypeEmail>();
    case SecureValueType::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<td_api::object_ptr<td_api::PassportDataType>> get_passport_data_types_object(
    const vector<SecureValueType> &types) {
  return transform(types, get_passport_data_type_object);
}

bool operator==(const SecureFile &lhs, const SecureFile &rhs) {
  return lhs.file_id == rhs.file_id && lhs.file_hash == rhs.file_hash && lhs.encrypted_secret == rhs.encrypted_secret;
}

bool operator!=(const SecureFile &lhs, const SecureFile &rhs) {
  return !(lhs == rhs);
}

SecureFile get_secure_file(FileManager *file_manager, tl_object_ptr<telegram_api::SecureFile> &&secure_file_ptr) {
  CHECK(secure_file_ptr != nullptr);
  SecureFile result;
  switch (secure_file_ptr->get_id()) {
    case telegram_api::secureFileEmpty::ID:
      break;
    case telegram_api::secureFile::ID: {
      auto secure_file = telegram_api::move_object_as<telegram_api::secureFile>(secure_file_ptr);
      auto dc_id = secure_file->dc_id_;
      if (!DcId::is_valid(dc_id)) {
        LOG(ERROR) << "Wrong dc_id = " << dc_id;
        break;
      }
      result.file_id = file_manager->register_remote(
          FullRemoteFileLocation(FileType::Secure, secure_file->id_, secure_file->access_hash_, DcId::internal(dc_id)),
          FileLocationSource::FromServer, {}, 0, 0, "");
      result.encrypted_secret = secure_file->secret_.as_slice().str();
      result.file_hash = secure_file->file_hash_.as_slice().str();
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

vector<SecureFile> get_secure_files(FileManager *file_manager,
                                    vector<tl_object_ptr<telegram_api::SecureFile>> &&secure_files) {
  vector<SecureFile> results;
  results.reserve(secure_files.size());
  for (auto &secure_file : secure_files) {
    auto result = get_secure_file(file_manager, std::move(secure_file));
    if (result.file_id.is_valid()) {
      results.push_back(std::move(result));
    }
  }
  return results;
}

telegram_api::object_ptr<telegram_api::InputSecureFile> get_input_secure_file_object(FileManager *file_manager,
                                                                                     const SecureFile &file,
                                                                                     SecureInputFile &input_file) {
  CHECK(file_manager->get_file_view(file.file_id).file_id() ==
        file_manager->get_file_view(input_file.file_id).file_id());
  auto res = std::move(input_file.input_file);
  if (res == nullptr) {
    return file_manager->get_file_view(file.file_id).remote_location().as_input_secure_file();
  }
  telegram_api::downcast_call(*res, overloaded(
                                        [&](telegram_api::inputSecureFileUploaded &uploaded) {
                                          uploaded.secret_ = BufferSlice(file.encrypted_secret);
                                          uploaded.file_hash_ = BufferSlice(file.file_hash);
                                        },
                                        [&](telegram_api::inputSecureFile &) { UNREACHABLE(); }));
  return res;
}

td_api::object_ptr<td_api::file> get_encrypted_file_object(FileManager *file_manager, const SecureFile &file) {
  auto file_view = file_manager->get_file_view(file.file_id);
  auto file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::SecureRaw, file_view.remote_location().get_id(),
                             file_view.remote_location().get_access_hash(), file_view.remote_location().get_dc_id()),
      FileLocationSource::FromServer, {}, 0, 0, "");
  return file_manager->get_file_object(file_id);
}

vector<td_api::object_ptr<td_api::file>> get_encrypted_files_object(FileManager *file_manager,
                                                                    const vector<SecureFile> &files) {
  return transform(files,
                   [file_manager](const SecureFile &file) { return get_encrypted_file_object(file_manager, file); });
}

vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> get_input_secure_files_object(
    FileManager *file_manager, const vector<SecureFile> &files, vector<SecureInputFile> &input_files) {
  CHECK(files.size() == input_files.size());
  vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> res;
  res.resize(files.size());
  for (size_t i = 0; i < files.size(); i++) {
    res[i] = get_input_secure_file_object(file_manager, files[i], input_files[i]);
  }
  LOG(ERROR) << res.size();
  return res;
}

bool operator==(const SecureData &lhs, const SecureData &rhs) {
  return lhs.data == rhs.data && lhs.hash == rhs.hash && lhs.encrypted_secret == rhs.encrypted_secret;
}

bool operator!=(const SecureData &lhs, const SecureData &rhs) {
  return !(lhs == rhs);
}

SecureData get_secure_data(tl_object_ptr<telegram_api::secureData> &&secure_data) {
  CHECK(secure_data != nullptr);
  SecureData result;
  result.data = secure_data->data_.as_slice().str();
  result.hash = secure_data->data_hash_.as_slice().str();
  result.encrypted_secret = secure_data->secret_.as_slice().str();
  return result;
}

telegram_api::object_ptr<telegram_api::secureData> get_secure_data_object(const SecureData &data) {
  return telegram_api::make_object<telegram_api::secureData>(BufferSlice(data.data), BufferSlice(data.hash),
                                                             BufferSlice(data.encrypted_secret));
}

bool operator==(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs) {
  return lhs.type == rhs.type && lhs.data == rhs.data && lhs.files == rhs.files && lhs.selfie == rhs.selfie;
}

bool operator!=(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs) {
  return !(lhs == rhs);
}

EncryptedSecureValue get_encrypted_secure_value(FileManager *file_manager,
                                                tl_object_ptr<telegram_api::secureValue> &&secure_value) {
  EncryptedSecureValue result;
  CHECK(secure_value != nullptr);
  result.type = get_secure_value_type(std::move(secure_value->type_));
  if (secure_value->plain_data_ != nullptr) {
    switch (secure_value->plain_data_->get_id()) {
      case telegram_api::securePlainPhone::ID:
        result.data.data =
            std::move(static_cast<telegram_api::securePlainPhone *>(secure_value->plain_data_.get())->phone_);
        break;
      case telegram_api::securePlainEmail::ID:
        result.data.data =
            std::move(static_cast<telegram_api::securePlainEmail *>(secure_value->plain_data_.get())->email_);
        break;
      default:
        UNREACHABLE();
    }
  }
  if (secure_value->data_ != nullptr) {
    result.data = get_secure_data(std::move(secure_value->data_));
  }
  result.files = get_secure_files(file_manager, std::move(secure_value->files_));
  if (secure_value->selfie_ != nullptr) {
    result.selfie = get_secure_file(file_manager, std::move(secure_value->selfie_));
  }
  return result;
}

vector<EncryptedSecureValue> get_encrypted_secure_values(
    FileManager *file_manager, vector<tl_object_ptr<telegram_api::secureValue>> &&secure_values) {
  return transform(std::move(secure_values), [file_manager](tl_object_ptr<telegram_api::secureValue> &&secure_value) {
    return get_encrypted_secure_value(file_manager, std::move(secure_value));
  });
}

td_api::object_ptr<td_api::encryptedPassportData> get_encrypted_passport_data_object(
    FileManager *file_manager, const EncryptedSecureValue &value) {
  bool is_plain = value.data.hash.empty();
  return td_api::make_object<td_api::encryptedPassportData>(
      get_passport_data_type_object(value.type), is_plain ? string() : value.data.data,
      get_encrypted_files_object(file_manager, value.files), is_plain ? value.data.data : string(),
      get_encrypted_file_object(file_manager, value.selfie));
}

telegram_api::object_ptr<telegram_api::inputSecureValue> get_input_secure_value_object(
    FileManager *file_manager, const EncryptedSecureValue &value, std::vector<SecureInputFile> &input_files,
    optional<SecureInputFile> &selfie) {
  bool is_plain = value.type == SecureValueType::PhoneNumber || value.type == SecureValueType::EmailAddress;
  bool has_selfie = value.selfie.file_id.is_valid();
  int32 flags = 0;
  tl_object_ptr<telegram_api::SecurePlainData> plain_data;
  if (is_plain) {
    if (value.type == SecureValueType::PhoneNumber) {
      plain_data = make_tl_object<telegram_api::securePlainPhone>(value.data.data);
    } else {
      plain_data = make_tl_object<telegram_api::securePlainEmail>(value.data.data);
    }
    flags |= telegram_api::inputSecureValue::PLAIN_DATA_MASK;
  } else {
    flags |= telegram_api::inputSecureValue::DATA_MASK;
  }
  if (!value.files.empty()) {
    flags |= telegram_api::inputSecureValue::FILES_MASK;
  }
  if (has_selfie) {
    flags |= telegram_api::inputSecureValue::SELFIE_MASK;
    CHECK(selfie);
  }
  return telegram_api::make_object<telegram_api::inputSecureValue>(
      flags, get_secure_value_type_telegram_object(value.type), is_plain ? nullptr : get_secure_data_object(value.data),
      get_input_secure_files_object(file_manager, value.files, input_files), std::move(plain_data),
      has_selfie ? get_input_secure_file_object(file_manager, value.selfie, *selfie) : nullptr);
}

vector<td_api::object_ptr<td_api::encryptedPassportData>> get_encrypted_passport_data_object(
    FileManager *file_manager, const vector<EncryptedSecureValue> &values) {
  return transform(values, [file_manager](const EncryptedSecureValue &value) {
    return get_encrypted_passport_data_object(file_manager, value);
  });
}

bool operator==(const SecureCredentials &lhs, const SecureCredentials &rhs) {
  return lhs.data == rhs.data && lhs.hash == rhs.hash && lhs.encrypted_secret == rhs.encrypted_secret;
}

bool operator!=(const SecureCredentials &lhs, const SecureCredentials &rhs) {
  return !(lhs == rhs);
}

SecureCredentials get_secure_credentials(tl_object_ptr<telegram_api::secureCredentialsEncrypted> &&credentials) {
  CHECK(credentials != nullptr);
  SecureCredentials result;
  result.data = credentials->data_.as_slice().str();
  result.hash = credentials->hash_.as_slice().str();
  result.encrypted_secret = credentials->secret_.as_slice().str();
  return result;
}

td_api::object_ptr<td_api::encryptedCredentials> get_encrypted_credentials_object(
    const SecureCredentials &credentials) {
  return td_api::make_object<td_api::encryptedCredentials>(credentials.data, credentials.hash,
                                                           credentials.encrypted_secret);
}

Result<SecureValue> get_secure_value(FileManager *file_manager,
                                     td_api::object_ptr<td_api::inputPassportData> &&input_passport_data) {
  if (input_passport_data == nullptr) {
    return Status::Error(400, "InputPassportData must not be empty");
  }

  SecureValue res;
  res.type = get_secure_value_type_td_api(std::move(input_passport_data->type_));
  res.data = std::move(input_passport_data->data_);
  for (auto &file : input_passport_data->files_) {
    TRY_RESULT(file_id, file_manager->get_input_file_id(FileType::Secure, std::move(file), DialogId{}, false, false,
                                                        false, true));
    res.files.push_back(file_id);
  }
  if (input_passport_data->selfie_) {
    TRY_RESULT(file_id, file_manager->get_input_file_id(FileType::Secure, std::move(input_passport_data->selfie_),
                                                        DialogId{}, false, false, false, true));
    res.selfie = file_id;
  }
  return res;
}
td_api::object_ptr<td_api::passportData> get_passport_data_object(FileManager *file_manager, const SecureValue &value) {
  std::vector<td_api::object_ptr<td_api::file>> files;
  files = transform(value.files, [&](FileId id) { return file_manager->get_file_object(id, true); });

  td_api::object_ptr<td_api::file> selfie;
  if (value.selfie.is_valid()) {
    selfie = file_manager->get_file_object(value.selfie, true);
  }

  return td_api::make_object<td_api::passportData>(get_passport_data_type_object(value.type), value.data,
                                                   std::move(files), std::move(selfie));
}

Result<FileId> decrypt_secure_file(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                   const SecureFile &secure_file) {
  if (!secure_file.file_id.is_valid()) {
    return secure_file.file_id;
  }
  TRY_RESULT(hash, secure_storage::ValueHash::create(secure_file.file_hash));
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secure_file.encrypted_secret));
  TRY_RESULT(secret, encrypted_secret.decrypt(PSLICE() << master_secret.as_slice() << hash.as_slice()));
  FileEncryptionKey key{secret};
  key.set_value_hash(hash);
  file_manager->set_encryption_key(secure_file.file_id, std::move(key));
  return secure_file.file_id;
}

Result<vector<FileId>> decrypt_secure_files(FileManager *file_manager, const secure_storage::Secret &secret,
                                            const vector<SecureFile> &secure_files) {
  vector<FileId> res;
  res.reserve(secure_files.size());
  for (auto &file : secure_files) {
    TRY_RESULT(decrypted_file, decrypt_secure_file(file_manager, secret, file));
    res.push_back(decrypted_file);
  }

  return std::move(res);
}
Result<string> decrypt_secure_data(const secure_storage::Secret &master_secret, const SecureData &secure_data) {
  TRY_RESULT(hash, secure_storage::ValueHash::create(secure_data.hash));
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secure_data.encrypted_secret));
  TRY_RESULT(secret, encrypted_secret.decrypt(PSLICE() << master_secret.as_slice() << hash.as_slice()));
  TRY_RESULT(value, secure_storage::decrypt_value(secret, hash, secure_data.data));
  return value.as_slice().str();
}

Result<SecureValue> decrypt_encrypted_secure_value(FileManager *file_manager, const secure_storage::Secret &secret,
                                                   const EncryptedSecureValue &encrypted_secure_value) {
  SecureValue res;
  res.type = encrypted_secure_value.type;
  switch (encrypted_secure_value.type) {
    case SecureValueType::EmailAddress:
    case SecureValueType::PhoneNumber:
      res.data = encrypted_secure_value.data.data;
      break;
    default: {
      TRY_RESULT(data, decrypt_secure_data(secret, encrypted_secure_value.data));
      res.data = std::move(data);
      TRY_RESULT(files, decrypt_secure_files(file_manager, secret, encrypted_secure_value.files));
      res.files = std::move(files);
      TRY_RESULT(selfie, decrypt_secure_file(file_manager, secret, encrypted_secure_value.selfie));
      res.selfie = std::move(selfie);
      break;
    }
  }
  return std::move(res);
}

SecureFile encrypt_secure_file(FileManager *file_manager, const secure_storage::Secret &master_secret, FileId file,
                               string &to_hash) {
  auto file_view = file_manager->get_file_view(file);
  if (file_view.empty()) {
    return {};
  }
  if (!file_view.encryption_key().is_secure()) {
    LOG(ERROR) << "File has no encryption key";
    return {};
  }
  if (!file_view.encryption_key().has_value_hash()) {
    LOG(ERROR) << "File has no hash";
    return {};
  }
  auto value_hash = file_view.encryption_key().value_hash();
  auto secret = file_view.encryption_key().secret();
  SecureFile res;
  res.file_id = file;
  res.file_hash = value_hash.as_slice().str();
  res.encrypted_secret = secret.encrypt(PSLICE() << master_secret.as_slice() << value_hash.as_slice()).as_slice().str();

  to_hash.append(res.file_hash);
  to_hash.append(secret.as_slice().str());
  return res;
}

vector<SecureFile> encrypt_secure_files(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                        vector<FileId> files, string &to_hash) {
  return transform(files,
                   [&](auto file_id) { return encrypt_secure_file(file_manager, master_secret, file_id, to_hash); });
}

SecureData encrypt_secure_data(const secure_storage::Secret &master_secret, Slice data, string &to_hash) {
  auto secret = secure_storage::Secret::create_new();
  auto encrypted = encrypt_value(secret, data).move_as_ok();
  SecureData res;
  res.encrypted_secret =
      secret.encrypt(PSLICE() << master_secret.as_slice() << encrypted.hash.as_slice()).as_slice().str();
  res.data = encrypted.data.as_slice().str();
  res.hash = encrypted.hash.as_slice().str();
  to_hash.append(res.hash);
  to_hash.append(secret.as_slice().str());
  return res;
}

EncryptedSecureValue encrypt_secure_value(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                          const SecureValue &secure_value) {
  EncryptedSecureValue res;
  res.type = secure_value.type;
  switch (res.type) {
    case SecureValueType::EmailAddress:
    case SecureValueType::PhoneNumber:
      res.data = SecureData{secure_value.data, "", ""};
      res.hash = secure_storage::calc_value_hash(secure_value.data).as_slice().str();
      break;
    default: {
      string to_hash;
      res.data = encrypt_secure_data(master_secret, secure_value.data, to_hash);
      res.files = encrypt_secure_files(file_manager, master_secret, secure_value.files, to_hash);
      res.selfie = encrypt_secure_file(file_manager, master_secret, secure_value.selfie, to_hash);
      res.hash = secure_storage::calc_value_hash(to_hash).as_slice().str();
      break;
    }
  }
  return res;
}

}  // namespace td

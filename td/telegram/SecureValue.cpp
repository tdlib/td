//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureValue.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Payments.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/overloaded.h"
#include "td/utils/utf8.h"

#include <limits>

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, const SecureValueType &type) {
  switch (type) {
    case SecureValueType::PersonalDetails:
      return string_builder << "PersonalDetails";
    case SecureValueType::Passport:
      return string_builder << "Passport";
    case SecureValueType::DriverLicense:
      return string_builder << "DriverLicense";
    case SecureValueType::IdentityCard:
      return string_builder << "IdentityCard";
    case SecureValueType::InternalPassport:
      return string_builder << "InternalPassport";
    case SecureValueType::Address:
      return string_builder << "Address";
    case SecureValueType::UtilityBill:
      return string_builder << "UtilityBill";
    case SecureValueType::BankStatement:
      return string_builder << "BankStatement";
    case SecureValueType::RentalAgreement:
      return string_builder << "RentalAgreement";
    case SecureValueType::PassportRegistration:
      return string_builder << "PassportRegistration";
    case SecureValueType::TemporaryRegistration:
      return string_builder << "TemporaryRegistration";
    case SecureValueType::PhoneNumber:
      return string_builder << "PhoneNumber";
    case SecureValueType::EmailAddress:
      return string_builder << "EmailAddress";
    case SecureValueType::None:
      return string_builder << "None";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

SecureValueType get_secure_value_type(const tl_object_ptr<telegram_api::SecureValueType> &secure_value_type) {
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
    case telegram_api::secureValueTypeInternalPassport::ID:
      return SecureValueType::InternalPassport;
    case telegram_api::secureValueTypeAddress::ID:
      return SecureValueType::Address;
    case telegram_api::secureValueTypeUtilityBill::ID:
      return SecureValueType::UtilityBill;
    case telegram_api::secureValueTypeBankStatement::ID:
      return SecureValueType::BankStatement;
    case telegram_api::secureValueTypeRentalAgreement::ID:
      return SecureValueType::RentalAgreement;
    case telegram_api::secureValueTypePassportRegistration::ID:
      return SecureValueType::PassportRegistration;
    case telegram_api::secureValueTypeTemporaryRegistration::ID:
      return SecureValueType::TemporaryRegistration;
    case telegram_api::secureValueTypePhone::ID:
      return SecureValueType::PhoneNumber;
    case telegram_api::secureValueTypeEmail::ID:
      return SecureValueType::EmailAddress;
    default:
      UNREACHABLE();
      return SecureValueType::None;
  }
}

SecureValueType get_secure_value_type_td_api(const tl_object_ptr<td_api::PassportDataType> &passport_data_type) {
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
    case td_api::passportDataTypeInternalPassport::ID:
      return SecureValueType::InternalPassport;
    case td_api::passportDataTypeAddress::ID:
      return SecureValueType::Address;
    case td_api::passportDataTypeUtilityBill::ID:
      return SecureValueType::UtilityBill;
    case td_api::passportDataTypeBankStatement::ID:
      return SecureValueType::BankStatement;
    case td_api::passportDataTypeRentalAgreement::ID:
      return SecureValueType::RentalAgreement;
    case td_api::passportDataTypePassportRegistration::ID:
      return SecureValueType::PassportRegistration;
    case td_api::passportDataTypeTemporaryRegistration::ID:
      return SecureValueType::TemporaryRegistration;
    case td_api::passportDataTypePhoneNumber::ID:
      return SecureValueType::PhoneNumber;
    case td_api::passportDataTypeEmailAddress::ID:
      return SecureValueType::EmailAddress;
    default:
      UNREACHABLE();
      return SecureValueType::None;
  }
}

static vector<SecureValueType> unique_types(vector<SecureValueType> types) {
  size_t size = types.size();
  for (size_t i = 0; i < size; i++) {
    for (size_t j = 0; j < i; j++) {
      if (types[i] == types[j]) {
        LOG(ERROR) << "Have duplicate Passport Data type " << types[i] << " at positions " << i << " and " << j;
        types[i--] = types[--size];
        break;
      }
    }
  }
  types.resize(size);
  return types;
}

vector<SecureValueType> get_secure_value_types(
    const vector<tl_object_ptr<telegram_api::SecureValueType>> &secure_value_types) {
  return unique_types(transform(secure_value_types, get_secure_value_type));
}

vector<SecureValueType> get_secure_value_types_td_api(
    const vector<tl_object_ptr<td_api::PassportDataType>> &secure_value_types) {
  return unique_types(transform(secure_value_types, get_secure_value_type_td_api));
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
    case SecureValueType::InternalPassport:
      return td_api::make_object<td_api::passportDataTypeInternalPassport>();
    case SecureValueType::Address:
      return td_api::make_object<td_api::passportDataTypeAddress>();
    case SecureValueType::UtilityBill:
      return td_api::make_object<td_api::passportDataTypeUtilityBill>();
    case SecureValueType::BankStatement:
      return td_api::make_object<td_api::passportDataTypeBankStatement>();
    case SecureValueType::RentalAgreement:
      return td_api::make_object<td_api::passportDataTypeRentalAgreement>();
    case SecureValueType::PassportRegistration:
      return td_api::make_object<td_api::passportDataTypePassportRegistration>();
    case SecureValueType::TemporaryRegistration:
      return td_api::make_object<td_api::passportDataTypeTemporaryRegistration>();
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

td_api::object_ptr<telegram_api::SecureValueType> get_input_secure_value_type(SecureValueType type) {
  switch (type) {
    case SecureValueType::PersonalDetails:
      return telegram_api::make_object<telegram_api::secureValueTypePersonalDetails>();
    case SecureValueType::Passport:
      return telegram_api::make_object<telegram_api::secureValueTypePassport>();
    case SecureValueType::DriverLicense:
      return telegram_api::make_object<telegram_api::secureValueTypeDriverLicense>();
    case SecureValueType::IdentityCard:
      return telegram_api::make_object<telegram_api::secureValueTypeIdentityCard>();
    case SecureValueType::InternalPassport:
      return telegram_api::make_object<telegram_api::secureValueTypeInternalPassport>();
    case SecureValueType::Address:
      return telegram_api::make_object<telegram_api::secureValueTypeAddress>();
    case SecureValueType::UtilityBill:
      return telegram_api::make_object<telegram_api::secureValueTypeUtilityBill>();
    case SecureValueType::BankStatement:
      return telegram_api::make_object<telegram_api::secureValueTypeBankStatement>();
    case SecureValueType::RentalAgreement:
      return telegram_api::make_object<telegram_api::secureValueTypeRentalAgreement>();
    case SecureValueType::PassportRegistration:
      return telegram_api::make_object<telegram_api::secureValueTypePassportRegistration>();
    case SecureValueType::TemporaryRegistration:
      return telegram_api::make_object<telegram_api::secureValueTypeTemporaryRegistration>();
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

string get_secure_value_data_field_name(SecureValueType type, string field_name) {
  switch (type) {
    case SecureValueType::PersonalDetails:
      if (field_name == "first_name" || field_name == "last_name" || field_name == "gender" ||
          field_name == "country_code" || field_name == "residence_country_code") {
        return field_name;
      }
      if (field_name == "birth_date") {
        return "birthdate";
      }
      break;
    case SecureValueType::Passport:
    case SecureValueType::DriverLicense:
    case SecureValueType::IdentityCard:
    case SecureValueType::InternalPassport:
      if (field_name == "expiry_date") {
        return field_name;
      }
      if (field_name == "document_no") {
        return "number";
      }
      break;
    case SecureValueType::Address:
      if (field_name == "state" || field_name == "city" || field_name == "street_line1" ||
          field_name == "street_line2" || field_name == "country_code") {
        return field_name;
      }
      if (field_name == "post_code") {
        return "postal_code";
      }
      break;
    case SecureValueType::UtilityBill:
    case SecureValueType::BankStatement:
    case SecureValueType::RentalAgreement:
    case SecureValueType::PassportRegistration:
    case SecureValueType::TemporaryRegistration:
    case SecureValueType::PhoneNumber:
    case SecureValueType::EmailAddress:
      break;
    case SecureValueType::None:
    default:
      UNREACHABLE();
      break;
  }
  LOG(ERROR) << "Receive error about unknown field \"" << field_name << "\" in type " << type;
  return string();
}

bool operator==(const DatedFile &lhs, const DatedFile &rhs) {
  return lhs.file_id == rhs.file_id && lhs.date == rhs.date;
}

bool operator!=(const DatedFile &lhs, const DatedFile &rhs) {
  return !(lhs == rhs);
}

bool operator==(const EncryptedSecureFile &lhs, const EncryptedSecureFile &rhs) {
  return lhs.file == rhs.file && lhs.file_hash == rhs.file_hash && lhs.encrypted_secret == rhs.encrypted_secret;
}

bool operator!=(const EncryptedSecureFile &lhs, const EncryptedSecureFile &rhs) {
  return !(lhs == rhs);
}

EncryptedSecureFile get_encrypted_secure_file(FileManager *file_manager,
                                              tl_object_ptr<telegram_api::SecureFile> &&secure_file_ptr) {
  CHECK(secure_file_ptr != nullptr);
  EncryptedSecureFile result;
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
      result.file.file_id = file_manager->register_remote(
          FullRemoteFileLocation(FileType::Secure, secure_file->id_, secure_file->access_hash_, DcId::internal(dc_id)),
          FileLocationSource::FromServer, DialogId(), 0, secure_file->size_, to_string(secure_file->id_) + ".jpg");
      result.file.date = secure_file->date_;
      if (result.file.date < 0) {
        LOG(ERROR) << "Receive wrong date " << result.file.date;
        result.file.date = 0;
      }
      result.encrypted_secret = secure_file->secret_.as_slice().str();
      result.file_hash = secure_file->file_hash_.as_slice().str();
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

vector<EncryptedSecureFile> get_encrypted_secure_files(FileManager *file_manager,
                                                       vector<tl_object_ptr<telegram_api::SecureFile>> &&secure_files) {
  vector<EncryptedSecureFile> results;
  results.reserve(secure_files.size());
  for (auto &secure_file : secure_files) {
    auto result = get_encrypted_secure_file(file_manager, std::move(secure_file));
    if (result.file.file_id.is_valid()) {
      results.push_back(std::move(result));
    }
  }
  return results;
}

telegram_api::object_ptr<telegram_api::InputSecureFile> get_input_secure_file_object(FileManager *file_manager,
                                                                                     const EncryptedSecureFile &file,
                                                                                     SecureInputFile &input_file) {
  if (!file.file.file_id.is_valid()) {
    LOG(ERROR) << "Receive invalid EncryptedSecureFile";
    return nullptr;
  }
  CHECK(file_manager->get_file_view(file.file.file_id).file_id() ==
        file_manager->get_file_view(input_file.file_id).file_id());
  auto res = std::move(input_file.input_file);
  if (res == nullptr) {
    return file_manager->get_file_view(file.file.file_id).remote_location().as_input_secure_file();
  }
  telegram_api::downcast_call(*res, overloaded(
                                        [&](telegram_api::inputSecureFileUploaded &uploaded) {
                                          uploaded.secret_ = BufferSlice(file.encrypted_secret);
                                          uploaded.file_hash_ = BufferSlice(file.file_hash);
                                        },
                                        [&](telegram_api::inputSecureFile &) { UNREACHABLE(); }));
  return res;
}

static td_api::object_ptr<td_api::datedFile> get_dated_file_object(FileManager *file_manager, DatedFile file) {
  return td_api::make_object<td_api::datedFile>(file_manager->get_file_object(file.file_id), file.date);
}

static td_api::object_ptr<td_api::datedFile> get_dated_file_object(FileManager *file_manager,
                                                                   const EncryptedSecureFile &file) {
  DatedFile dated_file = file.file;
  auto file_id = dated_file.file_id;
  CHECK(file_id.is_valid());
  auto file_view = file_manager->get_file_view(file_id);
  if (!file_view.has_remote_location() || file_view.remote_location().is_web()) {
    LOG(ERROR) << "Have wrong file in get_dated_file_object";
    return nullptr;
  }
  dated_file.file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::SecureRaw, file_view.remote_location().get_id(),
                             file_view.remote_location().get_access_hash(), file_view.remote_location().get_dc_id()),
      FileLocationSource::FromServer, DialogId(), file_view.size(), file_view.expected_size(),
      file_view.suggested_name());
  return get_dated_file_object(file_manager, dated_file);
}

static vector<td_api::object_ptr<td_api::datedFile>> get_dated_files_object(FileManager *file_manager,
                                                                            const vector<EncryptedSecureFile> &files) {
  return transform(
      files, [file_manager](const EncryptedSecureFile &file) { return get_dated_file_object(file_manager, file); });
}

vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> get_input_secure_files_object(
    FileManager *file_manager, const vector<EncryptedSecureFile> &files, vector<SecureInputFile> &input_files) {
  CHECK(files.size() == input_files.size());
  vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> results;
  results.reserve(files.size());
  for (size_t i = 0; i < files.size(); i++) {
    auto result = get_input_secure_file_object(file_manager, files[i], input_files[i]);
    if (result != nullptr) {
      results.push_back(std::move(result));
    }
  }
  return results;
}

bool operator==(const EncryptedSecureData &lhs, const EncryptedSecureData &rhs) {
  return lhs.data == rhs.data && lhs.hash == rhs.hash && lhs.encrypted_secret == rhs.encrypted_secret;
}

bool operator!=(const EncryptedSecureData &lhs, const EncryptedSecureData &rhs) {
  return !(lhs == rhs);
}

EncryptedSecureData get_encrypted_secure_data(tl_object_ptr<telegram_api::secureData> &&secure_data) {
  CHECK(secure_data != nullptr);
  EncryptedSecureData result;
  result.data = secure_data->data_.as_slice().str();
  result.hash = secure_data->data_hash_.as_slice().str();
  result.encrypted_secret = secure_data->secret_.as_slice().str();
  return result;
}

telegram_api::object_ptr<telegram_api::secureData> get_secure_data_object(const EncryptedSecureData &data) {
  return telegram_api::make_object<telegram_api::secureData>(BufferSlice(data.data), BufferSlice(data.hash),
                                                             BufferSlice(data.encrypted_secret));
}

bool operator==(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs) {
  return lhs.type == rhs.type && lhs.data == rhs.data && lhs.files == rhs.files && lhs.front_side == rhs.front_side &&
         lhs.reverse_side == rhs.reverse_side && lhs.selfie == rhs.selfie;
}

bool operator!=(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs) {
  return !(lhs == rhs);
}

static bool check_encrypted_secure_value(const EncryptedSecureValue &value) {
  bool has_encrypted_data = !value.data.hash.empty();
  bool has_plain_data = !has_encrypted_data && !value.data.data.empty();
  bool has_files = !value.files.empty();
  bool has_front_side = value.front_side.file.file_id.is_valid();
  bool has_reverse_side = value.reverse_side.file.file_id.is_valid();
  bool has_selfie = value.selfie.file.file_id.is_valid();
  switch (value.type) {
    case SecureValueType::PersonalDetails:
    case SecureValueType::Address:
      return has_encrypted_data && !has_files && !has_front_side && !has_reverse_side && !has_selfie;
    case SecureValueType::Passport:
    case SecureValueType::InternalPassport:
      return has_encrypted_data && !has_files && has_front_side && !has_reverse_side;
    case SecureValueType::DriverLicense:
    case SecureValueType::IdentityCard:
      return has_encrypted_data && !has_files && has_front_side && has_reverse_side;
    case SecureValueType::UtilityBill:
    case SecureValueType::BankStatement:
    case SecureValueType::RentalAgreement:
    case SecureValueType::PassportRegistration:
    case SecureValueType::TemporaryRegistration:
      return !has_encrypted_data && !has_plain_data && has_files && !has_front_side && !has_reverse_side && !has_selfie;
    case SecureValueType::PhoneNumber:
      return has_plain_data && !has_files && !has_front_side && !has_reverse_side && !has_selfie;
    case SecureValueType::EmailAddress:
      return has_plain_data && !has_files && !has_front_side && !has_reverse_side && !has_selfie;
    case SecureValueType::None:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

EncryptedSecureValue get_encrypted_secure_value(FileManager *file_manager,
                                                tl_object_ptr<telegram_api::secureValue> &&secure_value) {
  EncryptedSecureValue result;
  CHECK(secure_value != nullptr);
  result.type = get_secure_value_type(secure_value->type_);
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
    result.data = get_encrypted_secure_data(std::move(secure_value->data_));
  }
  result.files = get_encrypted_secure_files(file_manager, std::move(secure_value->files_));
  if (secure_value->front_side_ != nullptr) {
    result.front_side = get_encrypted_secure_file(file_manager, std::move(secure_value->front_side_));
  }
  if (secure_value->reverse_side_ != nullptr) {
    result.reverse_side = get_encrypted_secure_file(file_manager, std::move(secure_value->reverse_side_));
  }
  if (secure_value->selfie_ != nullptr) {
    result.selfie = get_encrypted_secure_file(file_manager, std::move(secure_value->selfie_));
  }
  result.hash = secure_value->hash_.as_slice().str();
  if (!check_encrypted_secure_value(result)) {
    LOG(ERROR) << "Receive invalid encrypted secure value of type " << result.type;
    return EncryptedSecureValue();
  }
  return result;
}

vector<EncryptedSecureValue> get_encrypted_secure_values(
    FileManager *file_manager, vector<tl_object_ptr<telegram_api::secureValue>> &&secure_values) {
  vector<EncryptedSecureValue> results;
  results.reserve(secure_values.size());
  for (auto &secure_value : secure_values) {
    auto result = get_encrypted_secure_value(file_manager, std::move(secure_value));
    if (result.type != SecureValueType::None) {
      results.push_back(std::move(result));
    }
  }
  return results;
}

td_api::object_ptr<td_api::encryptedPassportData> get_encrypted_passport_data_object(
    FileManager *file_manager, const EncryptedSecureValue &value) {
  bool is_plain = value.data.hash.empty();
  return td_api::make_object<td_api::encryptedPassportData>(
      get_passport_data_type_object(value.type), is_plain ? string() : value.data.data,
      value.front_side.file.file_id.is_valid() ? get_dated_file_object(file_manager, value.front_side) : nullptr,
      value.reverse_side.file.file_id.is_valid() ? get_dated_file_object(file_manager, value.reverse_side) : nullptr,
      value.selfie.file.file_id.is_valid() ? get_dated_file_object(file_manager, value.selfie) : nullptr,
      get_dated_files_object(file_manager, value.files), is_plain ? value.data.data : string());
}

telegram_api::object_ptr<telegram_api::inputSecureValue> get_input_secure_value_object(
    FileManager *file_manager, const EncryptedSecureValue &value, std::vector<SecureInputFile> &input_files,
    optional<SecureInputFile> &front_side, optional<SecureInputFile> &reverse_side, optional<SecureInputFile> &selfie) {
  bool is_plain = value.type == SecureValueType::PhoneNumber || value.type == SecureValueType::EmailAddress;
  bool has_front_side = value.front_side.file.file_id.is_valid();
  bool has_reverse_side = value.reverse_side.file.file_id.is_valid();
  bool has_selfie = value.selfie.file.file_id.is_valid();
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
  if (has_front_side) {
    flags |= telegram_api::inputSecureValue::FRONT_SIDE_MASK;
    CHECK(front_side);
  }
  if (has_reverse_side) {
    flags |= telegram_api::inputSecureValue::REVERSE_SIDE_MASK;
    CHECK(reverse_side);
  }
  if (has_selfie) {
    flags |= telegram_api::inputSecureValue::SELFIE_MASK;
    CHECK(selfie);
  }
  return telegram_api::make_object<telegram_api::inputSecureValue>(
      flags, get_input_secure_value_type(value.type), is_plain ? nullptr : get_secure_data_object(value.data),
      has_front_side ? get_input_secure_file_object(file_manager, value.front_side, *front_side) : nullptr,
      has_reverse_side ? get_input_secure_file_object(file_manager, value.reverse_side, *reverse_side) : nullptr,
      has_selfie ? get_input_secure_file_object(file_manager, value.selfie, *selfie) : nullptr,
      get_input_secure_files_object(file_manager, value.files, input_files), std::move(plain_data));
}

vector<td_api::object_ptr<td_api::encryptedPassportData>> get_encrypted_passport_data_object(
    FileManager *file_manager, const vector<EncryptedSecureValue> &values) {
  return transform(values, [file_manager](const EncryptedSecureValue &value) {
    return get_encrypted_passport_data_object(file_manager, value);
  });
}

bool operator==(const EncryptedSecureCredentials &lhs, const EncryptedSecureCredentials &rhs) {
  return lhs.data == rhs.data && lhs.hash == rhs.hash && lhs.encrypted_secret == rhs.encrypted_secret;
}

bool operator!=(const EncryptedSecureCredentials &lhs, const EncryptedSecureCredentials &rhs) {
  return !(lhs == rhs);
}

telegram_api::object_ptr<telegram_api::secureCredentialsEncrypted> get_secure_credentials_encrypted_object(
    const EncryptedSecureCredentials &credentials) {
  return telegram_api::make_object<telegram_api::secureCredentialsEncrypted>(
      BufferSlice(credentials.data), BufferSlice(credentials.hash), BufferSlice(credentials.encrypted_secret));
}

EncryptedSecureCredentials get_encrypted_secure_credentials(
    tl_object_ptr<telegram_api::secureCredentialsEncrypted> &&credentials) {
  CHECK(credentials != nullptr);
  EncryptedSecureCredentials result;
  result.data = credentials->data_.as_slice().str();
  result.hash = credentials->hash_.as_slice().str();
  result.encrypted_secret = credentials->secret_.as_slice().str();
  return result;
}

td_api::object_ptr<td_api::encryptedCredentials> get_encrypted_credentials_object(
    const EncryptedSecureCredentials &credentials) {
  return td_api::make_object<td_api::encryptedCredentials>(credentials.data, credentials.hash,
                                                           credentials.encrypted_secret);
}

static string lpad0(string str, size_t size) {
  if (str.size() >= size) {
    return str;
  }
  return string(size - str.size(), '0') + str;
}

// TODO tests
static Status check_date(int32 day, int32 month, int32 year) {
  if (day < 1 || day > 31) {
    return Status::Error(400, "Wrong day number specified");
  }
  if (month < 1 || month > 12) {
    return Status::Error(400, "Wrong month number specified");
  }
  if (year < 1 || year > 9999) {
    return Status::Error(400, "Wrong year number specified");
  }

  bool is_leap = month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  const int32 days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > days_in_month[month - 1] + static_cast<int32>(is_leap)) {
    return Status::Error(400, "Wrong day in month number specified");
  }

  return Status::OK();
}

static Result<string> get_date(td_api::object_ptr<td_api::date> &&date) {
  if (date == nullptr) {
    return string();
  }
  TRY_STATUS(check_date(date->day_, date->month_, date->year_));

  return PSTRING() << lpad0(to_string(date->day_), 2) << '.' << lpad0(to_string(date->month_), 2) << '.'
                   << lpad0(to_string(date->year_), 4);
}

static Result<int32> to_int32(Slice str) {
  CHECK(str.size() <= static_cast<size_t>(std::numeric_limits<int32>::digits10));
  int32 integer_value = 0;
  for (auto c : str) {
    if (!is_digit(c)) {
      return Status::Error(PSLICE() << "Can't parse \"" << str << "\" as number");
    }
    integer_value = integer_value * 10 + c - '0';
  }
  return integer_value;
}

static Result<td_api::object_ptr<td_api::date>> get_date_object(Slice date) {
  if (date.empty()) {
    return nullptr;
  }
  if (date.size() != 10u) {
    return Status::Error(400, "Date has wrong size");
  }
  auto parts = full_split(date, '.');
  if (parts.size() != 3 || parts[0].size() != 2 || parts[1].size() != 2 || parts[2].size() != 4) {
    return Status::Error(400, "Date has wrong parts");
  }
  TRY_RESULT(day, to_int32(parts[0]));
  TRY_RESULT(month, to_int32(parts[1]));
  TRY_RESULT(year, to_int32(parts[2]));
  TRY_STATUS(check_date(day, month, year));

  return td_api::make_object<td_api::date>(day, month, year);
}

static Status check_first_name(string &first_name) {
  if (!clean_input_string(first_name)) {
    return Status::Error(400, "First name must be encoded in UTF-8");
  }
  if (first_name.empty()) {
    return Status::Error(400, "First name must not be empty");
  }
  if (utf8_length(first_name) > 255) {
    return Status::Error(400, "First name is too long");
  }
  return Status::OK();
}

static Status check_last_name(string &last_name) {
  if (!clean_input_string(last_name)) {
    return Status::Error(400, "Last name must be encoded in UTF-8");
  }
  if (last_name.empty()) {
    return Status::Error(400, "Last name must not be empty");
  }
  if (utf8_length(last_name) > 255) {
    return Status::Error(400, "Last name is too long");
  }
  return Status::OK();
}

static Status check_gender(string &gender) {
  if (gender != "male" && gender != "female") {
    return Status::Error(400, "Unsupported gender specified");
  }
  return Status::OK();
}

static Result<string> get_personal_details(td_api::object_ptr<td_api::personalDetails> &&personal_details) {
  if (personal_details == nullptr) {
    return Status::Error(400, "Personal details must not be empty");
  }
  TRY_STATUS(check_first_name(personal_details->first_name_));
  TRY_STATUS(check_last_name(personal_details->last_name_));
  TRY_RESULT(birthdate, get_date(std::move(personal_details->birthdate_)));
  if (birthdate.empty()) {
    return Status::Error(400, "Birthdate must not be empty");
  }
  TRY_STATUS(check_gender(personal_details->gender_));
  TRY_STATUS(check_country_code(personal_details->country_code_));
  TRY_STATUS(check_country_code(personal_details->residence_country_code_));

  return json_encode<std::string>(json_object([&](auto &o) {
    o("first_name", personal_details->first_name_);
    o("last_name", personal_details->last_name_);
    o("birth_date", birthdate);
    o("gender", personal_details->gender_);
    o("country_code", personal_details->country_code_);
    o("residence_country_code", personal_details->residence_country_code_);
  }));
}

static Result<td_api::object_ptr<td_api::personalDetails>> get_personal_details_object(Slice personal_details) {
  auto personal_details_copy = personal_details.str();
  auto r_value = json_decode(personal_details_copy);
  if (r_value.is_error()) {
    return Status::Error(400, "Can't parse personal details JSON object");
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Personal details should be an Object");
  }

  auto &object = value.get_object();
  TRY_RESULT(first_name, get_json_object_string_field(object, "first_name", true));
  TRY_RESULT(last_name, get_json_object_string_field(object, "last_name", true));
  TRY_RESULT(birthdate, get_json_object_string_field(object, "birth_date", true));
  if (birthdate.empty()) {
    return Status::Error(400, "Birthdate must not be empty");
  }
  TRY_RESULT(gender, get_json_object_string_field(object, "gender", true));
  TRY_RESULT(country_code, get_json_object_string_field(object, "country_code", true));
  TRY_RESULT(residence_country_code, get_json_object_string_field(object, "residence_country_code", true));

  TRY_STATUS(check_first_name(first_name));
  TRY_STATUS(check_last_name(last_name));
  TRY_RESULT(date, get_date_object(birthdate));
  TRY_STATUS(check_gender(gender));
  TRY_STATUS(check_country_code(country_code));
  TRY_STATUS(check_country_code(residence_country_code));

  return td_api::make_object<td_api::personalDetails>(std::move(first_name), std::move(last_name), std::move(date),
                                                      std::move(gender), std::move(country_code),
                                                      std::move(residence_country_code));
}

static Status check_document_number(string &number) {
  if (!clean_input_string(number)) {
    return Status::Error(400, "Document number must be encoded in UTF-8");
  }
  if (number.empty()) {
    return Status::Error(400, "Document number must not be empty");
  }
  if (utf8_length(number) > 24) {
    return Status::Error(400, "Document number is too long");
  }
  return Status::OK();
}

static Result<DatedFile> get_secure_file(FileManager *file_manager, td_api::object_ptr<td_api::InputFile> &&file) {
  TRY_RESULT(file_id,
             file_manager->get_input_file_id(FileType::Secure, std::move(file), DialogId(), false, false, false, true));
  DatedFile result;
  result.file_id = file_id;
  result.date = G()->unix_time();
  return std::move(result);
}

static Result<vector<DatedFile>> get_secure_files(FileManager *file_manager,
                                                  vector<td_api::object_ptr<td_api::InputFile>> &&files) {
  vector<DatedFile> result;
  for (auto &file : files) {
    TRY_RESULT(dated_file, get_secure_file(file_manager, std::move(file)));
    result.push_back(std::move(dated_file));
  }
  return result;
}

static Result<SecureValue> get_identity_document(SecureValueType type, FileManager *file_manager,
                                                 td_api::object_ptr<td_api::inputIdentityDocument> &&identity_document,
                                                 bool need_reverse_side) {
  if (identity_document == nullptr) {
    return Status::Error(400, "Identity document must not be empty");
  }
  TRY_STATUS(check_document_number(identity_document->number_));
  TRY_RESULT(date, get_date(std::move(identity_document->expiry_date_)));

  SecureValue res;
  res.type = type;
  res.data = json_encode<std::string>(json_object([&](auto &o) {
    o("document_no", identity_document->number_);
    o("expiry_date", date);
  }));

  if (identity_document->front_side_ == nullptr) {
    return Status::Error(400, "Document's front side is required");
  }
  if (identity_document->reverse_side_ == nullptr) {
    if (need_reverse_side) {
      return Status::Error(400, "Document's reverse side is required");
    }
  } else {
    if (!need_reverse_side) {
      return Status::Error(400, "Document shouldn't have a reverse side");
    }
  }

  TRY_RESULT(front_side, get_secure_file(file_manager, std::move(identity_document->front_side_)));
  res.front_side = front_side;
  if (identity_document->reverse_side_ != nullptr) {
    TRY_RESULT(reverse_side, get_secure_file(file_manager, std::move(identity_document->reverse_side_)));
    res.reverse_side = reverse_side;
  }
  if (identity_document->selfie_ != nullptr) {
    TRY_RESULT(selfie, get_secure_file(file_manager, std::move(identity_document->selfie_)));
    res.selfie = selfie;
  }
  return res;
}

static Result<td_api::object_ptr<td_api::identityDocument>> get_identity_document_object(FileManager *file_manager,
                                                                                         const SecureValue &value) {
  CHECK(value.files.empty());

  td_api::object_ptr<td_api::datedFile> front_side;
  td_api::object_ptr<td_api::datedFile> reverse_side;
  td_api::object_ptr<td_api::datedFile> selfie;
  if (value.front_side.file_id.is_valid()) {
    front_side = get_dated_file_object(file_manager, value.front_side);
  }
  if (value.reverse_side.file_id.is_valid()) {
    reverse_side = get_dated_file_object(file_manager, value.reverse_side);
  }
  if (value.selfie.file_id.is_valid()) {
    selfie = get_dated_file_object(file_manager, value.selfie);
  }

  auto data_copy = value.data;
  auto r_value = json_decode(data_copy);
  if (r_value.is_error()) {
    return Status::Error(400, "Can't parse identity document JSON object");
  }

  auto json_value = r_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Identity document should be an Object");
  }

  auto &object = json_value.get_object();
  TRY_RESULT(number, get_json_object_string_field(object, "document_no", true));
  TRY_RESULT(expiry_date, get_json_object_string_field(object, "expiry_date", true));

  TRY_STATUS(check_document_number(number));
  TRY_RESULT(date, get_date_object(expiry_date));

  return td_api::make_object<td_api::identityDocument>(std::move(number), std::move(date), std::move(front_side),
                                                       std::move(reverse_side), std::move(selfie));
}

static Status check_phone_number(string &phone_number) {
  if (!clean_input_string(phone_number)) {
    return Status::Error(400, "Phone number must be encoded in UTF-8");
  }
  return Status::OK();
}

static Status check_email_address(string &email_address) {
  if (!clean_input_string(email_address)) {
    return Status::Error(400, "Email address must be encoded in UTF-8");
  }
  return Status::OK();
}

Result<SecureValue> get_secure_value(FileManager *file_manager,
                                     td_api::object_ptr<td_api::InputPassportData> &&input_passport_data) {
  if (input_passport_data == nullptr) {
    return Status::Error(400, "InputPassportData must not be empty");
  }

  SecureValue res;
  switch (input_passport_data->get_id()) {
    case td_api::inputPassportDataPersonalDetails::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataPersonalDetails>(input_passport_data);
      res.type = SecureValueType::PersonalDetails;
      TRY_RESULT(personal_details, get_personal_details(std::move(input->personal_details_)));
      res.data = std::move(personal_details);
      break;
    }
    case td_api::inputPassportDataPassport::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataPassport>(input_passport_data);
      return get_identity_document(SecureValueType::Passport, file_manager, std::move(input->passport_), false);
    }
    case td_api::inputPassportDataDriverLicense::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataDriverLicense>(input_passport_data);
      return get_identity_document(SecureValueType::DriverLicense, file_manager, std::move(input->driver_license_),
                                   true);
    }
    case td_api::inputPassportDataIdentityCard::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataIdentityCard>(input_passport_data);
      return get_identity_document(SecureValueType::IdentityCard, file_manager, std::move(input->identity_card_), true);
    }
    case td_api::inputPassportDataInternalPassport::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataInternalPassport>(input_passport_data);
      return get_identity_document(SecureValueType::InternalPassport, file_manager,
                                   std::move(input->internal_passport_), false);
    }
    case td_api::inputPassportDataAddress::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataAddress>(input_passport_data);
      res.type = SecureValueType::Address;
      TRY_RESULT(address, get_address(std::move(input->address_)));
      res.data = address_to_json(address);
      break;
    }
    case td_api::inputPassportDataUtilityBill::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataUtilityBill>(input_passport_data);
      res.type = SecureValueType::UtilityBill;
      TRY_RESULT(files, get_secure_files(file_manager, std::move(input->files_)));
      res.files = std::move(files);
      break;
    }
    case td_api::inputPassportDataBankStatement::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataBankStatement>(input_passport_data);
      res.type = SecureValueType::BankStatement;
      TRY_RESULT(files, get_secure_files(file_manager, std::move(input->files_)));
      res.files = std::move(files);
      break;
    }
    case td_api::inputPassportDataRentalAgreement::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataRentalAgreement>(input_passport_data);
      res.type = SecureValueType::RentalAgreement;
      TRY_RESULT(files, get_secure_files(file_manager, std::move(input->files_)));
      res.files = std::move(files);
      break;
    }
    case td_api::inputPassportDataPassportRegistration::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataPassportRegistration>(input_passport_data);
      res.type = SecureValueType::PassportRegistration;
      TRY_RESULT(files, get_secure_files(file_manager, std::move(input->files_)));
      res.files = std::move(files);
      break;
    }
    case td_api::inputPassportDataTemporaryRegistration::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataTemporaryRegistration>(input_passport_data);
      res.type = SecureValueType::TemporaryRegistration;
      TRY_RESULT(files, get_secure_files(file_manager, std::move(input->files_)));
      res.files = std::move(files);
      break;
    }
    case td_api::inputPassportDataPhoneNumber::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataPhoneNumber>(input_passport_data);
      res.type = SecureValueType::PhoneNumber;
      TRY_STATUS(check_phone_number(input->phone_number_));
      res.data = std::move(input->phone_number_);
      break;
    }
    case td_api::inputPassportDataEmailAddress::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataEmailAddress>(input_passport_data);
      res.type = SecureValueType::EmailAddress;
      TRY_STATUS(check_email_address(input->email_address_));
      res.data = std::move(input->email_address_);
      break;
    }
    default:
      UNREACHABLE();
  }
  return res;
}

Result<td_api::object_ptr<td_api::PassportData>> get_passport_data_object(FileManager *file_manager,
                                                                          const SecureValue &value) {
  switch (value.type) {
    case SecureValueType::PersonalDetails: {
      TRY_RESULT(personal_details, get_personal_details_object(value.data));
      return td_api::make_object<td_api::passportDataPersonalDetails>(std::move(personal_details));
    }
    case SecureValueType::Passport: {
      TRY_RESULT(passport, get_identity_document_object(file_manager, value));
      return td_api::make_object<td_api::passportDataPassport>(std::move(passport));
    }
    case SecureValueType::DriverLicense: {
      TRY_RESULT(driver_license, get_identity_document_object(file_manager, value));
      return td_api::make_object<td_api::passportDataDriverLicense>(std::move(driver_license));
    }
    case SecureValueType::IdentityCard: {
      TRY_RESULT(identity_card, get_identity_document_object(file_manager, value));
      return td_api::make_object<td_api::passportDataIdentityCard>(std::move(identity_card));
    }
    case SecureValueType::InternalPassport: {
      TRY_RESULT(internal_passport, get_identity_document_object(file_manager, value));
      return td_api::make_object<td_api::passportDataInternalPassport>(std::move(internal_passport));
    }
    case SecureValueType::Address: {
      TRY_RESULT(address, address_from_json(value.data));
      return td_api::make_object<td_api::passportDataAddress>(get_address_object(address));
    }
    case SecureValueType::UtilityBill:
    case SecureValueType::BankStatement:
    case SecureValueType::RentalAgreement:
    case SecureValueType::PassportRegistration:
    case SecureValueType::TemporaryRegistration: {
      auto files = transform(
          value.files, [file_manager](const DatedFile &file) { return get_dated_file_object(file_manager, file); });
      if (value.type == SecureValueType::UtilityBill) {
        return td_api::make_object<td_api::passportDataUtilityBill>(std::move(files));
      }
      if (value.type == SecureValueType::BankStatement) {
        return td_api::make_object<td_api::passportDataBankStatement>(std::move(files));
      }
      if (value.type == SecureValueType::RentalAgreement) {
        return td_api::make_object<td_api::passportDataRentalAgreement>(std::move(files));
      }
      if (value.type == SecureValueType::PassportRegistration) {
        return td_api::make_object<td_api::passportDataPassportRegistration>(std::move(files));
      }
      if (value.type == SecureValueType::TemporaryRegistration) {
        return td_api::make_object<td_api::passportDataTemporaryRegistration>(std::move(files));
      }
      UNREACHABLE();
      break;
    }
    case SecureValueType::PhoneNumber:
      return td_api::make_object<td_api::passportDataPhoneNumber>(value.data);
    case SecureValueType::EmailAddress:
      return td_api::make_object<td_api::passportDataEmailAddress>(value.data);
    case SecureValueType::None:
    default:
      UNREACHABLE();
      return Status::Error(400, "Wrong value type");
  }
}

td_api::object_ptr<td_api::allPassportData> get_all_passport_data_object(FileManager *file_manager,
                                                                         const vector<SecureValue> &values) {
  vector<td_api::object_ptr<td_api::PassportData>> result;
  result.reserve(values.size());
  for (auto &value : values) {
    auto r_obj = get_passport_data_object(file_manager, value);
    if (r_obj.is_error()) {
      LOG(ERROR) << "Can't get passport data object: " << r_obj.error();
      continue;
    }

    result.push_back(r_obj.move_as_ok());
  }

  return td_api::make_object<td_api::allPassportData>(std::move(result));
}

static Result<std::pair<DatedFile, SecureFileCredentials>> decrypt_secure_file(
    FileManager *file_manager, const secure_storage::Secret &master_secret, const EncryptedSecureFile &secure_file) {
  if (!secure_file.file.file_id.is_valid()) {
    return std::make_pair(DatedFile(), SecureFileCredentials());
  }
  TRY_RESULT(hash, secure_storage::ValueHash::create(secure_file.file_hash));
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secure_file.encrypted_secret));
  TRY_RESULT(secret, encrypted_secret.decrypt(PSLICE() << master_secret.as_slice() << hash.as_slice()));
  FileEncryptionKey key{secret};
  key.set_value_hash(hash);
  file_manager->set_encryption_key(secure_file.file.file_id, std::move(key));
  return std::make_pair(secure_file.file, SecureFileCredentials{secret.as_slice().str(), hash.as_slice().str()});
}

static Result<std::pair<vector<DatedFile>, vector<SecureFileCredentials>>> decrypt_secure_files(
    FileManager *file_manager, const secure_storage::Secret &secret, const vector<EncryptedSecureFile> &secure_files) {
  vector<DatedFile> result;
  vector<SecureFileCredentials> credentials;
  result.reserve(secure_files.size());
  credentials.reserve(secure_files.size());
  for (auto &file : secure_files) {
    TRY_RESULT(decrypted_file, decrypt_secure_file(file_manager, secret, file));
    result.push_back(std::move(decrypted_file.first));
    credentials.push_back(std::move(decrypted_file.second));
  }

  return std::make_pair(std::move(result), std::move(credentials));
}

static Result<std::pair<string, SecureDataCredentials>> decrypt_secure_data(const secure_storage::Secret &master_secret,
                                                                            const EncryptedSecureData &secure_data) {
  TRY_RESULT(hash, secure_storage::ValueHash::create(secure_data.hash));
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secure_data.encrypted_secret));
  TRY_RESULT(secret, encrypted_secret.decrypt(PSLICE() << master_secret.as_slice() << hash.as_slice()));
  TRY_RESULT(value, secure_storage::decrypt_value(secret, hash, secure_data.data));
  return std::make_pair(value.as_slice().str(), SecureDataCredentials{secret.as_slice().str(), hash.as_slice().str()});
}

Result<SecureValueWithCredentials> decrypt_secure_value(FileManager *file_manager, const secure_storage::Secret &secret,
                                                        const EncryptedSecureValue &encrypted_secure_value) {
  SecureValue res;
  SecureValueCredentials res_credentials;
  res.type = encrypted_secure_value.type;
  res_credentials.type = res.type;
  res_credentials.hash = encrypted_secure_value.hash;
  switch (encrypted_secure_value.type) {
    case SecureValueType::None:
      return Status::Error("Receive invalid Telegram Passport data");
    case SecureValueType::EmailAddress:
    case SecureValueType::PhoneNumber:
      res.data = encrypted_secure_value.data.data;
      break;
    case SecureValueType::UtilityBill:
    case SecureValueType::BankStatement:
    case SecureValueType::RentalAgreement:
    case SecureValueType::PassportRegistration:
    case SecureValueType::TemporaryRegistration: {
      TRY_RESULT(files, decrypt_secure_files(file_manager, secret, encrypted_secure_value.files));
      res.files = std::move(files.first);
      res_credentials.files = std::move(files.second);
      break;
    }
    default: {
      TRY_RESULT(data, decrypt_secure_data(secret, encrypted_secure_value.data));
      res.data = std::move(data.first);
      if (!res.data.empty()) {
        res_credentials.data = std::move(data.second);
      }
      CHECK(encrypted_secure_value.files.empty());
      TRY_RESULT(front_side, decrypt_secure_file(file_manager, secret, encrypted_secure_value.front_side));
      res.front_side = std::move(front_side.first);
      if (res.front_side.file_id.is_valid()) {
        res_credentials.front_side = std::move(front_side.second);
      }
      TRY_RESULT(reverse_side, decrypt_secure_file(file_manager, secret, encrypted_secure_value.reverse_side));
      res.reverse_side = std::move(reverse_side.first);
      if (res.reverse_side.file_id.is_valid()) {
        res_credentials.reverse_side = std::move(reverse_side.second);
      }
      TRY_RESULT(selfie, decrypt_secure_file(file_manager, secret, encrypted_secure_value.selfie));
      res.selfie = std::move(selfie.first);
      if (res.selfie.file_id.is_valid()) {
        res_credentials.selfie = std::move(selfie.second);
      }
      break;
    }
  }
  return SecureValueWithCredentials{std::move(res), std::move(res_credentials)};
}

Result<vector<SecureValueWithCredentials>> decrypt_secure_values(
    FileManager *file_manager, const secure_storage::Secret &secret,
    const vector<EncryptedSecureValue> &encrypted_secure_values) {
  vector<SecureValueWithCredentials> result;
  result.reserve(encrypted_secure_values.size());
  for (auto &encrypted_secure_value : encrypted_secure_values) {
    auto r_secure_value_with_credentials = decrypt_secure_value(file_manager, secret, encrypted_secure_value);
    if (r_secure_value_with_credentials.is_ok()) {
      result.push_back(r_secure_value_with_credentials.move_as_ok());
    } else {
      LOG(ERROR) << "Cannot decrypt secure value: " << r_secure_value_with_credentials.error();
    }
  }
  return std::move(result);
}

static EncryptedSecureFile encrypt_secure_file(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                               DatedFile file, string &to_hash) {
  auto file_view = file_manager->get_file_view(file.file_id);
  if (file_view.empty()) {
    return EncryptedSecureFile();
  }
  if (!file_view.encryption_key().is_secure()) {
    LOG(ERROR) << "File " << file.file_id << " has no encryption key";
    return EncryptedSecureFile();
  }
  if (!file_view.encryption_key().has_value_hash()) {
    LOG(ERROR) << "File " << file.file_id << " has no hash";
    return EncryptedSecureFile();
  }
  auto value_hash = file_view.encryption_key().value_hash();
  auto secret = file_view.encryption_key().secret();
  EncryptedSecureFile res;
  res.file = file;
  res.file_hash = value_hash.as_slice().str();
  res.encrypted_secret = secret.encrypt(PSLICE() << master_secret.as_slice() << value_hash.as_slice()).as_slice().str();

  to_hash.append(res.file_hash);
  to_hash.append(secret.as_slice().str());
  return res;
}

static vector<EncryptedSecureFile> encrypt_secure_files(FileManager *file_manager,
                                                        const secure_storage::Secret &master_secret,
                                                        vector<DatedFile> files, string &to_hash) {
  return transform(
      files, [&](auto dated_file) { return encrypt_secure_file(file_manager, master_secret, dated_file, to_hash); });
  /*
  vector<EncryptedSecureFile> result;
  result.reserve(files.size());
  for (auto &file : files) {
    auto encrypted_secure_file = encrypt_secure_file(file_manager, master_secret, file, to_hash);
    if (encrypted_secure_file.file.file_id.is_valid()) {
      result.push_back(std::move(encrypted_secure_file));
    }
  }
  return result;
*/
}

static EncryptedSecureData encrypt_secure_data(const secure_storage::Secret &master_secret, Slice data,
                                               string &to_hash) {
  auto secret = secure_storage::Secret::create_new();
  auto encrypted = encrypt_value(secret, data).move_as_ok();
  EncryptedSecureData res;
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
      res.data = EncryptedSecureData{secure_value.data, "", ""};
      res.hash = secure_storage::calc_value_hash(secure_value.data).as_slice().str();
      break;
    case SecureValueType::UtilityBill:
    case SecureValueType::BankStatement:
    case SecureValueType::RentalAgreement:
    case SecureValueType::PassportRegistration:
    case SecureValueType::TemporaryRegistration: {
      string to_hash;
      res.files = encrypt_secure_files(file_manager, master_secret, secure_value.files, to_hash);
      res.hash = secure_storage::calc_value_hash(to_hash).as_slice().str();
      break;
    }
    default: {
      string to_hash;
      res.data = encrypt_secure_data(master_secret, secure_value.data, to_hash);
      CHECK(secure_value.files.empty());
      res.front_side = encrypt_secure_file(file_manager, master_secret, secure_value.front_side, to_hash);
      res.reverse_side = encrypt_secure_file(file_manager, master_secret, secure_value.reverse_side, to_hash);
      res.selfie = encrypt_secure_file(file_manager, master_secret, secure_value.selfie, to_hash);
      res.hash = secure_storage::calc_value_hash(to_hash).as_slice().str();
      break;
    }
  }
  return res;
}

static auto as_jsonable(const SecureDataCredentials &credentials) {
  return json_object([&credentials](auto &o) {
    o("data_hash", base64_encode(credentials.hash));
    o("secret", base64_encode(credentials.secret));
  });
}

static auto as_jsonable(const SecureFileCredentials &credentials) {
  return json_object([&credentials](auto &o) {
    o("file_hash", base64_encode(credentials.hash));
    o("secret", base64_encode(credentials.secret));
  });
}

static auto as_jsonable(const vector<SecureFileCredentials> &files) {
  return json_array([&files](auto &arr) {
    for (auto &file : files) {
      arr(as_jsonable(file));
    }
  });
}

static Slice secure_value_type_as_slice(SecureValueType type) {
  switch (type) {
    case SecureValueType::PersonalDetails:
      return Slice("personal_details");
    case SecureValueType::Passport:
      return Slice("passport");
    case SecureValueType::DriverLicense:
      return Slice("driver_license");
    case SecureValueType::IdentityCard:
      return Slice("identity_card");
    case SecureValueType::InternalPassport:
      return Slice("internal_passport");
    case SecureValueType::Address:
      return Slice("address");
    case SecureValueType::UtilityBill:
      return Slice("utility_bill");
    case SecureValueType::BankStatement:
      return Slice("bank_statement");
    case SecureValueType::RentalAgreement:
      return Slice("rental_agreement");
    case SecureValueType::PassportRegistration:
      return Slice("passport_registration");
    case SecureValueType::TemporaryRegistration:
      return Slice("temporary_registration");
    case SecureValueType::PhoneNumber:
      return Slice("phone_number");
    case SecureValueType::EmailAddress:
      return Slice("email");
    default:
    case SecureValueType::None:
      UNREACHABLE();
      return Slice("none");
  }
}

static auto credentials_as_jsonable(const std::vector<SecureValueCredentials> &credentials, Slice payload,
                                    bool with_selfie) {
  return json_object([&credentials, payload, with_selfie](auto &o) {
    o("secure_data", json_object([&credentials, with_selfie](auto &o) {
        for (auto &cred : credentials) {
          if (cred.type == SecureValueType::PhoneNumber || cred.type == SecureValueType::EmailAddress) {
            continue;
          }

          o(secure_value_type_as_slice(cred.type), json_object([&cred, with_selfie](auto &o) {
              if (cred.data) {
                o("data", as_jsonable(cred.data.value()));
              }
              if (!cred.files.empty()) {
                o("files", as_jsonable(cred.files));
              }
              if (cred.front_side) {
                o("front_side", as_jsonable(cred.front_side.value()));
              }
              if (cred.reverse_side) {
                o("reverse_side", as_jsonable(cred.reverse_side.value()));
              }
              if (cred.selfie && with_selfie) {
                o("selfie", as_jsonable(cred.selfie.value()));
              }
            }));
        }
      }));
    o("payload", payload);
  });
}

Result<EncryptedSecureCredentials> get_encrypted_credentials(const std::vector<SecureValueCredentials> &credentials,
                                                             Slice payload, bool with_selfie, Slice public_key) {
  auto encoded_credentials = json_encode<std::string>(credentials_as_jsonable(credentials, payload, with_selfie));
  LOG(INFO) << "Created credentials " << encoded_credentials;

  auto secret = secure_storage::Secret::create_new();
  auto encrypted_value = secure_storage::encrypt_value(secret, encoded_credentials).move_as_ok();
  EncryptedSecureCredentials res;
  res.data = encrypted_value.data.as_slice().str();
  res.hash = encrypted_value.hash.as_slice().str();
  TRY_RESULT(encrypted_secret, rsa_encrypt_pkcs1_oaep(public_key, secret.as_slice()));
  res.encrypted_secret = encrypted_secret.as_slice().str();
  return res;
}

}  // namespace td

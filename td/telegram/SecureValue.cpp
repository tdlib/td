//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureValue.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/misc.h"
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

namespace td {

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
    const vector<tl_object_ptr<telegram_api::SecureValueType>> &secure_value_types) {
  return transform(secure_value_types, get_secure_value_type);
}

vector<SecureValueType> get_secure_value_types_td_api(
    const vector<tl_object_ptr<td_api::PassportDataType>> &secure_value_types) {
  return transform(secure_value_types, get_secure_value_type_td_api);
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

td_api::object_ptr<telegram_api::SecureValueType> get_secure_value_type_object(SecureValueType type) {
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

bool operator==(const EncryptedSecureFile &lhs, const EncryptedSecureFile &rhs) {
  return lhs.file_id == rhs.file_id && lhs.file_hash == rhs.file_hash && lhs.encrypted_secret == rhs.encrypted_secret;
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

vector<EncryptedSecureFile> get_encrypted_secure_files(FileManager *file_manager,
                                                       vector<tl_object_ptr<telegram_api::SecureFile>> &&secure_files) {
  vector<EncryptedSecureFile> results;
  results.reserve(secure_files.size());
  for (auto &secure_file : secure_files) {
    auto result = get_encrypted_secure_file(file_manager, std::move(secure_file));
    if (result.file_id.is_valid()) {
      results.push_back(std::move(result));
    }
  }
  return results;
}

telegram_api::object_ptr<telegram_api::InputSecureFile> get_input_secure_file_object(FileManager *file_manager,
                                                                                     const EncryptedSecureFile &file,
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

td_api::object_ptr<td_api::file> get_encrypted_file_object(FileManager *file_manager, const EncryptedSecureFile &file) {
  CHECK(file.file_id.is_valid());
  auto file_view = file_manager->get_file_view(file.file_id);
  auto file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::SecureRaw, file_view.remote_location().get_id(),
                             file_view.remote_location().get_access_hash(), file_view.remote_location().get_dc_id()),
      FileLocationSource::FromServer, {}, 0, 0, "");
  return file_manager->get_file_object(file_id);
}

vector<td_api::object_ptr<td_api::file>> get_encrypted_files_object(FileManager *file_manager,
                                                                    const vector<EncryptedSecureFile> &files) {
  return transform(
      files, [file_manager](const EncryptedSecureFile &file) { return get_encrypted_file_object(file_manager, file); });
}

vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> get_input_secure_files_object(
    FileManager *file_manager, const vector<EncryptedSecureFile> &files, vector<SecureInputFile> &input_files) {
  CHECK(files.size() == input_files.size());
  vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> res;
  res.resize(files.size());
  for (size_t i = 0; i < files.size(); i++) {
    res[i] = get_input_secure_file_object(file_manager, files[i], input_files[i]);
  }
  return res;
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
  return lhs.type == rhs.type && lhs.data == rhs.data && lhs.files == rhs.files && lhs.selfie == rhs.selfie;
}

bool operator!=(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs) {
  return !(lhs == rhs);
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
  if (secure_value->selfie_ != nullptr) {
    result.selfie = get_encrypted_secure_file(file_manager, std::move(secure_value->selfie_));
  }
  result.hash = secure_value->hash_.as_slice().str();
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
      value.selfie.file_id.is_valid() ? get_encrypted_file_object(file_manager, value.selfie) : nullptr);
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
      flags, get_secure_value_type_object(value.type), is_plain ? nullptr : get_secure_data_object(value.data),
      get_input_secure_files_object(file_manager, value.files, input_files), std::move(plain_data),
      has_selfie ? get_input_secure_file_object(file_manager, value.selfie, *selfie) : nullptr);
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
    return Status::Error(400, "Date must not be empty");
  }
  TRY_STATUS(check_date(date->day_, date->month_, date->year_));

  return PSTRING() << lpad0(to_string(date->day_), 2) << '.' << lpad0(to_string(date->month_), 2) << '.'
                   << lpad0(to_string(date->year_), 4);
}

static Result<td_api::object_ptr<td_api::date>> get_date_object(Slice date) {
  if (date.size() != 10u) {
    return Status::Error(400, "Date has wrong size");
  }
  auto parts = full_split(date, '.');
  if (parts.size() != 3 || parts[0].size() != 2 || parts[1].size() != 2 || parts[2].size() != 4) {
    return Status::Error(400, "Date has wrong parts");
  }
  TRY_RESULT(day, to_integer_safe<int32>(parts[0]));
  TRY_RESULT(month, to_integer_safe<int32>(parts[1]));
  TRY_RESULT(year, to_integer_safe<int32>(parts[2]));
  TRY_STATUS(check_date(day, month, year));

  return td_api::make_object<td_api::date>(day, month, year);
}

static Status check_first_name(string &first_name) {
  if (!clean_input_string(first_name)) {
    return Status::Error(400, "First name must be encoded in UTF-8");
  }
  return Status::OK();
}

static Status check_last_name(string &last_name) {
  if (!clean_input_string(last_name)) {
    return Status::Error(400, "Last name must be encoded in UTF-8");
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
  TRY_STATUS(check_gender(personal_details->gender_));
  TRY_STATUS(check_country_code(personal_details->country_code_));

  return json_encode<std::string>(json_object([&](auto &o) {
    o("first_name", personal_details->first_name_);
    o("last_name", personal_details->last_name_);
    o("birth_date", birthdate);
    o("gender", personal_details->gender_);
    o("country_code", personal_details->country_code_);
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
  TRY_RESULT(gender, get_json_object_string_field(object, "gender", true));
  TRY_RESULT(country_code, get_json_object_string_field(object, "country_code", true));

  TRY_STATUS(check_first_name(first_name));
  TRY_STATUS(check_last_name(last_name));
  TRY_RESULT(date, get_date_object(birthdate));
  TRY_STATUS(check_gender(gender));
  TRY_STATUS(check_country_code(country_code));

  return td_api::make_object<td_api::personalDetails>(std::move(first_name), std::move(last_name), std::move(date),
                                                      std::move(gender), std::move(country_code));
}

static Status check_document_number(string &number) {
  if (!clean_input_string(number)) {
    return Status::Error(400, "Document number must be encoded in UTF-8");
  }
  return Status::OK();
}

static Result<FileId> get_secure_file(FileManager *file_manager, td_api::object_ptr<td_api::InputFile> &&file) {
  return file_manager->get_input_file_id(FileType::Secure, std::move(file), DialogId(), false, false, false, true);
}

static Result<vector<FileId>> get_secure_files(FileManager *file_manager,
                                               vector<td_api::object_ptr<td_api::InputFile>> &&files) {
  vector<FileId> result;
  for (auto &file : files) {
    TRY_RESULT(file_id, get_secure_file(file_manager, std::move(file)));
    result.push_back(file_id);
  }
  return result;
}

static Result<SecureValue> get_identity_document(
    SecureValueType type, FileManager *file_manager,
    td_api::object_ptr<td_api::inputIdentityDocument> &&identity_document) {
  if (identity_document == nullptr) {
    return Status::Error(400, "Identity document must not be empty");
  }
  TRY_STATUS(check_document_number(identity_document->number_));
  TRY_RESULT(date, get_date(std::move(identity_document->expiry_date_)));
  TRY_RESULT(files, get_secure_files(file_manager, std::move(identity_document->files_)));

  SecureValue res;
  res.type = type;
  res.data = json_encode<std::string>(json_object([&](auto &o) {
    o("document_no", identity_document->number_);
    o("expiry_date", date);
  }));

  res.files = std::move(files);
  if (identity_document->selfie_ != nullptr) {
    TRY_RESULT(file_id, get_secure_file(file_manager, std::move(identity_document->selfie_)));
    res.selfie = file_id;
  }
  return res;
}

static Result<td_api::object_ptr<td_api::identityDocument>> get_identity_document_object(FileManager *file_manager,
                                                                                         const SecureValue &value) {
  auto files = transform(value.files, [file_manager](FileId id) { return file_manager->get_file_object(id); });

  td_api::object_ptr<td_api::file> selfie;
  if (value.selfie.is_valid()) {
    selfie = file_manager->get_file_object(value.selfie);
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

  return td_api::make_object<td_api::identityDocument>(std::move(number), std::move(date), std::move(files),
                                                       std::move(selfie));
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
      return get_identity_document(SecureValueType::Passport, file_manager, std::move(input->passport_));
    }
    case td_api::inputPassportDataDriverLicense::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataDriverLicense>(input_passport_data);
      return get_identity_document(SecureValueType::DriverLicense, file_manager, std::move(input->driver_license_));
    }
    case td_api::inputPassportDataIdentityCard::ID: {
      auto input = td_api::move_object_as<td_api::inputPassportDataIdentityCard>(input_passport_data);
      return get_identity_document(SecureValueType::IdentityCard, file_manager, std::move(input->identity_card_));
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
    case SecureValueType::Address: {
      TRY_RESULT(address, address_from_json(value.data));
      return td_api::make_object<td_api::passportDataAddress>(get_address_object(address));
    }
    case SecureValueType::UtilityBill:
    case SecureValueType::BankStatement:
    case SecureValueType::RentalAgreement: {
      auto files = transform(value.files, [file_manager](FileId id) { return file_manager->get_file_object(id); });
      if (value.type == SecureValueType::UtilityBill) {
        return td_api::make_object<td_api::passportDataUtilityBill>(std::move(files));
      }
      if (value.type == SecureValueType::BankStatement) {
        return td_api::make_object<td_api::passportDataBankStatement>(std::move(files));
      }
      if (value.type == SecureValueType::RentalAgreement) {
        return td_api::make_object<td_api::passportDataRentalAgreement>(std::move(files));
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

Result<std::pair<FileId, SecureFileCredentials>> decrypt_secure_file(FileManager *file_manager,
                                                                     const secure_storage::Secret &master_secret,
                                                                     const EncryptedSecureFile &secure_file) {
  if (!secure_file.file_id.is_valid()) {
    return std::make_pair(FileId(), SecureFileCredentials());
  }
  TRY_RESULT(hash, secure_storage::ValueHash::create(secure_file.file_hash));
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secure_file.encrypted_secret));
  TRY_RESULT(secret, encrypted_secret.decrypt(PSLICE() << master_secret.as_slice() << hash.as_slice()));
  FileEncryptionKey key{secret};
  key.set_value_hash(hash);
  file_manager->set_encryption_key(secure_file.file_id, std::move(key));
  return std::make_pair(secure_file.file_id, SecureFileCredentials{secret.as_slice().str(), hash.as_slice().str()});
}

Result<std::pair<vector<FileId>, vector<SecureFileCredentials>>> decrypt_secure_files(
    FileManager *file_manager, const secure_storage::Secret &secret, const vector<EncryptedSecureFile> &secure_files) {
  vector<FileId> res;
  vector<SecureFileCredentials> credentials;
  res.reserve(secure_files.size());
  for (auto &file : secure_files) {
    TRY_RESULT(decrypted_file, decrypt_secure_file(file_manager, secret, file));
    res.push_back(decrypted_file.first);
    credentials.push_back(decrypted_file.second);
  }

  return std::make_pair(std::move(res), std::move(credentials));
}

Result<std::pair<string, SecureDataCredentials>> decrypt_secure_data(const secure_storage::Secret &master_secret,
                                                                     const EncryptedSecureData &secure_data) {
  TRY_RESULT(hash, secure_storage::ValueHash::create(secure_data.hash));
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secure_data.encrypted_secret));
  TRY_RESULT(secret, encrypted_secret.decrypt(PSLICE() << master_secret.as_slice() << hash.as_slice()));
  TRY_RESULT(value, secure_storage::decrypt_value(secret, hash, secure_data.data));
  return std::make_pair(value.as_slice().str(), SecureDataCredentials{secret.as_slice().str(), hash.as_slice().str()});
}

Result<SecureValueWithCredentials> decrypt_encrypted_secure_value(FileManager *file_manager,
                                                                  const secure_storage::Secret &secret,
                                                                  const EncryptedSecureValue &encrypted_secure_value) {
  SecureValue res;
  SecureValueCredentials res_credentials;
  res.type = encrypted_secure_value.type;
  res_credentials.type = res.type;
  res_credentials.hash = encrypted_secure_value.hash;
  switch (encrypted_secure_value.type) {
    case SecureValueType::EmailAddress:
    case SecureValueType::PhoneNumber:
      res.data = encrypted_secure_value.data.data;
      break;
    default: {
      TRY_RESULT(data, decrypt_secure_data(secret, encrypted_secure_value.data));
      res.data = std::move(data.first);
      if (!res.data.empty()) {
        res_credentials.data = std::move(data.second);
      }
      TRY_RESULT(files, decrypt_secure_files(file_manager, secret, encrypted_secure_value.files));
      res.files = std::move(files.first);
      res_credentials.files = std::move(files.second);
      TRY_RESULT(selfie, decrypt_secure_file(file_manager, secret, encrypted_secure_value.selfie));
      res.selfie = std::move(selfie.first);
      if (res.selfie.is_valid()) {
        res_credentials.selfie = std::move(selfie.second);
      }
      break;
    }
  }
  return SecureValueWithCredentials{std::move(res), std::move(res_credentials)};
}

Result<vector<SecureValueWithCredentials>> decrypt_encrypted_secure_values(
    FileManager *file_manager, const secure_storage::Secret &secret,
    const vector<EncryptedSecureValue> &encrypted_secure_values) {
  vector<SecureValueWithCredentials> result;
  result.reserve(encrypted_secure_values.size());
  for (auto &encrypted_secure_value : encrypted_secure_values) {
    auto r_secure_value_with_credentials = decrypt_encrypted_secure_value(file_manager, secret, encrypted_secure_value);
    if (r_secure_value_with_credentials.is_ok()) {
      result.push_back(r_secure_value_with_credentials.move_as_ok());
    } else {
      LOG(ERROR) << "Cannot decrypt secure value: " << r_secure_value_with_credentials.error();
    }
  }
  return std::move(result);
}

EncryptedSecureFile encrypt_secure_file(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                        FileId file, string &to_hash) {
  auto file_view = file_manager->get_file_view(file);
  if (file_view.empty()) {
    return EncryptedSecureFile();
  }
  if (!file_view.encryption_key().is_secure()) {
    LOG(ERROR) << "File has no encryption key";
    return EncryptedSecureFile();
  }
  if (!file_view.encryption_key().has_value_hash()) {
    LOG(ERROR) << "File has no hash";
    return EncryptedSecureFile();
  }
  auto value_hash = file_view.encryption_key().value_hash();
  auto secret = file_view.encryption_key().secret();
  EncryptedSecureFile res;
  res.file_id = file;
  res.file_hash = value_hash.as_slice().str();
  res.encrypted_secret = secret.encrypt(PSLICE() << master_secret.as_slice() << value_hash.as_slice()).as_slice().str();

  to_hash.append(res.file_hash);
  to_hash.append(secret.as_slice().str());
  return res;
}

vector<EncryptedSecureFile> encrypt_secure_files(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                                 vector<FileId> files, string &to_hash) {
  return transform(files,
                   [&](auto file_id) { return encrypt_secure_file(file_manager, master_secret, file_id, to_hash); });
}

EncryptedSecureData encrypt_secure_data(const secure_storage::Secret &master_secret, Slice data, string &to_hash) {
  namespace ss = secure_storage;
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
    case SecureValueType::Address:
      return Slice("address");
    case SecureValueType::UtilityBill:
      return Slice("utility_bill");
    case SecureValueType::BankStatement:
      return Slice("bank_statement");
    case SecureValueType::RentalAgreement:
      return Slice("rental_agreement");
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

static auto credentials_as_jsonable(std::vector<SecureValueCredentials> &credentials, Slice payload) {
  return json_object([&credentials, &payload](auto &o) {
    o("secure_data", json_object([&credentials](auto &o) {
        for (auto &c : credentials) {
          if (c.type == SecureValueType::PhoneNumber || c.type == SecureValueType::EmailAddress) {
            continue;
          }

          o(secure_value_type_as_slice(c.type), json_object([&credentials = c](auto &o) {
              if (credentials.data) {
                o("data", as_jsonable(credentials.data.value()));
              }
              if (!credentials.files.empty()) {
                o("files", as_jsonable(credentials.files));
              }
              if (credentials.selfie) {
                o("selfie", as_jsonable(credentials.selfie.value()));
              }
            }));
        }
      }));
    o("payload", payload);
  });
}

Result<EncryptedSecureCredentials> encrypted_credentials(std::vector<SecureValueCredentials> &credentials,
                                                         Slice payload, Slice public_key) {
  auto encoded_credentials = json_encode<std::string>(credentials_as_jsonable(credentials, payload));

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

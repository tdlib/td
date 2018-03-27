//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/SecureStorage.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class FileManager;

enum class SecureValueType {
  None,
  PersonalDetails,
  Passport,
  DriverLicense,
  IdentityCard,
  Address,
  UtilityBill,
  BankStatement,
  RentalAgreement,
  PhoneNumber,
  EmailAddress
};

SecureValueType get_secure_value_type(tl_object_ptr<telegram_api::SecureValueType> &&secure_value_type);
SecureValueType get_secure_value_type_td_api(tl_object_ptr<td_api::PassportDataType> &&passport_data_type);

vector<SecureValueType> get_secure_value_types(
    vector<tl_object_ptr<telegram_api::SecureValueType>> &&secure_value_types);

td_api::object_ptr<td_api::PassportDataType> get_passport_data_type_object(SecureValueType type);
td_api::object_ptr<telegram_api::SecureValueType> get_secure_value_type_telegram_object(SecureValueType type);

vector<td_api::object_ptr<td_api::PassportDataType>> get_passport_data_types_object(
    const vector<SecureValueType> &types);

struct SecureFile {
  FileId file_id;
  string file_hash;
  string encrypted_secret;
};

bool operator==(const SecureFile &lhs, const SecureFile &rhs);
bool operator!=(const SecureFile &lhs, const SecureFile &rhs);

SecureFile get_secure_file(FileManager *file_manager, tl_object_ptr<telegram_api::SecureFile> &&secure_file_ptr);

vector<SecureFile> get_secure_files(FileManager *file_manager,
                                    vector<tl_object_ptr<telegram_api::SecureFile>> &&secure_files);

telegram_api::object_ptr<telegram_api::InputSecureFile> get_input_secure_file_object(FileManager *file_manager,
                                                                                     const SecureFile &file);

td_api::object_ptr<td_api::file> get_encrypted_file_object(FileManager *file_manager, const SecureFile &file);

vector<td_api::object_ptr<td_api::file>> get_encrypted_files_object(FileManager *file_manager,
                                                                    const vector<SecureFile> &files);

vector<telegram_api::object_ptr<telegram_api::InputSecureFile>> get_input_secure_files_object(
    FileManager *file_manager, const vector<SecureFile> &file);

struct SecureData {
  string data;
  string hash;
  string encrypted_secret;
};

bool operator==(const SecureData &lhs, const SecureData &rhs);
bool operator!=(const SecureData &lhs, const SecureData &rhs);

SecureData get_secure_data(tl_object_ptr<telegram_api::secureData> &&secure_data);

telegram_api::object_ptr<telegram_api::secureData> get_secure_data_object(const SecureData &data);

struct EncryptedSecureValue {
  SecureValueType type = SecureValueType::None;
  SecureData data;
  vector<SecureFile> files;
  SecureFile selfie;
  string hash;  // memory only
};

bool operator==(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs);
bool operator!=(const EncryptedSecureValue &lhs, const EncryptedSecureValue &rhs);

EncryptedSecureValue get_encrypted_secure_value(FileManager *file_manager,
                                                tl_object_ptr<telegram_api::secureValue> &&secure_value);

vector<EncryptedSecureValue> get_encrypted_secure_values(
    FileManager *file_manager, vector<tl_object_ptr<telegram_api::secureValue>> &&secure_values);

td_api::object_ptr<td_api::encryptedPassportData> get_encrypted_passport_data_object(FileManager *file_manager,
                                                                                     const EncryptedSecureValue &value);
telegram_api::object_ptr<telegram_api::inputSecureValue> get_input_secure_value_object(
    FileManager *file_manager, const EncryptedSecureValue &value);

vector<td_api::object_ptr<td_api::encryptedPassportData>> get_encrypted_passport_data_object(
    FileManager *file_manager, const vector<EncryptedSecureValue> &values);

struct SecureCredentials {
  string data;
  string hash;
  string encrypted_secret;
};

bool operator==(const SecureCredentials &lhs, const SecureCredentials &rhs);
bool operator!=(const SecureCredentials &lhs, const SecureCredentials &rhs);

SecureCredentials get_secure_credentials(tl_object_ptr<telegram_api::secureCredentialsEncrypted> &&credentials);

td_api::object_ptr<td_api::encryptedCredentials> get_encrypted_credentials_object(const SecureCredentials &credentials);

class SecureValue {
 public:
  SecureValueType type;
  string data;
  vector<FileId> files;
};

Result<SecureValue> get_secure_value(td_api::object_ptr<td_api::inputPassportData> &&input_passport_data);

Result<FileId> decrypt_secure_file(FileManager *file_manager, const secure_storage::Secret &secret,
                                   const SecureFile &secure_file);
Result<vector<FileId>> decrypt_secure_files(FileManager *file_manager, const secure_storage::Secret &secret,
                                            const vector<SecureFile> &secure_file);
Result<string> decrypt_secure_data(const secure_storage::Secret &secret, const SecureData &secure_data);
Result<SecureValue> decrypt_encrypted_secure_value(FileManager *file_manager, const secure_storage::Secret &secret,
                                                   const EncryptedSecureValue &encrypted_secure_value);

SecureFile encrypt_secure_file(FileManager *file_manager, const secure_storage::Secret &master_secret, FileId file,
                               string &to_hash);
vector<SecureFile> encrypt_secure_files(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                        vector<FileId> files, string &to_hash);
SecureData encrypt_secure_data(const secure_storage::Secret &master_secret, Slice data, string &to_hash);
EncryptedSecureValue encrypt_secure_value(FileManager *file_manager, const secure_storage::Secret &master_secret,
                                          const SecureValue &secure_value);

}  // namespace td

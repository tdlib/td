//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/Handshake.h"

#include "td/mtproto/KDF.h"
#include "td/mtproto/utils.h"

#include "td/mtproto/mtproto_api.h"

#include "td/utils/as.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

namespace td {
namespace mtproto {

template <class T>
static Result<typename T::ReturnType> fetch_result(Slice message, bool check_end = true) {
  TlParser parser(message);
  auto result = T::fetch_result(parser);

  if (check_end) {
    parser.fetch_end();
  }
  const char *error = parser.get_error();
  if (error != nullptr) {
    LOG(ERROR) << "Can't parse: " << format::as_hex_dump<4>(message);
    return Status::Error(500, Slice(error));
  }

  return std::move(result);
}

void AuthKeyHandshake::clear() {
  last_query_ = BufferSlice();
  state_ = Start;
}

bool AuthKeyHandshake::is_ready_for_start() const {
  return state_ == Start;
}
bool AuthKeyHandshake::is_ready_for_message(const UInt128 &message_nonce) const {
  return state_ != Finish && state_ != Start && nonce == message_nonce;
}
bool AuthKeyHandshake::is_ready_for_finish() const {
  return state_ == Finish;
}
void AuthKeyHandshake::on_finish() {
  clear();
}

template <class DataT>
Result<size_t> AuthKeyHandshake::fill_data_with_hash(uint8 *data_with_hash, const DataT &data) {
  // data_with_hash := SHA1(data) + data + (any random bytes); such that the length equal 255 bytes;
  uint8 *data_ptr = data_with_hash + 20;
  size_t data_size = tl_calc_length(data);
  if (data_size + 20 + 4 > 255) {
    return Status::Error("Too big data");
  }
  as<int32>(data_ptr) = data.get_id();
  auto real_size = tl_store_unsafe(data, data_ptr + 4);
  CHECK(real_size == data_size);
  sha1(Slice(data_ptr, data_size + 4), data_with_hash);
  return data_size + 20 + 4;
}

Status AuthKeyHandshake::on_res_pq(Slice message, Callback *connection, PublicRsaKeyInterface *public_rsa_key) {
  TRY_RESULT(res_pq, fetch_result<mtproto_api::req_pq_multi>(message, false));
  if (res_pq->nonce_ != nonce) {
    return Status::Error("Nonce mismatch");
  }

  server_nonce = res_pq->server_nonce_;

  auto r_rsa = public_rsa_key->get_rsa(res_pq->server_public_key_fingerprints_);
  if (r_rsa.is_error()) {
    public_rsa_key->drop_keys();
    return r_rsa.move_as_error();
  }
  int64 rsa_fingerprint = r_rsa.ok().second;
  RSA rsa = std::move(r_rsa.ok_ref().first);

  string p, q;
  if (pq_factorize(res_pq->pq_, &p, &q) == -1) {
    return Status::Error("Failed to factorize");
  }

  Random::secure_bytes(new_nonce.raw, sizeof(new_nonce));

  alignas(8) uint8 data_with_hash[255];
  Result<size_t> r_data_size = 0;
  switch (mode_) {
    case Mode::Main:
      r_data_size = fill_data_with_hash(
          data_with_hash, mtproto_api::p_q_inner_data_dc(res_pq->pq_, p, q, nonce, server_nonce, new_nonce, dc_id_));
      break;
    case Mode::Temp:
      r_data_size = fill_data_with_hash(
          data_with_hash,
          mtproto_api::p_q_inner_data_temp_dc(res_pq->pq_, p, q, nonce, server_nonce, new_nonce, dc_id_, expires_in_));
      expires_at_ = Time::now() + expires_in_;
      break;
    case Mode::Unknown:
    default:
      UNREACHABLE();
      r_data_size = Status::Error(500, "Unreachable");
  }
  if (r_data_size.is_error()) {
    return r_data_size.move_as_error();
  }
  size_t size = r_data_size.ok();

  // encrypted_data := RSA (data_with_hash, server_public_key); a 255-byte long number (big endian)
  //   is raised to the requisite power over the requisite modulus, and the result is stored as a 256-byte number.
  string encrypted_data(256, 0);
  rsa.encrypt(data_with_hash, size, sizeof(data_with_hash), reinterpret_cast<unsigned char *>(&encrypted_data[0]),
              encrypted_data.size());

  // req_DH_params#d712e4be nonce:int128 server_nonce:int128 p:string q:string public_key_fingerprint:long
  // encrypted_data:string = Server_DH_Params
  mtproto_api::req_DH_params req_dh_params(nonce, server_nonce, p, q, rsa_fingerprint, encrypted_data);

  send(connection, create_storer(req_dh_params));
  state_ = ServerDHParams;
  return Status::OK();
}

Status AuthKeyHandshake::on_server_dh_params(Slice message, Callback *connection, DhCallback *dh_callback) {
  TRY_RESULT(server_dh_params, fetch_result<mtproto_api::req_DH_params>(message, false));
  switch (server_dh_params->get_id()) {
    case mtproto_api::server_DH_params_ok::ID:
      break;
    case mtproto_api::server_DH_params_fail::ID:
      return Status::Error("Server dh params fail");
    default:
      return Status::Error("Unknown result");
  }

  auto dh_params = move_tl_object_as<mtproto_api::server_DH_params_ok>(server_dh_params);

  // server_DH_params_ok#d0e8075c nonce:int128 server_nonce:int128 encrypted_answer:string = Server_DH_Params;
  if (dh_params->nonce_ != nonce) {
    return Status::Error("Nonce mismatch");
  }
  if (dh_params->server_nonce_ != server_nonce) {
    return Status::Error("Server nonce mismatch");
  }
  if (dh_params->encrypted_answer_.size() & 15) {
    return Status::Error("Bad padding for encrypted part");
  }

  tmp_KDF(server_nonce, new_nonce, &tmp_aes_key, &tmp_aes_iv);
  auto save_tmp_aes_iv = tmp_aes_iv;
  // encrypted_answer := AES256_ige_encrypt (answer_with_hash, tmp_aes_key, tmp_aes_iv);
  MutableSlice answer(const_cast<char *>(dh_params->encrypted_answer_.begin()), dh_params->encrypted_answer_.size());
  aes_ige_decrypt(as_slice(tmp_aes_key), as_slice(tmp_aes_iv), answer, answer);
  tmp_aes_iv = save_tmp_aes_iv;

  // answer_with_hash := SHA1(answer) + answer + (0-15 random bytes)
  TlParser answer_parser(answer);
  UInt<160> answer_sha1 = answer_parser.fetch_binary<UInt<160>>();
  int32 id = answer_parser.fetch_int();
  if (id != mtproto_api::server_DH_inner_data::ID) {
    return Status::Error("Failed to fetch server_DH_inner_data");
  }
  mtproto_api::server_DH_inner_data dh_inner_data(answer_parser);
  if (answer_parser.get_error() != nullptr) {
    return Status::Error("Failed to fetch server_DH_inner_data");
  }

  size_t pad = answer_parser.get_left_len();
  if (pad >= 16) {
    return Status::Error("Too much pad");
  }

  size_t dh_inner_data_size = answer.size() - pad - 20;
  UInt<160> answer_real_sha1;
  sha1(answer.substr(20, dh_inner_data_size), answer_real_sha1.raw);
  if (answer_sha1 != answer_real_sha1) {
    return Status::Error("SHA1 mismatch");
  }

  if (dh_inner_data.nonce_ != nonce) {
    return Status::Error("Nonce mismatch");
  }
  if (dh_inner_data.server_nonce_ != server_nonce) {
    return Status::Error("Server nonce mismatch");
  }

  server_time_diff_ = dh_inner_data.server_time_ - Time::now();

  DhHandshake handshake;
  handshake.set_config(dh_inner_data.g_, dh_inner_data.dh_prime_);
  handshake.set_g_a(dh_inner_data.g_a_);
  TRY_STATUS(handshake.run_checks(false, dh_callback));
  string g_b = handshake.get_g_b();
  auto auth_key_params = handshake.gen_key();

  mtproto_api::client_DH_inner_data data(nonce, server_nonce, 0, g_b);
  size_t data_size = 4 + tl_calc_length(data);
  size_t encrypted_data_size = 20 + data_size;
  size_t encrypted_data_size_with_pad = (encrypted_data_size + 15) & -16;
  string encrypted_data_str(encrypted_data_size_with_pad, 0);
  MutableSlice encrypted_data = encrypted_data_str;
  as<int32>(encrypted_data.begin() + 20) = data.get_id();
  auto real_size = tl_store_unsafe(data, encrypted_data.ubegin() + 20 + 4);
  CHECK(real_size + 4 == data_size);
  sha1(encrypted_data.substr(20, data_size), encrypted_data.ubegin());
  Random::secure_bytes(encrypted_data.ubegin() + encrypted_data_size,
                       encrypted_data_size_with_pad - encrypted_data_size);
  tmp_KDF(server_nonce, new_nonce, &tmp_aes_key, &tmp_aes_iv);
  aes_ige_encrypt(as_slice(tmp_aes_key), as_slice(tmp_aes_iv), encrypted_data, encrypted_data);

  mtproto_api::set_client_DH_params set_client_dh_params(nonce, server_nonce, encrypted_data);
  send(connection, create_storer(set_client_dh_params));

  auth_key_ = AuthKey(auth_key_params.first, std::move(auth_key_params.second));
  if (mode_ == Mode::Temp) {
    auth_key_.set_expires_at(expires_at_);
  }
  auth_key_.set_created_at(dh_inner_data.server_time_);

  server_salt_ = as<int64>(new_nonce.raw) ^ as<int64>(server_nonce.raw);

  state_ = DHGenResponse;
  return Status::OK();
}

Status AuthKeyHandshake::on_dh_gen_response(Slice message, Callback *connection) {
  TRY_RESULT(answer, fetch_result<mtproto_api::set_client_DH_params>(message, false));
  switch (answer->get_id()) {
    case mtproto_api::dh_gen_ok::ID:
      state_ = Finish;
      break;
    case mtproto_api::dh_gen_fail::ID:
      return Status::Error("DhGenFail");
    case mtproto_api::dh_gen_retry::ID:
      return Status::Error("DhGenRetry");
    default:
      return Status::Error("Unknown set_client_DH_params response");
  }
  return Status::OK();
}

void AuthKeyHandshake::send(Callback *connection, const Storer &storer) {
  auto size = storer.size();
  auto writer = BufferWriter{size, 0, 0};
  auto real_size = storer.store(writer.as_slice().ubegin());
  CHECK(real_size == size);
  last_query_ = writer.as_buffer_slice();
  return do_send(connection, create_storer(last_query_.as_slice()));
}

void AuthKeyHandshake::do_send(Callback *connection, const Storer &storer) {
  return connection->send_no_crypto(storer);
}

Status AuthKeyHandshake::start_main(Callback *connection) {
  mode_ = Mode::Main;
  return on_start(connection);
}

Status AuthKeyHandshake::start_tmp(Callback *connection, int32 expires_in) {
  mode_ = Mode::Temp;
  expires_in_ = expires_in;
  return on_start(connection);
}

void AuthKeyHandshake::resume(Callback *connection) {
  if (state_ == Start) {
    return on_start(connection).ignore();
  }
  if (state_ == Finish) {
    LOG(ERROR) << "State is Finish during resume. UNREACHABLE";
    return clear();
  }
  if (last_query_.empty()) {
    LOG(ERROR) << "Last query empty! UNREACHABLE " << state_;
    return clear();
  }
  LOG(INFO) << "RESUME";
  do_send(connection, create_storer(last_query_.as_slice()));
}

Status AuthKeyHandshake::on_start(Callback *connection) {
  if (state_ != Start) {
    clear();
    return Status::Error(PSLICE() << "on_start called after start " << tag("state", state_));
  }
  Random::secure_bytes(nonce.raw, sizeof(nonce));
  send(connection, create_storer(mtproto_api::req_pq_multi(nonce)));
  state_ = ResPQ;

  return Status::OK();
}

Status AuthKeyHandshake::on_message(Slice message, Callback *connection, AuthKeyHandshakeContext *context) {
  Status status = [&] {
    switch (state_) {
      case ResPQ:
        return on_res_pq(message, connection, context->get_public_rsa_key_interface());
      case ServerDHParams:
        return on_server_dh_params(message, connection, context->get_dh_callback());
      case DHGenResponse:
        return on_dh_gen_response(message, connection);
      default:
        UNREACHABLE();
    }
  }();
  if (status.is_error()) {
    clear();
  }
  return status;
}

}  // namespace mtproto
}  // namespace td

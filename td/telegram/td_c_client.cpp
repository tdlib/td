//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/td_c_client.h"

#include "td/telegram/Client.h"
#include "td/telegram/Log.h"

#include "td/telegram/td_tdc_api_inner.h"

#include <cstring>

void *TdCClientCreate() {
  return new td::Client();
}

void TdCClientSend(void *instance, struct TdRequest request) {
  auto client = static_cast<td::Client *>(instance);
  td::Client::Request client_request;
  client_request.id = request.id;
  client_request.function = TdConvertToInternal(request.function);
  TdDestroyObjectFunction(request.function);
  client->send(std::move(client_request));
}

TdResponse TdCClientReceive(void *instance, double timeout) {
  auto client = static_cast<td::Client *>(instance);
  auto response = client->receive(timeout);
  TdResponse c_response;
  c_response.id = response.id;
  c_response.object = response.object == nullptr ? nullptr : TdConvertFromInternal(*response.object);
  return c_response;
}

void TdCClientDestroy(void *instance) {
  auto client = static_cast<td::Client *>(instance);
  delete client;
}

void TdCClientSetVerbosity(int new_verbosity_level) {
  td::Log::set_verbosity_level(new_verbosity_level);
}

TdVectorInt *TdCreateObjectVectorInt(int size, int *data) {
  auto res = new TdVectorInt();
  res->len = size;
  res->data = new int[size];
  std::memcpy(res->data, data, sizeof(int) * size);
  return res;
}

TdVectorLong *TdCreateObjectVectorLong(int size, long long *data) {
  auto res = new TdVectorLong();
  res->len = size;
  res->data = new long long[size];
  std::memcpy(res->data, data, sizeof(long long) * size);
  return res;
}

TdVectorObject *TdCreateObjectVectorObject(int size, TdObject **data) {
  auto res = new TdVectorObject();
  res->len = size;
  res->data = new TdObject *[size];
  std::memcpy(res->data, data, sizeof(TdObject *) * size);
  return res;
}

TdBytes TdCreateObjectBytes(unsigned char *data, int len) {
  TdBytes res;
  res.len = len;
  res.data = new unsigned char[len];
  std::memcpy(res.data, data, len);
  return res;
}

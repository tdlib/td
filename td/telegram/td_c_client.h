//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "td/telegram/td_tdc_api.h"

struct TdVectorInt *TdCreateObjectVectorInt(int size, int *data);
struct TdVectorLong *TdCreateObjectVectorLong(int size, long long *data);
struct TdVectorObject *TdCreateObjectVectorObject(int size, struct TdObject **data);
struct TdBytes TdCreateObjectBytes(unsigned char *data, int len);

struct TdRequest {
  long long id;
  TdFunction *function;
};

struct TdResponse {
  long long id;
  TdObject *object;
};

void *TdCClientCreate();
void TdCClientSend(void *instance, struct TdRequest cmd);
struct TdResponse TdCClientSendCommandSync(void *instance, double timeout);
void TdCClientDestroy(void *instance);

void TdCClientSetVerbosity(int new_verbosity_level);

#ifdef __cplusplus
}
#endif

#!/bin/sh
cd $(dirname $0)
tl-parser -e td_api.tlo td_api.tl
tl-parser -e telegram_api.tlo telegram_api.tl
tl-parser -e mtproto_api.tlo mtproto_api.tl
tl-parser -e secret_api.tlo secret_api.tl

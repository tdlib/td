// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include <stdint.h>

#ifndef SQLITE_EXTRA_INIT
#define SQLITE_EXTRA_INIT sqlcipher_extra_init
#endif

#ifndef SQLITE_EXTRA_SHUTDOWN
#define SQLITE_EXTRA_SHUTDOWN sqlcipher_extra_shutdown
#endif

#include "generated/tdsqlite_rename.h"
#include "upstream/sqlite3.c"

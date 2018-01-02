//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

/**
 * \file
 * C interface for managing the internal logging of TDLib.
 * By default TDLib writes logs to stderr or an OS specific log and uses a verbosity level of 5.
 */

#include "td/telegram/tdjson_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets the path to the file where the internal TDLib log will be written.
 * By default TDLib writes logs to stderr or an OS specific log.
 * Use this method to write the log to a file instead.
 *
 * \param[in]  path Path to a file where the internal TDLib log will be written. Use an empty path to switch back to
 *                  the default logging behaviour.
 */
TDJSON_EXPORT void td_set_log_file_path(const char *path);

/**
 * Sets the verbosity level of the internal logging of TDLib.
 * By default the TDLib uses a log verbosity level of 5.
 *
 * \param[in]  new_verbosity_level New value of logging verbosity level.
 *                                 Value 0 corresponds to fatal errors,
 *                                 value 1 corresponds to errors,
 *                                 value 2 corresponds to warnings and debug warnings,
 *                                 value 3 corresponds to informational,
 *                                 value 4 corresponds to debug,
 *                                 value 5 corresponds to verbose debug,
 *                                 value greater than 5 and up to 1024 can be used to enable even more logging.
 */
TDJSON_EXPORT void td_set_log_verbosity_level(int new_verbosity_level);

#ifdef __cplusplus
}  // extern "C"
#endif

# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
THREAD_LOCAL_H = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "thread_local.h"
THREAD_ID_GUARD_CPP = (
    REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "detail" / "ThreadIdGuard.cpp"
)
THREAD_LOCAL_STORAGE_H = REPO_ROOT / "tdutils" / "td" / "utils" / "ThreadLocalStorage.h"
CONCURRENT_HASH_TABLE_H = (
    REPO_ROOT / "tdutils" / "td" / "utils" / "ConcurrentHashTable.h"
)
TS_FILE_LOG_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "TsFileLog.cpp"


def normalize(source: str) -> str:
    return "".join(source.split())


class ThreadIdCapacityContractTest(unittest.TestCase):
    def test_public_thread_id_capacity_constants_are_declared_once(self) -> None:
        source = normalize(THREAD_LOCAL_H.read_text(encoding="utf-8"))

        self.assertIn("inlineconstexprint32kThreadIdSlotCount=128;", source)
        self.assertIn(
            "inlineconstexprint32kMaxRegisteredThreadId=kThreadIdSlotCount-1;",
            source,
        )

    def test_thread_id_manager_checks_capacity_before_allocating_new_id(self) -> None:
        source = normalize(THREAD_ID_GUARD_CPP.read_text(encoding="utf-8"))

        self.assertIn(
            "if(unused_thread_ids_.empty()){CHECK(max_thread_id_<kMaxRegisteredThreadId);return++max_thread_id_;}",
            source,
        )

    def test_thread_id_indexed_structures_use_shared_capacity_constant(self) -> None:
        thread_local_storage = normalize(
            THREAD_LOCAL_STORAGE_H.read_text(encoding="utf-8")
        )
        concurrent_hash_table = normalize(
            CONCURRENT_HASH_TABLE_H.read_text(encoding="utf-8")
        )
        ts_file_log = normalize(TS_FILE_LOG_CPP.read_text(encoding="utf-8"))

        self.assertIn("kThreadIdSlotCount", thread_local_storage)
        self.assertIn("kThreadIdSlotCount", concurrent_hash_table)
        self.assertIn("kThreadIdSlotCount", ts_file_log)

        self.assertNotIn("HAZARD_POINTER_THREAD_SLOTS=128", concurrent_hash_table)
        self.assertNotIn("staticconstexprint32MAX_THREAD_ID=128;", thread_local_storage)
        self.assertNotIn("staticconstexprsize_tMAX_THREAD_ID=128;", ts_file_log)


if __name__ == "__main__":
    unittest.main()

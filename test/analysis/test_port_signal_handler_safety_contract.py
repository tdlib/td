# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PORT_TEST_CPP = REPO_ROOT / "tdutils" / "test" / "port.cpp"


def extract_region(text: str, begin_marker: str, end_marker: str) -> str:
    begin = text.find(begin_marker)
    if begin == -1:
        raise AssertionError(f"missing begin marker: {begin_marker}")
    end = text.find(end_marker, begin + len(begin_marker))
    if end == -1:
        raise AssertionError(f"missing end marker: {end_marker}")
    if end <= begin:
        raise AssertionError("invalid region bounds")
    return text[begin:end]


class PortSignalHandlerSafetyContractTest(unittest.TestCase):
    def test_user_signal_handler_avoids_mutex_and_dynamic_string_work(self) -> None:
        source = PORT_TEST_CPP.read_text(encoding="utf-8")
        handler = extract_region(
            source,
            "static void on_user_signal(int sig) {",
            "TEST(Port, SignalsAndThread)",
        )

        self.assertNotIn("std::unique_lock<std::mutex>", handler)
        self.assertNotIn("td::to_string", handler)
        self.assertNotIn("push_back(", handler)
        self.assertNotIn("addrs[signal_thread_id]", handler)

    def test_signal_handler_uses_sig_atomic_thread_state_only(self) -> None:
        source = PORT_TEST_CPP.read_text(encoding="utf-8")

        self.assertIn(
            "static TD_THREAD_LOCAL volatile sig_atomic_t signal_thread_id;",
            source,
        )
        self.assertNotIn("static const void *addrs[", source)
        self.assertNotIn("addrs_snapshot", source)

    def test_signal_test_keeps_observed_results_out_of_handler_context(self) -> None:
        source = PORT_TEST_CPP.read_text(encoding="utf-8")

        self.assertIn("TEST(Port, SignalsAndThread)", source)
        self.assertNotIn(
            "ptrs.push_back(td::to_string(thread_id));",
            source,
            msg="signal delivery bookkeeping must not allocate strings inside the signal handler",
        )


if __name__ == "__main__":
    unittest.main()

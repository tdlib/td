#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import json
import pathlib
from typing import Any

from common_tls import CANONICAL_ROUTE_MODES


def load_json_report(path: str | pathlib.Path) -> dict[str, Any]:
    report_path = pathlib.Path(path)
    with report_path.open("r", encoding="utf-8") as infile:
        report = json.load(infile)
    if not isinstance(report, dict):
        raise ValueError("report must be a JSON object")
    return report


def append_failure_once(failures: list[str], failure: str) -> None:
    if failure not in failures:
        failures.append(failure)


def validate_smoke_scenario(report: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    active_policy = report.get("active_policy")
    if not isinstance(active_policy, str) or active_policy not in CANONICAL_ROUTE_MODES:
        append_failure_once(failures, "active-policy-known")

    quic_enabled = report.get("quic_enabled")
    if quic_enabled is not False:
        append_failure_once(failures, "quic-disabled")
    return failures
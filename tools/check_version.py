#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify that all public SDK version declarations match VERSION.txt."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def require(pattern: str, text: str, path: Path, expected: str) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        raise ValueError(f"{path}: version declaration not found")
    actual = match.group(1)
    if actual != expected:
        raise ValueError(f"{path}: expected {expected}, found {actual}")
    return actual


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    version = (root / "VERSION.txt").read_text(encoding="utf-8").strip()

    checks = (
        (
            root / "CMakeLists.txt",
            r"project\(moonlight_haptics\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
            version,
        ),
        (
            root / "include/moonlight_haptics/version.h",
            r'MOONLIGHT_HAPTICS_VERSION_STRING\s+"([^"]+)"',
            version,
        ),
        (
            root / "platform/android/build.gradle",
            r"sdkVersion'\) \?: '([^']+)-SNAPSHOT'",
            version,
        ),
        (
            root / "tests/c_api_test.c",
            r'ah_get_version_string\(\),\s*"([^"]+)"',
            version,
        ),
    )

    try:
        for path, pattern, expected in checks:
            require(pattern, path.read_text(encoding="utf-8"), path, expected)
    except (OSError, ValueError) as error:
        print(f"version-check: {error}", file=sys.stderr)
        return 1

    print(f"version-check: {version} declarations agree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

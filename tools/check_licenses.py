#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail SDK builds when sources violate the reviewed license boundary."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


SPDX = "SPDX-License-Identifier: Apache-2.0"
REVIEWED_THIRD_PARTY_PREFIXES = {
    ("third_party", "libfvad"),
}
SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".gradle",
    ".h",
    ".hpp",
    ".kt",
    ".kts",
    ".py",
    ".xml",
}
FORBIDDEN_SOURCE_MARKERS = (
    "#include \"aubio",
    "#include <aubio",
    "GPL-2.0",
    "GPL-3.0",
    "AGPL-",
)
SKIP_DIRS = {
    "build",
    "consumer-build",
    "dist",
    "install",
    ".cxx",
    ".git",
    ".gradle",
    "__pycache__",
}


def iter_source_files(root: Path):
    for path in root.rglob("*"):
        if not path.is_file() or any(part in SKIP_DIRS for part in path.parts):
            continue
        if path.suffix.lower() in SOURCE_SUFFIXES or path.name == "CMakeLists.txt":
            yield path


def check(root: Path) -> list[str]:
    errors: list[str] = []
    license_path = root / "LICENSE"
    if not license_path.exists():
        errors.append("missing LICENSE")
    else:
        license_text = license_path.read_text(encoding="utf-8")
        if "Apache License" not in license_text or "Version 2.0, January 2004" not in license_text:
            errors.append("LICENSE is not the Apache License 2.0 text")

    notices_path = root / "THIRD_PARTY_NOTICES.md"
    if not notices_path.exists():
        errors.append("missing THIRD_PARTY_NOTICES.md")
    else:
        notices_text = notices_path.read_text(encoding="utf-8")
        if "libfvad" not in notices_text or "532ab666" not in notices_text:
            errors.append("THIRD_PARTY_NOTICES.md: missing pinned libfvad notice")

    libfvad_license_path = root / "third_party/libfvad/LICENSE"
    if not libfvad_license_path.exists():
        errors.append("third_party/libfvad/LICENSE: missing BSD-3-Clause text")
    else:
        libfvad_license = libfvad_license_path.read_text(encoding="utf-8")
        if ("Redistribution and use in source and binary forms" not in
                libfvad_license or
                "Neither the name of Google" not in libfvad_license):
            errors.append("third_party/libfvad/LICENSE: unexpected license text")

    aar_license_path = (
        root / "platform/android/src/main/resources/META-INF/LICENSE.libfvad"
    )
    if not aar_license_path.exists():
        errors.append("Android AAR libfvad license resource is missing")
    else:
        aar_license = aar_license_path.read_text(encoding="utf-8")
        if ("Redistribution and use in source and binary forms" not in
                aar_license or "Neither the name of Google" not in aar_license):
            errors.append("Android AAR libfvad license resource is incomplete")

    aar_notice_path = (
        root /
        "platform/android/src/main/resources/META-INF/NOTICE.moonlight-audio-haptics"
    )
    if not aar_notice_path.exists():
        errors.append("Android AAR third-party notice resource is missing")
    else:
        aar_notice = aar_notice_path.read_text(encoding="utf-8")
        if "libfvad" not in aar_notice or "532ab666" not in aar_notice:
            errors.append("Android AAR third-party notice resource is incomplete")

    for path in iter_source_files(root):
        text = path.read_text(encoding="utf-8")
        first_lines = "\n".join(text.splitlines()[:5])
        relative = path.relative_to(root)
        prefix = tuple(relative.parts[:2])
        reviewed_third_party = prefix in REVIEWED_THIRD_PARTY_PREFIXES
        if not reviewed_third_party and SPDX not in first_lines:
            errors.append(f"{relative}: missing Apache-2.0 SPDX in first 5 lines")
        if path.resolve() != Path(__file__).resolve():
            for marker in FORBIDDEN_SOURCE_MARKERS:
                if marker in text:
                    errors.append(f"{relative}: forbidden SDK source marker {marker!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    errors = check(args.root.resolve())
    if errors:
        for error in errors:
            print(f"license-check: {error}", file=sys.stderr)
        return 1
    print("license-check: reviewed permissive SDK boundary OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

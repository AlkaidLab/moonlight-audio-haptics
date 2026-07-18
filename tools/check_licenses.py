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
        if ("libfvad" not in notices_text or "532ab666" not in notices_text or
                "third_party/libfvad/PATENTS" not in notices_text or
                "third_party/libfvad/PATCHES.md" not in notices_text):
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

    libfvad_patents_path = root / "third_party/libfvad/PATENTS"
    libfvad_patents = ""
    if not libfvad_patents_path.exists():
        errors.append("third_party/libfvad/PATENTS: missing WebRTC patent grant")
    else:
        libfvad_patents = libfvad_patents_path.read_text(encoding="utf-8")
        if ("Additional IP Rights Grant (Patents)" not in libfvad_patents or
                "perpetual, worldwide, non-exclusive" not in libfvad_patents or
                "patent rights granted to you" not in libfvad_patents):
            errors.append("third_party/libfvad/PATENTS: unexpected grant text")

    libfvad_patches_path = root / "third_party/libfvad/PATCHES.md"
    if not libfvad_patches_path.exists():
        errors.append("third_party/libfvad/PATCHES.md: missing local patch record")
    else:
        libfvad_patches = libfvad_patches_path.read_text(encoding="utf-8")
        if ("vad_core.c" not in libfvad_patches or
                "vad_sp.c" not in libfvad_patches or
                "532ab666" not in libfvad_patches):
            errors.append("third_party/libfvad/PATCHES.md: incomplete patch record")

    upstream_path = root / "third_party/libfvad/UPSTREAM.md"
    if not upstream_path.exists():
        errors.append("third_party/libfvad/UPSTREAM.md: missing import record")
    else:
        upstream_text = upstream_path.read_text(encoding="utf-8")
        if ("532ab666" not in upstream_text or
                "PATENTS" not in upstream_text or
                "PATCHES.md" not in upstream_text):
            errors.append("third_party/libfvad/UPSTREAM.md: incomplete import record")

    vad_core_path = root / "third_party/libfvad/src/vad/vad_core.c"
    vad_sp_path = root / "third_party/libfvad/src/vad/vad_sp.c"
    if vad_core_path.exists():
        vad_core = vad_core_path.read_text(encoding="utf-8")
        if ("const uint32_t product" not in vad_core or
                "UINT32_MAX - product" not in vad_core):
            errors.append("third_party/libfvad/src/vad/vad_core.c: safety patch missing")
    if vad_sp_path.exists():
        vad_sp = vad_sp_path.read_text(encoding="utf-8")
        if "for (j = i; j < 15; j++)" not in vad_sp:
            errors.append("third_party/libfvad/src/vad/vad_sp.c: bounds patch missing")

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

    aar_patents_path = (
        root / "platform/android/src/main/resources/META-INF/PATENTS.libfvad"
    )
    if not aar_patents_path.exists():
        errors.append("Android AAR libfvad patent resource is missing")
    else:
        aar_patents = aar_patents_path.read_text(encoding="utf-8")
        if not libfvad_patents or aar_patents != libfvad_patents:
            errors.append("Android AAR libfvad patent resource differs from source grant")

    aar_notice_path = (
        root /
        "platform/android/src/main/resources/META-INF/NOTICE.moonlight-audio-haptics"
    )
    if not aar_notice_path.exists():
        errors.append("Android AAR third-party notice resource is missing")
    else:
        aar_notice = aar_notice_path.read_text(encoding="utf-8")
        if ("libfvad" not in aar_notice or "532ab666" not in aar_notice or
                "PATENTS.libfvad" not in aar_notice or
                "PATCHES.md" not in aar_notice):
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

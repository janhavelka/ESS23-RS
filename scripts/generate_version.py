#!/usr/bin/env python3
"""Generate Version.h from library.json, following sibling library conventions.

Only sync/check are needed by this library; no application dependency pins,
git calls or non-reproducible build timestamps are generated.
"""

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    command = sys.argv[1] if len(sys.argv) == 2 else "sync" if len(sys.argv) == 1 else ""
    if command not in ("sync", "check"):
        raise SystemExit("Usage: python scripts/generate_version.py [sync|check]")
    version = json.loads((ROOT / "library.json").read_text(encoding="utf-8"))["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("library.json version must be MAJOR.MINOR.PATCH")
    major, minor, patch = map(int, version.split("."))
    if major > 65535 or minor > 99 or patch > 99:
        raise SystemExit("Version components exceed the numeric version encoding")
    output = (
        "/** @file Version.h Generated from library.json; do not edit. */\n"
        "#pragma once\n\n#include <stdint.h>\n\nnamespace MotorControlRS {\n"
        f"static constexpr uint16_t VERSION_MAJOR = {major};\n"
        f"static constexpr uint16_t VERSION_MINOR = {minor};\n"
        f"static constexpr uint16_t VERSION_PATCH = {patch};\n"
        f"static constexpr uint32_t VERSION_CODE = {major * 10000 + minor * 100 + patch};\n"
        f'static constexpr const char* VERSION = "{version}";\n'
        "}  // namespace MotorControlRS\n"
    )
    target = ROOT / "include/MotorControlRS/Version.h"
    current = target.read_text(encoding="utf-8") if target.exists() else None
    if current == output:
        print("Version.h is current")
    elif command == "check":
        raise SystemExit("Version.h is stale; run scripts/generate_version.py sync")
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(output, encoding="utf-8", newline="\n")
        print("Updated Version.h")


if __name__ == "__main__":
    main()

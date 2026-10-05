#!/usr/bin/env python3
"""Offline snapshot, maintained Markdown-link and core metadata checks.

No downloads, Git, framework tools or PDF parser are required. PDF bytes and
their recorded page-count metadata are checked here; the contrast preparation
tool separately checks actual PDF page counts. Anchors and external URLs are
not availability checks. Historical reports and raw PDF extracts are excluded
as link sources; links to those files must still resolve.
"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from urllib.parse import unquote, urlsplit


REFERENCE_PATHS = {
    "sources.json": {
        "vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf",
        "vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf",
        "vendor/ESS23-RS20_Full_Datasheet.pdf",
        "vendor/ESS23-RS10_Full_Datasheet.pdf",
        "vendor/cad/ESS23-RS20.STEP", "vendor/cad/ESS23-RS10.STEP",
        "vendor/software/Y_Series_Stepper_Driver_Debug_Software_V1.2.7.zip",
        "standards/Modbus_Application_Protocol_V1.1b3.pdf",
        "standards/Modbus_Serial_Line_V1.02.pdf",
    },
    "serial_contrasts_sources.json": {
        "vendor/contrasts/Leadshine_iEM-RS_User_Manual_V2.0.pdf",
        "vendor/contrasts/Oriental_Motor_AZ_HM-60262E.pdf",
        "vendor/contrasts/Nanotec_PD4E_ModbusRTU_V1.6.0.pdf",
        "vendor/contrasts/Applied_Motion_Host_Command_Reference_920-0002W.pdf",
        "vendor/contrasts/Makerbase_SERVO42_57D_Modbus_V1.0.9.pdf",
    },
}
SIBLING_REFERENCES = {"FieldCore-node", "SHZK-PT", "VTN4xx", "VibWire-108"}
GENERATED_DIRECTORIES = {"build", ".pio", ".git", ".venv", "__pycache__"}


def inside(path, root):
    return path == root or root in path.parents


def load_json(path, errors):
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise ValueError("expected a JSON object")
        return value
    except (OSError, ValueError) as exc:
        errors.append(f"{path}: {exc}")
        return None


def check_references(root):
    errors = []
    docs = (root / "docs").resolve()
    for name, expected in REFERENCE_PATHS.items():
        manifest = docs / "reference" / name
        data = load_json(manifest, errors)
        if data is None:
            continue
        records = data.get("references")
        if not isinstance(records, list) or any(not isinstance(r, dict) for r in records):
            errors.append(f"{manifest}: references must be a list of objects")
            continue
        paths = [r.get("path") for r in records]
        if any(not isinstance(p, str) for p in paths) or len(paths) != len(expected) or set(paths) != expected:
            errors.append(f"{manifest}: must contain exactly the {len(expected)} reviewed reference paths")
        for record in records:
            relative = record.get("path")
            if not isinstance(relative, str):
                continue
            # Reject traversal even if it would normalize back inside docs.
            parts = PurePosixPath(relative.replace("\\", "/"))
            path = (docs / relative).resolve()
            if parts.is_absolute() or ".." in parts.parts or ":" in relative or not inside(path, docs):
                errors.append(f"{manifest}: unsafe reference path {relative!r}")
                continue
            try:
                raw = path.read_bytes()
            except OSError as exc:
                errors.append(f"{relative}: missing/unreadable reference: {exc}")
                continue
            if type(record.get("bytes")) is not int or record["bytes"] != len(raw):
                errors.append(f"{relative}: byte count mismatch")
            if record.get("sha256") != hashlib.sha256(raw).hexdigest():
                errors.append(f"{relative}: SHA-256 mismatch")
            suffix = path.suffix.lower()
            signature_ok = (
                (suffix == ".pdf" and raw.startswith(b"%PDF-")) or
                (suffix == ".zip" and raw.startswith(b"PK\x03\x04")) or
                (suffix == ".step" and b"ISO-10303-21" in raw[:200])
            )
            if not signature_ok:
                errors.append(f"{relative}: invalid {suffix} signature")
            if suffix == ".pdf" and (type(record.get("pages")) is not int or record["pages"] <= 0):
                errors.append(f"{relative}: missing/invalid recorded PDF page count")
            for field in ("text_extract", "review"):
                if field not in record:
                    continue
                target = record[field]
                if not isinstance(target, str):
                    errors.append(f"{relative}: invalid {field} path")
                    continue
                referenced = (docs / target).resolve()
                if not inside(referenced, docs) or not referenced.is_file():
                    errors.append(f"{relative}: missing/unsafe {field} file {target!r}")
    return errors


def prose_only(text):
    """Blank code/comments while preserving offsets and source line numbers."""
    blank = lambda match: re.sub(r"[^\n]", " ", match.group())
    lines = text.splitlines(keepends=True)
    fence = None
    for index, line in enumerate(lines):
        match = re.match(r"^ {0,3}(`{3,}|~{3,})", line)
        if fence:
            lines[index] = re.sub(r"[^\n]", " ", line)
            if match and match[1][0] == fence[0] and len(match[1]) >= len(fence):
                fence = None
        elif match:
            fence = match[1]
            lines[index] = re.sub(r"[^\n]", " ", line)
        elif line.startswith("    ") or line.startswith("\t"):
            lines[index] = re.sub(r"[^\n]", " ", line)
    text = "".join(lines)
    text = re.sub(r"<!--.*?-->", blank, text, flags=re.S)
    return re.sub(r"(`+)(?!`).*?(?<!`)\1(?!`)", blank, text, flags=re.S)


def destination(text, start):
    """Read a Markdown destination, including balanced filename parentheses."""
    while start < len(text) and text[start].isspace():
        start += 1
    if start >= len(text):
        return "", start
    if text[start] == "<":
        end = text.find(">", start + 1)
        return (text[start + 1:end], end + 1) if end >= 0 else ("", start)
    end, depth = start, 0
    while end < len(text):
        char = text[end]
        if char == "\\" and end + 1 < len(text):
            end += 2
            continue
        if char == "(":
            depth += 1
        elif char == ")":
            if not depth:
                break
            depth -= 1
        elif char.isspace() and not depth:
            break
        end += 1
    return re.sub(r"\\([() ])", r"\1", text[start:end]), end


def markdown_links(text):
    text = prose_only(text)
    definitions = {}
    for match in re.finditer(r"^ {0,3}\[([^]\n]+)\]:[ \t]*(.*)$", text, flags=re.M):
        target, _ = destination(match[2], 0)
        definitions[" ".join(match[1].lower().split())] = target
        # Definitions are checked when used, not merely declared.
        text = text[:match.start()] + re.sub(r"[^\n]", " ", match.group()) + text[match.end():]
    for match in re.finditer(r"(?<!\\)\[([^]\n]*)\]", text):
        after = match.end()
        if after < len(text) and text[after] == "(":
            target, _ = destination(text, after + 1)
            yield match.start(), target
        elif after < len(text) and text[after] == "[":
            end = text.find("]", after + 1)
            if end >= 0:
                label = text[after + 1:end] or match[1]
                key = " ".join(label.lower().split())
                if key in definitions:
                    yield match.start(), definitions[key]
                else:
                    yield match.start(), "!undefined-reference:" + label
        else:
            key = " ".join(match[1].lower().split())
            if key in definitions:
                yield match.start(), definitions[key]


def markdown_files(root):
    files = list(root.glob("*.md"))
    for folder in ("docs", "examples", "scripts", "test"):
        files.extend((root / folder).rglob("*.md"))
    for path in sorted(files):
        relative = path.relative_to(root)
        if any(part in GENERATED_DIRECTORIES for part in relative.parts):
            continue
        if relative.parts[:2] in (("docs", "reports"), ("docs", "pdf-extracted-md")):
            continue
        yield path


def check_docs(root):
    errors = []
    for source in markdown_files(root):
        text = source.read_text(encoding="utf-8")
        for offset, target in markdown_links(text):
            location = f"{source.relative_to(root).as_posix()}:{text.count(chr(10), 0, offset) + 1}"
            if target.startswith("!undefined-reference:"):
                errors.append(f"{location}: undefined Markdown reference {target.split(':', 1)[1]!r}")
                continue
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            relative = unquote(parsed.path).replace("\\", "/")
            path = ((root / relative.lstrip("/")) if relative.startswith("/") else
                    source.parent / relative).resolve()
            if not inside(path, root):
                # Only named adjacent reference repositories are optional.
                if any(inside(path, root.parent / sibling) for sibling in SIBLING_REFERENCES):
                    continue
                errors.append(f"{location}: local link escapes repository: {target!r}")
            elif not path.exists():
                errors.append(f"{location}: missing local link target {target!r}")
    return errors


def check_metadata(root):
    errors = []
    metadata = load_json(root / "library.json", errors)
    if metadata is not None:
        if metadata.get("name") != "MotorControl-RS":
            errors.append("library.json: package name must be MotorControl-RS")
        if metadata.get("repository", {}).get("url") != "https://github.com/janhavelka/MotorControl-RS.git":
            errors.append("library.json: repository URL must use canonical MotorControl-RS identity")
        if metadata.get("build", {}).get("includeDir") != "include" or metadata.get("build", {}).get("srcDir") != "src":
            errors.append("library.json: core includeDir/srcDir must be include/src")
        exports = metadata.get("export", {}).get("include", [])
        allowed = {"include/", "src/", "CMakeLists.txt", "library.json", "LICENSE", "README.md", "CHANGELOG.md"}
        if not isinstance(exports, list) or any(p not in allowed for p in exports) or not {"include/", "src/", "CMakeLists.txt", "LICENSE"}.issubset(exports):
            errors.append("library.json: export must contain the core and exclude firmware, tests, Python and vendor resources")
        elif any(not (root / p).exists() for p in exports):
            errors.append("library.json: exported path is missing")
    try:
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"CMakeLists.txt: {exc}")
        return errors
    if not re.search(r"project\(\s*MotorControlRS\b", cmake):
        errors.append("CMakeLists.txt: project identity must be MotorControlRS")
    source_block = re.search(r"set\(MOTORCONTROLRS_SOURCES\b(.*?)\)", cmake, re.S)
    registered = re.findall(r'"(src/[^"\n]+\.cpp)"', source_block[1]) if source_block else []
    actual = {p.relative_to(root).as_posix() for p in (root / "src").rglob("*.cpp")}
    if len(registered) != len(actual) or set(registered) != actual or not actual:
        errors.append("CMakeLists.txt: MOTORCONTROLRS_SOURCES must list every core .cpp exactly once")
    banned = re.compile(r"^(?:Arduino(?:\.h|/)|freertos/|driver/|esp_|FieldCore|TunnelMonitor/)", re.I)
    for base in (root / "include", root / "src"):
        for path in sorted(base.rglob("*")):
            if path.suffix not in (".h", ".hpp", ".cpp"):
                continue
            code = re.sub(r"/\*.*?\*/|//[^\n]*", "", path.read_text(encoding="utf-8"), flags=re.S)
            for match in re.finditer(r'^\s*#\s*include\s*[<"]([^">]+)[">]', code, re.M):
                if banned.match(match[1]) or match[1].startswith(("examples/", "../examples/")):
                    errors.append(f"{path.relative_to(root)}: core includes platform/application header {match[1]!r}")
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--section", action="append", choices=("references", "docs", "metadata"), help="Repeat to select checks; default: all")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    checks = {"references": check_references, "docs": check_docs, "metadata": check_metadata}
    errors = []
    for section in args.section or checks:
        findings = checks[section](root)
        errors.extend(findings)
        print(f"{section}: {'FAIL' if findings else 'PASS'}")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())

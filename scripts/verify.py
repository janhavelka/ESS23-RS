#!/usr/bin/env python3
"""Fail-closed repository verification; full adds explicitly selected firmware builds."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def require_file(path):
    if not path.is_file():
        raise RuntimeError(f"missing required check/input: {path}")
    return path


def tool(name):
    found = shutil.which(name)
    if not found:
        raise RuntimeError(f"missing required tool: {name}")
    return found


def check_inventory(root, listing):
    tests = listing.get("tests", [])
    if not tests:
        raise RuntimeError("CTest registered no tests")
    commands = {Path(argument.replace("\\", "/")).name for t in tests for argument in t.get("command", [])}
    names = {t["name"] for t in tests}
    for path in sorted((root / "test").glob("*_test.cpp")):
        registered = next((t for t in tests if t["name"] == path.stem[:-5]), None)
        if not registered or Path(registered.get("command", [""])[0]).stem != path.stem:
            raise RuntimeError(f"unregistered native test: {path.name}")
    for path in sorted((root / "test").glob("*_test.py")):
        if path.name not in commands:
            raise RuntimeError(f"unregistered Python test: {path.name}")
    for name in ("codec_independent", "probe_app_load", "idf_move_app", "idf_control_app", "units_preview",
                 "register_gaps", "operations_inventory_failures"):
        if name not in names:
            raise RuntimeError(f"missing required CTest check: {name}")
    for name, script in (("version_generated", "generate_version.py"), ("registers_generated", "generate_ess_registers.py"),
                         ("repository_checks", "check_repository.py"), ("operations_inventory", "check_ess_operations.py")):
        registered = next((t for t in tests if t["name"] == name), None)
        if not registered or script not in {Path(arg.replace("\\", "/")).name for arg in registered.get("command", [])}:
            raise RuntimeError(f"missing required CTest check: {name}")
    return len(tests)


def check_test_log(path):
    content = path.read_text(encoding="utf-8", errors="replace")
    if re.search(r"\bSkipped\b|\bDISABLED\b|\bDisabled\b|\*\*\*Not\s*Run|OK \(skipped=|Ran 0 tests", content):
        raise RuntimeError(f"a required test was skipped or empty: {path}")


def export_core(root, destination):
    metadata = json.loads((root / "library.json").read_text(encoding="utf-8"))
    allowed = {"include/", "src/", "CMakeLists.txt", "library.json", "LICENSE", "README.md", "CHANGELOG.md"}
    entries = metadata["export"]["include"]
    if set(entries) != allowed or len(entries) != len(allowed):
        raise RuntimeError("core export must contain only the reviewed source-package inputs")
    destination.mkdir()
    for entry in entries:
        source = root / entry
        if source.is_dir():
            shutil.copytree(source, destination / entry)
        else:
            shutil.copy2(require_file(source), destination / entry)
    archive = destination.parent / f"MotorControl-RS-{metadata['version']}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo(path.relative_to(destination).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                output.writestr(info, path.read_bytes())
    return metadata["version"], archive


class Verification:
    def __init__(self, root, output):
        self.root, self.output = root, output
        self.results = []

    def run(self, name, command, cwd=None):
        print(f"[{len(self.results)+1}] {name}", flush=True)
        log = self.output / f"{len(self.results)+1:02d}-{name}.log"
        start = time.monotonic()
        with log.open("w", encoding="utf-8") as stream:
            stream.write(json.dumps([str(v) for v in command]) + "\n")
            stream.flush()
            result = subprocess.run([str(v) for v in command], cwd=cwd or self.root,
                                    stdout=stream, stderr=subprocess.STDOUT)
        self.results.append({"check": name, "exit": result.returncode,
                             "seconds": round(time.monotonic()-start, 3), "log": log.name})
        if result.returncode:
            tail = log.read_text(encoding="utf-8", errors="replace")[-5000:]
            raise RuntimeError(f"{name} failed ({result.returncode}); {log}\n{tail}")

    def cmake(self, name, source, build, *options):
        self.run(name+"-configure", ["cmake", "-S", source, "-B", build, "-G", "Ninja",
                                    "-DCMAKE_BUILD_TYPE=Release", *options])
        self.run(name+"-build", ["cmake", "--build", build, "--parallel", "4"])

    def tests(self, name, build):
        self.run(name, ["ctest", "--test-dir", build, "--output-on-failure", "--no-tests=error"])
        check_test_log(self.output / self.results[-1]["log"])
        check_test_log(build / "Testing/Temporary/LastTest.log")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("quick", "full"), default="quick")
    parser.add_argument("--firmware", choices=("all", "arduino", "idf"), default="all",
                        help="explicit full-mode firmware selection; quick never builds firmware")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--idf-path", type=Path, default=os.environ.get("IDF_PATH"))
    parser.add_argument("--idf-python", default=sys.executable)
    args = parser.parse_args(argv)
    output = (args.build_dir or ROOT / "build/verify" / datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")).resolve()
    verification = None
    summary = {"mode": args.mode, "firmware": args.firmware if args.mode == "full" else "none",
               "hardware": "NOT RUN (build and host tests only)", "python": sys.version}
    try:
        if sys.version_info < (3, 10) or sys.flags.optimize or os.environ.get("PYTHONOPTIMIZE"):
            raise RuntimeError("Python 3.10+ without PYTHONOPTIMIZE is required (test assertions must run)")
        for name in ("cmake", "ctest", "ninja"):
            tool(name)
        ctest_version = subprocess.check_output(["ctest", "--version"], text=True).splitlines()[0]
        parsed_version = re.search(r"([0-9]+)\.([0-9]+)", ctest_version)
        if not parsed_version or tuple(map(int, parsed_version.groups())) < (3, 26):
            raise RuntimeError("CTest 3.26+ is required for fail-on-zero-tests behavior")
        summary["ctest"] = ctest_version
        for name in ("generate_version.py", "generate_ess_registers.py", "check_ess_operations.py",
                     "check_repository.py", "prepare_serial_contrasts.py"):
            require_file(ROOT / "scripts" / name)
        for name in ("units_test.cpp", "repository_check_test.py", "verification_test.py", "generator_test.py"):
            require_file(ROOT / "test" / name)
        if output.exists():
            raise RuntimeError(f"use a new empty build directory: {output}")
        output.mkdir(parents=True)
        verification = Verification(ROOT, output)
        verification.run("offline-contrasts", [sys.executable, ROOT / "scripts/prepare_serial_contrasts.py", "--check"])
        native = output / "native"
        verification.run("native-configure", ["cmake", "-S", ROOT, "-B", native, "-G", "Ninja",
                         "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON", f"-DPython3_EXECUTABLE={sys.executable}"])
        verification.run("native-build", ["cmake", "--build", native, "--parallel", "4"])
        listing = subprocess.check_output(["ctest", "--test-dir", str(native), "--show-only=json-v1"], text=True)
        summary["registered_tests"] = check_inventory(ROOT, json.loads(listing))
        verification.tests("native-tests", native)
        core = output / "MotorControlRS"
        version, archive = export_core(ROOT, core)
        summary["package"] = {"version": version, "file": archive.name,
                              "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
        install = output / "install"
        package_build = output / "package-build"
        verification.cmake("package", core, package_build, "-DBUILD_TESTING=OFF",
                           "-DCMAKE_INSTALL_INCLUDEDIR=sdk/include", f"-DCMAKE_INSTALL_PREFIX={install.as_posix()}")
        verification.run("package-install", ["cmake", "--install", package_build])
        fixture = output / "consumer"
        shutil.copytree(ROOT / "test/installed_consumer", fixture)
        for mode, headers, options in (
                ("source", core / "include", [f"-DMOTORCONTROLRS_SOURCE_DIR={core.as_posix()}"]),
                ("installed", install / "sdk/include", [f"-DCMAKE_PREFIX_PATH={install.as_posix()}"])):
            build = output / (mode+"-consumer")
            verification.cmake(mode, fixture, build, f"-DMOTORCONTROLRS_HEADERS_DIR={headers.as_posix()}",
                               f"-DMOTORCONTROLRS_EXPECTED_VERSION={version}", *options)
            verification.tests(mode+"-tests", build)
            binary = build / ("codec_consumer.exe" if os.name == "nt" else "codec_consumer")
            if b"DRIVER_MODEL" in binary.read_bytes() or b"A table default/model code is not proof" in binary.read_bytes():
                raise RuntimeError("codec-only consumer pulled descriptive catalogue strings")
            cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
            nm = re.search(r"^CMAKE_NM:FILEPATH=(.+)$", cache, re.M)
            if not nm:
                raise RuntimeError("compiler symbol inspector CMAKE_NM is required")
            verification.run(mode+"-codec-symbols", [nm.group(1).strip(), "-C", binary])
            symbols = (output / verification.results[-1]["log"]).read_text(encoding="utf-8", errors="replace")
            if re.search(r"ESS_RS::(registerCount|registerAt|findRegister)|ESS_RS::.*CATALOGUE", symbols):
                raise RuntimeError("codec-only consumer linked catalogue symbols")
        if args.mode == "full" and args.firmware in ("all", "arduino"):
            pio = shutil.which("pio") or shutil.which("platformio")
            if not pio and os.name == "nt":
                pio = str(require_file(ROOT / "scripts/pio.cmd"))
            if not pio:
                raise RuntimeError("missing required tool: PlatformIO")
            verification.run("arduino-builds", [pio, "run", "-e", "bench_s3_units", "-e", "bench_s3_probe",
                                                 "-e", "bench_s3_load_poll", "-e", "bench_s3_load_timer"])
        if args.mode == "full" and args.firmware in ("all", "idf"):
            if not args.idf_path:
                raise RuntimeError("full IDF verification requires IDF_PATH or --idf-path")
            idf = require_file(args.idf_path.resolve() / "tools/idf.py")
            os.environ["IDF_PATH"] = str(args.idf_path.resolve())
            for name, project, target in (("idf-app", "examples/probe_idf", "esp32s3"),
                    ("idf-core-s3", "test/idf_consumer", "esp32s3"),
                    ("idf-core-s2", "test/idf_consumer", "esp32s2"),
                    ("idf-portable-s2", "test/portability_consumer", "esp32s2")):
                build = output / name
                options = [] if name == "idf-app" else [f"-DMOTORCONTROLRS_COMPONENT_PATH={core.as_posix()}"]
                verification.run(name, [args.idf_python, idf, "-C", ROOT / project, "-B", build,
                    f"-DSDKCONFIG={(build / 'sdkconfig').as_posix()}", f"-DIDF_TARGET={target}", *options, "build"])
        summary["status"] = "PASS"
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError, KeyError) as error:
        summary["status"], summary["error"] = "FAIL", str(error)
        print(str(error), file=sys.stderr)
    if verification:
        summary["checks"] = verification.results
        (output / "summary.json").write_text(json.dumps(summary, indent=2)+"\n", encoding="utf-8")
    print(f"{summary['status']}: {output}")
    return 0 if summary["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())

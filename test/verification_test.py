"""Failure gates must reject absent/empty checks rather than report PASS."""
import importlib.util
import copy
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

spec = importlib.util.spec_from_file_location("verify", Path(__file__).resolve().parents[1] / "scripts/verify.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class VerificationFailures(unittest.TestCase):
    def inventory(self, root):
        (root / "test").mkdir()
        (root / "test/foo_test.py").touch()
        (root / "test/foo_test.cpp").touch()
        build = root / "build"
        suffix = ".exe" if verify.os.name == "nt" else ""
        tests = [{"name": name, "command": [str(build / (binary + suffix))]} for name, binary in
                 [("foo", "foo_test"), ("units_preview", "units_preview"),
                  *[(n, n + "_test") for n in ("codec_independent", "probe_app_load", "idf_move_app", "idf_control_app")]]]
        tests.append({"name": "foo_python", "command": [sys.executable, str(root / "test/foo_test.py")]})
        for name, script, args in (("version_generated", "generate_version.py", ["check"]),
                ("registers_generated", "generate_ess_registers.py", ["--check"]),
                ("repository_checks", "check_repository.py", []), ("operations_inventory", "check_ess_operations.py", [])):
            tests.append({"name": name, "command": [sys.executable, str(root / "scripts" / script), *args]})
        return {"tests": tests}, build

    def test_missing_input(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "missing required"):
                verify.require_file(Path(directory) / "generator.py")

    def test_unregistered_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            listing, build = self.inventory(root)
            (root / "test/extra_test.py").touch()
            with self.assertRaisesRegex(RuntimeError, "unregistered Python"):
                verify.check_inventory(root, listing, build)

    def test_no_tests(self):
        with self.assertRaisesRegex(RuntimeError, "no tests"):
            verify.check_inventory(Path("."), {"tests": []}, Path("build"))

    def test_similar_name_does_not_register_test(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            listing, build = self.inventory(root)
            listing["tests"][6]["command"][1] = str(root / "test/prefix_foo_test.py")
            with self.assertRaisesRegex(RuntimeError, "unregistered Python"):
                verify.check_inventory(root, listing, build)

    def test_required_command_identity_and_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            listing, build = self.inventory(root)
            self.assertEqual(verify.check_inventory(root, listing, build), len(listing["tests"]))
            for name, command in (("foo", [str(build / "other_test")]),
                    ("foo_python", ["echo", str(root / "test/foo_test.py")]),
                    ("foo_python", [sys.executable, "-O", str(root / "test/foo_test.py")]),
                    ("foo_python", [sys.executable, str(root / "test/foo_test.py"), "-k", "one_case"]),
                    ("version_generated", [sys.executable, str(root / "scripts/generate_version.py")]),
                    ("registers_generated", [sys.executable, str(root / "scripts/generate_ess_registers.py")]),
                    ("repository_checks", [sys.executable, str(root / "scripts/check_repository.py"), "--section", "metadata"]),
                    ("version_generated", [sys.executable, str(root / "elsewhere/generate_version.py"), "check"])):
                changed = copy.deepcopy(listing)
                next(t for t in changed["tests"] if t["name"] == name)["command"] = command
                with self.subTest(name=name, command=command), self.assertRaises(RuntimeError):
                    verify.check_inventory(root, changed, build)
            listing["tests"].append(copy.deepcopy(listing["tests"][0]))
            with self.assertRaisesRegex(RuntimeError, "duplicate"):
                verify.check_inventory(root, listing, build)

    def test_per_test_optimization_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            listing, build = self.inventory(root)
            test = next(t for t in listing["tests"] if t["name"] == "foo_python")
            for name, value in (("ENVIRONMENT", ["PYTHONOPTIMIZE=1"]),
                                ("ENVIRONMENT_MODIFICATION", ["PYTHONOPTIMIZE=set:1"]),
                                ("ENVIRONMENT", ["pythonoptimize=1"]),
                                ("ENVIRONMENT_MODIFICATION", ["pythonoptimize=set:1"])):
                test["properties"] = [{"name": name, "value": value}]
                with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "PYTHONOPTIMIZE"):
                    verify.check_inventory(root, listing, build)

    def test_exit_failure_cannot_be_relabelled_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            listing, build = self.inventory(root)
            for name, value in (("WILL_FAIL", True), ("PASS_REGULAR_EXPRESSION", [".*"])):
                listing["tests"][0]["properties"] = [{"name": name, "value": value}]
                with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "exit-code failure"):
                    verify.check_inventory(root, listing, build)

    def test_archive_bytes_and_members_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / "stage"
            stage.mkdir()
            (stage / "private.h").write_bytes(b"required private helper")
            archive, extracted = root / "core.zip", root / "extracted"
            for entries in ([], [("private.h", b"different")], [("../escaped", b"bad")],
                            [("private.h", b"required private helper"), ("extra", b"unexpected")]):
                with zipfile.ZipFile(archive, "w") as package:
                    for name, content in entries:
                        package.writestr(name, content)
                with self.subTest(entries=entries), self.assertRaises(RuntimeError):
                    verify.unpack_core(archive, stage, extracted)
                self.assertFalse(extracted.exists())
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("private.h", b"required private helper")
            verify.unpack_core(archive, stage, extracted)
            self.assertEqual((extracted / "private.h").read_bytes(), (stage / "private.h").read_bytes())

    def test_entrypoint_missing_check(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(verify, "ROOT", root), mock.patch.object(verify, "tool", return_value="available"):
                self.assertEqual(verify.main(["--build-dir", str(root / "output")]), 1)
            self.assertFalse((root / "output").exists())

    def test_skips_and_zero_work(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "LastTest.log"
            for text in ("OK (skipped=1)", "Ran 0 tests", "***Not Run", "***NotRun", "Skipped", "***Not Run (Disabled)", "DISABLED"):
                log.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "skipped or empty"):
                    verify.check_test_log(log)
            log.write_text("Ran 3 tests\nOK", encoding="utf-8")
            verify.check_test_log(log)


if __name__ == "__main__":
    unittest.main()

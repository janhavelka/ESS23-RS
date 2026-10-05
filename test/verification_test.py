"""Failure gates must reject absent/empty checks rather than report PASS."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("verify", Path(__file__).resolve().parents[1] / "scripts/verify.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class VerificationFailures(unittest.TestCase):
    def test_missing_input(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "missing required"):
                verify.require_file(Path(directory) / "generator.py")

    def test_unregistered_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test").mkdir()
            (root / "test/extra_test.py").touch()
            tests = [{"name": n, "command": ["python", "other.py"]} for n in
                     ("version_generated", "registers_generated", "repository_checks", "operations_inventory")]
            with self.assertRaisesRegex(RuntimeError, "unregistered Python"):
                verify.check_inventory(root, {"tests": tests})

    def test_no_tests(self):
        with self.assertRaisesRegex(RuntimeError, "no tests"):
            verify.check_inventory(Path("."), {"tests": []})

    def test_similar_name_does_not_register_test(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test").mkdir()
            (root / "test/foo_test.py").touch()
            with self.assertRaisesRegex(RuntimeError, "unregistered Python"):
                verify.check_inventory(root, {"tests": [{"name": "other", "command": ["python", "prefix_foo_test.py"]}]})

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

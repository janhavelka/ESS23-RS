"""Offline checker failures and Markdown edge cases in disposable repositories."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("repository_check", ROOT / "scripts/check_repository.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class RepositoryChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name) / "repo"
        self.root.mkdir()

    def write(self, relative, contents):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(contents, bytes):
            path.write_bytes(contents)
        else:
            path.write_text(contents, encoding="utf-8")
        return path

    def snapshots(self):
        manifests = {}
        for name, paths in checker.REFERENCE_PATHS.items():
            records = []
            for relative in sorted(paths):
                extension = Path(relative).suffix.lower()
                raw = {".pdf": b"%PDF-1.4\nfixture", ".zip": b"PK\x03\x04fixture", ".step": b"ISO-10303-21;\nfixture"}[extension]
                self.write("docs/" + relative, raw)
                record = {"path": relative, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
                if extension == ".pdf":
                    record["pages"] = 1
                records.append(record)
            manifests[name] = {"references": records}
            self.write("docs/reference/" + name, json.dumps(manifests[name]))
        return manifests

    def test_all_fourteen_original_snapshots(self):
        self.snapshots()
        self.assertEqual(checker.check_references(self.root), [])

    def test_checksum_mismatch_at_unchanged_size(self):
        self.snapshots()
        relative = sorted(checker.REFERENCE_PATHS["sources.json"])[0]
        path = self.root / "docs" / relative
        path.write_bytes(path.read_bytes()[:-1] + b"X")
        findings = checker.check_references(self.root)
        self.assertTrue(any("SHA-256 mismatch" in item for item in findings), findings)
        self.assertFalse(any("byte count mismatch" in item for item in findings), findings)

    def test_missing_reference_and_incomplete_manifest(self):
        manifests = self.snapshots()
        record = manifests["sources.json"]["references"].pop()
        (self.root / "docs" / record["path"]).unlink()
        self.assertTrue(any("missing/unreadable reference" in item for item in checker.check_references(self.root)))
        self.write("docs/reference/sources.json", json.dumps(manifests["sources.json"]))
        self.assertTrue(any("exactly the 9" in item for item in checker.check_references(self.root)))

    def test_manifest_cannot_escape_docs(self):
        manifests = self.snapshots()
        manifests["sources.json"]["references"][0]["path"] = "../../outside.pdf"
        self.write("docs/reference/sources.json", json.dumps(manifests["sources.json"]))
        findings = checker.check_references(self.root)
        self.assertTrue(any("unsafe reference path" in item for item in findings), findings)

    def test_wrong_signature_rejected_even_with_matching_hash(self):
        manifests = self.snapshots()
        record = next(r for r in manifests["sources.json"]["references"] if r["path"].endswith(".pdf"))
        raw = b"<html>not a PDF</html>"
        self.write("docs/" + record["path"], raw)
        record.update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        self.write("docs/reference/sources.json", json.dumps(manifests["sources.json"]))
        findings = checker.check_references(self.root)
        self.assertTrue(any("invalid .pdf signature" in item for item in findings), findings)

    def test_broken_internal_link_and_allowed_missing_sibling(self):
        self.write("docs/guide.md", "[reference](../../FieldCore-node/missing.h)\n[other](../../Unreviewed/missing.h)\n[broken](missing.md)\n")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 2, findings)
        self.assertTrue(any("escapes repository" in item for item in findings))
        self.assertTrue(any("missing local link" in item for item in findings))

    def test_balanced_encoded_parentheses_directories_and_code(self):
        self.write("docs/vendor/Manual (v1).pdf", b"fixture")
        self.write("docs/guide.md", """[manual](vendor/Manual%20(v1).pdf#page=2)
[with title](<vendor/Manual (v1).pdf> "Manual")
[dir](vendor/?view=1#top)
[web](https://example.invalid/missing)
[anchor](#not-validated)
`[sample](missing.md)`
````text
[sample](missing.md)
```
[still code](missing.md)
````
    [indented code](missing.md)
<!-- [comment](missing.md) -->
""")
        self.assertEqual(checker.check_docs(self.root), [])

    def test_reference_links_check_used_definitions(self):
        self.write("docs/present.md", "present")
        self.write("docs/guide.md", """[visible][ok]
[bad][]
[shortcut]
[ok]: present.md
[bad]: absent.md
[shortcut]: missing.md
[unused]: ignored.md
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 2, findings)
        self.assertTrue(any("absent.md" in item for item in findings))
        self.assertTrue(any("missing.md" in item for item in findings))

    def test_historical_and_generated_sources_excluded_targets_still_checked(self):
        self.write("docs/reports/old.md", "[historical](missing.md)")
        self.write("docs/pdf-extracted-md/raw.md", "[raw](missing.md)")
        self.write("examples/build/generated.md", "[generated](missing.md)")
        self.write("README.md", "[report](docs/reports/old.md)\n[missing report](docs/reports/absent.md)")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 1, findings)
        self.assertIn("absent.md", findings[0])

    def metadata(self):
        self.write("src/Core.cpp", '#include "MotorControlRS/Core.h"\n')
        self.write("include/MotorControlRS/Core.h", "#pragma once\n")
        self.write("LICENSE", "license")
        self.write("CMakeLists.txt", 'project(MotorControlRS LANGUAGES CXX)\nset(MOTORCONTROLRS_SOURCES "src/Core.cpp")\n')
        data = {"name": "MotorControl-RS", "repository": {"url": "https://github.com/janhavelka/MotorControl-RS.git"},
                "build": {"includeDir": "include", "srcDir": "src"},
                "export": {"include": ["include/", "src/", "CMakeLists.txt", "LICENSE"]}}
        self.write("library.json", json.dumps(data))
        return data

    def test_metadata_identity_registration_and_platform_boundary(self):
        data = self.metadata()
        self.assertEqual(checker.check_metadata(self.root), [])
        data["repository"]["url"] = "https://github.com/janhavelka/ESS23-RS.git"
        self.write("library.json", json.dumps(data))
        self.write("src/Unregistered.cpp", "#include <freertos/FreeRTOS.h>\n")
        findings = checker.check_metadata(self.root)
        self.assertEqual(len(findings), 3, findings)
        self.assertTrue(any("canonical" in item for item in findings))
        self.assertTrue(any("every core" in item for item in findings))
        self.assertTrue(any("platform/application" in item for item in findings))

    def test_export_does_not_admit_vendor_or_python(self):
        data = self.metadata()
        data["export"]["include"].append("docs/vendor/")
        self.write("library.json", json.dumps(data))
        self.assertTrue(any("exclude firmware" in item for item in checker.check_metadata(self.root)))

    def test_cli_fails_on_missing_required_manifest(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(checker.main(["--root", str(self.root), "--section", "references"]), 1)
        self.snapshots()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(checker.main(["--root", str(self.root), "--section", "references"]), 0)


if __name__ == "__main__":
    unittest.main()

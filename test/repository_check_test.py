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

    def test_nested_and_continued_list_links_are_prose(self):
        self.write("docs/guide.md", """- outer
    - [nested](nested-missing.md)

      [continued](continued-missing.md)
- sibling

    [paragraph](paragraph-missing.md)
1. ordered
    [ordered paragraph](ordered-missing.md)
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 4, findings)
        for name in ("nested", "continued", "paragraph", "ordered"):
            self.assertTrue(any(name + "-missing.md" in item for item in findings), findings)

    def test_list_indented_and_fenced_code_stays_excluded(self):
        self.write("docs/guide.md", """- item

      [indented sample](missing.md)
    ```text
    [fenced sample](missing.md)
    ```
    [real link](real-missing.md)

Outside the list.

    [top-level code](missing.md)
-     [item starts with code](missing.md)
- ```text
  [unterminated item fence](missing.md)

Outside [real link](outside-missing.md).
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 2, findings)
        self.assertIn("real-missing.md", findings[0])
        self.assertIn("outside-missing.md", findings[1])

    def test_multiline_inline_and_reference_labels(self):
        self.write("docs/guide.md", """[inline
label](inline-missing.md)
[reference
label][definition label]
[shortcut
label]
[definition
label]: reference-missing.md
[shortcut label]: shortcut-missing.md
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 3, findings)
        for name in ("inline", "reference", "shortcut"):
            self.assertTrue(any(name + "-missing.md" in item for item in findings), findings)

    def test_fence_closure_requires_only_trailing_whitespace(self):
        self.write("docs/guide.md", """```text
```not-a-close
[code](missing.md)
````\x20\x20
[prose](prose-missing.md)
~~~text
~~~not-a-close
[code](missing.md)
~~~~
[prose](other-missing.md)
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 2, findings)
        for name in ("prose", "other"):
            self.assertTrue(any(name + "-missing.md" in item for item in findings), findings)

    def test_lazy_list_and_adjacent_indentation_stay_prose(self):
        self.write("docs/guide.md", """- list paragraph
lazy continuation
    [four-space prose](four-missing.md)
      [six-space prose](six-missing.md)

      [actual list code](missing.md)

Outside paragraph.
    [adjacent indentation](outside-missing.md)

    [actual document code](missing.md)
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 3, findings)
        for name in ("four", "six", "outside"):
            self.assertTrue(any(name + "-missing.md" in item for item in findings), findings)

    def test_nested_and_escaped_label_brackets(self):
        self.write("docs/guide.md", r"""[text [nested]](nested-missing.md)
[text \[escaped\]](escaped-missing.md)
[outer [inner](inner-missing.md)]
\[literal](missing.md)
""")
        findings = checker.check_docs(self.root)
        self.assertEqual(len(findings), 3, findings)
        for name in ("nested", "escaped", "inner"):
            self.assertTrue(any(name + "-missing.md" in item for item in findings), findings)

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

    def test_common_headers_do_not_import_profile_types(self):
        self.metadata()
        # Common implementation may dispatch to a real profile; profile headers
        # may reuse one another. Their types must not leak into common headers.
        self.write("src/Core.cpp", '#include "MotorControlRS/profiles/ess_rs/Reads.h"\n')
        self.write("include/MotorControlRS/profiles/ess_rs/Reads.h", '#include "Codec.h"\n')
        self.assertEqual(checker.check_metadata(self.root), [])
        for include in ('MotorControlRS/profiles/ess_rs/Reads.h', 'profiles/ess_rs/Reads.h'):
            with self.subTest(include=include):
                self.write("include/MotorControlRS/Core.h", '#include "' + include + '"\n')
                findings = checker.check_metadata(self.root)
                self.assertEqual(len(findings), 1, findings)
                self.assertIn("common public header includes manufacturer profile", findings[0])

    def test_cli_fails_on_missing_required_manifest(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(checker.main(["--root", str(self.root), "--section", "references"]), 1)
        self.snapshots()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(checker.main(["--root", str(self.root), "--section", "references"]), 0)


if __name__ == "__main__":
    unittest.main()

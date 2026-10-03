# Reference preparation

`prepare_references.py` downloads the nine references listed in the source
inventory, validates their formats, extracts searchable PDF text, and writes
`docs/reference/sources.json` with SHA-256 checksums.

Requirements: Python 3.10+, `requests`, and `PyMuPDF` (all available during initial
preparation). Run from the project root:

```powershell
python scripts/prepare_references.py
```

The downloader uses official regional storefronts when the main vendor host
rejects file downloads. Existing files with changed content are not replaced;
review such changes explicitly. This helper is documentation tooling only.

`prepare_serial_contrasts.py` preserves the five additional manufacturer
manuals. Use `python scripts/prepare_serial_contrasts.py --check` to verify
their recorded hashes offline without downloading or replacing anything.

`generate_ess_registers.py` generates public descriptors, a compact private
codec access table and the readable catalogue from the ESS JSON ledger. It
checks complete coverage of documented words and declared gaps. Run it after
editing that ledger; `--check` verifies the generated files without changing them.

`generate_version.py sync` updates the version header from `library.json`;
`generate_version.py check` verifies it. These two generators and the native
generator tests use only the Python standard library.

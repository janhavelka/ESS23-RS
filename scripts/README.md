# Reference preparation

`prepare_references.py` downloads the nine references listed in the source
inventory, validates their formats, extracts searchable PDF text, and writes
`docs/reference/sources.json` with SHA-256 checksums.

Requirements: Python 3, `requests`, and `PyMuPDF` (all available during initial
preparation). Run from the project root:

```powershell
python scripts/prepare_references.py
```

The downloader uses official regional storefronts when the main vendor host
rejects file downloads. Existing files with changed content are not replaced;
review such changes explicitly. This helper is documentation tooling only.

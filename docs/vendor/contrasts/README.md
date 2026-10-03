# Contrasting serial drive manuals

These are original manufacturer PDFs collected on 2026-10-03 to review the
ESS codec boundary against other serial motor protocols. They do not establish
implemented or tested support for these products.

See the [protocol review](../../reference/08_serial_protocol_review.md) for
findings and page references, and the
[source manifest](../../reference/serial_contrasts_sources.json) for original
URLs, retrieval dates, SHA-256 hashes, byte sizes and page counts.

Run `python scripts/prepare_serial_contrasts.py --check` from the repository
root to verify the downloaded snapshots without network access. Running the
script without `--check` fetches the listed manufacturer URLs and refuses to
replace an existing file with different bytes. `--extract` writes disposable
search text under `build/reference_extracts/`; inspect original PDF pages when
tables or examples are ambiguous.

Manufacturer documents retain their owners' rights. They are reference
material and are not covered by the library's MIT license.

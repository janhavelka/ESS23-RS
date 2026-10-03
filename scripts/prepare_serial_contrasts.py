"""Download the reviewed serial-drive manuals without replacing snapshots.

Run with --check for an offline SHA-256/PDF check. Add --extract to write
page-indexed search text under build/reference_extracts (not authoritative).
Requires requests and PyMuPDF, like prepare_references.py.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

import fitz
import requests


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
MANIFEST = DOCS / "reference" / "serial_contrasts_sources.json"
SOURCES = [
    (
        "Leadshine_iEM-RS_User_Manual_V2.0.pdf",
        "https://www.leadshine.com/upfiles/downloads/3b01ef83987a2abfffcb4d4301a9cf4b_1689587677198.pdf",
        "Leadshine", "iEM-RS", "V2.0, 2022-10-20",
    ),
    (
        "Oriental_Motor_AZ_HM-60262E.pdf",
        "https://www.orientalmotor.com/products/pdfs/opmanuals/HM-60262E.pdf",
        "Oriental Motor", "AZ series, Modbus RTU sections", "HM-60262E",
    ),
    (
        "Nanotec_PD4E_ModbusRTU_V1.6.0.pdf",
        "https://www.nanotec.com/fileadmin/files/Handbuecher/Plug_Drive/PD4-E/fir-v2213/PD4E_ModbusRTU_Technical-Manual_V1.6.0.pdf",
        "Nanotec", "PD4-E Modbus RTU", "V1.6.0, FIR-v2213",
    ),
    (
        "Applied_Motion_Host_Command_Reference_920-0002W.pdf",
        "https://applied-motion.s3.amazonaws.com/documents/Manuals/Host-Command-Reference_920-0002W.pdf",
        "Applied Motion Products", "SCL / Q command families; model compatibility must be checked", "920-0002 Rev. W",
    ),
    (
        "Makerbase_SERVO42_57D_Modbus_V1.0.9.pdf",
        "https://raw.githubusercontent.com/makerbase-motor/MKS-SERVO42D-57D/master/User%20Manual/V1.0.9/MKS%20SERVO42%2657D_Modbus%20RTU%20User%20Manual%20V1.0.9.pdf",
        "Makerbase", "MKS SERVO42D / 57D", "V1.0.9",
    ),
]


def inspect_pdf(path, data):
    if not data.startswith(b"%PDF-"):
        raise ValueError(f"Not a PDF: {path}")
    with fitz.open(stream=data, filetype="pdf") as pdf:
        pages = len(pdf)
    return {
        "bytes": len(data), "pages": pages,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def download(source):
    name, url, manufacturer, family, revision = source
    relative = "vendor/contrasts/" + name
    path = DOCS / relative
    response = requests.get(url, headers={"User-Agent": "Mozilla/5.0"}, timeout=(15, 120))
    response.raise_for_status()
    data = response.content
    details = inspect_pdf(path, data)
    if path.exists() and path.read_bytes() != data:
        raise ValueError(f"Existing reference differs; review before replacing: {relative}")
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_bytes(data)
    print(f"Verified {relative}: {details['bytes']} bytes", flush=True)
    return {
        "path": relative, "url": url, "resolved_url": response.url,
        "retrieved_at_utc": datetime.now(timezone.utc).isoformat(),
        "manufacturer": manufacturer, "family": family, "revision": revision,
        **details,
    }


def verify(record):
    path = DOCS / record["path"]
    actual = inspect_pdf(path, path.read_bytes())
    for key, value in actual.items():
        if record[key] != value:
            raise ValueError(f"Snapshot {key} mismatch: {path}")


def extract(record):
    destination = ROOT / "build" / "reference_extracts"
    destination.mkdir(parents=True, exist_ok=True)
    path = DOCS / record["path"]
    text = [f"# {path.name}\n\nPhysical PDF page numbers start at 1.\n"
            "Extracted text may misorder tables; inspect the PDF before implementation.\n"]
    with fitz.open(path) as pdf:
        for number, page in enumerate(pdf, 1):
            text.append(f"\n## PDF page {number}\n\n{page.get_text(sort=True)}")
    (destination / (path.stem + ".txt")).write_text("\n".join(text), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify local snapshots without network access")
    parser.add_argument("--extract", action="store_true", help="Write disposable searchable PDF text under build/")
    args = parser.parse_args()
    previous = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.exists() else {}
    if args.check:
        records = previous.get("references", [])
        expected = {"vendor/contrasts/" + source[0] for source in SOURCES}
        if {record["path"] for record in records} != expected or len(records) != len(SOURCES):
            raise ValueError("Manifest must contain exactly the configured references")
    else:
        with ThreadPoolExecutor(max_workers=4) as pool:
            records = list(pool.map(download, SOURCES))
        old = {record["path"]: record for record in previous.get("references", [])}
        # Retain the original retrieval date and any page-review annotations.
        records = [old.get(record["path"], record) for record in records]
        for record in records:
            verify(record)
        MANIFEST.write_text(json.dumps({
            "purpose": "Contrasting serial-drive evidence; no support or hardware qualification implied",
            "references": records,
        }, indent=2) + "\n", encoding="utf-8", newline="\n")
    for record in records:
        verify(record)
        if args.extract:
            extract(record)
    print(f"Complete: {len(records)} verified references")


if __name__ == "__main__":
    main()

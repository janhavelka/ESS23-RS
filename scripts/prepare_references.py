"""Download vendor references and make page-indexed searchable PDF extracts.

Run from any directory: python scripts/prepare_references.py
Requires requests and PyMuPDF. Downloads are reference data; software is not run.
"""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from urllib.parse import quote

import fitz
import requests

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
BASE = "https://www.omc-stepperonline.com/index.php?route=product/product/get_file&file="
SOURCES = [
    ("vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf", BASE + "5476/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf", "Motor functions and ESS-RS register appendix"),
    ("vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf", BASE + "5475/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf", "Shared ESS23-RS10/RS20 wiring and electrical requirements"),
    ("vendor/ESS23-RS20_Full_Datasheet.pdf", BASE + "5476/ESS23-RS20_Full_Datasheet.pdf", "ESS23-RS20 electrical and mechanical specifications"),
    ("vendor/ESS23-RS10_Full_Datasheet.pdf", BASE + "5475/ESS23-RS10_Full_Datasheet.pdf", "ESS23-RS10 electrical and mechanical specifications"),
    ("vendor/cad/ESS23-RS20.STEP", BASE + "5476/ESS23-RS20.STEP", "Optional RS20 mechanical reference"),
    ("vendor/cad/ESS23-RS10.STEP", BASE + "5475/ESS23-RS10.STEP", "Optional RS10 mechanical reference"),
    ("vendor/software/Y_Series_Stepper_Driver_Debug_Software_V1.2.7.zip", "https://raw.githubusercontent.com/StepperOnline/Tunning-Software/main/Y_Series_Stepper_Driver_Debug_Software_V1.2.7.zip", "Tuning software linked from both official motor product pages"),
    ("standards/Modbus_Application_Protocol_V1.1b3.pdf", "https://www.modbus.org/file/secure/modbusprotocolspecification.pdf", "Supplemental Modbus function, frame and exception definitions"),
    ("standards/Modbus_Serial_Line_V1.02.pdf", "https://www.modbus.org/file/secure/modbusoverserial.pdf", "Supplemental Modbus RTU framing, CRC and serial-line requirements"),
]


def download(source):
    relative, url, purpose = source
    path = DOCS / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    retrieved = datetime.now(timezone.utc).isoformat()
    headers = {"User-Agent": "Mozilla/5.0", "Referer": "https://www.omc-stepperonline.com/"}
    if "omc-stepperonline.com/index.php" in url:
        file = url.split("&file=", 1)[1]
        url = "https://www.omc-stepperonline.com/index.php?file=" + quote(file, safe="") + "&route=product%2Fproduct%2Fget_file"
    candidates = [url]
    if "omc-stepperonline.com/index.php" in url:
        candidates.extend([
            "https://www.omc-stepperonline.com/download/" + path.name,
            url.replace("www.omc-stepperonline.com", "www.stepperonline.co.uk"),
            url.replace("www.omc-stepperonline.com", "www.stepperonline.nl"),
        ])
    if "modbus.org/file/secure/" in url:
        candidates.append(url.replace("/file/secure/", "/docs/"))
    for candidate in candidates:
        response = requests.get(candidate, headers=headers, timeout=(15, 120))
        if response.ok:
            break
        print(f"HTTP {response.status_code}: {candidate}; {response.text[:100]!r}", flush=True)
    response.raise_for_status()
    data = response.content
    if path.suffix.lower() == ".pdf" and not data.startswith(b"%PDF-"):
        raise ValueError(f"Not a PDF: {url}")
    if path.suffix.lower() == ".zip" and not data.startswith(b"PK"):
        raise ValueError(f"Not a ZIP: {url}")
    if path.suffix.lower() == ".step" and b"ISO-10303-21" not in data[:200]:
        raise ValueError(f"Not a STEP model: {url}")
    if path.exists() and path.read_bytes() != data:
        raise ValueError(f"Existing reference differs; review before replacing: {relative}")
    path.write_bytes(data)
    result = {
        "path": relative, "url": url, "resolved_url": response.url,
        "retrieved_at_utc": retrieved, "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(), "purpose": purpose,
    }
    if path.suffix.lower() == ".pdf":
        extract_dir = DOCS / "pdf-extracted-md"
        extract_dir.mkdir(exist_ok=True)
        with fitz.open(path) as pdf:
            result["pages"] = len(pdf)
            contents = [f"# {path.name}\n\nSource: [{path.name}](../{relative})\n\n"
                        "Raw text extracted with PyMuPDF. Tables, figures and symbols may be\n"
                        "misordered or missing; verify against the original PDF. Page numbers\n"
                        "below are physical PDF pages, starting at 1.\n"]
            for number, page in enumerate(pdf, 1):
                contents.append(f"\n## PDF page {number}\n\n```text\n{page.get_text(sort=True).rstrip()}\n```\n")
        extract = extract_dir / (path.stem + ".md")
        extract.write_text("\n".join(contents), encoding="utf-8", newline="\n")
        result["text_extract"] = str(extract.relative_to(DOCS)).replace("\\", "/")
    print(f"Downloaded {relative}: {len(data)} bytes", flush=True)
    return result


def main():
    (DOCS / "reference").mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(download, SOURCES))
    manifest = {"collected_on": "2026-10-02", "references": results}
    (DOCS / "reference" / "sources.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(f"Complete: {len(results)} verified references", flush=True)


if __name__ == "__main__":
    main()

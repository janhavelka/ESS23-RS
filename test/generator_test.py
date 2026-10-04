"""Reject corrupt audited gap metadata before generating an access policy."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("catalogue", ROOT / "scripts/generate_ess_registers.py")
catalogue = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalogue)
SOURCE = json.loads(catalogue.LEDGER.read_text(encoding="utf-8"))


class GapValidation(unittest.TestCase):
    def rejected(self, change):
        data = copy.deepcopy(SOURCE)
        change(data["address_coverage"])
        with self.assertRaises(ValueError):
            catalogue.validate(data, catalogue.expand(data))

    def test_complete_span(self):
        occupied = catalogue.validate(SOURCE, catalogue.expand(SOURCE))
        self.assertEqual(len(occupied), 242)

    def test_missing_gap(self):
        self.rejected(lambda coverage: coverage["gaps"].pop())

    def test_overlapping_register(self):
        self.rejected(lambda coverage: coverage["gaps"][0].update(first="0x0000"))

    def test_duplicate_gap(self):
        self.rejected(lambda coverage: coverage["gaps"].append(coverage["gaps"][0]))

    def test_bad_gap_count(self):
        self.rejected(lambda coverage: coverage.update(gap_words=77))

    def test_gap_outside_map(self):
        self.rejected(lambda coverage: coverage["gaps"][-1].update(last="0xFFFF"))

    def test_missing_reserved_word(self):
        self.rejected(lambda coverage: coverage["explicit_reserved"].pop())

    def test_unspecified_access_is_distinct(self):
        self.rejected(lambda coverage: coverage.update(access_unspecified=[]))


class WriteWindowValidation(unittest.TestCase):
    def rejected(self, change):
        data = copy.deepcopy(SOURCE)
        change(data)
        with self.assertRaises(ValueError):
            catalogue.validate(data, catalogue.expand(data))

    def test_exact_source_windows(self):
        self.assertEqual({(w["start"], w["count"]) for w in SOURCE["write_multiple_windows"]},
                         {("0x0024", 2), ("0x0021", 5), ("0x001D", 3), ("0x0031", 6)})

    def test_hexadecimal_address_is_never_emitted_as_octal(self):
        data = copy.deepcopy(SOURCE)
        data["write_multiple_windows"][0]["start"] = "0024"
        rows = catalogue.expand(data)
        catalogue.validate(data, rows)
        self.assertEqual(catalogue.access_header(data, rows), catalogue.access_header(SOURCE, rows))

    def test_no_windows(self):
        self.rejected(lambda d: d.update(write_multiple_windows=[]))

    def test_duplicate_window(self):
        self.rejected(lambda d: d["write_multiple_windows"].append(d["write_multiple_windows"][0]))

    def test_nonwritable_reserved_and_gap_windows(self):
        for start, count in (("0x000A", 2), ("0x0024", 3), ("0x0060", 6), ("0xFFFF", 2)):
            with self.subTest(start=start, count=count):
                self.rejected(lambda d: d["write_multiple_windows"][0].update(start=start, count=count))

    def test_no_pair_splitting(self):
        for start, count in (("0x0024", 1), ("0x0025", 1), ("0x0021", 4), ("0x0031", 5)):
            with self.subTest(start=start, count=count):
                self.rejected(lambda d: d["write_multiple_windows"][0].update(start=start, count=count))

    def test_bounded_integer_count(self):
        for count in (0, -1, True, "2", 124):
            with self.subTest(count=count):
                self.rejected(lambda d: d["write_multiple_windows"][0].update(count=count))

    def test_source_and_atomicity_not_invented(self):
        for change in (dict(pages=[]), dict(pages=[80]), dict(evidence=""), dict(evidence=" "),
                       dict(evidence=8), dict(partial_application="ATOMIC")):
            with self.subTest(change=change):
                self.rejected(lambda d: d["write_multiple_windows"][0].update(change))

    def test_missing_pair_disposition(self):
        self.rejected(lambda d: next(r for r in d["registers"] if r["name"] == "HOMING_OFFSET").pop("write_constraint"))
        self.rejected(lambda d: next(g for g in d["indexed_groups"] if g["name"] == "POSITION_SEGMENT")["templates"][0].pop("write_constraint"))


if __name__ == "__main__":
    unittest.main()

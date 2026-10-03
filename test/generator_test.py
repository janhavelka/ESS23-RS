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


if __name__ == "__main__":
    unittest.main()

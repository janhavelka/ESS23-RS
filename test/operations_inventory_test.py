"""Validate coverage links and prevent source evidence from becoming API qualification."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("operations", ROOT / "scripts/check_ess_operations.py")
operations = importlib.util.module_from_spec(spec)
spec.loader.exec_module(operations)
INVENTORY = json.loads(operations.INVENTORY.read_text(encoding="utf-8"))
LEDGER = json.loads(operations.catalogue.LEDGER.read_text(encoding="utf-8"))


class Coverage(unittest.TestCase):
    def test_pair_write_dispositions_do_not_create_implementation_credit(self):
        records = {row["id"]: row for row in operations.check(INVENTORY, LEDGER)["records"]}
        pairs = [row for row in records.values() if "pair_write_policy" in row]
        self.assertEqual(len(pairs), 20)
        home = records["HOMING_OFFSET"]
        self.assertEqual(home["pair_write_policy"]["reviewed_windows"],
                         [dict(start="0x0031", count=6, pages=[20])])
        self.assertEqual(home["obligations"]["write"], "NOT_IMPLEMENTED")
        for row in pairs:
            policy = row["pair_write_policy"]
            self.assertTrue(policy["reason"])
            self.assertEqual(policy["partial_application"], "UNSPECIFIED")
            self.assertEqual(policy["split_fc06"], "UNAVAILABLE")
            if row["id"] not in ("POSITION_PULSES", "HOMING_OFFSET"):
                self.assertEqual(policy["reviewed_windows"], [])
                self.assertEqual(row["obligations"]["write"], "UNSUPPORTED" if row["id"] in
                                 ("POSITIVE_SOFT_LIMIT", "NEGATIVE_SOFT_LIMIT") else "NOT_IMPLEMENTED")

    def rejected(self, change):
        inventory = copy.deepcopy(INVENTORY)
        change(inventory)
        with self.assertRaises(ValueError):
            operations.check(inventory, LEDGER)

    def test_complete_independent_obligations(self):
        result = operations.check(INVENTORY, LEDGER)
        summary = result["summary"]
        self.assertEqual((summary["records"], summary["reserved"], summary["unresolved_access"], summary["named_choices"]),
                         (221, 16, 2, 135))
        self.assertEqual(sum(count for state, count in summary["read"].items() if state in operations.IMPLEMENTATION), 201)
        self.assertEqual(summary["write"]["NOT_IMPLEMENTED"], 175)
        self.assertEqual(summary["write"]["UNSUPPORTED"], 2)
        self.assertEqual(summary["action"]["IN_PROGRESS"], 2)
        linked = {record for group in INVENTORY["operations"] for record in group["records"]}
        self.assertEqual(len(linked), 38)
        self.assertEqual(len(result["records"]), len({record["id"] for record in result["records"]}))
        self.assertTrue(all(choice["default_disposition"] == operations.DEFAULT for choice in result["named_choices"]))

    def test_only_exact_action_and_setting_choices_implemented(self):
        result = operations.check(INVENTORY, LEDGER)
        implemented = {choice["id"] for choice in result["named_choices"]
                       if choice["disposition"]["implementation"] == "IMPLEMENTED"}
        self.assertEqual(implemented, {"AuxiliaryCommand.ENABLE", "AuxiliaryCommand.RELEASE",
            "AuxiliaryCommand.CLEAR_ALARM", "AuxiliaryCommand.CLEAR_POSITION", "MotionCommandBit.STOP",
            "MotionCommandBit.EMERGENCY_STOP", "MotionCommandBit.START_POSITION", "MotionCommandBit.ABSOLUTE_POSITION",
            "MotionCommandBit.START_SPEED", "DefaultDirection.NORMAL", "DefaultDirection.REVERSED",
            "WordOrder.HIGH_WORD_FIRST", "WordOrder.LOW_WORD_FIRST", "SoftLimitEnable.LIMITS_OFF",
            "SoftLimitEnable.AFTER_HOMING", "OverLimitStop.FREE_PARKING", "OverLimitStop.EMERGENCY_STOP",
            "PvTriggerMode.LEVEL", "PvTriggerMode.RISING_EDGE", "PositionMode.RELATIVE", "PositionMode.ABSOLUTE"})
        self.assertEqual(result["summary"]["choice_implementation"], {"NOT_IMPLEMENTED": 114, "IMPLEMENTED": 21})
        for record in result["records"]:
            if record["id"] in ("AUXILIARY_COMMAND", "MOTION_COMMAND"):
                self.assertEqual(record["obligations"]["write"], "IN_PROGRESS")
                self.assertEqual(record["obligations"]["action"], "IN_PROGRESS")

    def test_action_choices_must_match_ledger_record(self):
        def operation(value, name):
            return next(group for group in value["operations"] if group["id"] == name)
        for choices in ([], ["AuxiliaryCommand.MISSING"], ["MotionCommandBit.STOP"],
                        ["AuxiliaryCommand.ENABLE", "AuxiliaryCommand.ENABLE"]):
            self.rejected(lambda value: operation(value, "enable").update(choices=choices))
        self.rejected(lambda value: value["operations"][0].update(choices=["AuxiliaryCommand.ENABLE"]))
        self.rejected(lambda value: operation(value, "release").update(choices=["AuxiliaryCommand.ENABLE"]))
        self.rejected(lambda value: operation(value, "enable")["records"].append("MOTION_COMMAND"))

    def test_setting_choices_are_separate_from_reads_and_actions(self):
        def update(value):
            return next(group for group in value["operations"] if group["id"] == "driver_settings_update")
        for choices in (["AuxiliaryCommand.ENABLE"], ["DefaultDirection.NORMAL"] * 2, ["DefaultDirection.MISSING"]):
            self.rejected(lambda value: update(value).update(choices=choices))
        result = operations.check(INVENTORY, LEDGER)
        records = {row["id"]: row for row in result["records"]}
        self.assertEqual(records["DEFAULT_DIRECTION"]["obligations"]["action"], "NOT_APPLICABLE")
        self.assertEqual(records["DEFAULT_DIRECTION"]["obligations"]["write"], "IMPLEMENTED")

    def test_unresolved_and_reserved_never_disappear(self):
        records = {record["id"]: record for record in operations.check(INVENTORY, LEDGER)["records"]}
        self.assertEqual(records["COLLISION_THRESHOLD_003B"]["obligations"]["read"], "UNRESOLVED_ACCESS")
        reserved = [record for record in records.values() if record["source_certainty"] == "EXPLICIT_RESERVED"]
        self.assertEqual(len(reserved), 16)
        self.assertTrue(all(record["obligations"]["write"] == "ACCOUNTED_RESERVED" for record in reserved))
        self.assertEqual(records["SUBDIVISION"]["issues"], ["SCALE_UNRESOLVED"])
        self.assertEqual(records["DRIVER_MODEL"]["source_certainty"], "DOCUMENTED_NO_RECORDED_ISSUES")
        self.assertEqual(INVENTORY["model_availability"]["qualification"], "UNQUALIFIED")

    def test_unknown_record_rejected(self):
        self.rejected(lambda value: value["operations"][1]["records"].append("UNKNOWN_REGISTER"))

    def test_no_duplicate_address_source(self):
        self.rejected(lambda value: value["operations"][1].update(address=0))
        self.rejected(lambda value: value["operations"][1]["api"].update(address=0))

    def test_duplicate_record_and_operation_rejected(self):
        self.rejected(lambda value: value["operations"][1]["records"].append("DRIVER_VERSION"))
        self.rejected(lambda value: value["operations"].append(copy.deepcopy(value["operations"][0])))

    def test_unreadable_and_unspecified_records_rejected(self):
        for name in ("MOTION_COMMAND", "COLLISION_THRESHOLD_003B", "POSITION_SEGMENT_01_RESERVED"):
            with self.subTest(name=name):
                self.rejected(lambda value: value["operations"][1]["records"].append(name))

    def test_accidental_overlap_rejected(self):
        self.rejected(lambda value: value["operations"][2]["records"].append("DRIVER_VERSION"))
        self.rejected(lambda value: value.update(shared_records=[]))
        self.rejected(lambda value: value["shared_records"].append("DRIVER_VERSION"))

    def test_future_states_cannot_claim_implementation(self):
        for column, state in (("implementation", "IMPLEMENTED"), ("cli", "REACHABLE"), ("native", "PASS"), ("hardware", "PASS")):
            with self.subTest(column=column):
                self.rejected(lambda value: value["default_disposition"].update({column: state}))

    def test_reachability_needs_real_api(self):
        self.rejected(lambda value: value["operations"][1].update(implementation="IN_PROGRESS", cli="REACHABLE"))
        self.rejected(lambda value: value["operations"][0]["api"].update(symbols=["missingDeclaration"]))
        self.rejected(lambda value: value["operations"][0]["api"].update(header="examples/common/RtuBusOwner.h"))

    def test_evidence_needs_file_and_explicit_reason(self):
        self.rejected(lambda value: value["operations"][0]["hardware"].update(evidence=[]))
        self.rejected(lambda value: value["operations"][0]["native"].update(evidence=["missing.cpp"]))
        self.rejected(lambda value: value["operations"][0]["hardware"].update(state="NOT_APPLICABLE"))
        self.rejected(lambda value: value["operations"][0]["native"].update(evidence=["../FieldCore-node/AGENTS.md"]))

    def test_documented_model_is_not_qualified_hardware(self):
        self.rejected(lambda value: value["model_availability"].update(qualification="QUALIFIED"))
        self.assertEqual(INVENTORY["model_availability"]["exact_model"], "ESS23-RS20")
        self.rejected(lambda value: value["model_availability"].update(exact_model="UNREVIEWED_MODEL"))


if __name__ == "__main__":
    unittest.main()

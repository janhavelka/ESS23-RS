#!/usr/bin/env python3
"""Check typed ESS coverage against the original register ledger; never generate addresses."""
import argparse
from collections import Counter
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "docs/reference/ess_rs_operations.json"
spec = importlib.util.spec_from_file_location("ess_register_catalogue", ROOT / "scripts/generate_ess_registers.py")
catalogue = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalogue)

DEFAULT = {"implementation": "NOT_IMPLEMENTED", "cli": "NOT_REACHABLE",
           "native": "NOT_RUN", "hardware": "NOT_RUN"}
IMPLEMENTATION = {"NOT_IMPLEMENTED", "IN_PROGRESS", "IMPLEMENTED", "UNSUPPORTED"}
EVIDENCE = {"NOT_RUN", "PASS", "FAIL", "NOT_APPLICABLE"}


def local_file(value):
    """Evidence/API references must designate files inside this checkout."""
    if not isinstance(value, str) or not value or Path(value).is_absolute():
        raise ValueError("invalid local file reference")
    path = (ROOT / value).resolve()
    try:
        path.relative_to(ROOT)
    except ValueError:
        raise ValueError("file reference outside checkout: " + value) from None
    return path


def check(inventory, ledger):
    def keys(value, allowed, label):
        if not isinstance(value, dict) or set(value) - set(allowed.split()):
            raise ValueError("unknown fields or invalid object: " + label)

    keys(inventory, "schema_version register_ledger scope default_disposition model_availability shared_records operations coverage_notes", "inventory")
    if inventory.get("schema_version") != 1 or inventory.get("register_ledger") != "docs/reference/ess_rs_registers.json":
        raise ValueError("unsupported inventory or register ledger")
    if inventory.get("default_disposition") != DEFAULT:
        raise ValueError("future obligations must default to not implemented/not reachable/not run")
    model = inventory.get("model_availability", {})
    keys(model, "documented_models observed_model_raw exact_model firmware_compatibility qualification reason", "model availability")
    if (set(model.get("documented_models", [])) != set(ledger["qualification"]["models"])
            or model.get("exact_model") not in {"UNKNOWN", *ledger["qualification"]["models"]}
            or model.get("firmware_compatibility") != "UNKNOWN"
            or model.get("qualification") != "UNQUALIFIED" or not model.get("reason")):
        raise ValueError("documented models must not imply exact-model or firmware qualification")

    rows = catalogue.expand(ledger)
    catalogue.validate(ledger, rows)
    records = {}
    for row in rows:
        reserved = row["signedness"] == "RESERVED"
        unresolved = row["source_access"] == "UNSPECIFIED"
        obligations = {"read": "NOT_APPLICABLE", "write": "NOT_APPLICABLE", "action": "NOT_APPLICABLE"}
        if reserved or unresolved:
            obligations = dict.fromkeys(obligations, "ACCOUNTED_RESERVED" if reserved else "UNRESOLVED_ACCESS")
        else:
            if row["source_access"] in {"RO", "RW", "RW/S"}: obligations["read"] = "NOT_IMPLEMENTED"
            if row["source_access"] in {"WO", "RW", "RW/S"}: obligations["write"] = "NOT_IMPLEMENTED"
            if row["source_access"] == "WO": obligations["action"] = "NOT_IMPLEMENTED"
        records[row["name"]] = {
            "id": row["name"], "address": row["address"], "words": row["words"],
            "access": row["source_access"], "pages": row["pages"], "issues": row["issues"],
            "source_certainty": "EXPLICIT_RESERVED" if reserved else "UNRESOLVED_ACCESS" if unresolved else
                "DOCUMENTED_WITH_ISSUES" if row["issues"] else "DOCUMENTED_NO_RECORDED_ISSUES",
            "obligations": obligations, "read_operations": [], "write_operations": [], "action_operations": [], "default_disposition": DEFAULT.copy()
        }

    choices = {group["name"] + "." + value["name"]: {"id": group["name"] + "." + value["name"],
                "value": value["value"], "pages": value["pages"], "default_disposition": DEFAULT.copy(),
                "disposition": DEFAULT.copy(), "operations": []}
               for group in ledger["enums"] for value in group["values"]}
    choice_groups = {row["name"]: row.get("choices") for row in rows}
    seen = set()
    referenced = Counter()
    for operation in inventory.get("operations", []):
        keys(operation, "id kind records choices api cli_commands implementation cli native hardware reason", "operation")
        op_id = operation.get("id")
        if not isinstance(op_id, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", op_id) or op_id in seen:
            raise ValueError("invalid or duplicate operation ID")
        seen.add(op_id)
        kind = operation.get("kind")
        if kind not in {"READ", "WRITE", "ACTION"}:
            raise ValueError("unsupported operation kind")
        selected = operation.get("choices", [])
        if kind != "ACTION" and selected:
            raise ValueError("read/write coverage is not named-choice action coverage")
        if kind == "ACTION" and (not selected or len(selected) != len(set(selected))):
            raise ValueError("action requires distinct explicit source choices")
        linked = operation.get("records", [])
        if not linked or len(linked) != len(set(linked)):
            raise ValueError("empty or duplicate record IDs in " + op_id)
        for record_id in linked:
            if record_id not in records:
                raise ValueError("unknown record ID: " + str(record_id))
            record = records[record_id]
            if record["obligations"][kind.lower()] not in IMPLEMENTATION:
                raise ValueError("read group references reserved/unreadable/unresolved record: " + record_id)
            referenced[record_id] += 1
            record[kind.lower() + "_operations"].append(op_id)
        if kind == "ACTION" and {choice_id.split(".")[0] for choice_id in selected} != {choice_groups[name] for name in linked}:
            raise ValueError("every action record requires explicit matching choices")
        for choice_id in selected:
            if choice_id not in choices or choice_id.split(".")[0] not in {choice_groups[name] for name in linked}:
                raise ValueError("unknown choice or choice not owned by action record: " + str(choice_id))
            if choices[choice_id]["operations"]:
                raise ValueError("duplicate named-choice action coverage: " + choice_id)
            choices[choice_id]["operations"].append(op_id)

        state = operation.get("implementation")
        cli = operation.get("cli")
        if state not in IMPLEMENTATION or cli not in {"NOT_REACHABLE", "REACHABLE"}:
            raise ValueError("invalid implementation/CLI disposition: " + op_id)
        if cli == "REACHABLE" and (state != "IMPLEMENTED" or not operation.get("cli_commands")):
            raise ValueError("reachable CLI needs an implemented operation and command: " + op_id)
        api = operation.get("api", {})
        keys(api, "header symbols", "api")
        header = local_file(api.get("header"))
        try:
            header.relative_to(ROOT / "include/MotorControlRS")
        except ValueError:
            raise ValueError("public API must not depend on examples: " + op_id) from None
        symbols = api.get("symbols", [])
        if len(symbols) != len(set(symbols)) or any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in symbols):
            raise ValueError("invalid API symbols: " + op_id)
        if state == "IMPLEMENTED":
            if not symbols or not header.is_file():
                raise ValueError("implemented operation needs a real public API: " + op_id)
            text = header.read_text(encoding="utf-8")
            if any(not re.search(r"\b" + name + r"\s*\(", text) for name in symbols):
                raise ValueError("API symbol absent from header: " + op_id)
        for column in ("native", "hardware"):
            evidence = operation.get(column, {})
            keys(evidence, "state evidence reason", column)
            if evidence.get("state") not in EVIDENCE or not isinstance(evidence.get("evidence"), list):
                raise ValueError("invalid " + column + " disposition: " + op_id)
            if evidence["state"] in {"PASS", "FAIL"} and not evidence["evidence"]:
                raise ValueError(column + " result needs evidence: " + op_id)
            if evidence["state"] == "NOT_APPLICABLE" and not evidence.get("reason"):
                raise ValueError(column + " not applicable needs a reason: " + op_id)
            for reference in evidence["evidence"]:
                if not local_file(reference).is_file():
                    raise ValueError("missing " + column + " evidence: " + reference)
        for choice_id in selected:
            choices[choice_id]["disposition"] = {"implementation": state, "cli": cli,
                "native": operation["native"]["state"], "hardware": operation["hardware"]["state"]}
        for record_id in linked:
            if kind in {"READ", "WRITE"}:
                column = kind.lower()
                current = records[record_id]["obligations"][column]
                if state == "IMPLEMENTED" or current != "IMPLEMENTED":
                    records[record_id]["obligations"][column] = state
            elif state in {"IMPLEMENTED", "IN_PROGRESS"}:
                # Partial choice coverage never marks the entire command register implemented.
                for column in ("write", "action"):
                    records[record_id]["obligations"][column] = "IN_PROGRESS"

    shared = inventory.get("shared_records", [])
    if len(shared) != len(set(shared)) or set(shared) != {name for name, count in referenced.items() if count > 1}:
        raise ValueError("record overlap must match explicit shared_records exactly")

    # Source choices remain a separate denominator, including inactive/unknown
    # interpretations. Reading a raw setting is never setting/action coverage.
    summary = {"records": len(records), "reserved": sum(row["signedness"] == "RESERVED" for row in rows),
               "unresolved_access": sum(row["source_access"] == "UNSPECIFIED" for row in rows),
               "operations": len(seen), "named_choices": len(choices),
               "choice_implementation": dict(Counter(choice["disposition"]["implementation"] for choice in choices.values()))}
    for kind in ("read", "write", "action"):
        summary[kind] = dict(Counter(record["obligations"][kind] for record in records.values()))
    return {"summary": summary, "records": list(records.values()), "named_choices": list(choices.values()),
            "operations": inventory["operations"], "model_availability": model}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--details", action="store_true", help="Print derived record/choice dispositions instead of just counts.")
    args = parser.parse_args()
    try:
        result = check(json.loads(INVENTORY.read_text(encoding="utf-8")),
                       json.loads(catalogue.LEDGER.read_text(encoding="utf-8")))
    except (ValueError, KeyError, OSError) as error:
        print("ESS operation inventory: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps(result if args.details else result["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

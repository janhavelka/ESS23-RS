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
CONSOLE = ROOT / "examples/probe_cli/ProbeConsole.cpp"
PUBLIC_DECLARATION = re.compile(
    r"^(?:(?:inline|constexpr)\s+)*(?:const\s+)?[A-Za-z_][\w:]*(?:\s*\*)?\s+([A-Za-z_]\w*)\s*\(", re.MULTILINE)


def console_commands():
    """Read the production inventory, rather than a second list of top-level names."""
    source = CONSOLE.read_text(encoding="utf-8")
    table = source.split("const Entry COMMANDS[] = {", 1)[1].split("\n};", 1)[0]
    return {name: (kind, syntax) for name, kind, syntax in re.findall(
        r'\{"([^"\\]+)",\s*Command::(\w+),\s*"((?:\\.|[^"\\])*)"', table)}, source


def check_cli_route(command, metadata, source, operation_kind):
    """Check a documented operation prefix and its real family grammar.

    This is a source/inventory check, not a substitute for executing console
    tests. Parameter placeholders deliberately designate bounded leaf prefixes.
    """
    tokens = command.split()
    if not tokens or tokens[0] not in metadata:
        raise ValueError("CLI command absent from production inventory: " + command)
    root = tokens[0]
    if root == "profile":
        raise ValueError("profile is inventory only; use the canonical operation command: " + command)

    tail = tokens[1:]
    valid = True
    effects = set()
    if root in {"driver", "io", "control"}:
        valid = tail in [["read"], ["set"], ["set", "FIELD", "INTEGER"]]
        if root == "control" and tail == ["set", "lock-delay", "INTEGER"]:
            valid = True
        if valid: effects = {"READ" if tail[0] == "read" else "WRITE"}
    elif root == "segment":
        valid = len(tail) == 3 and tail[0] in {"position", "speed", "start"} and tail[1] == "INDEX" and tail[2] in {"read", "set"}
        if valid: effects = {"READ" if tail[2] == "read" else "WRITE"}
    elif root == "tuning":
        valid = len(tail) >= 2 and tail[0] in {"filters", "current-loop", "la", "collision"} and tail[1] in {"read", "set"}
        if valid and tail[1] == "read":
            valid = len(tail) == 2
        elif valid:
            valid = len(tail) == 4 and tail[2] in {"FIELD", "input-filter"} and tail[3] == "INTEGER"
        if valid: effects = {"READ" if tail[1] == "read" else "WRITE"}
    elif root == "communication":
        valid = len(tail) == 3 and tail[0] == "begin" and tail[1] in {"address", "baud", "format"} and tail[2] == "VALUE"
        effects = {"WRITE"}
    elif root == "persistence":
        valid = len(tail) == 2 and tail[0] == "begin" and tail[1] in {"save", "factory-restore"}
        effects = {"ACTION"}
    elif root == "motion-profile":
        valid = len(tail) == 1 and tail[0] in {"read", "inspect", "restore", "forget"}
        if valid: effects = {"READ"} if tail[0] == "read" else {"WRITE"} if tail[0] == "restore" else set()
    elif root == "move":
        valid = len(tail) == 1 and tail[0] in {"relative", "absolute", "angle"}
        effects = {"WRITE", "ACTION"}
    elif root == "stop" and tokens[0] == "stop":
        valid = len(tail) == 1 and tail[0] in {"normal", "fast"}
        effects = {"ACTION"}
    elif root == "read":
        valid = len(tail) == 1 and tail[0] in {"identity", "config", "state"}
        effects = {"READ"}
    elif root == "discover":
        valid = not tail or tail in [["inspect"], ["cancel"], ["restore"], ["finish"]]
        # Session inspection/control are mapped in the separate host surface.
        effects = {"READ"} if not tail else set()
    elif root == "monitor":
        valid = tail == ["INTERVAL_MS", "COUNT"]
        effects = {"READ"}
    else:
        valid = not tail
        if root == "probe": effects = {"READ"}
        elif root in {"home", "velocity", "move-relative", "move-absolute", "move-angle"}: effects = {"WRITE", "ACTION"}
        elif root in {"enable", "motor-release", "alarm-clear", "position-clear"}: effects = {"ACTION"}
    if not valid or operation_kind not in effects:
        raise ValueError("invalid operation CLI prefix: " + command)


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

    keys(inventory, "schema_version register_ledger scope default_disposition model_availability shared_records operations coverage_notes gaps public_surface non_operation_choices", "inventory")
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
        if row["words"] == 2 and "write_constraint" in row:
            first = int(row["address"], 16)
            windows = [dict(start=window["start"], count=window["count"], pages=window["pages"])
                       for window in ledger["write_multiple_windows"]
                       if int(window["start"], 16) <= first and
                       first + 2 <= int(window["start"], 16) + window["count"]]
            records[row["name"]]["pair_write_policy"] = {
                "reviewed_windows": windows, "reason": row["write_constraint"],
                "partial_application": "UNSPECIFIED", "split_fc06": "UNAVAILABLE"}

    choices = {group["name"] + "." + value["name"]: {"id": group["name"] + "." + value["name"],
                "value": value["value"], "pages": value["pages"], "default_disposition": DEFAULT.copy(),
                "disposition": DEFAULT.copy(), "coverage_kind": "UNASSIGNED", "operations": []}
               for group in ledger["enums"] for value in group["values"]}
    choice_groups = {row["name"]: row.get("choices") for row in rows}
    choice_records = {group["name"]: [row for row in rows if row.get("choices") == group["name"]]
                      for group in ledger["enums"]}
    for item in inventory.get("non_operation_choices", []):
        keys(item, "id reason", "non-operation choice")
        choice_id = item.get("id")
        # The reviewed INVALID value is metadata, not another auxiliary action.
        # Other choices cannot be removed from the denominator by relabelling them.
        if (choice_id != "AuxiliaryCommand.INVALID" or choice_id not in choices or
                choices[choice_id]["value"] != 0 or not item.get("reason") or
                choices[choice_id]["coverage_kind"] != "UNASSIGNED"):
            raise ValueError("invalid or duplicate non-operation choice")
        choices[choice_id].update(coverage_kind="METADATA", reason=item["reason"],
            disposition={"implementation": "NOT_APPLICABLE", "cli": "NOT_REACHABLE",
                         "native": "NOT_APPLICABLE", "hardware": "NOT_APPLICABLE"})
    seen = set()
    referenced = Counter()
    metadata, console_source = console_commands()
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
        if selected and len(selected) != len(set(selected)):
            raise ValueError("setting/action choices must be distinct")
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
        if kind != "READ" and (kind == "ACTION" or selected) and {choice_id.split(".")[0] for choice_id in selected} != {choice_groups[name] for name in linked if choice_groups[name]}:
            raise ValueError("setting/action records require explicit matching choices")
        for choice_id in selected:
            if choice_id not in choices or choice_id.split(".")[0] not in {choice_groups[name] for name in linked}:
                raise ValueError("unknown choice or choice not owned by action record: " + str(choice_id))
            if kind == "READ" and any(row["source_access"] != "RO"
                                       for row in choice_records[choice_id.split(".")[0]]):
                raise ValueError("raw setting reads do not establish setting/action choice coverage")
            if choices[choice_id]["operations"] or choices[choice_id]["coverage_kind"] == "METADATA":
                raise ValueError("duplicate named-choice action coverage: " + choice_id)
            choices[choice_id]["operations"].append(op_id)

        state = operation.get("implementation")
        cli = operation.get("cli")
        if state not in IMPLEMENTATION or cli not in {"NOT_REACHABLE", "REACHABLE"}:
            raise ValueError("invalid implementation/CLI disposition: " + op_id)
        if cli == "REACHABLE" and (state != "IMPLEMENTED" or not operation.get("cli_commands")):
            raise ValueError("reachable CLI needs an implemented operation and command: " + op_id)
        commands = operation.get("cli_commands", [])
        if not isinstance(commands, list) or len(commands) != len(set(commands)):
            raise ValueError("duplicate or invalid CLI commands: " + op_id)
        if cli == "REACHABLE":
            for command in commands:
                if not isinstance(command, str):
                    raise ValueError("invalid CLI command: " + op_id)
                check_cli_route(command, metadata, console_source, kind)
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
            choices[choice_id]["coverage_kind"] = kind
            choices[choice_id]["disposition"] = {"implementation": state, "cli": cli,
                "native": operation["native"]["state"], "hardware": operation["hardware"]["state"]}
            if kind == "READ":
                # A successful read is not physical exercise of every alarm/bit.
                choices[choice_id]["disposition"]["hardware"] = "NOT_RUN"
                choices[choice_id]["hardware_reason"] = "Decoder coverage only; physical activation of this named state is not established by the read operation's hardware evidence."
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

    gap_ids = set()
    for gap in inventory.get("gaps", []):
        keys(gap, "id records choices kinds owner disposition reason", "gap")
        gap_id = gap.get("id")
        if not isinstance(gap_id, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", gap_id) or gap_id in gap_ids:
            raise ValueError("invalid or duplicate named gap")
        gap_ids.add(gap_id)
        if (gap.get("disposition") not in {"NOT_IMPLEMENTED", "UNRESOLVED", "PARTIAL"}
                or not gap.get("reason") or not re.fullmatch(r"(?:0[1-9]|1[0-9]|2[0-7])(?:/(?:0[1-9]|1[0-9]|2[0-7]))*", gap.get("owner", ""))):
            raise ValueError("gap needs implementation/semantic disposition and owning prompt: " + gap_id)
        if not gap.get("records") or any(name not in records for name in gap["records"]):
            raise ValueError("gap references missing records: " + gap_id)
        if not gap.get("kinds") or any(kind not in {"READ", "WRITE", "ACTION", "ACCESS"} for kind in gap["kinds"]):
            raise ValueError("invalid gap obligation: " + gap_id)
        if any(name not in choices for name in gap.get("choices", [])):
            raise ValueError("gap references missing choices: " + gap_id)
        if any(name.split(".")[0] not in {choice_groups[record] for record in gap["records"]}
               for name in gap.get("choices", [])):
            raise ValueError("gap choice does not belong to its records: " + gap_id)
        for name in gap["records"]:
            records[name].setdefault("gaps", []).append(gap_id)
    if any(not referenced[name] and not records[name].get("gaps") for name in records
           if records[name]["source_certainty"] != "EXPLICIT_RESERVED"):
        raise ValueError("unlinked nonreserved source record needs a named owning gap")
    for choice in choices.values():
        if choice["operations"] or choice["coverage_kind"] == "METADATA":
            continue
        owners = {row["name"] for row in choice_records[choice["id"].split(".")[0]]
                  if row["source_access"] in {"WO", "RW", "RW/S"}}
        if owners and not any(
                (choice["id"] in gap.get("choices", []) and {"WRITE", "ACTION"}.intersection(gap["kinds"])) or
                (not gap.get("choices") and owners.intersection(gap["records"]) and
                 {"WRITE", "ACTION"}.intersection(gap["kinds"]))
                for gap in inventory.get("gaps", [])):
            raise ValueError("unimplemented writable choice needs a named owning gap: " + choice["id"])

    # All installed free-function declarations must have an explicit role.
    # Member APIs (TrafficCapture/Status) retain their documented owner contract.
    surfaced = set()
    for item in inventory.get("public_surface", []):
        keys(item, "header symbols classification cli_topics reason", "public surface")
        header = local_file(item.get("header"))
        try:
            header.relative_to(ROOT / "include/MotorControlRS")
        except ValueError:
            raise ValueError("surface map must use installed public headers") from None
        if (item.get("classification") not in {"DEVICE_OPERATION", "SEQUENCER", "OBSERVATION", "HOST_CONFIGURATION", "CONVERSION", "CODEC", "METADATA", "DIAGNOSTIC"}
                or not item.get("reason") or not item.get("symbols") or not header.is_file()):
            raise ValueError("invalid public surface classification")
        declared = set(PUBLIC_DECLARATION.findall(header.read_text(encoding="utf-8")))
        for symbol in item["symbols"]:
            key = (header, symbol)
            if symbol not in declared or key in surfaced:
                raise ValueError("absent or duplicate classified public symbol: " + str(symbol))
            surfaced.add(key)
        for topic in item.get("cli_topics", []):
            if not isinstance(topic, str) or topic not in metadata:
                raise ValueError("public surface CLI topic absent from production inventory: " + str(topic))
    declarations = {(header.resolve(), symbol) for header in (ROOT / "include/MotorControlRS").rglob("*.h")
                    for symbol in PUBLIC_DECLARATION.findall(header.read_text(encoding="utf-8"))}
    missing = declarations - surfaced
    if missing:
        raise ValueError("unclassified installed public functions: " + ", ".join(sorted(str(header.relative_to(ROOT)) + ":" + symbol for header, symbol in missing)))

    # Read-only decoding, settings/actions and reviewed no-op metadata retain
    # distinct roles. Reading a raw setting is never setting/action coverage.
    summary = {"records": len(records), "reserved": sum(row["signedness"] == "RESERVED" for row in rows),
               "unresolved_access": sum(row["source_access"] == "UNSPECIFIED" for row in rows),
               "operations": len(seen), "named_choices": len(choices),
               "choice_implementation": dict(Counter(choice["disposition"]["implementation"] for choice in choices.values()))}
    summary["choice_coverage_kind"] = dict(Counter(choice["coverage_kind"] for choice in choices.values()))
    summary["named_gaps"] = len(gap_ids)
    summary["classified_public_functions"] = len(surfaced)
    for kind in ("read", "write", "action"):
        summary[kind] = dict(Counter(record["obligations"][kind] for record in records.values()))
    return {"summary": summary, "records": list(records.values()), "named_choices": list(choices.values()),
            "operations": inventory["operations"], "gaps": inventory.get("gaps", []),
            "public_surface": inventory.get("public_surface", []), "model_availability": model}


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

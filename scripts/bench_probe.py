#!/usr/bin/env python3
"""Bounded read tests and explicit one-attempt actions for the standalone MotorControl-RS JSONL console.

Reviewed probes, typed reads and explicitly selected actions create motor-bus traffic. Status, health and memory are cached
host reports. A lost or malformed reply stops the run; nothing is replayed and
host recovery is never automatic. Python 3.10+; pyserial is needed only for a
real port. See ``--help`` for finite probe, stress, watch and load runs. Load
settings change the host fixture only; they never change motor settings.
The Console API also exposes explicit result/release/cancel/recover/reset host
controls. Sequential probes release their retained result with a logged command;
begin/wait permit bounded interleaving and caller-controlled result retention.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import math
from fractions import Fraction
from pathlib import Path
import re
import sys
import time


MAX_LINE = 8192
MAX_INPUT = 32768
MAX_TRAFFIC_INPUT = 1048576  # Independent finite diagnostic budget per command/drain.
COMMANDS = frozenset({"version", "config", "probe", "capture-read", "status", "health", "memory", "stats", "load",
                      "drv", "result", "release", "cancel", "recover", "reset", "caps", "host", "useaddr", "wiring", "communication", "persistence", "motion-profile", "debug",
                      "read-identity", "read-config", "read-state", "health-check", "monitor", "axis", "prepare", "enable", "motor-release", "alarm-clear", "stop", "position-clear", "move-relative", "move-absolute", "move-angle", "velocity", "driver", "io", "segment", "control", "tuning", "home", "discover", "profile-list"})
MAX_COMMANDS = 11  # Eight ordinary operations, recovery, reserved stop and local query.
MAX_OPERATIONS = 10  # Eight ordinary operations, one recovery and one reserved stop.
MAX_PROBES = 8
TYPED_READS = {"read-identity": "identity", "read-config": "config", "read-state": "state"}
READ_COMMANDS = ("probe", "capture-read", *TYPED_READS)
HOST_BAUDS = (9600, 19200, 38400, 115200)
HOST_FORMATS = ("8N1", "8N2", "8E1", "8O1")
ADMISSION_FAILURES = frozenset({"busy", "recovery_required", "unavailable", "failed", "queue_full", "results_full",
    "ids_exhausted", "invalid", "already_terminal", "timing_unqualified", "unsupported", "axis_conflict",
    "unresolved", "unimplemented", "invalid_arguments", "invalid_address", "invalid_value", "invalid_field",
    "unsupported_policy", "invalid_axis_configuration", "output_full", "output_capacity"})


def discovery_arguments(tokens: tuple[str, ...]) -> dict:
    """Explicit ESS candidates and finite scan budgets; never guessed protocols."""
    if not isinstance(tokens, tuple) or len(tokens)>18 or any(type(v) is not str for v in tokens):
        raise ValueError("discovery arguments require a tuple of strings")
    if tokens in (("inspect",), ("cancel",), ("restore",), ("finish",)):
        return dict(action=tokens[0])
    result = dict(action="begin", profile="ess_rs", tuples=[], identity=False)
    seen = set()
    index = 0
    while index < len(tokens):
        key = tokens[index]
        index += 1
        if key != "tuple" and key in seen:
            raise ValueError("duplicate discovery option")
        seen.add(key)
        def take(count):
            nonlocal index
            if index + count > len(tokens):
                raise ValueError("incomplete discovery option")
            values = tokens[index:index + count]
            index += count
            return values
        def integer(raw, low, high):
            if not raw.isascii() or not raw.isdigit() or not low <= int(raw) <= high:
                raise ValueError("discovery numeric option outside its finite range")
            return int(raw)
        if key in ("profile", "manufacturer"):
            value, = take(1)
            if (key == "profile" and value != "ess_rs") or (key == "manufacturer" and value != "stepperonline"):
                raise ValueError("discovery selects only the implemented ESS profile")
            if {"profile", "manufacturer"} <= seen:
                raise ValueError("select profile or manufacturer, not both")
        elif key == "addresses":
            first, last = take(2)
            result["first"] = integer(first, 1, 247)
            result["last"] = integer(last, result["first"], 247)
        elif key == "tuple":
            baud, fmt = take(2)
            candidate = dict(baud=integer(baud, 9600, 115200), format=fmt)
            if candidate["baud"] not in HOST_BAUDS or fmt not in HOST_FORMATS or candidate in result["tuples"] or len(result["tuples"]) == 4:
                raise ValueError("unsupported, duplicate or excessive discovery tuple")
            result["tuples"].append(candidate)
        elif key in ("query-ms", "overall-ms", "requests", "results"):
            raw, = take(1)
            bounds = {"query-ms": (1, 5000), "overall-ms": (1, 60000), "requests": (1, 256), "results": (1, 8)}
            result[key] = integer(raw, *bounds[key])
        elif key == "identity":
            result["identity"] = True
        else:
            raise ValueError("unknown discovery option")
    return result


DISCOVERY_EVIDENCE_COLUMNS = ["first", "count", "event", "raw_hex", "received_length", "qualified", "attempted_us",
                              "earliest_us", "latest_us", "delivered_us", "transport_detail", "tx_accepted",
                              "execution_unknown", "status", "detail", "frame_error"]


def debug_arguments(tokens: tuple[str, ...]) -> str | None:
    if not isinstance(tokens, tuple) or len(tokens) > 1 or (tokens and tokens[0] not in ("off", "raw", "decoded")):
        raise ValueError("debug accepts off, raw, decoded or no arguments")
    return tokens[0] if tokens else None


@contextmanager
def debug_session(console, mode: str | None, timeout_s: float):
    """Observe ordinary commands on their existing connection; restore display only."""
    evidence = {}
    if mode is None:
        yield evidence
        return
    debug_arguments((mode,))
    def command(tokens=()):
        result = console.command("debug", host_args=tokens, timeout_s=timeout_s)
        if not result["ok"]:
            raise BenchError("debug selection/query failed: " + str(result.get("result")))
        return result
    evidence["before"] = command()
    original = evidence["before"]["mode"]
    changed = False
    failed = False
    try:
        evidence["selected"] = command((mode,))
        changed = original != mode
        yield evidence
    except BaseException:
        failed = True
        raise
    finally:
        cleanup_error = None
        if not console.synchronized:
            evidence["cleanup"] = "skipped_unsynchronized"
        else:
            try:
                evidence["after"] = command()
            except BaseException as error:
                evidence["cleanup_error"] = str(error)
                cleanup_error = error
            # A refused local query does not imply lost framing. Restoring the
            # prior display remains a separate one-attempt, host-only command.
            if changed and console.synchronized:
                try:
                    evidence["restored"] = command((original,))
                except BaseException as error:
                    evidence["restore_error"] = str(error)
                    if cleanup_error is None:
                        evidence["cleanup_error"] = str(error)
                        cleanup_error = error
            evidence["cleanup"] = ("failed" if cleanup_error is not None else
                                   "restored" if "restored" in evidence else
                                   "skipped_unsynchronized" if changed else "unchanged")
        try:
            console.emit("debug_session", **evidence)
        except BaseException as error:
            evidence["evidence_error"] = str(error)
            if cleanup_error is None:
                cleanup_error = error
        if cleanup_error is not None and not failed:
            raise cleanup_error


def motion_profile_arguments(tokens: tuple[str, ...]) -> str:
    """One explicit profile operation; inspection never repeats a motor write."""
    if not isinstance(tokens, tuple) or len(tokens) != 1 or tokens[0] not in ("read", "inspect", "restore", "forget"):
        raise ValueError("motion-profile requires read, inspect, restore or forget")
    return tokens[0]


def wiring_arguments(tokens: tuple[str, ...]) -> tuple[str, int] | None:
    """Declared host wiring only; no drive assignment or observed level changes."""
    if not isinstance(tokens, tuple) or any(type(token) is not str for token in tokens):
        raise ValueError("wiring requires a tuple of exact terminal/disposition tokens")
    if not tokens:
        return None
    terminals = ("x0", "x1", "x2", "x3", "y0", "y1")
    dispositions = ("unknown", "unconnected", "connected")
    if len(tokens) != 2 or tokens[0] not in terminals or tokens[1] not in dispositions:
        raise ValueError("wiring requires x0..x3|y0..y1 unknown|unconnected|connected")
    return tokens[0], dispositions.index(tokens[1])


def communication_arguments(tokens: tuple[str, ...]) -> dict:
    """Finite exact-candidate operations, never raw register writes or scans."""
    if not isinstance(tokens, tuple) or any(type(v) is not str for v in tokens):
        raise ValueError("communication arguments require a tuple of strings")
    if tokens in ((), ("inspect",), ("finish",)):
        return {"action": 6 if tokens == ("finish",) else -1}
    if len(tokens) == 2 and tokens[0] in ("host", "confirm") and tokens[1] in ("before", "requested"):
        return {"action": (2 if tokens[0] == "host" else 4) + (tokens[1] == "requested")}
    if len(tokens) not in (3, 4) or tokens[0] not in ("plan", "begin"):
        raise ValueError("communication requires inspect, plan/begin FIELD VALUE [address], host/confirm before/requested or finish")
    field, raw = tokens[1:3]
    if field == "address" and raw.isascii() and raw.isdigit() and 1 <= int(raw) <= 247:
        value, reg = int(raw), 0x13
    elif field == "baud" and raw.isascii() and raw.isdigit() and int(raw) in HOST_BAUDS:
        value, reg = (115200, 38400, 19200, 9600).index(int(raw)), 0x14
    elif field == "format" and raw in HOST_FORMATS:
        value, reg = HOST_FORMATS.index(raw), 0x15
    else:
        raise ValueError("invalid communication field or value")
    result = dict(action=0 if tokens[0] == "plan" else 1, field=field, value=value, register=reg)
    if len(tokens) == 4:
        if not tokens[3].isascii() or not tokens[3].isdigit() or not 1 <= int(tokens[3]) <= 247:
            raise ValueError("communication target address must be 1..247")
        result["address"] = int(tokens[3])
    return result


def persistence_arguments(tokens: tuple[str, ...]) -> dict:
    """Explicit bounded save/restore workflow; snapshot and verify only read."""
    if not isinstance(tokens, tuple) or any(type(v) is not str for v in tokens):
        raise ValueError("persistence arguments require a tuple of strings")
    if tokens in ((), ("inspect",), ("verify",), ("finish",), ("snapshot",)):
        return {"action": {"verify": 2, "finish": 3, "snapshot": 4}.get(tokens[0] if tokens else "inspect", -1)}
    if tokens == ("host", "before"):
        return {"action": 5}
    if len(tokens) != 2 or tokens[0] not in ("plan", "begin") or tokens[1] not in ("save", "factory-restore"):
        raise ValueError("persistence requires inspect, snapshot, plan/begin save/factory-restore, verify, host before or finish")
    return dict(action=0 if tokens[0] == "plan" else 1, kind=tokens[1], register=45,
                value=66 if tokens[1] == "save" else 65)


def host_arguments(arguments: tuple[str, ...]) -> dict:
    """Only reviewed host tuples; no device configuration or recovery is implied."""
    if not isinstance(arguments, tuple) or any(type(t) is not str for t in arguments):
        raise ValueError("host arguments require ASCII tokens")
    if arguments in ((), ("caps",), ("restore",)):
        return {"restore": arguments == ("restore",)}
    if len(arguments) == 2 and arguments[0] == "baud":
        candidate = {"baud": arguments[1]}
    elif len(arguments) == 2 and arguments[0] == "fmt":
        candidate = {"format": arguments[1]}
    elif len(arguments) == 3 and arguments[0] == "set":
        candidate = {"baud": arguments[1], "format": arguments[2]}
    else:
        raise ValueError("host requires baud RATE, fmt FORMAT, set RATE FORMAT, restore or caps")
    if "baud" in candidate:
        value = candidate["baud"]
        if not re.fullmatch(r"[0-9]+", value) or len(value) > 6 or int(value) not in HOST_BAUDS:
            raise ValueError("host baud is outside reviewed tuples")
        candidate["baud"] = int(value)
    if "format" in candidate and candidate["format"] not in HOST_FORMATS:
        raise ValueError("host format is outside reviewed tuples")
    return candidate


def host_timing(baud: int, fmt: str) -> dict:
    """Independent integer bounds for the reviewed host policy, not qualification."""
    if baud not in HOST_BAUDS or fmt not in HOST_FORMATS:
        raise ValueError("unreviewed host tuple")
    bits = 10 if fmt == "8N1" else 11
    ceil = lambda numerator, denominator: (numerator + denominator - 1) // denominator
    gap15 = 750 if baud > 19200 else ceil(bits * 3000000, baud * 2)
    gap35 = 1750 if baud > 19200 else ceil(bits * 7000000, baud * 2)
    maximum = ceil(bits * 100000000, baud * 98)
    return dict(character_min_us=bits * 100000000 // (baud * 102), character_max_us=maximum,
                stop_guard_us=ceil((2 if fmt == "8N2" else 1) * 100000000, baud * 98) + 2, capture_period_us=20,
                gap15_us=gap15, gap35_us=gap35,
                reply_gap_us=304 if (baud, fmt) == (115200, "8N1") else gap35,
                response_timeout_us=200000, request_timeout_us=500000,
                recovery_guard_us=500000, tx_timeout_us=max(20000, 32 * maximum + 10040))
ACTION_COMMANDS = ("enable", "motor-release", "alarm-clear", "stop", "position-clear")
MOVE_COMMANDS = ("move-relative", "move-absolute", "move-angle")
ACTION_KINDS = {"enable": "enable", "motor-release": "release", "alarm-clear": "clear_alarm", "stop": "stop", "position-clear": "clear_position"}
DRIVER_FIELDS = ("direction", "subdivision", "word-order", "soft-limit", "over-limit", "interruption", "position-mode", "positive-limit", "negative-limit")
DRIVER_REGISTERS = (0x10, 0x11, 0x19, 0x18, 0x17, 0x51, 0x50)
IO_FIELDS = ("input-polarity", "x0", "x1", "x2", "x3", "output-polarity", "y0", "y1", "custom")
IO_REGISTERS = (0x40, 0x41, 0x42, 0x43, 0x44, 0x4B, 0x4C, 0x4D, 0x4F)
CONTROL_FIELDS = ("algorithm", "encoder-resolution", "maximum-effective-current", "closed-maximum-current", "closed-base-current", "open-maximum-current", "lock-current", "lock-delay")
CONTROL_REGISTERS = tuple(range(0x100, 0x108))
CONTROL_WINDOWS = ((0x100, 4), (0x104, 4))
TUNING_GROUPS = {"filters": "filters", "current-loop": "current_loop", "la": "la", "collision": "collision"}
TUNING_FIELDS = {"filters": ("input-filter", "pulse-low-pass", "deviation-threshold", "arrival-window", "arrival-time", "pulse-mean"),
                 "current_loop": ("multiplier", "kp", "ki", "kc"),
                 "la": ("kp1", "kv1", "node1", "kp2", "kv2", "node2", "kvf", "position-ki"),
                 "collision": ("threshold", "current")}
TUNING_REGISTERS = {"filters": tuple(range(0x108, 0x10E)), "current_loop": tuple(range(0x10E, 0x112)),
                    "la": tuple(range(0x112, 0x11A)), "collision": (0x122, 0x123)}
TUNING_WINDOWS = {"filters": ((0x108, 4), (0x10C, 2)), "current_loop": ((0x10E, 4),),
                  "la": ((0x112, 4), (0x116, 4)), "collision": ((0x122, 2),)}
TUNING_RANGES = {"filters": ((0, 65535), (0, 1024), (1, 65535), (1, 256), (0, 200), (0, 512)),
                 "current_loop": ((0, 65535),) * 4, "la": ((0, 65535),) * 8,
                 "collision": ((200, 4000), (20, 100))}
IO_WINDOWS = ((0x40, 5), (0x4B, 3), (0x4F, 1))
HOME_EVIDENCE_COLUMNS = ["step", "event", "raw_hex", "received_length", "tx_accepted", "tx_complete", "response_confirmed", "qualified", "execution_unknown", "earliest_us", "latest_us", "delivered_us", "transport_detail", "status", "detail", "frame_error"]
DRIVER_PROGRESS_COLUMNS = ["field", "register", "previous", "requested", "acknowledged", "readback_known", "readback", "active_known", "active", "execution"]
DRIVER_EVIDENCE_COLUMNS = ["step", "register", "count", "write", "event", "raw_hex", "received_length", "tx_accepted", "tx_complete", "response_confirmed", "qualified", "execution_unknown", "earliest_us", "latest_us", "delivered_us", "transport_detail", "status", "detail", "frame_error", "attempted_us"]


def home_arguments(arguments: tuple[str, ...]) -> tuple[int, int, int, int]:
    """Explicit native words and zero offset; unavailable methods reach API gates."""
    if (not isinstance(arguments, tuple) or len(arguments) != 5 or arguments[4] != "zero" or
            any(type(token) is not str for token in arguments) or
            not re.fullmatch(r"-?[0-9]+", arguments[0]) or
            any(not re.fullmatch(r"[0-9]+", token) for token in arguments[1:4])):
        raise ValueError("home requires method search_native return_native ramp_native zero")
    values = tuple(int(token) for token in arguments[:4])
    if not (-32768 <= values[0] <= 32767 and 5 <= values[1] <= 3000 and
            5 <= values[2] <= 300 and 30 <= values[3] <= 2000):
        raise ValueError("home native words exceed the reviewed ranges")
    return values


SEGMENT_GROUPS = {"position": "position_segment", "speed": "speed_segment", "start": "segment_start_speed"}


def segment_arguments(arguments: tuple[str, ...]) -> tuple[str, int, dict]:
    """Bounded record configuration; no execution command exists."""
    if not isinstance(arguments, tuple) or len(arguments) < 3 or arguments[0] not in SEGMENT_GROUPS or not re.fullmatch(r"[0-9]+", arguments[1]):
        raise ValueError("segment requires position|speed|start INDEX read|set")
    index = int(arguments[1])
    if not 1 <= index <= 16: raise ValueError("segment index outside 1..16")
    group = SEGMENT_GROUPS[arguments[0]]
    return group, index, driver_arguments(arguments[2:], group)


def tuning_arguments(arguments: tuple[str, ...]) -> tuple[str, dict]:
    """Named native groups only; physical scales and legacy access stay guarded."""
    if not isinstance(arguments, tuple) or len(arguments) < 2 or arguments[0] not in TUNING_GROUPS:
        raise ValueError("tuning requires filters|current-loop|la|collision read|set")
    group = TUNING_GROUPS[arguments[0]]
    return group, driver_arguments(arguments[1:], group)


def driver_arguments(arguments: tuple[str, ...], group: str = "drive") -> dict:
    """Strict typed profile grammar; value semantics remain in the public API."""
    if (not isinstance(arguments, tuple) or not 1 <= len(arguments) <= 19 or
            any(type(token) is not str or not token or any(ord(c) < 33 or ord(c) > 126 for c in token) for token in arguments)):
        raise ValueError("driver requires bounded ASCII tokens")
    if group not in ("drive", "io", "control_settings", *SEGMENT_GROUPS.values(), *TUNING_FIELDS):
        raise ValueError("unknown settings group")
    if arguments == ("read",):
        return {}
    if arguments[0] != "set" or len(arguments) < 3 or len(arguments) % 2 != 1:
        raise ValueError("driver requires read or complete set field/integer pairs")
    names = TUNING_FIELDS[group] if group in TUNING_FIELDS else CONTROL_FIELDS if group == "control_settings" else IO_FIELDS if group == "io" else ("value",) if group == "segment_start_speed" else ("speed", "acceleration", "deceleration", "target") if group == "position_segment" else ("speed", "acceleration", "deceleration") if group == "speed_segment" else DRIVER_FIELDS
    result = {}
    for index in range(1, len(arguments), 2):
        field, value = arguments[index:index + 2]
        if group == "io" and field in ("x0", "x1", "x2", "x3", "y0", "y1") and value == "none":
            value = "0"
        if group == "control_settings" and field == "algorithm":
            value = {"open-loop": "1", "algorithm-1": "2"}.get(value, value)
        if field not in names or field in result or not re.fullmatch(r"-?[0-9]+", value):
            raise ValueError("driver field is unknown, repeated or not an integer")
        number = int(value)
        if not (-2**63 <= number < 2**63 if field in ("positive-limit", "negative-limit", "target") else -2**31 <= number < 2**31 if field in ("speed", "value") else 0 <= number <= 65535):
            raise ValueError("driver integer exceeds installed API storage")
        result[field] = number
    return result


def operation_quota(command):
    return command if command in ("stop", "recover") else "ordinary"

TYPED_WINDOWS = {"identity": ((0x0000, 4),),
                 "config": ((0x0010, 2), (0x0013, 3), (0x0017, 3), (0x0040, 5), (0x0100, 2)),
                 "state": ((0x0006, 2), (0x0008, 2), (0x000A, 3))}
LOAD_FIELDS = ("workload_us", "owner_delay_us", "console_bytes")
LOAD_LIMITS = (5000, 20000, 256)
LOAD_COUNTERS = (
    "elapsed_us", "work_us", "work_iterations", "console_lines", "console_dropped",
    "capture_us", "capture_samples", "timer_callbacks", "sample_gap_limit_us",
    "capture_high_water", "owner_gap_max_us", "capture_gap_max_us", "work_stack_free_bytes",
)
MEMORY_FIELDS = (
    "internal_free", "internal_min", "internal_largest", "psram_free", "psram_min",
    "psram_largest", "stack_free_bytes",
)
FAULT_TEXT = re.compile(
    r"Guru Meditation|stack canary|watchdog.*(?:trigger|timeout)|assert failed|"
    r"abort\(\) was called|brownout detector|backtrace:|(?:^|\s)rst:0x",
    re.IGNORECASE,
)


class BenchError(RuntimeError):
    """Evidence is incomplete, the console failed, or a probe was rejected."""


def positive(value: float, label: str) -> float:
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label} must be finite and greater than zero")
    return value


def check_load(settings: tuple[int, int, int]) -> None:
    """Check the fixture's explicit, motor-independent workload limits."""
    if not isinstance(settings, tuple) or len(settings) != len(LOAD_LIMITS):
        raise ValueError("load settings must be (work_us, owner_delay_us, console_bytes)")
    for name, value, maximum in zip(LOAD_FIELDS, settings, LOAD_LIMITS):
        if type(value) is not int or not 0 <= value <= maximum:
            raise ValueError(f"{name} must be an integer within 0..{maximum}")


def check_load_reply(response: dict, settings: tuple[int, int, int] | None) -> None:
    """Validate cached fixture evidence, including the applied configuration."""
    values = tuple(response.get(name) for name in LOAD_FIELDS)
    try:
        check_load(values)
    except ValueError as exc:
        raise BenchError("load reply has invalid configuration") from exc
    if settings is not None and values != settings:
        raise BenchError("load reply configuration does not match request")
    if (response.get("ready") is not True
            or response.get("capture_mode") not in ("poll", "timer")):
        raise BenchError("load fixture is not ready or capture mode is unknown")
    check_counts(response, LOAD_COUNTERS, "load")
    if type(response.get("cpu_valid")) is not bool:
        raise BenchError("load reply lacks CPU measurement availability")
    for field in ("cpu0_busy_pct", "cpu1_busy_pct"):
        value = response.get(field)
        if response["cpu_valid"]:
            if type(value) is not int or not 0 <= value <= 100:
                raise BenchError("load CPU measurement exceeds percentage bounds")
        elif field not in response or value is not None:
            raise BenchError("unavailable load CPU measurement must be null")
    if type(response.get("sample_gap_exceeded")) is not bool:
        raise BenchError("load reply lacks a valid sample_gap_exceeded")
    if response["timer_callbacks"] > response["capture_samples"]:
        raise BenchError("load timer callbacks exceed capture samples")
    if (response["sample_gap_limit_us"] > 0) != (response["capture_mode"] == "timer"):
        raise BenchError("load sample gap limit does not match capture mode")


def check_counts(response: dict, names: tuple[str, ...], command: str) -> None:
    """Require actual nonnegative integer counters, never missing values or bools."""
    for name in names:
        value = response.get(name)
        if type(value) is not int or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
            raise BenchError(f"{command} reply lacks a valid {name}")


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def invalid_constant(value: str) -> None:
    raise ValueError(f"invalid JSON number: {value}")


def evidence_age_valid(observed_us: int, now_us: int, maximum_age_us: int) -> bool:
    """Zero disables age expiry only; callers still check provenance/binding."""
    return observed_us <= now_us and (maximum_age_us == 0 or now_us - observed_us < maximum_age_us)


def evidence_age_deadline(observed_us: int, maximum_age_us: int) -> int:
    return min(2**64 - 1, observed_us + maximum_age_us) if maximum_age_us else 2**64 - 1


def wire_crc(data: bytes) -> int:
    """Check retained read-only fixture bytes independently of firmware parsing."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def _reply_status(raw: bytes, received: int, address: int, function: int,
                  normal_length: int, echo: tuple[int, int] | None = None) -> tuple[str, int, int]:
    """Check retained reply bytes in the public codec's bounded validation order."""
    if received < 5 or received > normal_length:
        error = 2
    elif raw[0] != address:
        error = 3
    elif raw[1] not in (function, function | 0x80):
        error = 4
    elif received != (5 if raw[1] == (function | 0x80) else normal_length):
        error = 2
    elif function == 3 and raw[1] == function and raw[2] != normal_length - 5:
        error = 5
    elif wire_crc(raw) != 0:
        error = 6
    elif raw[1] == (function | 0x80):
        return "EXCEPTION", raw[2], 10
    elif echo is not None and (int.from_bytes(raw[2:4], "big"), int.from_bytes(raw[4:6], "big")) != echo:
        error = 7
    else:
        return "OK", 0, 0
    return ("CRC_ERROR" if error == 6 else "FRAME_ERROR"), error, error


class Evidence:
    """Stream evidence to an exclusive new file; do not retain a growing list."""

    def __init__(self, stream, clock: Callable[[], float] = time.monotonic, *,
                 max_records: int = 200000, max_bytes: int = 64 * 1024 * 1024,
                 max_record_bytes: int = 1024 * 1024):
        if (type(max_records) is not int or max_records < 2 or
                type(max_bytes) is not int or max_bytes < 512 or
                type(max_record_bytes) is not int or max_record_bytes < 256):
            raise ValueError("evidence requires finite record/byte capacities")
        self.stream = stream
        self.clock = clock
        self.started = clock()
        self.max_records, self.max_bytes = max_records, max_bytes
        self.max_record_bytes = max_record_bytes
        self.records = self.bytes = 0
        self.exhausted = False

    def _write(self, line: str) -> None:
        try:
            if self.stream.write(line) != len(line):
                raise BenchError("short evidence write; no further work permitted")
            self.stream.flush()
        except BaseException:
            # This can fail outside Console.begin/wait (for example while
            # streaming a campaign summary). Latch it before cleanup can send
            # another command into an experiment whose evidence was lost.
            self.exhausted = True
            raise

    def __call__(self, event: str, **fields) -> None:
        if self.exhausted:
            raise BenchError("evidence capacity exhausted; new work must stop")
        record = {
            "event": event,
            "utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "elapsed_s": round(self.clock() - self.started, 6),
            **fields,
        }
        line = json.dumps(record, ensure_ascii=True, allow_nan=False) + "\n"
        # Reserve one small terminal record. Do not silently drop experiment
        # evidence or continue issuing writes after its finite budget is full.
        if (self.records >= self.max_records - 1 or len(line) > self.max_record_bytes or
                self.bytes + len(line) > self.max_bytes - 256):
            self.exhausted = True
            limit = json.dumps(dict(event="evidence_limit", ok=False,
                records=self.records, bytes=self.bytes, rejected_event=event[:64])) + "\n"
            self._write(limit)
            self.records += 1
            self.bytes += len(limit)
            raise BenchError("evidence capacity exhausted; new work must stop")
        self._write(line)
        self.records += 1
        self.bytes += len(line)


def move_arguments(kind: str, arguments: tuple[str, ...]) -> dict:
    """Validate the finite console grammar; conversions stay in the public API."""
    if (kind not in ("relative", "absolute", "angle") or not isinstance(arguments, tuple) or
            not 5 <= len(arguments) <= 14 or any(type(token) is not str or not token or
            any(ord(char) < 33 or ord(char) > 126 for char in token) for token in arguments)):
        raise ValueError("move requires bounded ASCII coordinate and ramp tokens")
    value, unit, frame = arguments[:3]
    if unit not in ("steps", "fullsteps", "counts", "turn", "deg", "rad", "mm") or frame not in ("native", "motor", "load"):
        raise ValueError("move unit or frame is invalid")
    result = dict(value=value, unit=unit, frame=frame, path=2, tie=0, rounding=0,
                  error="0", approximate=False, approximation_error="0", basis=0)
    index = 3
    if kind == "angle":
        if unit not in ("turn", "deg", "rad") or frame == "native":
            raise ValueError("wrapped orientation requires a motor or load angular unit")
        if len(arguments) < 7 or arguments[index] not in ("positive", "negative", "shortest") or arguments[index + 1] not in ("reject", "positive", "negative"):
            raise ValueError("angle requires explicit path and half-turn tie")
        result["path"] = ("positive", "negative", "shortest").index(arguments[index])
        result["tie"] = ("reject", "positive", "negative").index(arguments[index + 1])
        index += 2
    result.update(speed=arguments[index], ramp=arguments[index + 1])
    if result["ramp"] != "configured":
        raise ValueError("move requires configured ramp")
    index += 2
    if index < len(arguments) and arguments[index] == "basis":
        if kind != "relative" or index + 1 >= len(arguments) or arguments[index + 1] not in ("actual", "commanded", "queued"):
            raise ValueError("relative basis is invalid")
        result["basis"] = ("actual", "commanded", "queued").index(arguments[index + 1])
        index += 2
    if index < len(arguments) and arguments[index] == "round":
        if index + 2 >= len(arguments) or arguments[index + 1] not in ("exact", "nearest", "zero", "floor", "ceil"):
            raise ValueError("move rounding policy is invalid")
        result["rounding"] = ("exact", "nearest", "zero", "floor", "ceil").index(arguments[index + 1])
        result["error"] = arguments[index + 2]
        index += 3
        if index < len(arguments) and arguments[index] == "approx":
            if unit != "rad" or index + 1 >= len(arguments):
                raise ValueError("approximation allowance requires radians")
            result.update(approximate=True, approximation_error=arguments[index + 1])
            index += 2
    if index != len(arguments):
        raise ValueError("move arguments exceed the explicit grammar")
    for key in ("value", "speed", "error", "approximation_error"):
        token = result[key]
        if re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]+)?|[0-9]+/[0-9]+)", token) is None:
            raise ValueError("move numbers require exact integer, decimal or fraction syntax")
        try:
            number = Fraction(token)
        except (ValueError, ZeroDivisionError) as exc:
            raise ValueError("move exact number is invalid") from exc
        if not -2**63 <= number.numerator < 2**63 or not 1 <= number.denominator < 2**64:
            raise ValueError("move exact number exceeds installed API storage")
        if key == "speed" and (number.denominator != 1 or not 1 <= number <= 65535):
            raise ValueError("native rpm requires a positive integer")
        if key in ("error", "approximation_error") and number < 0:
            raise ValueError("numerical allowances must be nonnegative")
    if result["approximate"] and Fraction(result["approximation_error"]) <= 0:
        raise ValueError("radians require a positive approximation allowance")
    return result


def velocity_arguments(arguments: tuple[str, ...]) -> dict:
    """Exact finite signed-velocity grammar; conversion belongs to the core API."""
    if (not isinstance(arguments, tuple) or not 6 <= len(arguments) <= 11 or
            any(type(token) is not str or not token or any(ord(c) < 33 or ord(c) > 126 for c in token) for token in arguments)):
        raise ValueError("velocity requires bounded ASCII tokens")
    value, unit, frame, duration, ramp, stop = arguments[:6]
    units = {"rpm": (3, 1), "steps/s": (0, 0), "fullsteps/s": (1, 0), "counts/s": (2, 0),
             "turn/s": (3, 0), "turns/s": (3, 0), "deg/s": (4, 0), "rad/s": (5, 0), "mm/s": (6, 0)}
    if unit not in units or frame not in ("native", "motor", "load") or ramp != "configured" or stop not in ("normal", "direct"):
        raise ValueError("velocity unit, frame, ramp or stop is invalid")
    if re.fullmatch(r"[0-9]+", duration) is None or not 1 <= int(duration) <= 1000:
        raise ValueError("velocity duration must be within 1..1000 ms")
    result = dict(value=value, unit=units[unit], frame=("native", "motor", "load").index(frame), duration_us=int(duration) * 1000,
                  stop=stop, rounding=0, error="0", approximate=False, approximation_error="0")
    index = 6
    if index < len(arguments) and arguments[index] == "round":
        if index + 2 >= len(arguments) or arguments[index + 1] not in ("exact", "nearest", "zero", "floor", "ceil"):
            raise ValueError("velocity rounding policy is invalid")
        result.update(rounding=("exact", "nearest", "zero", "floor", "ceil").index(arguments[index + 1]), error=arguments[index + 2]); index += 3
        if index < len(arguments) and arguments[index] == "approx":
            if unit != "rad/s" or index + 1 >= len(arguments): raise ValueError("velocity approximation requires radians")
            result.update(approximate=True, approximation_error=arguments[index + 1]); index += 2
    if index != len(arguments): raise ValueError("velocity arguments exceed explicit grammar")
    for key in ("value", "error", "approximation_error"):
        token = result[key]
        if re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]+)?|[0-9]+/[0-9]+)", token) is None:
            raise ValueError("velocity numbers require exact syntax")
        try: number = Fraction(token)
        except (ValueError, ZeroDivisionError) as exc: raise ValueError("velocity number is invalid") from exc
        if not -2**63 <= number.numerator < 2**63 or not 1 <= number.denominator < 2**64:
            raise ValueError("velocity number exceeds installed API storage")
        if key != "value" and number < 0: raise ValueError("velocity error allowance must be nonnegative")
    if result["approximate"] and Fraction(result["approximation_error"]) <= 0:
        raise ValueError("velocity radians require positive approximation allowance")
    return result


class Command:
    """One bounded Console.begin/wait handle; IDs are host correlation only."""

    def __init__(self, owner, request_id, command, started, deadline, address, load, operation_id):
        self.owner = owner
        self.id = request_id
        self.command = command
        self.started = started
        self.deadline = deadline
        self.address = address
        self.load = load
        self.operation_id = operation_id
        self.accepted = False
        self.terminal = None
        self.input_bytes = 0
        self.traffic_bytes = 0
        self.released = False
        self.stop_policy = None
        self.move_args = None
        self.velocity_args = None
        self.driver_args = None
        self.home_args = None
        self.host_args = None
        self.serial = None


class Console:
    """One cooperative owner, bounded interleaved commands, strict correlation.

    The port must supply nonblocking ``read(size)`` and a bounded ``write``.
    Once command framing fails this object cannot issue another command. Open
    a fresh, explicitly inspected session instead of retrying an uncertain one.
    ``on_event`` receives bounded raw lines, commands and decoded responses.
    """

    def __init__(
        self,
        port,
        *,
        on_event: Callable[..., None] | None = None,
        clock: Callable[[], float] = time.monotonic,
        sleeper: Callable[[float], None] = time.sleep,
    ):
        self.port = port
        self.emit = on_event or (lambda *_args, **_kwargs: None)
        self.clock = clock
        self.sleep = sleeper
        self.buffer = bytearray()
        self.next_id = 1
        self.synchronized = True
        self.identified = False
        self.uptime_ms: int | None = None
        self.pending: dict[int, Command] = {}
        self.operations: dict[int, Command] = {}
        self.last_operation_id = 0
        self.serial = None
        self.communication = None
        self.persistence = None
        self.persistence_invocations = 0
        self.communication_candidate = None
        self.motion_profile = None
        self.debug_mode = None
        self.debug_snapshot = None
        self.discovery = None
        self.traffic_sequence = 0

    def _lines(self, data: bytes) -> list[bytes]:
        self.buffer.extend(data)
        lines = []
        while b"\n" in self.buffer:
            offset = self.buffer.index(b"\n")
            if offset > MAX_LINE:
                raise BenchError("console line exceeds input limit")
            lines.append(bytes(self.buffer[:offset]).rstrip(b"\r"))
            del self.buffer[:offset + 1]
        if len(self.buffer) > MAX_LINE:
            raise BenchError("unterminated console line exceeds input limit")
        return lines

    def _decode(self, raw: bytes, *, startup: bool = False) -> dict | None:
        self.emit("line", text=raw.decode("utf-8", errors="backslashreplace"))
        try:
            text = raw.decode("utf-8")
        except UnicodeError as exc:
            raise BenchError("console returned invalid UTF-8") from exc
        if FAULT_TEXT.search(text) and not startup:
            raise BenchError("console reported reset, panic or watchdog fault")
        if not text.lstrip().startswith("{"):
            return None
        try:
            item = json.loads(
                text, object_pairs_hook=unique_object, parse_constant=invalid_constant
            )
        except (ValueError, RecursionError) as exc:
            raise BenchError("malformed JSON console line") from exc
        if not isinstance(item, dict):
            raise BenchError("console JSON must be an object")
        if item.get("type") == "boot" and startup:
            return None
        return item

    def _read(self) -> bytes:
        data = self.port.read(256)
        if not isinstance(data, bytes) or len(data) > 256:
            raise BenchError("serial read violated its byte limit")
        return data

    def drain_startup(self, duration_s: float) -> None:
        """Collect bounded boot diagnostics before identifying the console."""
        positive(duration_s, "startup duration")
        if not self.synchronized or self.next_id != 1:
            raise BenchError("startup drain is only allowed before the first command")
        total = 0
        deadline = self.clock() + duration_s
        try:
            while self.clock() < deadline:
                data = self._read()
                total += len(data)
                if total > MAX_INPUT:
                    raise BenchError("startup diagnostics exceed input limit")
                for raw in self._lines(data):
                    item = self._decode(raw, startup=True)
                    if item is not None:
                        if item.get("type") == "traffic":
                            self._check_traffic(item)
                        else:
                            raise BenchError("unexpected structured reply during startup")
                if not data:
                    self.sleep(0.005)
            if self.buffer and not self._diagnostic_line_pending():
                raise BenchError("startup ended with an incomplete line")
        except BaseException:
            self.synchronized = False
            raise

    def _diagnostic_line_pending(self) -> bool:
        if self._load_line_pending():
            return True
        compact = re.sub(rb"\s+", b"", bytes(self.buffer))
        prefix = b'{"type":"traffic"'
        return bool(compact) and (compact.startswith(prefix + b",") or
            ((self.debug_mode in ("raw", "decoded") or self.traffic_sequence > 0) and prefix.startswith(compact)))

    def _load_line_pending(self) -> bool:
        """Recognize the bounded load fixture text stream."""
        prefix = b"# load "
        return bool(self.buffer) and (prefix.startswith(self.buffer)
                                      or self.buffer.startswith(prefix))

    def _check_deadlines(self) -> None:
        for handle in self.pending.values():
            if handle.terminal is None and self.clock() >= handle.deadline:
                raise BenchError("command response deadline expired; command was not replayed")

    @staticmethod
    def _operation_id(value) -> bool:
        return type(value) is int and 1 <= value <= 0xFFFFFFFF

    @staticmethod
    def _check_probe(item: dict, address: int | None) -> None:
        if (item.get("capture_read", False) is not False or item.get("recovery", False) is not False
                or item.get("read_kind") is not None):
            raise BenchError("probe result kind is inconsistent")
        model = item.get("raw_model")
        if item["ok"] and (type(model) is not int or not 0 <= model <= 65535):
            raise BenchError("successful probe lacks consistent result evidence")
        if item["ok"] or "confidence" in item:
            confidence = ("responder_model_unresolved" if item["ok"] else "responder_only"
                          if item.get("transport") == "FRAME" and item.get("codec") == "EXCEPTION" else "none")
            if (item.get("confidence") != confidence or any(item.get(key) is not False for key in
                    ("manufacturer_confirmed", "exact_model_confirmed", "collision_excluded"))):
                raise BenchError("probe confidence overstates checked identity evidence")
        Console._check_read_evidence(item, address, 7)
        if not item["ok"]:
            return
        try:
            frames = []
            for name, size in (("tx_hex", 8), ("rx_hex", 7)):
                value = item.get(name)
                if (type(value) is not str or
                        re.fullmatch(r"[0-9A-Fa-f]{%d}" % (2 * size), value) is None):
                    raise ValueError("invalid raw frame")
                frames.append(bytes.fromhex(value))
            tx, rx = frames
            if (tx[:6] != bytes((item["address"], 3, 0, 0, 0, 1)) or wire_crc(tx) != 0 or
                    _reply_status(rx, len(rx), item["address"], 3, 7) != ("OK", 0, 0) or
                    int.from_bytes(rx[3:5], "big") != model or
                    type(item.get("register_start")) is not int or item["register_start"] != 0 or
                    type(item.get("register_count")) is not int or item["register_count"] != 1 or
                    item.get("identity") != "responder_only"):
                raise ValueError("wrong probe request, reply or decoded model")
        except ValueError as exc:
            raise BenchError("probe raw frame evidence is inconsistent") from exc

    @staticmethod
    def _check_read_evidence(item: dict, address: int | None, reply_size: int) -> None:
        actual = item.get("address")
        if (type(actual) is not int or not 1 <= actual <= 247
                or (address is not None and actual != address)):
            raise BenchError("probe terminal address does not match acceptance")
        if not item["ok"]:
            return
        if (item.get("transport") != "FRAME" or item.get("codec") != "OK"
                or type(item.get("detail")) is not int or item["detail"] != 0
                or type(item.get("frame_error")) is not int or item["frame_error"] != 0
                or item.get("outcome") != "success" or item.get("execution_unknown") is not False
                or type(item.get("tx_bytes")) is not int or item["tx_bytes"] != 8
                or type(item.get("rx_bytes")) is not int or item["rx_bytes"] != reply_size
                or type(item.get("duration_us")) is not int
                or not 0 <= item["duration_us"] <= 0xFFFFFFFFFFFFFFFF
                or item.get("timing_valid") is not True
                or item.get("raw_truncated") is not False):
            raise BenchError("successful probe lacks consistent result evidence")
        check_counts(item, ("observed_earliest_us", "observed_latest_us", "delivered_us"), "probe")
        if not item["observed_earliest_us"] <= item["observed_latest_us"] <= item["delivered_us"]:
            raise BenchError("probe observation and delivery bounds are inconsistent")

    @staticmethod
    def _check_capture_read(item: dict, address: int | None) -> None:
        if (item.get("capture_read") is not True
                or item.get("recovery", False) is not False
                or item.get("read_kind") is not None
                or type(item.get("register_start")) is not int or item["register_start"] != 0x0130
                or type(item.get("register_count")) is not int or item["register_count"] != 16
                or "raw_model" not in item or item["raw_model"] is not None
                or item.get("identity") != "not_requested"):
            raise BenchError("capture-read result kind/window is inconsistent")
        Console._check_read_evidence(item, address, 37)
        if not item["ok"]:
            return
        try:
            raw = []
            for name, size in (("tx_hex", 8), ("rx_hex", 37)):
                value = item.get(name)
                if not isinstance(value, str) or re.fullmatch(r"[0-9A-Fa-f]{%d}" % (2 * size), value) is None:
                    raise ValueError("invalid raw frame")
                frame = bytes.fromhex(value)
                if wire_crc(frame) != 0:
                    raise ValueError("invalid CRC")
                raw.append(frame)
            tx, rx = raw
            if tx[:6] != bytes((item["address"], 3, 1, 0x30, 0, 16)) or rx[:3] != bytes((item["address"], 3, 32)):
                raise ValueError("wrong request/reply shape")
        except ValueError as exc:
            raise BenchError("capture-read raw frame evidence is inconsistent") from exc

    @staticmethod
    def _check_velocity(item: dict, address: int | None, arguments: tuple[str, ...] | None) -> None:
        """Retain start uncertainty separately from a checked, finite stop report."""
        def require(condition, message):
            if not condition: raise BenchError("velocity " + message)
        def integer(value, low=0, high=2**64 - 1): return type(value) is int and low <= value <= high
        require(item.get("velocity") is True and item.get("move_kind") is None and item.get("action_kind") is None and
                item.get("read_kind") is None and not item.get("recovery", False) and not item.get("capture_read", False), "kind differs")
        require(integer(item.get("address"), 1, 247) and (address is None or item["address"] == address) and
                all(Console._operation_id(item.get(key)) for key in ("target", "generation", "configuration_generation")), "binding differs")
        require(item.get("state") == ("succeeded" if item["ok"] else "failed") and
                item.get("completion") in ("observed", "not_observed") and item.get("outcome") in
                ("observed", "reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified", "unconfirmed_response", "observation_limit"), "state differs")
        require(item.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"} and
                integer(item.get("detail"), -2**31, 2**31 - 1) and
                (item["ok"] or (item["outcome"] not in ("none", "observed") and item["status"] != "OK")), "status differs")
        require(all(integer(item.get(k)) for k in ("started_us", "deadline_us", "stop_due_us", "serviced_us", "polls", "phase")) and
                item["started_us"] < item["stop_due_us"] < item["deadline_us"] and item["serviced_us"] >= item["started_us"] and
                item["polls"] <= 64 and item["phase"] <= 3, "time bounds differ")
        for key in ("staging_applied", "uncertain", "needs_stop", "running_observed", "service_missed", "observation_known", "interrupted_by_stop", "exact_arithmetic"):
            require(type(item.get(key)) is bool, "missing " + key)
        require(integer(item.get("native_rpm"), -3000, 3000) and item["native_rpm"] != 0 and item.get("ramp") == "configured" and
                item.get("stop_policy") in ("normal", "direct"), "speed or stop policy differs")
        words = item.get("staging_words")
        require(isinstance(words, list) and len(words) == 3 and words[0] == (item["native_rpm"] & 65535) and
                all(integer(word, 0, 2000) for word in words[1:]), "staged words differ")
        requested = item.get("requested")
        require(isinstance(requested, dict) and integer(requested.get("numerator"), -2**63, 2**63 - 1) and
                integer(requested.get("denominator"), 1) and integer(requested.get("position_unit"), 0, 6) and
                integer(requested.get("time_unit"), 0, 2) and integer(requested.get("frame"), 0, 2) and integer(requested.get("rounding"), 0, 4) and
                integer(requested.get("duration_us"), 1000, 1000000) and item["stop_due_us"] - item["started_us"] == requested["duration_us"] and
                type(requested.get("approximate")) is bool, "retained request differs")
        for key in ("rounding_error", "approximation_error_bound", "requested_rpm_approximate"):
            require(type(item.get(key)) in (int, float) and math.isfinite(item[key]), "invalid " + key)
        for key in ("maximum_quantization_error_rpm", "maximum_approximation_error_rpm"):
            require(type(requested.get(key)) in (int, float) and math.isfinite(requested[key]) and requested[key] >= 0, "invalid allowance")
        require(abs(item["rounding_error"]) + item["approximation_error_bound"] <= requested["maximum_quantization_error_rpm"] and
                item["approximation_error_bound"] >= 0 and item["approximation_error_bound"] <= requested["maximum_approximation_error_rpm"], "numerical allowance exceeded")
        if item["exact_arithmetic"]:
            require(requested["position_unit"] != 5 and item["approximation_error_bound"] == 0 and
                    item["requested_rpm_approximate"] == 0, "exact preparation contains approximate provenance")
        else:
            require(requested["position_unit"] == 5 and requested["approximate"] and requested["rounding"] != 0 and
                    requested["maximum_approximation_error_rpm"] > 0, "approximate preparation lost radian provenance")
        if requested["rounding"] == 0:
            require(item["exact_arithmetic"] and item["rounding_error"] == 0, "EXACT preparation rounded velocity")
        if arguments is not None:
            parsed = velocity_arguments(arguments)
            def retained_allowance(text, value):
                exact = Fraction(text)
                return Fraction(value) <= exact and (Fraction(value) == exact or exact < Fraction(math.nextafter(value, math.inf)))
            require(Fraction(parsed["value"]) == Fraction(requested["numerator"], requested["denominator"]) and
                    parsed["unit"] == (requested["position_unit"], requested["time_unit"]) and parsed["frame"] == requested["frame"] and
                    parsed["duration_us"] == requested["duration_us"] and parsed["stop"] == item["stop_policy"] and parsed["rounding"] == requested["rounding"] and
                    parsed["approximate"] == requested["approximate"] and retained_allowance(parsed["error"], requested["maximum_quantization_error_rpm"]) and
                    retained_allowance(parsed["approximation_error"], requested["maximum_approximation_error_rpm"]), "request correlation differs")
        evidence = {}
        for name, token, size in (("staging_evidence", 0, 15), ("trigger_evidence", 1, 8), ("activity_evidence", None, 8),
                ("failure_evidence", None, None), ("stop_write_evidence", 0, 8), ("stop_observation", None, 8), ("stop_failure_evidence", None, 8)):
            entry = item.get(name)
            require(isinstance(entry, dict) and all(integer(entry.get(k), 0, n) for k, n in (("step", 255), ("event", 3),
                    ("received_length", 2**32 - 1), ("tx_accepted", 15), ("frame_error", 255))) and
                    all(type(entry.get(k)) is bool for k in ("tx_complete", "response_confirmed", "qualified", "execution_unknown")) and
                    all(integer(entry.get(k)) for k in ("earliest_us", "latest_us", "delivered_us")) and
                    all(integer(entry.get(k), -2**31, 2**31 - 1) for k in ("detail", "transport_detail")) and
                    entry.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"}, name + " invalid")
            raw = entry.get("raw_hex")
            require(isinstance(raw, str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,9}", raw) is not None, name + " raw invalid")
            raw = bytes.fromhex(raw); require(len(raw) == min(entry["received_length"], 9), name + " length differs")
            empty = all(entry.get(key) == value for key, value in dict(step=0, event=0, raw_hex="", received_length=0,
                tx_accepted=0, tx_complete=False, response_confirmed=False, qualified=False, execution_unknown=False,
                earliest_us=0, latest_us=0, delivered_us=0, transport_detail=0, status="OK", detail=0, frame_error=0).items())
            if not empty:
                require(token is None or entry["step"] == token, name + " step differs")
                require(item["started_us"] <= entry["delivered_us"] <= item["serviced_us"] and entry["tx_accepted"] <= (size or (15 if entry["step"] == 0 else 8)) and
                        (not entry["tx_complete"] or entry["tx_accepted"] == (size or (15 if entry["step"] == 0 else 8))), name + " TX differs")
                require(entry["event"] == 0 or not entry["response_confirmed"], name + " local response")
            if entry["qualified"]:
                require(not empty and entry["event"] == 0 and entry["tx_complete"] and
                        item["started_us"] <= entry["earliest_us"] <= entry["latest_us"] <= entry["delivered_us"], name + " closure differs")
            else: require(entry["earliest_us"] == entry["latest_us"] == 0, name + " unqualified timing")
            evidence[name] = (entry, raw, empty)
        def confirmed(name, prefix, size):
            e, raw, empty = evidence[name]
            require(not empty and e["qualified"] and e["response_confirmed"] and e["tx_complete"] and e["status"] == "OK" and
                    e["detail"] == e["frame_error"] == 0 and len(raw) == size and raw.startswith(bytes(prefix)) and wire_crc(raw) == 0,
                    name + " checked response absent")
            return e, raw
        for field, name, prefix in (("setup_execution", "staging_evidence", (item["address"], 16, 0, 0x1D, 0, 3)),
                ("execution", "trigger_evidence", (item["address"], 6, 0, 0x27, 0, 2)),
                ("stop_execution", "stop_write_evidence", (item["address"], 6, 0, 0x27, 1 if item["stop_policy"] == "normal" else 2, 0))):
            execution = item.get(field); e, raw, empty = evidence[name]
            require(execution in ("not_transmitted", "acknowledged", "rejected", "unknown"), field + " invalid")
            if execution == "acknowledged": confirmed(name, prefix, 8)
            elif execution == "not_transmitted": require(e["tx_accepted"] == 0 and not e["execution_unknown"], name + " transmission contradiction")
            elif execution == "unknown":
                require(not empty and (e["tx_accepted"] > 0 or e["execution_unknown"]) and not
                        (e["event"] == 0 and e["qualified"] and e["response_confirmed"] and
                         (e["status"] == "OK" or (e["status"] == "EXCEPTION" and 1 <= e["detail"] <= 7))), name + " uncertainty absent or contradicts checked reply")
            else:
                require(not empty and e["qualified"] and e["response_confirmed"] and e["status"] == "EXCEPTION" and e["frame_error"] == 10 and
                        len(raw) == 5 and raw[:2] == bytes((item["address"], prefix[1] | 128)) and 1 <= raw[2] <= 7 and e["detail"] == raw[2] and wire_crc(raw) == 0,
                        name + " rejection absent")
        require(item["staging_applied"] == (item["setup_execution"] == "acknowledged"), "staging validity differs")
        if item["execution"] != "not_transmitted": require(item["staging_applied"], "start without applied setup")
        if not evidence["trigger_evidence"][2]:
            require(evidence["trigger_evidence"][0]["delivered_us"] >= evidence["staging_evidence"][0]["delivered_us"], "trigger precedes setup")
        if not evidence["stop_write_evidence"][2]:
            require(item["execution"] in ("acknowledged", "unknown") and
                    evidence["stop_write_evidence"][0]["delivered_us"] >= evidence["trigger_evidence"][0]["delivered_us"] and
                    (not evidence["stop_write_evidence"][0]["qualified"] or
                     evidence["stop_write_evidence"][0]["earliest_us"] >= evidence["trigger_evidence"][0]["delivered_us"]),
                    "stop precedes possibly executed trigger")
        for name in ("staging_evidence", "trigger_evidence"):
            if item["ok"]:
                require(evidence[name][0]["latest_us"] <= item["stop_due_us"], name + " exceeds finite run deadline")
        if item["running_observed"]:
            activity, raw = confirmed("activity_evidence", (item["address"], 3, 4), 9)
            require(item["execution"] == "acknowledged" and raw[3:5] == b"\0\0" and int.from_bytes(raw[5:7], "big") & 4 and
                    not int.from_bytes(raw[5:7], "big") & 0x78 and 2 <= activity["step"] <= item["polls"] + 1 and
                    activity["earliest_us"] > evidence["trigger_evidence"][0]["delivered_us"] and activity["latest_us"] <= item["stop_due_us"], "running report differs")
        else:
            require(evidence["activity_evidence"][2], "unobserved activity retains a report")
        latest = item.get("last_observation")
        require(isinstance(latest, dict) and integer(latest.get("step"), 0, 255) and
                all(integer(latest.get(key)) for key in ("earliest_us", "latest_us", "delivered_us")) and
                isinstance(latest.get("raw_hex"), str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,9}", latest["raw_hex"]) is not None, "latest observation invalid")
        if item["observation_known"]:
            raw = bytes.fromhex(latest["raw_hex"])
            require(len(raw) == 9 and raw[:3] == bytes((item["address"], 3, 4)) and wire_crc(raw) == 0 and
                    item["execution"] == "acknowledged" and item["polls"] >= 1 and latest["step"] == item["polls"] + 1 and
                    latest["earliest_us"] > evidence["trigger_evidence"][0]["delivered_us"] and
                    item["started_us"] <= latest["earliest_us"] <= latest["latest_us"] <= min(latest["delivered_us"], item["stop_due_us"]) and
                    latest["delivered_us"] <= item["serviced_us"] and item.get("raw_alarm") == int.from_bytes(raw[3:5], "big") and
                    item.get("raw_motion") == int.from_bytes(raw[5:7], "big"), "observation differs")
            if item["running_observed"]:
                activity = evidence["activity_evidence"][0]
                require(latest["step"] >= activity["step"] and latest["latest_us"] >= activity["latest_us"] and
                        latest["delivered_us"] >= activity["delivered_us"], "latest observation precedes activity")
                if latest["step"] == activity["step"]:
                    require(all(latest[key] == activity[key] for key in latest), "same observation token changed retained evidence")
        else:
            require(item.get("raw_alarm") is None and item.get("raw_motion") is None and latest ==
                    dict(step=0, raw_hex="", earliest_us=0, latest_us=0, delivered_us=0), "unknown observation published")
        require(item.get("stop_completion") in ("observed", "not_observed") and item.get("stop_outcome") in
                ("none", "observed", "reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified", "unconfirmed_response", "observation_limit") and
                (item["stop_completion"] == "observed") == (item["stop_outcome"] == "observed") and
                (item["completion"] == "observed") == (item["stop_completion"] == "observed"), "stop completion invalid")
        require(item["uncertain"] == (not item["ok"] and any(
                    evidence[name][0]["tx_accepted"] or evidence[name][0]["execution_unknown"]
                    for name in ("staging_evidence", "trigger_evidence"))), "uncertainty differs from setup/start evidence")
        require(item["needs_stop"] == (item["execution"] in ("acknowledged", "unknown") and
                    item["stop_completion"] != "observed"), "stop obligation differs from possible start execution")
        if item["stop_completion"] == "observed":
            observation, raw = confirmed("stop_observation", (item["address"], 3, 4), 9)
            require(not int.from_bytes(raw[5:7], "big") & 4 and not item["needs_stop"] and item["stop_execution"] == "acknowledged" and
                    1 <= observation["step"] <= 64 and
                    observation["earliest_us"] > evidence["stop_write_evidence"][0]["delivered_us"] and observation["latest_us"] <= item["deadline_us"] and
                    evidence["stop_write_evidence"][0]["latest_us"] <= item["deadline_us"], "stop observation differs")
        if item["ok"]:
            require(item["outcome"] == "observed" and item["status"] == "OK" and item["detail"] == 0 and item["running_observed"] and
                    item["execution"] == "acknowledged" and item["stop_completion"] == "observed" and item["completion"] == "observed" and not item["uncertain"] and
                    not item["service_missed"] and not item["interrupted_by_stop"], "success lacks complete evidence")
            require(evidence["failure_evidence"][2] and evidence["stop_failure_evidence"][2], "success retains failure evidence")
        elif evidence["failure_evidence"][2]:
            # Time service and an absent activity report have no transaction to
            # copy. A stop failure has its own independently retained evidence.
            stop_failed = (item["outcome"] == item["stop_outcome"] and
                           not evidence["stop_failure_evidence"][2])
            local_deadline = item["outcome"] == "deadline" and item["status"] == "ILLEGAL_VALUE" and (
                item["serviced_us"] >= item["deadline_us"] or item["service_missed"] or
                (item["detail"] == 17 and item["phase"] < 2 and item["serviced_us"] >= item["stop_due_us"]) or
                (item["detail"] == 10 and item["phase"] < 2))
            absent_activity = (item["outcome"] == "observation_limit" and item["detail"] == 25 and
                               not item["running_observed"] and item["stop_completion"] == "observed")
            require(stop_failed or local_deadline or absent_activity, "failure lacks terminal evidence")

    @staticmethod
    def _check_move(item: dict, address: int | None, arguments: tuple[str, ...] | None, kind: str = "relative") -> None:
        """Check staged/triggered execution and fresh running-to-arrival evidence."""
        def require(condition, text):
            if not condition:
                raise BenchError("move " + text)
        def integer(value, low=0, high=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and low <= value <= high
        require(item.get("move_kind") == kind and item.get("action_kind") is None and
                item.get("read_kind") is None and item.get("capture_read") is False and item.get("recovery") is False,
                "kind is inconsistent")
        require(integer(item.get("address"), 1, 247) and (address is None or item["address"] == address) and
                all(Console._operation_id(item.get(key)) for key in ("target", "generation", "configuration_generation")),
                "target binding is inconsistent")
        require(item.get("state") == ("succeeded" if item["ok"] else "failed") and
                item.get("completion") == ("observed" if item["ok"] else "not_observed") and
                item.get("outcome") in ({"observed"} if item["ok"] else {"reply_error", "transport_error", "cancelled", "deadline",
                    "timing_unqualified", "unconfirmed_response", "observation_limit"}), "terminal state is inconsistent")
        require(all(integer(item.get(key)) for key in ("started_us", "deadline_us", "serviced_us", "polls")) and
                item["started_us"] < item["deadline_us"] and item["serviced_us"] >= item["started_us"] and item["polls"] <= 64,
                "time or observation bounds are inconsistent")
        require(item.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"} and
                integer(item.get("detail"), -2**31, 2**31 - 1) and (not item["ok"] or (item["status"] == "OK" and item["detail"] == 0)),
                "status is inconsistent")
        for key in ("staging_applied", "uncertain", "running_observed", "observation_known", "interrupted_by_stop",
                    "endpoint_known", "zero_displacement", "exact_arithmetic"):
            require(type(item.get(key)) is bool, "missing " + key)
        prerequisites = item.get("prerequisites")
        require(isinstance(prerequisites, dict) and all(prerequisites.get(key) == item[key] for key in
                ("target", "generation", "configuration_generation")), "qualification binding differs")
        displacement_known = item.get("displacement_known", True)
        require(type(displacement_known) is bool, "invalid displacement validity")
        require(integer(item.get("effective_native"), -2**31, 2**31 - 1) and
                integer(item.get("displacement_native"), -2**63, 2**63 - 1) and
                (kind != "relative" or item["displacement_native"] == item["effective_native"]) and item["zero_displacement"] is False and
                integer(item.get("endpoint_native"), -2**63, 2**63 - 1), "effective target is invalid")
        if not displacement_known:
            require(kind == "absolute" and item.get("displacement_known") is False and item["displacement_native"] == 0 and
                    item["effective_native"] == item["endpoint_native"] and item["endpoint_known"],
                    "absolute target publishes an inferred displacement")
        else:
            require(displacement_known and item["displacement_native"] != 0, "displacement is absent or zero")
        require(integer(item.get("native_rpm"), 1, 3000) and item.get("ramp") == "configured", "speed or ramp is invalid")
        words = item.get("staging_words")
        require(isinstance(words, list) and len(words) == 5 and all(integer(word, 0, 65535) for word in words) and
                words[0] <= 2000 and words[1] <= 2000 and words[2] == item["native_rpm"], "staged parameter words are inconsistent")
        encoded = item["effective_native"] & 0xFFFFFFFF
        require(words[3:] in ([encoded >> 16, encoded & 65535], [encoded & 65535, encoded >> 16]), "staged target differs from effective target")
        require(all(prerequisites.get(key) is True for key in ("command_units_verified", "configured_ramp_verified",
                "serial_inputs_permit", "readiness_qualified", "word_order_known", "start_speed_known")) and
                type(prerequisites.get("negative_encoding_verified")) is bool and
                (item["effective_native"] >= 0 or prerequisites["negative_encoding_verified"]) and
                (kind != "relative" or prerequisites.get("relative_basis_verified") is True), "qualification is missing")
        require(integer(prerequisites.get("observed_us")) and integer(prerequisites.get("maximum_age_us")) and
                evidence_age_valid(prerequisites["observed_us"], item["started_us"], prerequisites["maximum_age_us"]) and
                prerequisites.get("raw_alarm") == 0 and integer(prerequisites.get("raw_motion"), 0, 65535) and
                not prerequisites["raw_motion"] & 0x7C and integer(prerequisites.get("word_order"), 0, 1) and
                integer(prerequisites.get("start_speed"), 0, item["native_rpm"]), "readiness or configured speed evidence is inconsistent")
        write_deadline = min(item["deadline_us"],
                             evidence_age_deadline(prerequisites["observed_us"], prerequisites["maximum_age_us"]))
        reference = item.get("reference")
        require(isinstance(reference, dict) and type(reference.get("native_known")) is bool and
                all(integer(reference.get(key), 0, 0xFFFFFFFF) for key in ("target", "generation", "configuration_generation")) and
                integer(reference.get("native_position"), -2**63, 2**63 - 1) and integer(reference.get("basis"), 0, 2) and
                integer(reference.get("source"), 0, 4) and integer(reference.get("observed_us")) and integer(reference.get("maximum_age_us")),
                "reference provenance is invalid")
        if not displacement_known:
            require(reference["native_known"] is False and all(reference[key] == 0 for key in
                    ("target", "generation", "configuration_generation", "native_position", "basis", "source", "observed_us", "maximum_age_us")),
                    "unreferenced absolute target invents a coordinate reference")
        elif kind != "relative" or reference["native_known"]:
            require(reference["native_known"] and all(reference[key] == item[key] for key in ("target", "generation", "configuration_generation")) and
                    reference["basis"] == 0 and reference["source"] != 0 and
                    evidence_age_valid(reference["observed_us"], item["started_us"], reference["maximum_age_us"]),
                    "reference is stale or mismatched")
            require(item["endpoint_known"] and item["endpoint_native"] - reference["native_position"] == item["displacement_native"] and
                    (kind == "relative" or item["effective_native"] == item["endpoint_native"]), "reference endpoint differs")
            write_deadline = min(write_deadline, evidence_age_deadline(reference["observed_us"], reference["maximum_age_us"]))
        expected_words = [encoded >> 16, encoded & 65535]
        if prerequisites["word_order"] == 1: expected_words.reverse()
        require(words[3:] == expected_words, "staged target word order differs from qualification")
        requested = item.get("requested")
        require(isinstance(requested, dict) and integer(requested.get("numerator"), -2**63, 2**63 - 1) and
                integer(requested.get("denominator"), 1, 2**64 - 1) and requested.get("unit") in
                ("steps", "fullsteps", "counts", "turn", "deg", "rad", "mm") and integer(requested.get("frame"), 0, 2) and
                requested.get("relative") is (kind == "relative") and requested.get("wrapped") is (kind == "angle") and
                integer(requested.get("angle_path"), 0, 2) and integer(requested.get("half_turn_tie"), 0, 2) and requested.get("basis") == 0 and integer(requested.get("rounding"), 0, 4),
                "request provenance is invalid")
        require(kind != "angle" or (requested["unit"] in ("turn", "deg", "rad") and requested["frame"] != 0), "wrapped request is not an angular frame")
        if not displacement_known:
            require(requested["relative"] is False and requested["wrapped"] is False and requested["basis"] == 0,
                    "unreferenced target must be unwrapped absolute")
        def finite(value):
            return type(value) in (int, float) and math.isfinite(value)
        require(type(requested.get("approximate")) is bool and type(requested.get("rational_radians")) is bool and
                finite(requested.get("maximum_quantization_error")) and requested["maximum_quantization_error"] >= 0 and
                finite(item.get("rounding_error")) and finite(item.get("approximation_error_bound")) and
                item["approximation_error_bound"] >= 0 and
                abs(item["rounding_error"]) <= requested["maximum_quantization_error"], "numerical error provenance is inconsistent")
        require((requested.get("radians") is None) if requested["unit"] != "rad" or requested["rational_radians"] else
                finite(requested.get("radians")), "selected radian input is missing")
        if item["exact_arithmetic"]:
            require(item["approximation_error_bound"] == 0 and item.get("requested_native_approximate") is None and
                    (requested.get("maximum_approximation_error") is None or
                     (requested["unit"] == "rad" and requested["approximate"] and
                      (requested["numerator"] == 0 if requested["rational_radians"] else requested["radians"] == 0) and
                      finite(requested.get("maximum_approximation_error")) and requested["maximum_approximation_error"] > 0)),
                    "exact preparation contains approximate provenance")
        else:
            require(requested["unit"] == "rad" and requested["approximate"] is True and requested["rounding"] != 0 and
                    finite(requested.get("maximum_approximation_error")) and requested["maximum_approximation_error"] > 0 and
                    item["approximation_error_bound"] <= requested["maximum_approximation_error"] and
                    finite(item.get("requested_native_approximate")),
                    "approximate preparation lost numerical provenance")
        if requested["rounding"] == 0:
            require(item["exact_arithmetic"] is True and item["rounding_error"] == 0, "EXACT preparation rounded the target")
        if arguments is not None:
            parsed = move_arguments(kind, arguments)
            def retained_allowance(text, value):
                # The public parser narrows allowances toward zero. Check its
                # retained binary64 interval without repeating target math.
                exact = Fraction(text)
                return (finite(value) and value >= 0 and Fraction(value) <= exact and
                        (exact == Fraction(value) or exact < Fraction(math.nextafter(value, math.inf))))
            require(Fraction(parsed["value"]) == Fraction(requested["numerator"], requested["denominator"]) and
                    parsed["unit"] == requested["unit"] and ("native", "motor", "load")[requested["frame"]] == parsed["frame"] and
                    Fraction(parsed["speed"]) == item["native_rpm"] and parsed["ramp"] == item["ramp"] and
                    parsed["basis"] == requested["basis"] and
                    parsed["rounding"] == requested["rounding"] and
                    retained_allowance(parsed["error"], requested["maximum_quantization_error"]) and
                    parsed["approximate"] == requested["approximate"], "request differs from admitted input")
            if kind == "angle":
                require(parsed["path"] == requested["angle_path"] and parsed["tie"] == requested["half_turn_tie"], "angle policy differs")
            if parsed["approximate"]:
                require(retained_allowance(parsed["approximation_error"], requested["maximum_approximation_error"]), "radian error differs")
        evidence = {}
        for name, token, tx_size in (("staging_evidence", 0, 19), ("trigger_evidence", 1, 8),
                                    ("activity_evidence", None, 8), ("last_observation", None, 8), ("failure_evidence", None, None)):
            entry = item.get(name)
            require(isinstance(entry, dict), "missing " + name)
            require(all(integer(entry.get(key), 0, limit) for key, limit in (("event", 3), ("step", 65),
                    ("received_length", 0xFFFFFFFF), ("tx_accepted", 19), ("frame_error", 255))), name + " bounds are invalid")
            require(all(integer(entry.get(key)) for key in ("earliest_us", "latest_us", "delivered_us")) and
                    all(type(entry.get(key)) is bool for key in ("tx_complete", "response_confirmed", "qualified", "execution_unknown")) and
                    all(integer(entry.get(key), -2**31, 2**31 - 1) for key in ("detail", "transport_detail")) and
                    entry.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"},
                    name + " evidence is malformed")
            raw = entry.get("raw_hex")
            require(isinstance(raw, str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,9}", raw) is not None, name + " raw bytes are invalid")
            raw = bytes.fromhex(raw)
            require(len(raw) == min(entry["received_length"], 9), name + " retained length differs")
            empty = all(entry.get(key) == value for key, value in dict(step=0, event=0, raw_hex="", received_length=0,
                tx_accepted=0, tx_complete=False, response_confirmed=False, qualified=False, execution_unknown=False,
                earliest_us=0, latest_us=0, delivered_us=0, transport_detail=0, status="OK", detail=0, frame_error=0).items())
            if not empty:
                require(token is None or entry["step"] == token, name + " token differs")
                require(item["started_us"] <= entry["delivered_us"] <= item["serviced_us"], name + " is outside operation lifetime")
                size = tx_size or (19 if entry["step"] == 0 else 8)
                require(entry["tx_accepted"] <= size and (not entry["tx_complete"] or entry["tx_accepted"] == size), name + " TX evidence differs")
                require(entry["event"] == 0 or not entry["response_confirmed"], name + " local event claims a response")
                if entry["qualified"]:
                    require(entry["event"] == 0 and entry["tx_complete"] and item["started_us"] <= entry["earliest_us"] <= entry["latest_us"] <= entry["delivered_us"],
                            name + " timing evidence differs")
                else:
                    require(entry["earliest_us"] == entry["latest_us"] == 0, name + " unqualified times are published")
            evidence[name] = (entry, raw, empty)
        def confirmed(name, prefix, size):
            entry, raw, empty = evidence[name]
            require(not empty and entry["event"] == 0 and entry["qualified"] and entry["response_confirmed"] and entry["tx_complete"] and
                    entry["status"] == "OK" and entry["detail"] == entry["frame_error"] == 0 and len(raw) == size and
                    raw[:len(prefix)] == bytes(prefix) and wire_crc(raw) == 0 and
                    (name in ("staging_evidence", "trigger_evidence") or entry["latest_us"] <= item["deadline_us"]),
                    name + " lacks a checked response")
            return entry, raw
        for execution, name in (("setup_execution", "staging_evidence"), ("execution", "trigger_evidence")):
            require(item.get(execution) in ("not_transmitted", "acknowledged", "rejected", "unknown"), execution + " is invalid")
            entry, raw, empty = evidence[name]
            if item[execution] == "acknowledged":
                confirmed(name, (item["address"], 16, 0, 0x21, 0, 5) if execution == "setup_execution" else (item["address"], 6, 0, 0x27, 0, 1 if kind == "relative" else 5), 8)
            elif item[execution] == "rejected":
                require(not empty and entry["qualified"] and entry["response_confirmed"] and entry["tx_complete"] and
                        entry["status"] == "EXCEPTION" and entry["frame_error"] == 10 and len(raw) == 5 and
                        raw[:2] == bytes((item["address"], 0x90 if execution == "setup_execution" else 0x86)) and
                        1 <= raw[2] <= 7 and raw[2] == entry["detail"] and wire_crc(raw) == 0, name + " rejection is unproven")
            elif item[execution] == "not_transmitted":
                require(entry["tx_accepted"] == 0 and not entry["execution_unknown"], name + " contradicts non-transmission")
            else:
                require(not empty and (entry["tx_accepted"] > 0 or entry["execution_unknown"]), name + " uncertainty lacks TX")
                require(not (entry["event"] == 0 and entry["qualified"] and entry["response_confirmed"] and
                    (entry["status"] == "OK" or (entry["status"] == "EXCEPTION" and 1 <= entry["detail"] <= 7))),
                    name + " unknown execution contradicts checked acknowledgement/rejection")
        require(item["staging_applied"] == (item["setup_execution"] == "acknowledged"), "staging validity differs")
        stage, trigger = evidence["staging_evidence"][0], evidence["trigger_evidence"][0]
        trigger_raw = evidence["trigger_evidence"][1]
        unconfirmed_trigger = (item["execution"] == "unknown" and
            trigger["event"] == 0 and trigger["qualified"] and not trigger["response_confirmed"] and
            trigger["tx_complete"] and trigger["tx_accepted"] == 8 and trigger["status"] == "OK" and
            trigger["detail"] == trigger["frame_error"] == 0 and len(trigger_raw) == 8 and
            trigger_raw[:6] == bytes((item["address"], 6, 0, 0x27, 0, 1 if kind == "relative" else 5)) and
            wire_crc(trigger_raw) == 0 and trigger["latest_us"] <= write_deadline)
        observable_trigger = item["execution"] == "acknowledged" or unconfirmed_trigger
        if not evidence["trigger_evidence"][2]:
            require(item["staging_applied"] and stage["delivered_us"] <= trigger["delivered_us"] and
                    (not trigger["qualified"] or stage["delivered_us"] <= trigger["earliest_us"]), "trigger precedes checked staging")
            require(stage["latest_us"] <= write_deadline and stage["delivered_us"] < write_deadline,
                    "trigger follows expired staging readiness")
        if item["running_observed"]:
            activity, raw = confirmed("activity_evidence", (item["address"], 3, 4), 9)
            motion = int.from_bytes(raw[5:7], "big")
            require(item["observation_known"] and 2 <= activity["step"] <= item["polls"] + 1 and
                    trigger["delivered_us"] < activity["earliest_us"] and int.from_bytes(raw[3:5], "big") == 0 and
                    bool(motion & 4) and not motion & 0x78, "activity is stale, faulted or lacks RUNNING")
        else:
            require(evidence["activity_evidence"][2], "unobserved activity retains a report")
        if item["observation_known"]:
            observed, raw = confirmed("last_observation", (item["address"], 3, 4), 9)
            require(observable_trigger and item["polls"] >= 1 and observed["step"] == item["polls"] + 1 and
                    trigger["delivered_us"] < observed["earliest_us"] and
                    item.get("raw_alarm") == int.from_bytes(raw[3:5], "big") and item.get("raw_motion") == int.from_bytes(raw[5:7], "big"),
                    "last report differs from retained RX")
            require(trigger["latest_us"] <= write_deadline, "observations follow a trigger after readiness expiry")
            if item["running_observed"] and activity["step"] == observed["step"]:
                require(activity == observed, "same activity and observation token changed retained evidence")
        else:
            require(item.get("raw_alarm") is None and item.get("raw_motion") is None and evidence["last_observation"][2], "unknown report publishes values")
        if item["ok"]:
            require(item["running_observed"] and item["observation_known"] and item["uncertain"] is False and
                    activity["delivered_us"] < observed["earliest_us"] and observed["delivered_us"] == item["serviced_us"] and
                    item["raw_alarm"] == 0 and item["raw_motion"] & 0x7D == 1 and evidence["failure_evidence"][2],
                    "completion lacks new running-to-arrival evidence")
        else:
            require(not evidence["failure_evidence"][2] and evidence["failure_evidence"][0]["delivered_us"] == item["serviced_us"],
                    "failure lacks terminal evidence")
            failure, raw, _ = evidence["failure_evidence"]
            if failure["step"] < 2:
                require(failure == evidence["staging_evidence" if failure["step"] == 0 else "trigger_evidence"][0],
                        "failed write differs from retained evidence")
            else:
                require(observable_trigger and failure["step"] in (item["polls"] + 1, item["polls"] + 2),
                        "failure token skips an observation")
                previous = evidence["last_observation"][0] if item["observation_known"] else trigger
                if failure["step"] == item["polls"] + 1:
                    require(failure == previous, "terminal observation differs from retained report")
                else:
                    require(previous["delivered_us"] <= failure["delivered_us"] and
                            (not failure["qualified"] or previous["delivered_us"] < failure["earliest_us"]), "failure precedes prior transaction")
            if failure["event"] != 0:
                expected = {1: "transport_error", 2: "cancelled", 3: "deadline"}[failure["event"]]
                if failure["event"] == 3:
                    require(item["serviced_us"] >= (write_deadline if failure["step"] < 2 else item["deadline_us"]),
                            "local deadline precedes its transaction budget")
            elif not failure["qualified"]:
                expected = "timing_unqualified"
            elif failure["latest_us"] > (write_deadline if failure["step"] < 2 else item["deadline_us"]):
                expected = "deadline"
            elif failure["status"] != "OK":
                expected = "reply_error"
            elif not failure["response_confirmed"] and not (failure["step"] == 1 and unconfirmed_trigger):
                expected = "unconfirmed_response"
            elif failure["step"] >= 2 and len(raw) == 9 and (int.from_bytes(raw[3:5], "big") or int.from_bytes(raw[5:7], "big") & 0x78):
                expected = "reply_error"
            else:
                require(item["outcome"] in ("deadline", "observation_limit") and
                        (item["outcome"] != "deadline" or item["serviced_us"] >=
                            (write_deadline if failure["step"] == 0 else item["deadline_us"])) and
                        (item["outcome"] != "observation_limit" or failure["step"] >= 2), "checked reply does not establish failure")
                expected = item["outcome"]
            require(item["outcome"] == expected, "failure outcome contradicts terminal evidence")
        require(item["uncertain"] == (not item["ok"] and bool(stage["tx_accepted"] or stage["execution_unknown"] or
                    trigger["tx_accepted"] or trigger["execution_unknown"])), "uncertainty differs from staging/trigger evidence")

    @staticmethod
    def _check_driver(item: dict, address: int | None, arguments: tuple[str, ...] | None, expected_group: str | None = "drive") -> None:
        """Retain exact partial progress; acknowledgement is not activation."""
        def require(condition, message):
            if not condition: raise BenchError("driver " + message)

        def integer(value, maximum=2**64 - 1):
            return type(value) is int and 0 <= value <= maximum

        group = item.get("driver_group", "drive")
        segment = group in SEGMENT_GROUPS.values()
        tuning = group in TUNING_FIELDS
        require(group in ("drive", "io", "control_settings", *SEGMENT_GROUPS.values(), *TUNING_FIELDS) and (expected_group is None or group == expected_group or expected_group == "segment" and segment or expected_group == "tuning" and tuning), "group differs from command")
        if segment:
            require(integer(item.get("segment_index"), 16) and item["segment_index"] >= 1, "record index differs")
            require(item.get("execution_path") == "external_input" and item.get("storage_capacity") == 16 and item.get("maximum_input_selections") == 8, "invented segment execution")
            if arguments is not None:
                requested_group, requested_index, unused = segment_arguments(arguments)
                require(group == requested_group and item["segment_index"] == requested_index, "record correlation differs")
                arguments = arguments[2:]
        if tuning and arguments is not None:
            requested_group, unused = tuning_arguments(arguments)
            require(group == requested_group, "tuning group correlation differs")
            arguments = arguments[1:]
        io = group == "io"
        control = group == "control_settings"
        stored = io or segment or control or tuning
        names = TUNING_FIELDS[group] if tuning else CONTROL_FIELDS if control else IO_FIELDS if io else DRIVER_FIELDS[:7]
        registers = TUNING_REGISTERS[group] if tuning else CONTROL_REGISTERS if control else IO_REGISTERS if io else DRIVER_REGISTERS
        masks = tuple(1 << (index + (23 if control else 9 if io else 0)) for index in range(len(names)))
        by_field = dict(zip(masks, zip(names, registers)))
        windows = TUNING_WINDOWS[group] if tuning else CONTROL_WINDOWS if control else IO_WINDOWS if io else ((0x10, 2), (0x17, 3), (0x37, 4), (0x50, 2))
        if segment:
            record_index = item["segment_index"]
            if group == "position_segment":
                base = 0x60 + 6 * (record_index - 1)
                names, registers, windows = ("speed", "acceleration", "deceleration"), (base + 2, base + 3, base + 4), ((base, 5),)
                masks = (1 << 18, 1 << 19, 1 << 20)
            elif group == "speed_segment":
                base = 0xC0 + 3 * (record_index - 1)
                names, registers, windows = ("speed", "acceleration", "deceleration"), (base, base + 1, base + 2), ((base, 3),)
                masks = (1 << 18, 1 << 19, 1 << 20)
            else:
                base = 0x130 + record_index - 1
                names, registers, windows, masks = ("value",), (base,), ((base, 1),), (1 << 21,)
            by_field = dict(zip(masks, zip(names, registers)))
        read_steps, update_steps, raw_limit = len(windows), 2 * len(names), 15 if stored else 13
        echo_policy = item.get("echo_readback_policy", False)
        require(type(echo_policy) is bool and (stored or not echo_policy), "echo readback policy differs")
        if stored:
            require(type(item.get("echo_readback_policy")) is bool, "IO verification policy is missing")
            require(item.get("settlement") == "stored_readback", "IO activation settlement was fabricated")
        elif "settlement" in item:
            require(item["settlement"] == "checked_ack_readback", "drive settlement differs")

        def legal(field, value):
            name = by_field[field][0]
            if segment: return value <= (180 if name == "value" else 3000 if name == "speed" else 2000)
            if tuning:
                minimum, maximum = TUNING_RANGES[group][names.index(name)]
                return minimum <= value <= maximum
            if control:
                index = CONTROL_FIELDS.index(name)
                return value in (1, 2) if index == 0 else 1 <= value <= 65535 if index == 1 else value <= (5600, 150, 75, 100, 100, 20000)[index - 2]
            if io:
                if name.startswith("x"): return value <= 17
                if name.startswith("y"): return value in (*range(6), 9, 10)
                return value <= (15 if name == "input-polarity" else 3)
            return 400 <= value <= 51200 if field == 2 else value <= 1

        require(item.get("driver") is True and item.get("driver_kind") in ("read", "update") and
                item.get("velocity", False) is False and all(item.get(k) is None for k in ("read_kind", "move_kind", "action_kind")) and
                item.get("recovery", False) is False and item.get("capture_read", False) is False, "kind differs")
        require(integer(item.get("address"), 247) and item["address"] >= 1 and (address is None or item["address"] == address), "target differs")
        require(all(Console._operation_id(item.get(k)) for k in ("target", "generation", "configuration_generation")), "generation is invalid")
        require(all(integer(item.get(k)) for k in ("started_us", "deadline_us", "stationary_valid_until_us", "serviced_us", "completed_steps", "fields", "effects")) and
                item["started_us"] < item["deadline_us"] and item["serviced_us"] >= item["started_us"] and
                item["started_us"] < item["stationary_valid_until_us"] <= item["deadline_us"] and
                item["completed_steps"] <= update_steps and item["fields"] & ~sum(masks) == 0 and item["effects"] & ~item["fields"] == 0, "budget or field mask differs")
        require(type(item.get("uncertain")) is bool and item.get("atomic") is False and item.get("active_settings_known") is False, "activation/rollback claim is invalid")
        require(item.get("state") == ("succeeded" if item["ok"] else "failed") and
                item.get("outcome") in ({"success"} if item["ok"] else {"reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified", "unconfirmed_response", "readback_mismatch"}) and
                item.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"} and
                (item["ok"] == (item["status"] == "OK")) and type(item.get("detail")) is int, "terminal result differs")
        parsed = None if arguments is None else driver_arguments(arguments, group)
        if parsed is not None:
            require(all(k in names for k in parsed), "unavailable field was admitted")
            mask = sum(masks[names.index(k)] for k in parsed)
            require(item["fields"] == mask and item["driver_kind"] == ("update" if parsed else "read"), "candidate differs from command")
        require(item.get("progress_columns") == DRIVER_PROGRESS_COLUMNS and item.get("evidence_columns") == DRIVER_EVIDENCE_COLUMNS, "evidence schema differs")
        progress, evidence = item.get("progress"), item.get("evidence")
        require(isinstance(progress, list) and len(progress) <= len(names) and isinstance(evidence, list) and len(evidence) <= update_steps, "storage bounds exceeded")
        fields = {}
        for row in progress:
            require(isinstance(row, list) and len(row) == len(DRIVER_PROGRESS_COLUMNS), "progress row is invalid")
            p = dict(zip(DRIVER_PROGRESS_COLUMNS, row)); field = p["field"]
            require(type(field) is int and field in by_field and field not in fields and item["fields"] & field and
                    p["register"] == by_field[field][1], "progress field differs")
            require(all(integer(p[k], 65535) for k in ("previous", "requested", "readback", "active")) and
                    all(type(p[k]) is bool for k in ("acknowledged", "readback_known", "active_known")) and
                    p["active_known"] is False and p["execution"] in ("not_transmitted", "acknowledged", "rejected", "unknown"), "progress values are invalid")
            require(legal(field, p["requested"]), "candidate is outside reviewed range")
            if parsed is not None: require(p["requested"] == parsed[by_field[field][0]], "requested value changed")
            fields[field] = p
        require(sum(fields) == item["fields"], "selected field progress is missing")
        steps = []
        for index, row in enumerate(evidence):
            require(isinstance(row, list) and len(row) == len(DRIVER_EVIDENCE_COLUMNS), "evidence row is invalid")
            e = dict(zip(DRIVER_EVIDENCE_COLUMNS, row))
            require(e["step"] == index and all(integer(e[k], limit) for k, limit in (("register", 65535), ("count", 5 if io or segment else 4), ("event", 3), ("received_length", 2**32 - 1), ("tx_accepted", 8), ("frame_error", 255))) and e["count"] >= 1 and
                    all(type(e[k]) is bool for k in ("write", "tx_complete", "response_confirmed", "qualified", "execution_unknown")) and
                    all(integer(e[k]) for k in ("earliest_us", "latest_us", "delivered_us", "attempted_us")) and
                    all(type(e[k]) is int and -2**31 <= e[k] < 2**31 for k in ("detail", "transport_detail")) and
                    e["status"] in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"}, "evidence values are invalid")
            require(isinstance(e["raw_hex"], str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0," + str(raw_limit) + r"}", e["raw_hex"]) is not None, "raw frame is malformed")
            raw = bytes.fromhex(e["raw_hex"])
            require(len(raw) == min(e["received_length"], raw_limit) and item["started_us"] <= e["attempted_us"] <= e["delivered_us"] <= item["serviced_us"] and
                    (not e["tx_complete"] or e["tx_accepted"] == 8) and (e["event"] == 0 or not e["response_confirmed"]), "transport evidence differs")
            require((e["qualified"] and e["event"] == 0 and e["attempted_us"] <= e["earliest_us"] <= e["latest_us"] <= e["delivered_us"]) or
                    (not e["qualified"] and e["earliest_us"] == e["latest_us"] == 0), "closure bounds differ")
            if item["driver_kind"] == "read":
                require(index < read_steps and not e["write"] and (e["register"], e["count"]) == windows[index], "read window differs")
            else:
                selected = sorted(fields.values(), key=lambda p: p["field"])
                if group == "drive" and 8 in fields and fields[8]["requested"] == 0:
                    selected = [fields[8]] + [p for p in selected if p["field"] != 8]
                require(index // 2 < len(selected) and e["register"] == selected[index // 2]["register"] and e["count"] == 1 and
                        e["write"] == (index % 2 == 0), "update sequence differs")
            if e["event"] == 0:
                require(e["tx_complete"] and e["tx_accepted"] == 8, "frame lacks completed request")
                echo = None
                if e["write"]:
                    p = next(p for p in fields.values() if p["register"] == e["register"])
                    echo = (e["register"], p["requested"])
                expected = _reply_status(raw, e["received_length"], item["address"],
                                         6 if e["write"] else 3, 8 if e["write"] else 5 + 2 * e["count"], echo)
                require((e["status"], e["detail"], e["frame_error"]) == expected,
                        "codec evidence differs from retained reply")
            else:
                require((e["status"], e["detail"], e["frame_error"]) ==
                        ("ILLEGAL_VALUE", {1: 14, 2: 15, 3: 13}[e["event"]], 0),
                        "local event codec evidence differs")
            steps.append((e, raw))
        effects = 0
        uncertain = False
        for p in fields.values():
            rows = [(e, raw) for e, raw in steps if e["register"] == p["register"]]
            acknowledged = any(e["write"] and e["event"] == 0 and e["status"] == "OK" and e["qualified"] and e["response_confirmed"] and e["latest_us"] <= item["stationary_valid_until_us"] for e, raw in rows)
            readbacks = [(e, raw) for e, raw in rows if not e["write"] and e["event"] == 0 and e["status"] == "OK" and e["qualified"] and e["response_confirmed"] and e["latest_us"] <= item["deadline_us"]]
            require(p["acknowledged"] == acknowledged and p["readback_known"] == bool(readbacks), "acknowledgement/readback provenance differs")
            if readbacks: require(p["readback"] == int.from_bytes(readbacks[-1][1][3:5], "big"), "readback value differs")
            writes = [e for e, raw in rows if e["write"]]
            execution = "not_transmitted"
            for e in writes:
                if e["tx_accepted"] or e["execution_unknown"]:
                    effects |= p["field"]; execution = "unknown"
                if e["event"] == 0 and e["qualified"] and e["response_confirmed"] and e["latest_us"] <= item["stationary_valid_until_us"]:
                    if e["status"] == "OK": execution = "acknowledged"
                    elif e["status"] == "EXCEPTION" and 1 <= e["detail"] <= 7: execution = "rejected"
            require(p["execution"] == execution, "execution differs from write evidence")
            stored_settled = stored and echo_policy and p["readback_known"] and p["readback"] == p["requested"]
            uncertain |= (execution == "unknown" and not stored_settled) or (execution == "acknowledged" and (not p["readback_known"] or p["readback"] != p["requested"]))
        require(item["effects"] == effects and item["uncertain"] == uncertain, "changed-setting mask or uncertainty differs")
        total = read_steps if item["driver_kind"] == "read" else 2 * len(fields)
        successful_steps = 0
        for index, (e, raw) in enumerate(steps):
            deadline = item["stationary_valid_until_us"] if e["write"] else item["deadline_us"]
            good = e["event"] == 0 and e["qualified"] and (e["response_confirmed"] or (stored and echo_policy and e["write"])) and e["latest_us"] <= deadline and e["status"] == "OK"
            if good and item["driver_kind"] == "update" and not e["write"]:
                p = next(p for p in fields.values() if p["register"] == e["register"])
                good = int.from_bytes(raw[3:5], "big") == p["requested"]
            if index + 1 < len(steps):
                require(good and e["delivered_us"] <= steps[index + 1][0]["attempted_us"] and
                        e["delivered_us"] < item["deadline_us"], "sequence continued after failure or without causal budget")
            successful_steps += good
        require(item["completed_steps"] == successful_steps and len(steps) <= total and
                (item["ok"] or item["completed_steps"] < total), "completed progress contradicts terminal state")
        if item["ok"]:
            require(not item["uncertain"] and item["detail"] == 0 and item["completed_steps"] == len(steps) == (read_steps if item["driver_kind"] == "read" else 2 * len(fields)) and
                    all(e["qualified"] and (e["response_confirmed"] or (stored and echo_policy and e["write"])) and e["status"] == "OK" and e["latest_us"] <= item["deadline_us"] for e, raw in steps), "success lacks complete checked evidence")
            require(all(p["readback_known"] and p["requested"] == p["readback"] and ((p["acknowledged"] and p["execution"] == "acknowledged") or (stored and echo_policy and not p["acknowledged"] and p["execution"] == "unknown")) for p in fields.values()), "success lacks matching readback")
        if item["driver_kind"] == "read" and item["ok"]:
            require(all(not e["execution_unknown"] for e, raw in steps),
                    "successful read retains execution uncertainty")
            v = item.get("observation")
            require(isinstance(v, dict), "complete read observation is missing")
            words = [[int.from_bytes(raw[i:i+2], "big") for i in range(3, len(raw) - 2, 2)] for e, raw in steps]
            if io:
                values = words[0] + words[1] + words[2]
                require(v.get("raw") == values and not any(k in v for k in ("positive_words", "negative_words", "pair_known", "positive_bits", "negative_bits", "signed_limits", "native_scale")), "IO observation invents drive limits")
                known = sum(mask for mask, value in zip(masks, values) if legal(mask, value))
                require(v.get("known_fields") == known, "unknown IO enums were normalized")
                require(v.get("unknown_input_polarity_bits") == values[0] & ~15 and
                        v.get("unknown_output_polarity_bits") == values[5] & ~3 and
                        v.get("unknown_custom_output_bits") == values[8] & ~3, "reserved IO bits were lost")
            elif tuning:
                values = [value for window in words for value in window]
                require(v.get("raw") == values and v.get("known_fields") == sum(mask for mask, value in zip(masks, values) if legal(mask, value)), "native tuning observation differs")
                require(v.get("native_units_only") is True and v.get("physical_scaling_known") is False and v.get("active_settings_known") is False, "tuning physical meaning fabricated")
                require(not any(k in v for k in ("arrival_time_ms", "position_error_counts", "node_rpm", "filter_delay_us", "current_ma", "pair_known")), "unknown tuning units fabricated")
            elif control:
                values = words[0] + words[1]
                require(v.get("raw") == values and v.get("known_fields") == sum(mask for mask, value in zip(masks, values) if legal(mask, value)), "unknown control values were normalized")
                require(v.get("algorithm_known") is (values[0] in (1, 2)) and v.get("encoder_scale_usable") is (values[1] != 0), "control scale/algorithm differs")
                labels = ("encoder_resolution", "maximum_effective_current_ma", "closed_maximum_percent", "closed_base_percent", "open_maximum_percent", "lock_percent", "lock_delay_ms")
                require(all(v.get(label) == value for label, value in zip(labels, values[1:])), "native control units differ")
                require(v.get("current_percent_base") == "unresolved" and v.get("active_settings_known") is False, "current formula or activation fabricated")
                require(not any(k in v for k in ("maximum_peak_current_ma", "physical_encoder_resolution", "current_ma", "torque", "positive_words", "signed_encoding")), "control observation invents physical meaning")
            elif segment:
                payload = words[0]
                values = payload[2:] + payload[:2] if group == "position_segment" else payload + [0] * (5 - len(payload))
                require(v.get("raw") == values and v.get("known_fields") == sum(mask for mask, value in zip(masks, values) if legal(mask, value)), "record raw observation differs")
                require(v.get("paired_write_supported") is False and v.get("signed_encoding") == "unresolved" and v.get("active_settings_known") is False, "record semantics fabricated")
            else:
                require(v.get("signed_limits") == "unresolved" and v.get("native_scale") == "unresolved", "limit resolution was fabricated")
                values = [words[0][0], words[0][1], words[1][2], words[1][1], words[1][0], words[3][1], words[3][0]]
            if not stored:
                require(v.get("raw") == values and v.get("positive_words") == words[2][:2] and v.get("negative_words") == words[2][2:], "observation differs from raw replies")
                known = sum(1 << i for i, value in enumerate(values) if (400 <= value <= 51200 if i == 1 else value <= 1))
                require(v.get("known_fields") == known, "unknown enums were normalized")
                require(v.get("pair_known") == (values[2] in (0, 1)), "pair word order differs")
                if v["pair_known"]:
                    pair = lambda w: (w[0] << 16 | w[1]) if values[2] == 0 else (w[1] << 16 | w[0])
                    require(v.get("positive_bits") == pair(words[2][:2]) and v.get("negative_bits") == pair(words[2][2:]), "paired bits differ")
                else: require(v.get("positive_bits") == v.get("negative_bits") == 0, "unknown word order was decoded")
        else: require(item.get("observation") is None, "partial update publishes a whole observation")
        if not item["ok"]:
            require(bool(steps), "failure lacks terminal evidence")
            e, raw = steps[-1]
            deadline = item["stationary_valid_until_us"] if e["write"] else item["deadline_us"]
            if e["event"] != 0:
                expected_outcome = {1: "transport_error", 2: "cancelled", 3: "deadline"}[e["event"]]
            elif not e["qualified"]:
                expected_outcome = "timing_unqualified"
            elif e["latest_us"] > deadline:
                expected_outcome = "deadline"
            elif e["status"] != "OK":
                expected_outcome = "reply_error"
            elif not e["response_confirmed"] and not (stored and echo_policy and e["write"]):
                expected_outcome = "unconfirmed_response"
            elif item["driver_kind"] == "update" and not e["write"] and any(
                    p["register"] == e["register"] and p["requested"] != int.from_bytes(raw[3:5], "big")
                    for p in fields.values()):
                expected_outcome = "readback_mismatch"
            else:
                require(item["completed_steps"] < total and e["delivered_us"] >= item["deadline_us"],
                        "checked frame does not establish failure")
                expected_outcome = "deadline"
            require(item["outcome"] == expected_outcome, "failure priority contradicts terminal evidence")
        if item["outcome"] == "readback_mismatch":
            require(item["status"] == "ILLEGAL_VALUE" and item["detail"] == 18 and steps and not steps[-1][0]["write"] and
                    any(p["readback_known"] and p["readback"] != p["requested"] for p in fields.values()), "mismatch lacks readback disagreement")
        if item["outcome"] in ("transport_error", "cancelled"):
            require(steps and steps[-1][0]["event"] == (1 if item["outcome"] == "transport_error" else 2) and
                    item["status"] == steps[-1][0]["status"] == "ILLEGAL_VALUE" and
                    item["detail"] == steps[-1][0]["detail"] == (14 if item["outcome"] == "transport_error" else 15), "local failure lacks evidence")
        if item["outcome"] == "reply_error":
            require(steps and steps[-1][0]["event"] == 0 and steps[-1][0]["status"] != "OK" and
                    item["status"] == steps[-1][0]["status"] and item["detail"] == steps[-1][0]["detail"], "reply failure lacks evidence")
        if item["outcome"] == "timing_unqualified":
            require(item["status"] == "ILLEGAL_VALUE" and item["detail"] == 16 and steps and
                    steps[-1][0]["event"] == 0 and not steps[-1][0]["qualified"], "timing failure lacks unqualified frame")
        if item["outcome"] == "unconfirmed_response":
            require(item["status"] == "ILLEGAL_VALUE" and item["detail"] == 17 and steps and
                    steps[-1][0]["event"] == 0 and steps[-1][0]["status"] == "OK" and
                    steps[-1][0]["qualified"] and not steps[-1][0]["response_confirmed"] and
                    steps[-1][0]["latest_us"] <= (item["stationary_valid_until_us"] if steps[-1][0]["write"] else item["deadline_us"]), "unconfirmed failure lacks ambiguous response")
        if item["outcome"] == "deadline":
            require(item["status"] == "ILLEGAL_VALUE" and item["detail"] == 13 and steps, "deadline result is invalid")
            e = steps[-1][0]; deadline = item["stationary_valid_until_us"] if e["write"] else item["deadline_us"]
            require((e["event"] == 3 and e["delivered_us"] >= deadline) or
                    (e["event"] == 0 and e["qualified"] and e["latest_us"] > deadline) or
                    (len(steps) == item["completed_steps"] and item["completed_steps"] < total and e["delivered_us"] >= item["deadline_us"]), "deadline lacks expired budget evidence")

    @staticmethod
    def _check_home(item: dict, address: int | None, arguments: tuple[str, ...] | None) -> None:
        """Validate retained homing traffic without treating old HOMED as completion."""
        def require(condition, message):
            if not condition: raise BenchError("home " + message)
        def uint(value, limit=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and 0 <= value <= limit
        require(item.get("home") is True and all(item.get(k) is None for k in ("read_kind", "action_kind", "move_kind")) and
                all(item.get(k, False) is False for k in ("velocity", "driver", "capture_read", "recovery")), "kind is inconsistent")
        require(uint(item.get("address"), 247) and item["address"] > 0 and (address is None or item["address"] == address), "address differs")
        require(all(Console._operation_id(item.get(k)) for k in ("target", "generation", "configuration_generation")), "binding is invalid")
        check_counts(item, ("started_us", "deadline_us", "serviced_us", "polls", "completion_observed_us"), "home")
        require(item["started_us"] < item["deadline_us"] and item["started_us"] <= item["serviced_us"] and item["polls"] <= 64, "time/count bounds are invalid")
        require(item.get("method") in (33, 34, 35), "unimplemented method yielded traffic")
        require(uint(item.get("phase"), 3), "phase is invalid")
        words = item.get("staging_words")
        require(isinstance(words, list) and len(words) == 6 and all(uint(w, 65535) for w in words) and
                words[0] == item["method"] and 5 <= words[1] <= 3000 and 5 <= words[2] <= 300 and
                30 <= words[3] <= 2000 and words[4:] == [0, 0], "staging differs from reviewed zero-offset window")
        if arguments is not None: require(tuple(words[:4]) == home_arguments(arguments), "request differs from command")
        require(type(item.get("ok")) is bool and item.get("state") == ("succeeded" if item["ok"] else "failed") and
                item.get("completion") == ("observed" if item["ok"] else "not_observed"), "state/completion is inconsistent")
        require(item.get("outcome") in ({"observed"} if item["ok"] else
                {"reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified", "unconfirmed_response", "observation_limit"}), "outcome is inconsistent")
        for flag in ("staging_applied", "uncertain", "running_observed", "homed_low_observed", "observation_known", "interrupted_by_stop"):
            require(type(item.get(flag)) is bool, flag + " is invalid")
        p = item.get("prerequisites")
        require(isinstance(p, dict) and uint(p.get("observed_us")) and uint(p.get("maximum_age_us")) and
                evidence_age_valid(p["observed_us"], item["started_us"], p["maximum_age_us"]) and
                uint(p.get("raw_motion"), 65535) and not p["raw_motion"] & 0x7C and
                p.get("auxiliary") == 7 and type(p.get("reference_semantics_qualified")) is bool,
                "immutable prerequisites are invalid")
        qualified = p.get("qualified_parameters")
        require(isinstance(qualified, list) and len(qualified) == 4 and all(type(v) is int for v in qualified) and
                qualified == words[:4], "method/rate/ramp differs from exact qualified configuration")
        require(item.get("evidence_columns") == HOME_EVIDENCE_COLUMNS, "evidence schema is invalid")
        evidence = {}
        for name in ("staging_evidence", "trigger_evidence", "activity_evidence", "low_evidence", "last_observation", "completion_evidence", "zero_evidence", "failure_evidence"):
            values = item.get(name)
            require(isinstance(values, list) and len(values) == len(HOME_EVIDENCE_COLUMNS), "missing " + name)
            e = dict(zip(HOME_EVIDENCE_COLUMNS, values))
            for key, limit in (("step", 67), ("event", 3), ("received_length", 0xFFFFFFFF), ("tx_accepted", 21 if name == "staging_evidence" or e.get("step") == 0 else 8),
                               ("frame_error", 255), ("earliest_us", 0xFFFFFFFFFFFFFFFF), ("latest_us", 0xFFFFFFFFFFFFFFFF), ("delivered_us", 0xFFFFFFFFFFFFFFFF)):
                require(uint(e.get(key), limit), name + " invalid " + key)
            for key in ("tx_complete", "response_confirmed", "qualified", "execution_unknown"):
                require(type(e.get(key)) is bool, name + " invalid " + key)
            require(all(type(e.get(k)) is int and -0x80000000 <= e[k] <= 0x7FFFFFFF for k in ("detail", "transport_detail")) and
                    e.get("status") in ("OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"), name + " invalid status")
            raw = e.get("raw_hex")
            require(isinstance(raw, str) and re.fullmatch(r"(?:[0-9A-Fa-f]{2}){0,9}", raw) is not None, name + " malformed raw frame")
            raw = bytes.fromhex(raw)
            require(len(raw) == min(e["received_length"], 9), name + " inconsistent raw length")
            expected = 21 if e["step"] == 0 else 8
            require(not e["tx_complete"] or e["tx_accepted"] == expected, name + " incomplete accepted TX")
            if e["delivered_us"]:
                require(item["started_us"] <= e["delivered_us"] <= item["serviced_us"], name + " delivery outside lifetime")
                require(e["event"] != 0 or e["tx_complete"], name + " frame lacks physical TX completion")
            require(e["event"] == 0 or not e["response_confirmed"], name + " local event claims response")
            if e["qualified"]:
                require(e["event"] == 0 and item["started_us"] <= e["earliest_us"] <= e["latest_us"] <= e["delivered_us"], name + " invalid timing")
            else: require(e["earliest_us"] == e["latest_us"] == 0, name + " unqualified bounds")
            if e["status"] == "OK": require(e["detail"] == e["frame_error"] == 0, name + " OK retains errors")
            evidence[name] = (e, raw)
        def checked(name, prefix):
            e, raw = evidence[name]
            return (e["event"] == 0 and e["tx_complete"] and e["response_confirmed"] and e["qualified"] and
                    e["status"] == "OK" and e["frame_error"] == e["detail"] == 0 and len(raw) == len(prefix) + 2 and
                    raw[:-2] == prefix and wire_crc(raw) == 0)
        for name, field, prefix in (("staging_evidence", "setup_execution", bytes((item["address"], 16, 0, 0x31, 0, 6))),
                                    ("trigger_evidence", "execution", bytes((item["address"], 6, 0, 0x27, 0, 16)))):
            e, raw = evidence[name]
            require(e["step"] == (0 if name == "staging_evidence" else 1) or not e["delivered_us"], field + " token differs")
            execution = item.get(field)
            require(execution in ("not_transmitted", "acknowledged", "rejected", "unknown"), field + " invalid")
            if execution == "acknowledged": require(checked(name, prefix), field + " lacks checked echo")
            elif execution == "not_transmitted": require(not e["tx_accepted"] and not e["execution_unknown"], field + " contradicts TX")
            elif execution == "rejected":
                require(e["qualified"] and e["response_confirmed"] and e["tx_complete"] and e["event"] == 0 and
                        e["status"] == "EXCEPTION" and e["frame_error"] == 10 and len(raw) == 5 and raw[:2] == bytes((item["address"], prefix[1] | 128)) and
                        1 <= raw[2] <= 7 and raw[2] == e["detail"] and wire_crc(raw) == 0, field + " lacks documented exception")
            else: require(e["tx_accepted"] > 0 or e["execution_unknown"], field + " lacks uncertainty")
            if e["qualified"] and e["status"] == "OK":
                late = e["latest_us"] > min(item["deadline_us"], evidence_age_deadline(p["observed_us"], p["maximum_age_us"]))
                require(not late or (not item["ok"] and item["outcome"] == "deadline" and item["failure_evidence"] == item[name]), "write exceeds readiness without retained deadline")
        require(item["staging_applied"] == (item["setup_execution"] == "acknowledged"), "staging application differs")
        stage, _ = evidence["staging_evidence"]
        trigger, _ = evidence["trigger_evidence"]
        if trigger["tx_accepted"] or trigger["execution_unknown"]:
            cap = min(item["deadline_us"], evidence_age_deadline(p["observed_us"], p["maximum_age_us"]))
            require(item["setup_execution"] == "acknowledged" and stage["latest_us"] <= cap and stage["delivered_us"] < cap and
                    stage["delivered_us"] <= (trigger["earliest_us"] if trigger["qualified"] else trigger["delivered_us"]),
                    "trigger precedes checked in-budget staging")
        require(item["uncertain"] == (not item["ok"] and any(evidence[n][0]["tx_accepted"] or evidence[n][0]["execution_unknown"] for n in ("staging_evidence", "trigger_evidence"))), "uncertainty differs")
        require(uint(item.get("raw_alarm"), 65535) and uint(item.get("raw_motion"), 65535) and
                isinstance(item.get("raw_position_words"), list) and len(item["raw_position_words"]) == 2 and all(uint(w, 65535) for w in item["raw_position_words"]), "raw outputs invalid")
        if item["ok"]:
            require(item["setup_execution"] == item["execution"] == "acknowledged" and item["homed_low_observed"] and item["observation_known"], "completion lacks fresh transition")
            comp, raw = evidence["completion_evidence"]
            require(len(raw) == 9 and checked("completion_evidence", raw[:-2]) and raw[:3] == bytes((item["address"], 3, 4)) and
                    raw[3:5] == b"\0\0" and int.from_bytes(raw[5:7], "big") & 0x7F == 3 and
                    evidence["trigger_evidence"][0]["delivered_us"] < comp["earliest_us"] and comp["latest_us"] <= item["deadline_us"], "completion lacks stopped homed FC03")
            require(comp["step"] == item["polls"] + 1 and comp["step"] >= 2 and item["phase"] == 3 and
                    item["raw_alarm"] == 0 and item["raw_motion"] == int.from_bytes(raw[5:7], "big") and
                    item["last_observation"] == item["completion_evidence"] and
                    evidence["zero_evidence"][0]["step"] == comp["step"] + 1, "completion/zero tokens or decoded outputs differ")
            require(checked("zero_evidence", bytes((item["address"], 3, 4, 0, 0, 0, 0))) and item["raw_position_words"] == [0, 0] and
                    comp["delivered_us"] < evidence["zero_evidence"][0]["earliest_us"] and
                    item["started_us"] <= item["completion_observed_us"] <= comp["earliest_us"] and
                    evidence["zero_evidence"][0]["delivered_us"] == item["serviced_us"] and evidence["zero_evidence"][0]["latest_us"] <= item["deadline_us"], "zero/reference evidence is inconsistent")
            if item["method"] != 35 or p["raw_motion"] & 2:
                low, raw = evidence["low_evidence"]
                require(len(raw) == 9 and checked("low_evidence", raw[:-2]) and raw[:3] == bytes((item["address"], 3, 4)) and
                        raw[3:5] == b"\0\0" and not int.from_bytes(raw[5:7], "big") & (0x7E if item["method"] == 35 else 0x7A) and
                        2 <= low["step"] < comp["step"] and evidence["trigger_evidence"][0]["delivered_us"] < low["earliest_us"] and
                        low["delivered_us"] < comp["earliest_us"], "completion lacks new homed-low evidence")
            if item["method"] != 35:
                activity, raw = evidence["activity_evidence"]
                require(item["running_observed"] and len(raw) == 9 and checked("activity_evidence", raw[:-2]) and raw[:3] == bytes((item["address"], 3, 4)) and
                        raw[3:5] == b"\0\0" and not int.from_bytes(raw[5:7], "big") & 0x78 and int.from_bytes(raw[5:7], "big") & 4 and
                        2 <= activity["step"] < comp["step"] and evidence["trigger_evidence"][0]["delivered_us"] < activity["earliest_us"] and
                        activity["delivered_us"] < comp["earliest_us"], "index search lacks new running evidence")
            else:
                require(not item["running_observed"] and not evidence["activity_evidence"][0]["delivered_us"], "current-position origin claims motion")
            require(not evidence["failure_evidence"][0]["delivered_us"] and item["status"] == "OK" and item["detail"] == 0, "success retains failure")
        else:
            failure, raw = evidence["failure_evidence"]
            require(failure["delivered_us"] == item["serviced_us"] and failure["delivered_us"] > 0, "failure lacks terminal evidence")
            require(item.get("status") in ("ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION") and
                    type(item.get("detail")) is int, "failure has invalid terminal status")
            outcome = item["outcome"]
            if outcome in ("cancelled", "transport_error"):
                event, detail = (2, 22) if outcome == "cancelled" else (1, 21)
                require(failure["event"] == event and failure["status"] == item["status"] == "ILLEGAL_VALUE" and
                        failure["detail"] == item["detail"] == detail, "local failure differs from terminal evidence")
            elif outcome == "deadline":
                cap = min(item["deadline_us"], evidence_age_deadline(p["observed_us"], p["maximum_age_us"])) if failure["step"] < 2 else item["deadline_us"]
                require(item["status"] == "ILLEGAL_VALUE" and item["detail"] in (15, 20) and
                        ((failure["event"] == 3 and failure["delivered_us"] >= cap and
                          failure["status"] == item["status"] and failure["detail"] == item["detail"]) or
                         (failure["event"] == 0 and failure["qualified"] and
                          (failure["latest_us"] > cap or
                           (failure["status"] == "OK" and failure["response_confirmed"] and
                            item["failure_evidence"] != item["zero_evidence"] and failure["delivered_us"] >= cap)))),
                        "deadline lacks expired transaction or next-step budget")
            elif outcome in ("timing_unqualified", "unconfirmed_response"):
                unqualified = outcome == "timing_unqualified"
                require(item["status"] == "ILLEGAL_VALUE" and item["detail"] == (23 if unqualified else 24) and
                        failure["event"] == 0 and failure["qualified"] == (not unqualified) and
                        (unqualified or (failure["status"] == "OK" and not failure["response_confirmed"])),
                        "response qualification failure differs from evidence")
            else:
                require(failure["event"] == 0 and failure["qualified"], "reply failure lacks qualified frame")
                if outcome == "observation_limit":
                    require(item["status"] == "ILLEGAL_VALUE" and item["detail"] == 26 and item["polls"] > 0 and
                            failure["status"] == "OK" and failure["response_confirmed"] and
                            item["failure_evidence"] == item["last_observation"],
                            "observation limit lacks final observation")
                elif failure["status"] != "OK":
                    require(item["status"] == failure["status"] and item["detail"] == failure["detail"],
                            "parser failure differs from terminal evidence")
                else:
                    require(failure["response_confirmed"] and item["status"] == "ILLEGAL_VALUE" and len(raw) == 9 and raw[:3] == bytes((item["address"], 3, 4)) and
                            wire_crc(raw) == 0 and item["detail"] in (25, 27, 31), "decoded failure lacks checked read")
                    first, second = int.from_bytes(raw[3:5], "big"), int.from_bytes(raw[5:7], "big")
                    require((item["detail"] == 25 and (first or second & 0x78)) or
                            (item["detail"] == 27 and (first or second) and item["failure_evidence"] == item["zero_evidence"]) or
                            (item["detail"] == 31 and item["method"] == 35 and not first and not second & 0x78 and second & 4),
                            "decoded failure reason differs from frame")

    @staticmethod
    def _check_action(item: dict, command: str, address: int | None, policy: str | None) -> None:
        """Separate a confirmed write acknowledgement from a later drive report."""
        def require(condition, message):
            if not condition:
                raise BenchError("action " + message)

        def integer(value, maximum=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and 0 <= value <= maximum


        empty_evidence = dict(step=0, event=0, raw_hex="", received_length=0, tx_accepted=0,
            tx_complete=False, response_confirmed=False, qualified=False, execution_unknown=False,
            earliest_us=0, latest_us=0, delivered_us=0, transport_detail=0, status="OK", detail=0, frame_error=0)

        def empty(entry):
            return all(entry.get(key) == value for key, value in empty_evidence.items())

        require(item.get("action_kind") == ACTION_KINDS.get(command)
                and item.get("stop_policy") == policy and (command != "stop" or policy in ("normal", "direct"))
                and item.get("read_kind") is None and item.get("capture_read", False) is False
                and item.get("recovery", False) is False, "kind or policy does not match request")
        require(integer(item.get("address"), 247) and item["address"] >= 1
                and (address is None or item["address"] == address), "address does not match acceptance")
        require(Console._operation_id(item.get("target")) and Console._operation_id(item.get("generation")),
                "target or generation is invalid")
        check_counts(item, ("started_us", "deadline_us", "serviced_us", "polls"), "action")
        require(item["started_us"] < item["deadline_us"] and item["serviced_us"] >= item["started_us"]
                and item["polls"] <= 64, "time budget or observation count is inconsistent")
        require(item.get("state") == ("succeeded" if item["ok"] else "failed"), "state is inconsistent")
        require(item.get("outcome") in ({"observed"} if item["ok"] else
                {"reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified",
                 "unconfirmed_response", "observation_limit"}), "outcome is inconsistent")
        require(item.get("execution") in ("not_transmitted", "acknowledged", "rejected", "unknown"),
                "execution evidence is invalid")
        require(item.get("completion") == ("observed" if item["ok"] else "not_observed"),
                "reported completion is inconsistent")
        require(type(item.get("observation_known")) is bool, "observation validity is missing")
        require(type(item.get("interrupted_by_stop")) is bool, "stop interruption evidence is missing")
        if command == "position-clear":
            require(type(item.get("device_position")) is int and item["device_position"] == 0 and item.get("position_clear_qualified") is True,
                    "device position clear was not zero-only and qualified")
            require(item["observation_known"] or item.get("raw_position") is None, "absent clear observation publishes a counter")
        evidence = {}
        for name in ("write_evidence", "last_observation", "failure_evidence"):
            entry = item.get(name)
            require(isinstance(entry, dict), "missing " + name)
            for key, limit in (("received_length", 0xFFFFFFFF), ("tx_accepted", 8), ("event", 3),
                               ("step", 64), ("frame_error", 255), ("earliest_us", 0xFFFFFFFFFFFFFFFF),
                               ("latest_us", 0xFFFFFFFFFFFFFFFF), ("delivered_us", 0xFFFFFFFFFFFFFFFF)):
                require(integer(entry.get(key), limit), name + " has invalid " + key)
            for key in ("tx_complete", "response_confirmed", "qualified", "execution_unknown"):
                require(type(entry.get(key)) is bool, name + " has invalid " + key)
            require(all(type(entry.get(key)) is int and -0x80000000 <= entry[key] <= 0x7FFFFFFF
                        for key in ("detail", "transport_detail"))
                    and entry.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED",
                        "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"}, name + " has invalid status")
            raw = entry.get("raw_hex")
            require(isinstance(raw, str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,9}", raw) is not None,
                    name + " raw frame is malformed")
            raw = bytes.fromhex(raw)
            require(len(raw) == min(entry["received_length"], 9), name + " length is inconsistent")
            if entry["status"] == "OK":
                require(entry["detail"] == entry["frame_error"] == 0, name + " successful codec retains an error")
            elif entry["status"] == "EXCEPTION":
                require(entry["event"] == 0 and len(raw) == 5
                        and raw[:2] == bytes((item["address"], 0x86 if entry["step"] == 0 else 0x83))
                        and wire_crc(raw) == 0 and entry["detail"] == raw[2] and entry["frame_error"] == 10,
                        name + " exception differs from checked frame")
            require(not entry["tx_complete"] or entry["tx_accepted"] == 8, name + " physical TX lacks full acceptance")
            if not empty(entry):
                require(item["started_us"] <= entry["delivered_us"] <= item["serviced_us"],
                        name + " delivery is outside the action lifetime")
                require(entry["event"] != 0 or entry["tx_complete"], name + " frame precedes physical TX completion")
            require(entry["event"] == 0 or not entry["response_confirmed"],
                    name + " local event claims a confirmed response")
            if entry["qualified"]:
                require(entry["event"] == 0 and item["started_us"] <= entry["earliest_us"] <= entry["latest_us"]
                        <= entry["delivered_us"] <= item["serviced_us"], name + " timing is inconsistent")
            else:
                require(entry["earliest_us"] == entry["latest_us"] == 0, name + " unqualified bounds are published")
            evidence[name] = (entry, raw)
        write, raw = evidence["write_evidence"]
        require(write["step"] == 0 and not empty(write), "write token or evidence is inconsistent")
        reg, value = {"enable": (0x2D, 0x12), "motor-release": (0x2D, 0x11),
                      "alarm-clear": (0x2D, 0x21), "position-clear": (0x2D, 0x31),
                      "stop": (0x27, 0x100 if policy == "normal" else 0x200)}[command]
        unconfirmed_write = (item["execution"] == "unknown" and
            write["event"] == 0 and write["qualified"] and not write["response_confirmed"] and
            write["tx_complete"] and write["tx_accepted"] == 8 and write["status"] == "OK" and
            len(raw) == 8 and raw[:6] == bytes((item["address"], 6, reg >> 8, reg & 255, value >> 8, value & 255)) and
            wire_crc(raw) == 0 and write["latest_us"] <= item["deadline_us"])
        observable_write = item["execution"] == "acknowledged" or unconfirmed_write
        if item["execution"] in ("acknowledged", "rejected"):
            require(write["qualified"] and write["response_confirmed"] and write["tx_complete"]
                    and write["event"] == 0 and wire_crc(raw) == 0, "acknowledgement lacks confirmed drive response")
            if item["execution"] == "acknowledged":
                reg, value = {"enable": (0x2D, 0x12), "motor-release": (0x2D, 0x11),
                              "alarm-clear": (0x2D, 0x21), "position-clear": (0x2D, 0x31), "stop": (0x27, 0x100 if policy == "normal" else 0x200)}[command]
                require(raw[:6] == bytes((item["address"], 6, reg >> 8, reg & 255, value >> 8, value & 255))
                        and len(raw) == 8 and write["status"] == "OK" and write["frame_error"] == 0,
                        "write echo differs from requested action")
            else:
                require(len(raw) == 5 and raw[:2] == bytes((item["address"], 0x86))
                        and 1 <= raw[2] <= 7 and write["detail"] == raw[2]
                        and write["status"] == "EXCEPTION" and write["frame_error"] == 10,
                        "device rejection lacks checked documented exception")
        elif item["execution"] == "not_transmitted":
            require(write["tx_accepted"] == 0 and not write["execution_unknown"], "non-transmission contradicts TX evidence")
        else:
            require(write["tx_accepted"] > 0 or write["execution_unknown"], "unknown execution lacks transmission uncertainty")
            require(not (write["qualified"] and write["response_confirmed"]
                         and (write["status"] == "OK" or (write["status"] == "EXCEPTION" and 1 <= write["detail"] <= 7))),
                    "unknown execution contradicts a checked acknowledgement or documented rejection")
        if item["observation_known"]:
            obs, raw = evidence["last_observation"]
            require(observable_write and item["polls"] >= 1
                    and obs["step"] == item["polls"] and obs["event"] == 0 and obs["qualified"]
                    and obs["response_confirmed"] and obs["tx_complete"] and obs["status"] == "OK"
                    and obs["frame_error"] == 0 and obs["received_length"] == 9 and wire_crc(raw) == 0
                    and raw[:3] == bytes((item["address"], 3, 4)), "observation lacks checked FC03 evidence")
            require(write["delivered_us"] < obs["earliest_us"] <= obs["latest_us"] <= item["deadline_us"],
                    "observation precedes acknowledgement or exceeds the deadline")
            alarm, motion = int.from_bytes(raw[3:5], "big"), int.from_bytes(raw[5:7], "big")
            if command == "position-clear":
                require(item.get("raw_alarm") is None and item.get("raw_motion") is None and
                        type(item.get("raw_position")) is int and item["raw_position"] == (alarm << 16 | motion),
                        "clear-position observation differs from retained RX")
                observed = item["raw_position"] == 0
            else:
                require(type(item.get("raw_alarm")) is int and item["raw_alarm"] == alarm
                    and type(item.get("raw_motion")) is int and item["raw_motion"] == motion,
                    "decoded report differs from retained RX")
                observed = {"enable": not bool(motion & 16), "motor-release": bool(motion & 16),
                        "alarm-clear": alarm == 0 and not bool(motion & 8), "stop": not bool(motion & 4)}[command]
            require(observed == item["ok"], "status differs from requested reported completion")
        else:
            require(item.get("raw_alarm") is None and item.get("raw_motion") is None and not item["ok"]
                    and item["polls"] == 0 and empty(evidence["last_observation"][0]),
                    "unknown observation publishes completion or decoded values")
        failure, _ = evidence["failure_evidence"]
        if item["ok"]:
            require(empty(failure), "successful action retains failure evidence")
            require(evidence["last_observation"][0]["delivered_us"] == item["serviced_us"],
                    "successful action service differs from final observation")
        else:
            require(not empty(failure) and failure["delivered_us"] == item["serviced_us"],
                    "failure lacks terminal event evidence")
            require(failure["step"] in (item["polls"], item["polls"] + 1),
                    "failure token skips an observation")
            if failure["step"] == 0:
                require(failure == write, "failed write differs from retained write evidence")
            else:
                require(observable_write, "observation failure precedes a checked write")
                previous = evidence["last_observation"][0] if item["observation_known"] else write
                if failure["step"] == item["polls"]:
                    require(failure == previous, "failed observation differs from retained observation")
                else:
                    require(failure["delivered_us"] >= previous["delivered_us"]
                            and (not failure["qualified"] or failure["earliest_us"] > previous["delivered_us"]),
                            "failure precedes the previous transaction")
            if failure["event"] != 0:
                expected = {1: "transport_error", 2: "cancelled", 3: "deadline"}[failure["event"]]
            elif not failure["qualified"]:
                expected = "timing_unqualified"
            elif failure["latest_us"] > item["deadline_us"]:
                expected = "deadline"
            elif failure["status"] != "OK":
                expected = "reply_error"
            elif not failure["response_confirmed"] and not (failure["step"] == 0 and unconfirmed_write):
                expected = "unconfirmed_response"
            else:
                require(item["outcome"] in ("deadline", "observation_limit")
                        and (failure["step"] > 0 or item["outcome"] == "deadline"),
                        "checked response does not establish the failure")
                expected = item["outcome"]
            require(item["outcome"] == expected, "failure outcome contradicts terminal evidence")
            if expected == "deadline":
                require(item["serviced_us"] >= item["deadline_us"], "deadline failure precedes deadline")

    @staticmethod
    def _check_typed_read(item: dict, kind: str, address: int | None) -> None:
        """Check retained FC03 provenance and decoded fields independently."""
        def require(condition, message):
            if not condition:
                raise BenchError("typed-read " + message)

        def integer(value, maximum=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and 0 <= value <= maximum

        def frame(value, maximum):
            require(isinstance(value, str) and len(value) <= 2 * maximum
                    and re.fullmatch(r"(?:[0-9A-Fa-f]{2})*", value) is not None,
                    "raw frame is malformed")
            return bytes.fromhex(value)

        require(item.get("read_kind") == kind and item.get("recovery", False) is False
                and item.get("capture_read", False) is False, "result kind is inconsistent")
        require(integer(item.get("address"), 247) and item["address"] >= 1
                and (address is None or item["address"] == address), "address does not match acceptance")
        require(Console._operation_id(item.get("target")) and Console._operation_id(item.get("generation")),
                "target or generation is invalid")
        check_counts(item, ("started_us", "deadline_us", "serviced_us", "completed_steps"), "typed-read")
        require(item["started_us"] < item["deadline_us"] and item["serviced_us"] >= item["started_us"],
                "absolute time budget is inconsistent")
        statuses = {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"}
        require(type(item.get("status")) is str and item["status"] in statuses
                and type(item.get("detail")) is int and -0x80000000 <= item["detail"] <= 0x7FFFFFFF,
                "status evidence is invalid")
        require(item.get("state") == ("succeeded" if item["ok"] else "failed"), "terminal state is inconsistent")
        outcome = item.get("outcome")
        require(type(outcome) is str and (outcome == "success" if item["ok"] else outcome in
                {"reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified"}),
                "terminal outcome is inconsistent")
        serial = item.get("active_serial")
        require(isinstance(serial, dict) and type(serial.get("known")) is bool, "active tuple is unavailable")
        for key, maximum in (("baud", 0xFFFFFFFF), ("data_bits", 255), ("parity", 3), ("stop_bits", 255)):
            require(integer(serial.get(key), maximum), "active tuple is invalid")
        if serial["known"]:
            require(serial["baud"] in HOST_BAUDS and serial["data_bits"] == 8
                    and (serial["parity"], serial["stop_bits"]) in ((1, 1), (1, 2), (2, 1), (3, 1)),
                    "active tuple is outside reviewed ESS host formats")
        windows = TYPED_WINDOWS[kind]
        steps = item.get("steps")
        complete = item["completed_steps"]
        require(isinstance(steps, list) and complete <= len(windows)
                and complete <= len(steps) <= min(len(windows), complete + 1), "step counts are inconsistent")
        if item["ok"]:
            require(complete == len(windows) and len(steps) == complete and item["status"] == "OK"
                    and item["detail"] == 0, "successful operation is incomplete")
        decoded_words = []
        previous_delivery = item["started_us"]
        for index, step in enumerate(steps):
            require(isinstance(step, dict), "step is not an object")
            first, count = windows[index]
            require(type(step.get("step")) is int and step["step"] == index
                    and type(step.get("first")) is int and step["first"] == first
                    and type(step.get("count")) is int and step["count"] == count, "step/window order is inconsistent")
            require(integer(step.get("event"), 3) and type(step.get("status")) is str
                    and step["status"] in statuses and integer(step.get("frame_error"), 10)
                    and type(step.get("qualified")) is bool and type(step.get("execution_unknown")) is bool
                    and integer(step.get("tx_accepted"), 8)
                    and type(step.get("transport_detail")) is int
                    and -0x80000000 <= step["transport_detail"] <= 0x7FFFFFFF,
                    "step evidence is invalid")
            check_counts(step, ("attempted_us", "earliest_us", "latest_us", "delivered_us", "received_length"), "typed-read step")
            require(type(step.get("detail")) is int and -0x80000000 <= step["detail"] <= 0x7FFFFFFF,
                    "step status detail is invalid")
            require(previous_delivery <= step["delivered_us"] <= item["serviced_us"], "step delivery order is inconsistent")
            require(previous_delivery <= step["attempted_us"] <= step["delivered_us"], "step eligibility order is inconsistent")
            if step["qualified"]:
                require(step["event"] == 0 and previous_delivery <= step["earliest_us"]
                        <= step["latest_us"] <= step["delivered_us"], "closure bounds are inconsistent")
            else:
                require(step["earliest_us"] == step["latest_us"] == 0, "unqualified step invents closure bounds")
            tx, rx = frame(step.get("tx"), 8), frame(step.get("rx"), 37)
            require(len(tx) == 8 and wire_crc(tx) == 0
                    and tx[:6] == bytes((item["address"], 3, first >> 8, first & 255, 0, count)),
                    "retained request does not match reviewed FC03 window")
            require(len(rx) == min(step["received_length"], 37), "captured prefix length is inconsistent")
            if step["event"] == 0:
                expected = _reply_status(rx, step["received_length"], item["address"], 3, 5 + 2 * count)
                require((step["status"], step["detail"], step["frame_error"]) == expected,
                        "codec evidence differs from retained RX")
            else:
                require(not step["qualified"] and step["status"] == "ILLEGAL_VALUE", "control event evidence is inconsistent")
            if index < complete:
                require(step["event"] == 0 and step["status"] == "OK" and step["qualified"]
                        and step["tx_accepted"] == 8 and step["execution_unknown"] is False
                        and step["latest_us"] <= item["deadline_us"]
                        and (index == len(windows) - 1 or not item["ok"] or step["delivered_us"] < item["deadline_us"]),
                        "completed step lacks on-time checked closure")
                decoded_words.extend(int.from_bytes(rx[3 + 2 * word:5 + 2 * word], "big") for word in range(count))
            previous_delivery = step["delivered_us"]
        decoded = item.get(kind)
        if kind == "state":
            Console._check_state_values(item, decoded_words)
        if not item["ok"]:
            require(kind == "state" or (kind in item and decoded is None), "failed operation published a decoded observation")
            require(bool(steps), "failed operation lacks retained terminal evidence")
            last = steps[-1]
            if outcome == "reply_error":
                require(last["event"] == 0 and last["qualified"] and last["latest_us"] <= item["deadline_us"]
                        and last["status"] != "OK" and item["status"] == last["status"]
                        and item["detail"] == last["detail"], "failure outcome differs from last step")
            elif outcome in ("transport_error", "cancelled"):
                require(last["event"] == (1 if outcome == "transport_error" else 2)
                        and item["status"] == last["status"] == "ILLEGAL_VALUE"
                        and item["detail"] == last["detail"], "failure outcome differs from last step")
            elif outcome == "timing_unqualified":
                require(last["event"] == 0 and not last["qualified"] and item["status"] == "ILLEGAL_VALUE",
                        "failure outcome differs from last step")
            else:
                expired_frame = (last["event"] == 0 and last["qualified"]
                                 and last["latest_us"] > item["deadline_us"])
                partial_budget = (complete == len(steps) < len(windows)
                                  and item["serviced_us"] >= item["deadline_us"])
                expired_control = last["event"] == 3 and last["delivered_us"] >= item["deadline_us"]
                require(item["status"] == "ILLEGAL_VALUE" and (expired_frame or partial_budget or expired_control),
                        "deadline outcome lacks absolute expiry evidence")
            return
        if kind == "state":
            return
        require(isinstance(decoded, dict), "successful operation lacks decoded observation")
        if kind == "identity":
            keys = ("raw_model", "raw_version", "raw_active_node", "raw_dip")
            for key, value in zip(keys, decoded_words):
                require(type(decoded.get(key)) is int and decoded[key] == value, "identity differs from retained RX")
            known = 1 <= decoded_words[2] <= 247
            require(type(decoded.get("active_node_known")) is bool and decoded["active_node_known"] == known
                    and type(decoded.get("active_node")) is int and decoded["active_node"] == (decoded_words[2] if known else 0),
                    "active node interpretation is inconsistent")
            for key, value in (("model_resolution", 2), ("version_resolution", 3), ("dip_resolution", 4), ("dip_issues", 320)):
                require(type(decoded.get(key)) is int and decoded[key] == value, "identity mapping claims unsupported certainty")
            require(decoded.get("model") == decoded.get("firmware") == "mapping_unresolved"
                    and decoded.get("dip") == "mapping_conflict_unresolved", "identity mapping claims unsupported certainty")
            return
        keys = ("direction", "subdivision", "custom_node", "baud", "format", "over_limit_stop", "soft_limit_enable",
                "word_order", "input_polarity", "algorithm", "encoder_resolution")
        values = decoded_words[:9] + decoded_words[13:15]
        raw, known = decoded.get("raw"), decoded.get("known")
        require(isinstance(raw, dict) and isinstance(known, dict), "configuration raw/known evidence is unavailable")
        for key, value in zip(keys, values):
            require(type(raw.get(key)) is int and raw[key] == value, "configuration differs from retained RX")
        require(decoded.get("stored_serial") == {"baud_code": raw["baud"], "format_code": raw["format"],
                                                   "activation": "power_cycle_required"}
                and decoded.get("units") == "command_scale_unresolved_encoder_readback_only",
                "stored settings were promoted to active transport or physical units")
        for key, valid in (("direction", raw["direction"] <= 1), ("baud", raw["baud"] <= 3),
                           ("format", raw["format"] <= 3), ("over_limit_stop", raw["over_limit_stop"] <= 1),
                           ("soft_limit_enable", raw["soft_limit_enable"] <= 1), ("word_order", raw["word_order"] <= 1),
                           ("algorithm", raw["algorithm"] in (1, 2))):
            require(type(known.get(key)) is bool and known[key] == valid, "unknown configuration code was normalized")
        for key, value in (("unknown_polarity_bits", raw["input_polarity"] & 0xFFF0),
                           ("subdivision_resolution", 5), ("encoder_resolution", 0 if raw["encoder_resolution"] else 6),
                           ("subdivision_issues", 4), ("custom_node_issues", 64), ("over_limit_stop_issues", 1024),
                           ("soft_limit_issues", 256), ("encoder_scale_source", 3 if raw["encoder_resolution"] else 0)):
            require(type(decoded.get(key)) is int and decoded[key] == value, "configuration provenance is inconsistent")
        inputs = decoded.get("inputs")
        require(isinstance(inputs, list) and len(inputs) == 4, "configuration input evidence is incomplete")
        for index, entry in enumerate(inputs):
            function = decoded_words[9 + index]
            require(isinstance(entry, dict) and type(entry.get("function")) is int and entry["function"] == function
                    and type(entry.get("known")) is bool and entry["known"] == (function <= 17)
                    and type(entry.get("inverted")) is bool and entry["inverted"] == bool(raw["input_polarity"] & (1 << index))
                    and integer(entry.get("wiring"), 2) and entry.get("level_known") is False
                    and "level" in entry and entry["level"] is None, "input assignment/wiring/level evidence is inconsistent")

    @staticmethod
    def _check_state_values(item: dict, words: list[int]) -> None:
        def require(condition, message):
            if not condition:
                raise BenchError("typed-read state " + message)

        blocks, config = item.get("state_blocks"), item.get("decode_config")
        require(isinstance(blocks, list) and len(blocks) == item["completed_steps"], "partial blocks are inconsistent")
        require(isinstance(config, dict) and type(config.get("operation_id")) is int
                and 0 <= config["operation_id"] <= 0xFFFFFFFF, "decode configuration is invalid")
        for flag, code, choices in (("word_order_known", "word_order", (0, 1)),
                                    ("algorithm_known", "algorithm", (1, 2))):
            require(type(config.get(flag)) is bool and type(config.get(code)) is int
                    and config[code] in choices and (not config[flag] or config["operation_id"] != 0),
                    "decode configuration is invalid")
        for index, block in enumerate(blocks):
            Console._check_state_block(block, index, words, config)

    @staticmethod
    def _check_state_block(block: dict, index: int, words: list[int] | None = None,
                           config: dict | None = None) -> None:
        """Check one bounded raw/decoded block for both live and cached reports."""
        def require(condition, message):
            if not condition:
                raise BenchError("typed-read state " + message)

        def raw(key, maximum=0xFFFF):
            value = block.get(key)
            require(type(value) is int and 0 <= value <= maximum, "raw field is invalid: " + key)
            return value

        require(isinstance(block, dict) and type(block.get("block")) is int and block["block"] == index,
                "block identity is inconsistent")
        config_id = raw("config_operation_id", 0xFFFFFFFF)
        if config is not None:
            require(config_id == config["operation_id"], "block configuration is inconsistent")
        if index == 0:
            alarm, motion = (words[:2] if words is not None else (raw("raw_alarm"), raw("raw_motion")))
            expected = dict(raw_alarm=alarm, alarm_known=alarm in (0, 1, 2, 3, 5), raw_motion=motion,
                            unknown_motion_bits=motion & 0xFF80, in_position=bool(motion & 1),
                            homing_complete=bool(motion & 2), running=bool(motion & 4), alarm_flag=bool(motion & 8),
                            released=bool(motion & 16), enabled=not bool(motion & 16),
                            positive_soft_limit=bool(motion & 32), negative_soft_limit=bool(motion & 64))
        elif index == 1:
            inputs, outputs = (words[2:4] if words is not None else (raw("raw_inputs"), raw("raw_outputs")))
            expected = dict(raw_inputs=inputs, raw_outputs=outputs, unknown_input_bits=inputs & 0xFFF0,
                            unknown_output_bits=outputs & 0xFFFC,
                            inputs=[bool(inputs & (1 << bit)) for bit in range(4)],
                            outputs=[bool(outputs & (1 << bit)) for bit in range(2)], levels="logical_valid_not_voltage")
        else:
            position = block.get("position_words")
            require(isinstance(position, list) and len(position) == 2
                    and all(type(value) is int and 0 <= value <= 0xFFFF for value in position),
                    "position words are invalid")
            first, second, speed = words[4:7] if words is not None else (*position, raw("raw_speed"))
            if config is not None:
                pair_known = config["word_order_known"]
                raw_position = ((first << 16 | second) if config["word_order"] == 0 else (second << 16 | first)) if pair_known else 0
                source = config["algorithm"] if config["algorithm_known"] else 0
            else:
                pair_known = block.get("pair_known")
                source = block.get("position_source")
                require(type(pair_known) is bool and type(source) is int and source in (0, 1, 2),
                        "position interpretation is invalid")
                require(config_id != 0 or (not pair_known and source == 0), "position lacks configuration provenance")
                raw_position = raw("raw_position", 0xFFFFFFFF)
                require(raw_position in ((first << 16 | second), (second << 16 | first)) if pair_known else raw_position == 0,
                        "paired position differs from raw words")
            expected = dict(position_words=[first, second], raw_speed=speed, pair_known=pair_known,
                            raw_position=raw_position, position_source=source,
                            word_order_resolution=0 if pair_known else 9,
                            position_source_resolution=0 if source else 10,
                            position_signed_resolution=7, position_scale_resolution=5,
                            speed_signed_resolution=7, speed_unit_resolution=8,
                            physical_units="unresolved", raw_encoder_counts=None)
        for key, value in expected.items():
            require(key in block and type(block[key]) is type(value) and block[key] == value,
                    "decoded field differs from retained RX: " + key)
        if index == 1:
            require(all(type(value) is bool for value in block["inputs"] + block["outputs"]), "logical levels are not booleans")

    @staticmethod
    def _check_cached_state(item: dict) -> None:
        def require(condition, message):
            if not condition:
                raise BenchError("cached state " + message)
        check_counts(item, ("now_us", "stale_after_ms", "selected_target", "selected_address", "selected_generation"), "cached state")
        require(item.get("monitoring") in ("enabled", "disabled") and item.get("atomic_snapshot") is False
                and item.get("sample_time") == "drive_internal_age_undocumented", "snapshot claims unsupported certainty")
        blocks = item.get("state_blocks")
        require(isinstance(blocks, list) and len(blocks) == 3, "observation blocks are incomplete")
        for index, block in enumerate(blocks):
            require(isinstance(block, dict) and type(block.get("block")) is int and block["block"] == index, "block identity is invalid")
            for key in ("valid", "current", "fresh", "attempt_known", "last_attempt_ok"):
                require(type(block.get(key)) is bool, "block flags are invalid")
            check_counts(block, ("target", "address", "generation", "operation_id", "last_attempt_us", "last_attempt_target",
                               "last_attempt_address", "last_attempt_generation", "last_attempt_operation_id", "last_success_us",
                               "observed_earliest_us", "observed_latest_us", "delivered_us", "invalidated_us"), "cached state block")
            require(block["last_attempt_us"] <= item["now_us"], "last attempt is in the future")
            require(type(block.get("last_attempt_status")) is str and type(block.get("last_attempt_detail")) is int, "attempt error is unavailable")
            if index == 2 and not block["valid"]:
                require(type(block.get("current_config_operation_id")) is int
                        and 0 <= block["current_config_operation_id"] <= 0xFFFFFFFF
                        and block.get("interpretation_current") is False,
                        "absent feedback interpretation is invalid")
            if not block["valid"]:
                require(not block["current"] and not block["fresh"] and block.get("value") is None
                        and block.get("age_us") is None and block.get("source") == "absent", "absent feedback was fabricated")
                continue
            require(isinstance(block.get("value"), dict) and block["value"].get("block") == index
                    and block.get("source") == "checked_rtu_register", "valid observation source is invalid")
            Console._check_state_block(block["value"], index)
            if index == 2:
                config_id = block.get("current_config_operation_id")
                require(type(config_id) is int and 0 <= config_id <= 0xFFFFFFFF
                        and type(block.get("interpretation_current")) is bool
                        and block["interpretation_current"] == (config_id != 0
                            and config_id == block["value"]["config_operation_id"]),
                        "feedback interpretation generation is inconsistent")
            require(0 <= block["observed_earliest_us"] <= block["observed_latest_us"] <= block["delivered_us"] <= item["now_us"]
                    and block["last_success_us"] == block["observed_latest_us"], "observation timing bounds are invalid")
            invalidated = block.get("invalidated_us")
            require(type(invalidated) is int and 0 <= invalidated <= item["now_us"], "action invalidation watermark is invalid")
            expected_current = all(block[key] == item["selected_" + key] for key in ("target", "address", "generation"))
            expected_current = expected_current and (invalidated == 0 or block["observed_earliest_us"] > invalidated)
            age = item["now_us"] - block["observed_earliest_us"]
            require(type(block.get("age_us")) is int and block["age_us"] == age and block["current"] == expected_current
                    and block["fresh"] == (expected_current and (item["stale_after_ms"] == 0 or age <= item["stale_after_ms"] * 1000)), "observation age or generation is inconsistent")
        if item.get("command") == "health":
            require(item.get("readiness") == "unknown", "observation claims unsupported readiness")
            motion = blocks[0]
            value = motion.get("value")
            expected_alarms = "unknown" if not motion["fresh"] else "present" if value["alarm_flag"] or 1 <= value["raw_alarm"] <= 5 else "clear" if value["raw_alarm"] == 0 else "unknown"
            require(item.get("alarms") == expected_alarms, "alarm presence differs from checked motion evidence")
            require(item.get("state") == ("observed" if motion["fresh"] else "unknown"), "state freshness differs from block evidence")
        require(type(item.get("communication_known")) is bool and item.get("age_source") == "model_probe", "communication evidence is unavailable")
        check_counts(item, ("communication_target", "communication_address", "communication_generation", "communication_earliest_us", "communication_latest_us"), "cached communication")
        if not item["communication_known"]:
            require(item.get("communication_age_us") is None, "absent communication age was fabricated")
        if item["communication_known"]:
            require(type(item.get("communication_age_us")) is int and item["communication_age_us"] == item["now_us"] - item["communication_earliest_us"], "communication age differs from evidence")
            require(0 <= item["communication_earliest_us"] <= item["communication_latest_us"] <= item["now_us"], "communication timing bounds are invalid")
            if item.get("command") == "health" and item.get("communication") not in ("unavailable", "failed"):
                same = all(item["communication_" + key] == item["selected_" + key] for key in ("target", "address", "generation"))
                expired = item["stale_after_ms"] != 0 and item["now_us"] - item["communication_earliest_us"] > item["stale_after_ms"] * 1000
                expected = "unknown" if not same else "stale" if expired else "current"
                require(item.get("communication") == expected, "communication freshness differs from evidence")

    @staticmethod
    def _check_recovery(item: dict) -> None:
        outcome = item.get("outcome")
        if (item.get("recovery") is not True
                or item.get("capture_read", False) is not False
                or item.get("read_kind") is not None
                or outcome not in ("recovered", "expired", "read_error", "transport_error")
                or item["ok"] != (outcome == "recovered")
                or not isinstance(item.get("transport"), str)
                or (item["ok"] and item["transport"] != "NONE")):
            raise BenchError("recovery terminal lacks consistent result evidence")
        check_counts(item, ("requested_us", "deadline_us", "finished_us"), "recovery")
        if (item["requested_us"] >= item["deadline_us"]
                or item["finished_us"] < item["requested_us"]
                or (item["ok"] and item["finished_us"] >= item["deadline_us"])):
            raise BenchError("recovery terminal deadlines are inconsistent")

    @staticmethod
    def _check_axis(item: dict, preparation: bool) -> None:
        """Validate host preparation evidence without implementing conversion."""
        def require(condition, message):
            if not condition:
                raise BenchError("host axis " + message)

        def integer(value, low, high):
            return type(value) is int and low <= value <= high

        require(item.get("bus_traffic") is False and item.get("motion_command") is False,
                "reply claims a device action")
        require(item.get("code") in ("OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED")
                and type(item.get("detail")) is int and item["ok"] == (item["code"] == "OK"),
                "validation outcome is inconsistent")
        if not item["ok"]:
            return
        require(item.get("wire_motion") == "not_requested", "host preview claims a wire motion request")
        require(integer(item.get("configuration_generation"), 1 if preparation else 0, 0xFFFFFFFF),
                "configuration generation is invalid")
        for name in ("target", "binding_generation"):
            require(integer(item.get(name), 1, 0xFFFFFFFF), "generation or target is invalid")
        require(integer(item.get("address"), 1, 247), "selected ESS address is invalid")
        if not preparation:
            scales = item.get("operator_scales")
            require(isinstance(scales, list) and len(scales) == 5, "scale inventory is incomplete")
            for scale in scales:
                require(isinstance(scale, dict) and integer(scale.get("numerator"), 0, 0xFFFFFFFF)
                        and integer(scale.get("denominator"), 1, 0xFFFFFFFF)
                        and integer(scale.get("source"), 0, 4), "scale provenance is invalid")
                require((scale["numerator"] == 0 and scale["denominator"] == 1) if scale["source"] == 0
                        else scale["numerator"] > 0, "known/unknown scale is contradictory")
            return
        requested = item.get("requested")
        require(isinstance(requested, dict) and integer(requested.get("numerator"), -2**63, 2**63 - 1)
                and integer(requested.get("denominator"), 1, 2**64 - 1)
                and requested.get("unit") in ("steps", "fullsteps", "counts", "turn", "deg", "rad", "mm")
                and integer(requested.get("frame"), 0, 2) and integer(requested.get("basis"), 0, 2)
                and integer(requested.get("rounding"), 0, 4) and type(requested.get("relative")) is bool
                and type(requested.get("wrapped")) is bool and integer(requested.get("angle_path"), 0, 2)
                and integer(requested.get("half_turn_tie"), 0, 2),
                "exact request is incomplete")
        require(not requested["wrapped"] or (not requested["relative"] and requested["frame"] != 0 and
                requested["unit"] in ("turn", "deg", "rad")), "wrapped preview lacks an explicit angular frame")
        for name in ("effective_native", "endpoint_native", "displacement_native"):
            require(integer(item.get(name), -2**63, 2**63 - 1), "native integer is invalid")
        for name in ("endpoint_known", "displacement_known", "zero_displacement", "exact_arithmetic"):
            require(type(item.get(name)) is bool, "validity is missing")
        require(item["zero_displacement"] == (item["displacement_known"] and item["displacement_native"] == 0),
                "zero displacement is inconsistent")
        require(not requested["relative"] or (item["displacement_known"]
                and item["displacement_native"] == item["effective_native"]), "relative displacement changed")
        require(requested["relative"] or (item["endpoint_known"]
                and item["endpoint_native"] == item["effective_native"]), "absolute endpoint changed")
        for name in ("rounding_error", "approximation_error_bound"):
            require(type(item.get(name)) in (int, float) and math.isfinite(item[name]), "error provenance is invalid")
        require(item["approximation_error_bound"] >= 0, "negative approximation bound")
        native = item.get("requested_native")
        if item["exact_arithmetic"]:
            require(isinstance(native, dict) and integer(native.get("integral"), -2**63, 2**63 - 1)
                    and integer(native.get("denominator"), 1, 2**64 - 1)
                    and integer(native.get("numerator"), 0, native["denominator"] - 1)
                    and type(native.get("negative")) is bool and item.get("requested_native_approximate") is None
                    and item["approximation_error_bound"] == 0, "exact native provenance is invalid")
        else:
            value = item.get("requested_native_approximate")
            require(native is None and requested["unit"] == "rad" and type(value) in (int, float)
                    and math.isfinite(value), "approximate native provenance is invalid")

    @staticmethod
    def _check_host_serial(item: dict, expected: dict | None = None) -> None:
        serial = item.get("host_serial")
        if serial is None and expected is None:
            return  # Older fake/consumer hooks do not supply serial provenance.
        if not isinstance(serial, dict) or type(serial.get("known")) is not bool:
            raise BenchError("host serial provenance is missing or invalid")
        if serial["known"]:
            valid = (type(serial.get("baud")) is int and serial["baud"] in HOST_BAUDS
                     and serial.get("format") in HOST_FORMATS
                     and Console._operation_id(serial.get("generation")))
        else:
            valid = (type(serial.get("baud")) is int and serial["baud"] == 0
                     and serial.get("format") == "unknown"
                     and type(serial.get("generation")) is int and serial["generation"] == 0)
        if not valid or (expected is not None and serial != expected):
            raise BenchError("host serial provenance does not match operation admission")
        active = item.get("active_serial")
        if isinstance(active, dict) and serial["known"]:
            parity, stops = {"8N1": (1, 1), "8N2": (1, 2), "8E1": (2, 1), "8O1": (3, 1)}[serial["format"]]
            if active != dict(known=True, baud=serial["baud"], data_bits=8, parity=parity, stop_bits=stops):
                raise BenchError("typed-read active serial differs from historical host tuple")

    def _check_host(self, handle: Command, item: dict) -> None:
        def require(condition, message):
            if not condition:
                raise BenchError("host " + message)

        if "active" not in item:
            require(item["ok"] is False and item.get("result") in
                    {"unavailable", "invalid_tuple", "unsupported", "active_tuple_unknown", "invalid_arguments", "output_full"},
                    "reply lacks tuple state")
            return
        for name in ("original", "requested", "active"):
            value = item.get(name)
            require(isinstance(value, dict) and set(value) == {"baud", "format"}
                    and type(value["baud"]) is int and value["baud"] in HOST_BAUDS
                    and value["format"] in HOST_FORMATS, "tuple is unreviewed")
        for name in ("active_known", "blocked", "configuring"):
            require(type(item.get(name)) is bool, "state flags are invalid")
        require(item["configuring"] is False and Console._operation_id(item.get("serial_generation")), "change is unsettled")
        require(item.get("device_settings_changed") is False, "claims device settings changed")
        require(item.get("failure") in {"none", "adapter", "runner", "restore"}, "failure is invalid")
        require(type(item.get("actual_baud")) is int and 0 <= item["actual_baud"] <= 4000000,
                "programmed baud is invalid")
        require((item["active_known"] and not item["blocked"] and item["failure"] == "none")
                or (not item["active_known"] and item["blocked"] and item["failure"] != "none"),
                "failure and active state disagree")
        require(item["active_known"] or item["actual_baud"] == 0, "unknown tuple claims a programmed baud")
        require(not item["active_known"] or item["active"]["baud"] * 98 <= item["actual_baud"] * 100 <= item["active"]["baud"] * 102,
                "programmed baud is unknown or outside character timing tolerance")
        rates, formats = item.get("supported_bauds"), item.get("supported_formats")
        require(isinstance(rates, list) and 1 <= len(rates) <= 4
                and all(type(rate) is int and rate in HOST_BAUDS for rate in rates)
                and len(set(rates)) == len(rates), "baud capabilities are invalid")
        require(isinstance(formats, list) and 1 <= len(formats) <= 4
                and all(type(fmt) is str and fmt in HOST_FORMATS for fmt in formats)
                and len(set(formats)) == len(formats), "format capabilities are invalid")
        require(all(item[name]["baud"] in rates and item[name]["format"] in formats
                    for name in ("original", "requested", "active")), "tuple exceeds adapter capabilities")
        timing = item.get("timing")
        require(isinstance(timing, dict) and all(type(value) is int and 0 <= value <= 0xFFFFFFFF
                                               for value in timing.values()), "timing is invalid")
        require(timing == host_timing(item["active"]["baud"], item["active"]["format"]), "timing does not match active tuple")
        old = self.serial
        args = host_arguments(handle.host_args)
        changes = handle.host_args not in ((), ("caps",))
        selected = dict(item["original"] if args.get("restore") else
                        (old["active"] if old is not None else item["active"]))
        selected.update({key: value for key, value in args.items() if key != "restore"})
        if old is not None:
            require(item["original"] == old["original"] and item["serial_generation"] >= old["serial_generation"],
                    "original tuple or serial generation changed")
            require(item["supported_bauds"] == old["supported_bauds"]
                    and item["supported_formats"] == old["supported_formats"], "adapter capabilities changed")
        if item["ok"]:
            require(item.get("result") == "done", "success result is not explicit")
            if changes:
                require(item["active_known"] and not item["blocked"]
                        and item["active"] == item["requested"] == selected, "success differs from requested tuple")
                if old is not None:
                    changed = not old["active_known"] or old["blocked"] or selected != old["active"]
                    require(item["serial_generation"] == old["serial_generation"] + int(changed), "generation differs from settled change")
                    if not changed:
                        require(all(item[key] == value for key, value in old.items()), "unchanged selection altered host state")
            elif old is not None:
                require(all(item[key] == value for key, value in old.items()), "query changed saved host state")
        else:
            require(item.get("result") in {"busy", "recovery_required", "unavailable", "unsupported", "failed", "ids_exhausted"}, "failure result is invalid")
            if item["result"] == "failed":
                require(changes and item["requested"] == selected and item["blocked"] and not item["active_known"],
                        "failed attempt differs from requested tuple")
                require(item["failure"] == "restore" if args.get("restore") else item["failure"] in {"adapter", "runner"},
                        "failed attempt reason differs from request")
                if old is not None:
                    require(item["active"] == old["active"] and item["serial_generation"] == old["serial_generation"] + 1,
                            "failed attempt tuple or generation differs")
            elif old is not None:
                require(all(item[key] == value for key, value in old.items()), "refused change altered host state")
        self.serial = {key: item[key].copy() if isinstance(item[key], (dict, list)) else item[key]
                       for key in ("original", "requested", "active", "active_known", "blocked", "serial_generation",
                                   "failure", "actual_baud", "supported_bauds", "supported_formats")}

    def _check_persistence(self, handle: Command, item: dict) -> None:
        expected = persistence_arguments(handle.host_args)
        if "action" not in item:
            if item["ok"]:
                raise BenchError("persistence reply omitted action")
            return
        def require(condition, message):
            if not condition:
                raise BenchError("persistence: " + message)
        require(type(item["action"]) is int and item["action"] == expected["action"], "wrong action")
        for key in ("pending", "owned", "route_ready", "snapshot_known", "snapshot_failed", "parent_session", "finished", "restart_performed", "write_replayed"):
            require(type(item.get(key)) is bool, "invalid " + key)
        require(not item["restart_performed"] and not item["write_replayed"], "unexpected restart or replay")
        require(not item["pending"] or item["owned"], "pending work has no owner")
        require(type(item.get("invocations")) is int and 0 <= item["invocations"] <= 2 and item.get("invocation_limit") == 2, "unbounded nonvolatile invocations")
        require(item["invocations"] >= self.persistence_invocations, "invocation budget disappeared")
        require(type(item.get("verification_attempts")) is int and 0 <= item["verification_attempts"] <= 2, "unbounded verification attempts")
        plan = item.get("plan")
        if expected["action"] in (0, 1):
            require(isinstance(plan, dict) and all(plan.get(k) == v for k, v in expected.items() if k != "action"), "wrong plan")
            require(plan.get("writes") == 1 and plan.get("scope") == "all_parameters" and plan.get("stopped_required") is True and
                    plan.get("completion") == "unresolved" and plan.get("durability") == "unverified" and
                    plan.get("restart") == "required_for_proof" and plan.get("backup") == "partial_standard_config", "false source claims")
        else:
            require(plan is None, "unexpected plan")
        host = item.get("host")
        require(isinstance(host, dict) and isinstance(host.get("active"), dict) and type(host.get("active_known")) is bool and
                type(host.get("blocked")) is bool and self._operation_id(host.get("serial_generation")), "invalid host")
        if host["active_known"]:
            require(not host["blocked"] and host["active"].get("baud") in HOST_BAUDS and host["active"].get("format") in HOST_FORMATS, "invalid active tuple")
        if self.serial is not None and host["serial_generation"] != self.serial["serial_generation"]:
            self.serial = None
        regs = (16,17,19,20,21,23,24,25,64,65,66,67,68,256,257)
        windows = ((16,2),(19,3),(23,3),(64,5),(256,2),(0,4),(6,2))
        def snapshot(value, *, stationary=False):
            require(isinstance(value, dict), "missing partial snapshot")
            endpoint = value.get("endpoint")
            require(isinstance(endpoint, dict) and self._operation_id(endpoint.get("target")) and self._operation_id(endpoint.get("generation")) and
                    type(endpoint.get("address")) is int and 1 <= endpoint["address"] <= 247 and endpoint.get("serial_known") is True and
                    endpoint.get("baud") in HOST_BAUDS and endpoint.get("data_bits") == 8 and
                    ((endpoint.get("parity") == 1 and endpoint.get("stop_bits") in (1,2)) or
                     (endpoint.get("parity") in (2,3) and endpoint.get("stop_bits") == 1)), "invalid snapshot endpoint")
            require(self._operation_id(value.get("configuration_generation")) and isinstance(value.get("operation_ids"), list) and
                    len(value["operation_ids"]) == 3 and all(self._operation_id(v) for v in value["operation_ids"]), "invalid snapshot IDs")
            for key, count in (("config_words",15),("identity_words",4),("motion_words",2)):
                require(isinstance(value.get(key), list) and len(value[key]) == count and
                        all(type(v) is int and 0 <= v <= 65535 for v in value[key]), "invalid " + key)
            if stationary:
                require(value["motion_words"][0] == 0 and not value["motion_words"][1] & ~0x73,
                        "invocation requires known alarm-free stopped state")
            evidence = value.get("provenance")
            require(isinstance(evidence, list) and len(evidence) == 7, "missing snapshot provenance")
            words = []
            for index, (p, window) in enumerate(zip(evidence, windows)):
                require(isinstance(p, list) and len(p) == 7 and p[:2] == list(window), "wrong read window")
                require(type(p[2]) is str and re.fullmatch(r"[0-9a-fA-F]*",p[2]) is not None and len(p[2])%2 == 0, "bad raw read")
                require(all(type(t) is int and 0 <= t <= 0xFFFFFFFFFFFFFFFF for t in p[3:]) and p[3] <= p[4] <= p[5] <= p[6], "bad read interval")
                raw=bytes.fromhex(p[2]);count=window[1]
                require(len(raw) == 5+count*2 and raw[:3] == bytes([endpoint["address"],3,count*2]) and
                        wire_crc(raw[:-2]) == int.from_bytes(raw[-2:],"little"), "invalid checked read")
                decoded=[int.from_bytes(raw[offset:offset+2],"big") for offset in range(3,3+2*count,2)]
                if index < 5: words.extend(decoded)
                else: require(decoded == value["identity_words" if index == 5 else "motion_words"], "snapshot differs from raw read")
            require(words == value["config_words"], "config differs from raw reads")
            return value
        baseline=item.get("baseline")
        if baseline is not None:
            snapshot(baseline)
            require(item["snapshot_known"], "unknown baseline was published")
        c=item.get("context");old=self.persistence
        if old or (item["ok"] and expected["action"] in (1,2)):
            require(isinstance(c,dict), "retained context disappeared")
        if c is not None:
            require(isinstance(c,dict) and self._operation_id(c.get("operation_id")), "invalid context ID")
            new = old is None or c["operation_id"] != old["operation_id"]
            require(new and old is None or not new or expected["action"] == 1, "session replaced without begin")
            require(c.get("kind") in ("save","factory-restore") and c.get("register") == 45 and
                    c.get("value") == (66 if c["kind"] == "save" else 65), "wrong command encoding")
            for key in ("effects","uncertain","snapshot_complete","verification_known","live_readback_known","restart_required_for_proof",
                        "manual_intervention_required","communication_changed","configuration_invalidated","complete_backup","restart_observed"):
                require(type(c.get(key)) is bool, "invalid context " + key)
            require(not c["snapshot_complete"], "partial snapshot claimed complete")
            require(type(c.get("state")) is int and c["state"] in (1,2,3) and type(c.get("outcome")) is int and 0 <= c["outcome"] <= 7 and
                    type(c.get("execution")) is int and 0 <= c["execution"] <= 3 and c.get("persistence") in (0,1), "invalid operation state")
            require((c["state"] == 1 and c["outcome"] == 0) or (c["state"] == 2 and c["outcome"] == 1) or
                    (c["state"] == 3 and 2 <= c["outcome"] <= 7), "state/outcome mismatch")
            require(c["uncertain"] == (c["execution"] == 3) and c["configuration_invalidated"] == c["effects"], "lost execution uncertainty/effects")
            require(type(c.get("verification_count")) is int and 0 <= c["verification_count"] <= 2, "unbounded verification")
            for key in ("matching_fields","verified_fields"):
                require(type(c.get(key)) is int and 0 <= c[key] <= 32767, "invalid field mask")
            require(all(type(c.get(k)) is int and 0 <= c[k] <= 0xFFFFFFFFFFFFFFFF for k in ("started_us","deadline_us","restart_us")) and
                    c["started_us"] < c["deadline_us"], "invalid write budget")
            before=snapshot(c.get("before"), stationary=True);after=c.get("verification")
            if c["verification_known"]:
                snapshot(after, stationary=True)
                require(c["verification_count"] > 0 and c["live_readback_known"], "missing verification")
                require(after["endpoint"] == before["endpoint"] and after["identity_words"][:2] == before["identity_words"][:2] and
                        after["configuration_generation"] >= before["configuration_generation"], "verification changed binding/identity")
            else:
                require(after is None and not c["live_readback_known"] and not c["matching_fields"] and not c["verified_fields"], "fabricated readback")
            wire=c.get("write_evidence")
            require(isinstance(wire,dict), "missing write evidence")
            for key in ("step","event","received_length","tx_accepted","earliest_us","latest_us","delivered_us","frame_error"):
                require(type(wire.get(key)) is int and 0 <= wire[key] <= 0xFFFFFFFFFFFFFFFF, "invalid wire " + key)
            for key in ("tx_complete","response_confirmed","qualified","execution_unknown"):
                require(type(wire.get(key)) is bool, "invalid wire " + key)
            require(wire["step"] == 0 and wire["event"] in (0,1,2,3) and wire["tx_accepted"] <= 8 and
                    (not wire["tx_complete"] or wire["tx_accepted"] == 8), "invalid TX/event")
            raw_hex=wire.get("raw_hex")
            require(type(raw_hex) is str and re.fullmatch(r"[0-9a-fA-F]*",raw_hex) is not None and len(raw_hex)%2 == 0, "bad raw write")
            raw=bytes.fromhex(raw_hex)
            require(len(raw) == min(9,wire["received_length"]), "lost raw write bytes")
            if c["state"] != 1:
                require(c["effects"] == bool(wire["tx_accepted"] or wire["execution_unknown"]), "lost effects")
            if wire["qualified"]:
                require(wire["event"] == 0 and c["started_us"] <= wire["earliest_us"] <= wire["latest_us"] <= wire["delivered_us"], "invalid write interval")
            else:
                require(wire["earliest_us"] == wire["latest_us"] == 0, "unqualified write bounds")
            if wire["delivered_us"]:
                if wire["event"] == 0:
                    require(wire["tx_complete"] and wire["tx_accepted"] == 8, "FRAME lacks completed TX")
                else:
                    require(not wire["qualified"] and not wire["response_confirmed"], "local event claims a response")
            if c["verification_known"]:
                require(all(p[3] >= (c["restart_us"] if c["restart_observed"] else wire["delivered_us"]) for p in after["provenance"]), "verification reused stale reads")
            if wire["delivered_us"] and wire.get("status") == "OK":
                expected_raw=bytes([before["endpoint"]["address"],6,0,45,0,c["value"]])
                require(raw[:-2] == expected_raw and len(raw) == 8 and wire_crc(raw[:-2]) == int.from_bytes(raw[-2:],"little"), "invalid write echo")
            if c["execution"] == 1 or c["state"] == 2:
                require(c["state"] == 2 and c["outcome"] == 1 and c["execution"] == 1 and c.get("status") == wire.get("status") == "OK" and
                        wire["qualified"] and wire["response_confirmed"] and wire["tx_complete"] and not wire["execution_unknown"] and
                        wire["latest_us"] <= c["deadline_us"], "false acknowledgement")
            if c["execution"] == 2:
                require(len(raw) == 5 and raw[:2] == bytes([before["endpoint"]["address"],0x86]) and raw[2] in (1,2,3,4,6,7) and
                        wire.get("status") == "EXCEPTION" and wire.get("detail") == raw[2] and wire_crc(raw[:-2]) == int.from_bytes(raw[-2:],"little") and
                        wire["qualified"] and wire["response_confirmed"] and not wire["execution_unknown"] and
                        wire["latest_us"] <= c["deadline_us"], "false rejected execution")
            fields=c.get("fields")
            require(isinstance(fields,list) and len(fields) == 15, "missing field uncertainty")
            matching=verified=0
            for index,(f,reg) in enumerate(zip(fields,regs)):
                require(isinstance(f,list) and len(f) == 6 and f[0] == reg and f[1] == before["config_words"][index] and
                        type(f[2]) is int and 0 <= f[2] <= 65535 and f[3] == ("RW/S" if reg >= 256 else "RW") and
                        type(f[4]) is bool and type(f[5]) is bool, "invalid per-field evidence")
                require(f[4] == c["verification_known"] and (not f[4] or f[2] == after["config_words"][index]), "field differs from readback")
                if f[4] and f[1] == f[2]:matching |= 1 << index
                if f[5]:
                    require(f[4] and f[1] == f[2] and c["kind"] == "save" and c["restart_observed"] and
                            self._operation_id(c.get("restart_source")) and wire["delivered_us"] < c["restart_us"] <= min(p[3] for p in after["provenance"]), "false durability proof")
                    verified |= 1 << index
            require(c["matching_fields"] == matching and c["verified_fields"] == verified and c["persistence"] == bool(verified), "wrong persistence masks")
            require(c["restart_required_for_proof"] == (not bool(verified)), "false restart proof requirement")
            if item["ok"] and expected["action"] == 1:
                require(new and c["kind"] == expected["kind"] and item["owned"] and item["route_ready"] and c["verification_count"] == 0, "invalid begin")
            if old and not new:
                for key in ("kind","register","value","before","started_us","deadline_us","backup_source","complete_backup"):
                    require(c.get(key) == old.get(key), "immutable context changed: " + key)
                require(not old["effects"] or c["effects"], "effects disappeared")
                if old["state"] != 1:
                    require(all(c[k] == old[k] for k in ("state","outcome","execution","write_evidence","uncertain")), "write result changed")
                require(c["verification_count"] >= old["verification_count"], "verification disappeared")
            self.persistence=json.loads(json.dumps(c))
        if item["ok"] and expected["action"] == 3:
            require(not item["pending"] and item["owned"] == item["parent_session"] and item["finished"] and host["active_known"] and not host["blocked"], "finish did not settle host/ownership")
            require(c is None or not c["effects"] or c["verification_known"], "finish lacks post-write verification")
        if item["ok"] and expected["action"] == 5:
            before = c["before"] if c is not None and not item["finished"] else baseline
            require(before is not None and item["owned"] and host["active_known"] and not host["blocked"], "host restore lacks retained context")
            endpoint=before["endpoint"]
            fmt="8N2" if endpoint["stop_bits"] == 2 else {1:"8N1",2:"8E1",3:"8O1"}[endpoint["parity"]]
            require(host["active"] == dict(baud=endpoint["baud"],format=fmt), "host differs from before tuple")
        self.persistence_invocations=item["invocations"]

    def _check_communication(self, handle: Command, item: dict) -> None:
        expected = communication_arguments(handle.host_args)
        # Syntax/availability errors have no session snapshot. Other refusals
        # retain exactly the same diagnostics as inspection.
        if "action" not in item:
            if item["ok"]:
                raise BenchError("communication reply omitted action")
            return
        def require(condition, message):
            if not condition:
                raise BenchError("communication: " + message)
        require(type(item["action"]) is int and item["action"] == expected["action"], "wrong requested action")
        for key in ("pending", "owned", "route_ready", "save_sent", "restart_performed", "write_replayed"):
            require(type(item.get(key)) is bool, "invalid " + key)
        require(not any(item[k] for k in ("save_sent", "restart_performed", "write_replayed")), "unexpected persistence/replay")
        require(not item["pending"] or item["owned"], "pending transaction has no owner")
        plan = item.get("plan")
        if expected["action"] in (0, 1):
            require(isinstance(plan, dict), "missing candidate plan")
            require(all(plan.get(k) == v for k, v in expected.items() if k != "action"), "plan differs from request")
            require(type(plan.get("address")) is int and 1 <= plan["address"] <= 247, "invalid target")
            address = expected["field"] == "address"
            require(plan.get("writes") == 1 and plan.get("activation") == "unresolved" and
                    plan.get("save") == ("required" if address else "unresolved") and
                    plan.get("restart") == ("unresolved" if address else "required"), "false source requirements")
        else:
            require(plan is None, "unexpected plan")
        host = item.get("host")
        require(isinstance(host, dict) and isinstance(host.get("active"), dict), "missing host context")
        require(type(host.get("active_known")) is bool and type(host.get("blocked")) is bool, "invalid host state")
        require(type(host.get("serial_generation")) is int and 1 <= host["serial_generation"] <= 0xFFFFFFFF, "invalid host generation")
        if host["active_known"]:
            require(not host["blocked"] and host["active"].get("baud") in HOST_BAUDS and
                    host["active"].get("format") in HOST_FORMATS, "unsettled known host")
        # A communication session can explicitly change UART settings. The
        # compact nested view is not the full HostSnapshot; refresh it through
        # `host` before applying full-snapshot immutability checks again.
        if self.serial is not None and host["serial_generation"] != self.serial["serial_generation"]:
            self.serial = None
        c = item.get("context")
        old = self.communication
        if item["owned"] or (item["ok"] and expected["action"] in (1, 2, 3, 4, 5, 6)) or old:
            require(isinstance(c, dict), "session lost retained context")
        candidate_check = self.communication_candidate
        if c is not None:
            require(isinstance(c, dict) and self._operation_id(c.get("operation_id")), "invalid context")
            new_session = old is None or c["operation_id"] != old["operation_id"]
            if old and new_session:
                require(expected["action"] == 1, "session changed without begin")
            if new_session:
                candidate_check = None
            for key in ("readback_known", "observed_active_known", "activation_unknown", "effects", "uncertain"):
                require(type(c.get(key)) is bool, "invalid context " + key)
            require(c["activation_unknown"], "readback cannot establish activation")
            require(type(c.get("confirmations")) is int and 0 <= c["confirmations"] <= 2, "unbounded confirmations")
            require(type(c.get("state")) is int and c["state"] in (1, 2, 3) and type(c.get("execution")) is int and c["execution"] in (0, 1, 2, 3), "invalid operation state")
            require(type(c.get("step")) is int and c["step"] == c["confirmations"], "step/confirmation mismatch")
            require(type(c.get("outcome")) is int and 0 <= c["outcome"] <= 10 and type(c.get("write_outcome")) is int and 0 <= c["write_outcome"] <= 10, "invalid outcome")
            require((c["state"] == 1 and c["outcome"] == 0) or (c["state"] == 2 and c["outcome"] in (1, 3)) or
                    (c["state"] == 3 and c["outcome"] in (2, 4, 5, 6, 7, 8, 9, 10)), "state/outcome mismatch")
            require(c.get("field") in ("address", "baud", "format") and c.get("register") == {"address":19,"baud":20,"format":21}[c["field"]], "wrong field/register")
            address_setting = c["field"] == "address"
            require(c.get("save") == ("required" if address_setting else "unresolved") and
                    c.get("restart") == ("unresolved" if address_setting else "required"), "false retained source requirements")
            require(type(c.get("requested")) is int and (1 <= c["requested"] <= 247 if c["field"] == "address" else 0 <= c["requested"] <= 3), "invalid candidate value")
            require(c["step"] > 0 or c["write_outcome"] == c["outcome"], "write outcome mismatch")
            require(c["step"] == 0 or c["write_outcome"] != 0, "confirmation lacks completed write")
            for name in ("before", "requested_endpoint", "observed_active"):
                require(isinstance(c.get(name), dict), "missing " + name)
            for name in ("before", "requested_endpoint"):
                endpoint = c[name]
                require(self._operation_id(endpoint.get("target")) and self._operation_id(endpoint.get("generation")) and
                        type(endpoint.get("address")) is int and 1 <= endpoint["address"] <= 247 and endpoint.get("serial_known") is True and
                        endpoint.get("baud") in HOST_BAUDS and endpoint.get("data_bits") == 8 and
                        ((endpoint.get("parity") == 1 and endpoint.get("stop_bits") in (1,2)) or
                         (endpoint.get("parity") in (2,3) and endpoint.get("stop_bits") == 1)), "invalid candidate endpoint")
            candidate = dict(c["before"])
            if c["field"] == "address":candidate["address"] = c["requested"]
            elif c["field"] == "baud":candidate["baud"] = (115200,38400,19200,9600)[c["requested"]]
            else:
                candidate["parity"] = (1,1,2,3)[c["requested"]]
                candidate["stop_bits"] = 2 if c["requested"] == 1 else 1
            require(candidate == c["requested_endpoint"], "requested tuple differs from exact candidate")
            require(isinstance(c.get("confirmation_evidence"), list) and len(c["confirmation_evidence"]) == c["confirmations"], "missing confirmation evidence")
            observations = [c.get("write_evidence"), *c["confirmation_evidence"]]
            for index, evidence in enumerate(observations):
                require(isinstance(evidence, dict), "missing wire observation")
                for key in ("eligible_us", "deadline_us"):
                    require(type(evidence.get(key)) is int and 0 <= evidence[key] <= 0xFFFFFFFFFFFFFFFF, "invalid retained " + key)
                require(isinstance(evidence.get("wire"), dict), "missing wire diagnostics")
                if evidence["deadline_us"]:
                    require(evidence["eligible_us"] < evidence["deadline_us"], "invalid transaction budget")
                else:
                    require(evidence["eligible_us"] == 0 and not evidence["wire"].get("delivered_us", 0), "published evidence lost its budget")
                if index == 0 and c.get("write_outcome", 0) != 0:
                    require(evidence["deadline_us"] > 0, "terminal write lost its budget")
                if not evidence["deadline_us"]:
                    require(index == c["step"] and c["state"] == 1, "terminal observation has no budget")
                    continue
                require(evidence.get("endpoint") == c["before"] if index == 0 else evidence.get("endpoint") in (c["before"],c["requested_endpoint"]), "foreign evidence endpoint")
                wire = evidence["wire"]
                require(type(wire.get("step")) is int and wire["step"] == index and wire.get("event") in (0,1,2,3), "wrong wire step/event")
                for key in ("tx_accepted","received_length","earliest_us","latest_us","delivered_us","frame_error"):
                    require(type(wire.get(key)) is int and 0 <= wire[key] <= 0xFFFFFFFFFFFFFFFF, "invalid wire " + key)
                for key in ("tx_complete","response_confirmed","qualified","execution_unknown"):
                    require(type(wire.get(key)) is bool, "invalid wire flag " + key)
                require(wire["tx_accepted"] <= 8 and (not wire["tx_complete"] or wire["tx_accepted"] == 8), "invalid TX evidence")
                raw_hex=wire.get("raw_hex")
                require(type(raw_hex) is str and re.fullmatch(r"[0-9a-fA-F]*",raw_hex) is not None and len(raw_hex)%2 == 0, "invalid raw frame")
                raw=bytes.fromhex(raw_hex)
                require(len(raw) == min(9,wire["received_length"]), "raw frame retention mismatch")
                if wire["qualified"]:
                    require(wire["event"] == 0 and evidence["eligible_us"] <= wire["earliest_us"] <= wire["latest_us"] <= wire["delivered_us"], "invalid qualified interval")
                else:require(wire["earliest_us"] == wire["latest_us"] == 0, "unqualified interval has bounds")
                if wire["event"] == 0:
                    require(wire["tx_accepted"] == 8 and wire["tx_complete"], "FRAME without completed request")
                else:require(not wire["qualified"] and not wire["response_confirmed"], "local event claims response")
                if wire.get("status") == "OK":
                    require(wire["event"] == 0 and wire["frame_error"] == 0 and len(raw) in (7,8) and wire_crc(raw[:-2]) == int.from_bytes(raw[-2:],"little"), "successful codec lacks valid CRC/frame")
                    expected_raw=bytes([evidence["endpoint"]["address"],6 if index == 0 else 3])
                    expected_raw+=(c["register"].to_bytes(2,"big")+c["requested"].to_bytes(2,"big")) if index == 0 else bytes([2])+raw[3:5]
                    require(raw[:-2] == expected_raw, "successful frame differs from request")
                if evidence.get("readback_known"):
                    require(index > 0 and wire.get("status") == "OK" and wire["qualified"] and wire["response_confirmed"] and
                            not wire["execution_unknown"] and wire["latest_us"] <= evidence["deadline_us"] and evidence.get("readback") == int.from_bytes(raw[3:5],"big"), "unproven readback")
            if c["state"] == 2:
                latest=observations[c["step"]];wire=latest["wire"]
                require(c.get("status") == "OK" and wire.get("status") == "OK" and wire["qualified"] and wire["response_confirmed"] and
                        not wire["execution_unknown"] and wire["latest_us"] <= latest["deadline_us"], "false terminal success")
                if c["step"] == 0:
                    require(c["outcome"] == 1 and c["execution"] == 1 and c["field"] != "address", "unproven acknowledgement")
                else:
                    require(c["outcome"] == 3 and latest.get("readback_known") is True and c["readback_known"] and
                            c["readback"] == latest["readback"] == c["requested"] and not c["uncertain"] and c["observed_active_known"], "false confirmed success")
            write=observations[0]["wire"]
            if c["write_outcome"]:
                require(c["effects"] == bool(write["tx_accepted"] or write["execution_unknown"]), "lost write effects")
                if c["execution"] == 1 or c["write_outcome"] in (1,2):
                    require(write.get("status") == "OK" and write["qualified"] and write["response_confirmed"] and
                            not write["execution_unknown"] and write["latest_us"] <= observations[0]["deadline_us"] and
                            ((c["field"] == "address" and c["execution"] == 3 and c["write_outcome"] == 2) or
                             (c["field"] != "address" and c["execution"] == 1 and c["write_outcome"] == 1)), "false retained acknowledgement")
            if c["observed_active_known"]:
                latest=observations[c["step"]]
                require(c["step"] > 0 and latest.get("readback_known") is True and c["observed_active"] == latest.get("endpoint"), "foreign or unproven observed interface")
            if expected["action"] == 1 and (item["ok"] or (old is not None and new_session)):
                require(new_session and c["step"] == 0, "begin did not start a new write attempt")
                require(c.get("field") == expected["field"] and c.get("requested") == expected["value"] and c.get("register") == expected["register"], "begun candidate differs")
                require(c["before"]["address"] == plan["address"], "begun target differs")
                require(item["owned"] and item["route_ready"], "begin lacks ownership/route")
            if item["ok"] and expected["action"] in (2, 3, 4, 5):
                require(item["owned"], "session action lost ownership")
                selected = c["before"] if expected["action"] in (2, 4) else c["requested_endpoint"]
                fmt = "8N2" if selected["stop_bits"] == 2 else {1:"8N1", 2:"8E1", 3:"8O1"}[selected["parity"]]
                require(host["active_known"] and not host["blocked"] and
                        host["active"] == dict(baud=selected["baud"], format=fmt), "selected host differs from requested candidate")
                if expected["action"] in (4, 5):
                    require(c["step"] > 0 and (old is None or c["step"] == old["step"] + 1), "confirmation did not start a new attempt")
                    candidate_check = (c["operation_id"], c["step"], dict(selected))
            if candidate_check is not None:
                operation_id, step, selected = candidate_check
                require(c["operation_id"] == operation_id and c["step"] >= step, "selected confirmation disappeared")
                observation = c["confirmation_evidence"][step - 1]
                if observation["deadline_us"]:
                    require(observation["endpoint"] == selected, "confirmation differs from selected candidate")
            if old and c["operation_id"] == old["operation_id"]:
                for key in ("field", "register", "previous", "requested", "before", "requested_endpoint"):
                    require(c.get(key) == old.get(key), "retained candidate changed: " + key)
                require(not old["effects"] or c["effects"], "write effects disappeared")
                require(c["confirmations"] >= old["confirmations"], "confirmation evidence disappeared")
                if old.get("write_outcome", 0) != 0:
                    require(c.get("write_evidence") == old.get("write_evidence") and c.get("write_outcome") == old.get("write_outcome"), "write evidence changed")
                for index, retained in enumerate(old["confirmation_evidence"]):
                    if retained["deadline_us"]:
                        require(c["confirmation_evidence"][index] == retained, "confirmation evidence or budget changed")
        if item["ok"] and expected["action"] == 6:
            require(not item["owned"] and not item["pending"] and host["active_known"] and not host["blocked"], "finish did not settle ownership")
            require(c is not None, "finish lost retained context")
            no_write=c["execution"] == 0 and not c["effects"]
            require(no_write or c["observed_active_known"], "finish lacks confirmed endpoint")
            settled=c["before"] if no_write else c["observed_active"]
            fmt="8N2" if settled["stop_bits"] == 2 else {1:"8N1",2:"8E1",3:"8O1"}[settled["parity"]]
            require(host["active"] == dict(baud=settled["baud"],format=fmt), "finish host differs from confirmed endpoint")
        if c is not None:
            self.communication = json.loads(json.dumps(c))
            self.communication_candidate = candidate_check

    def _check_debug(self, handle: Command, item: dict) -> None:
        if not item["ok"]:
            return
        desired = debug_arguments(handle.host_args)
        if item.get("mode") not in ("off", "raw", "decoded") or (desired is not None and desired != item["mode"]):
            raise BenchError("debug mode differs from requested mode")
        check_counts(item, ("observed", "emitted", "dropped", "missed", "skipped", "cursor", "overwritten", "capture_dropped", "retained", "capacity"), "debug")
        if (item["retained"] > item["capacity"] or item["capacity"] > 64 or
                min(0xFFFFFFFFFFFFFFFF,item["emitted"] + item["dropped"]) != item["observed"] or
                min(0xFFFFFFFFFFFFFFFF,item["observed"] + item["missed"] + item["skipped"]) != item["cursor"]):
            raise BenchError("debug counters are inconsistent")
        if self.debug_snapshot is not None and any(item[k] < self.debug_snapshot[k] for k in
                ("observed", "emitted", "dropped", "missed", "skipped", "cursor", "overwritten", "capture_dropped")):
            raise BenchError("debug counters regressed without a reset")
        owner, capture, memory = (item.get(k) for k in ("owner", "capture", "memory"))
        if not all(isinstance(v, dict) for v in (owner, capture, memory)):
            raise BenchError("debug cached diagnostics missing")
        if (owner.get("phase") not in ("IDLE", "WAIT_BUS", "SETUP", "DRAIN", "HOLD", "RECEIVE", "DONE", "FAULT") or
                any(type(owner.get(k)) is not bool for k in ("busy", "recovery_required")) or
                capture.get("mode") not in ("poll", "timer") or type(memory.get("valid")) is not bool):
            raise BenchError("debug cached diagnostics invalid")
        check_counts(owner, ("pending", "retained"), "debug owner")
        check_counts(capture, ("faults", "max_poll_gap_us"), "debug capture")
        check_counts(memory, ("stack_free_bytes", "internal_free", "psram_free"), "debug memory")
        self.debug_snapshot = dict(item)
        self.debug_mode = item["mode"]

    def _check_traffic(self, item: dict) -> None:
        """Validate observational copies without consuming or satisfying a command."""
        def require(value, message):
            if not value:
                raise BenchError("traffic: " + message)
        def integer(value, high=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and 0 <= value <= high
        require(item.get("type") == "traffic" and item.get("profile") == "ess_rs" and
                item.get("mode") in ("raw", "decoded") and "id" not in item and "operation_id" not in item,
                "invalid diagnostic envelope")
        require(all(integer(item.get(k)) for k in ("sequence", "transaction", "at_us", "start_us", "end_us", "expected_request_sequence")) and
                item["sequence"] > self.traffic_sequence, "invalid sequence or timestamps")
        require(integer(item.get("uncertainty_us"), 0xFFFFFFFF) and integer(item.get("length"), 256) and
                integer(item.get("code"), 65535) and type(item.get("complete")) is bool,
                "invalid record shape")
        kind = item.get("kind")
        require(kind in ("TX", "RX", "TX_END", "DIRECTION", "END"), "unknown event")
        require(item["start_us"] <= item["end_us"] <= item["at_us"], "invalid interval")
        frames = {}
        for key in ("raw_hex", "expected_request_hex"):
            value = item.get(key)
            require(isinstance(value, str) and re.fullmatch(r"(?:[0-9A-Fa-f]{2}){0,256}", value) is not None,
                    "invalid " + key)
            frames[key] = bytes.fromhex(value)
        raw, expected = frames["raw_hex"], frames["expected_request_hex"]
        require(len(raw) == item["length"], "raw length differs")
        metadata = kind not in ("TX", "RX")
        require(not metadata or (not raw and not item["complete"]), "metadata invents frame bytes")
        require(kind != "DIRECTION" or item["code"] <= 1, "invalid direction")
        status, decoded = item.get("decode_status"), item.get("decoded")
        require(type(item.get("decode_detail")) is int and -(2**31) <= item["decode_detail"] < 2**31 and
                integer(item.get("frame_error"), 255), "invalid decode evidence")
        if item["mode"] == "raw" or metadata:
            require(status is None and decoded is None and not expected and not item["expected_request_sequence"] and
                    item["decode_detail"] == item["frame_error"] == 0, "raw or metadata contains translation")
        else:
            require(status in ("OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"),
                    "invalid decoder status")
            if status not in ("OK", "EXCEPTION"):
                require(decoded is None, "failed translation publishes values")
            else:
                require(item["complete"] and isinstance(decoded, dict) and len(raw) >= 5 and wire_crc(raw) == 0,
                        "translation lacks a complete CRC-valid frame")
                request = raw if kind == "TX" else expected
                require(len(request) >= 8 and wire_crc(request) == 0 and 1 <= request[0] <= 247 and request[1] in (3, 6, 16),
                        "translation lacks checked request")
                address, function = request[0], request[1]
                first = int.from_bytes(request[2:4], "big")
                count = 1 if function == 6 else int.from_bytes(request[4:6], "big")
                require(1 <= count <= 16 and first + count <= 65536, "invalid request window")
                if function in (3, 6):
                    require(len(request) == 8, "invalid request length")
                else:
                    require(len(request) == 9 + 2 * count and request[6] == 2 * count and
                            (first, count) in ((0x24, 2), (0x21, 5), (0x1D, 3), (0x31, 6)), "invalid FC10 window")
                exception = kind == "RX" and raw[1] == (function | 128)
                require(all(integer(decoded.get(k), 65535) for k in ("address", "function", "register_start", "register_count", "exception_code")) and
                        decoded.get("address") == address and decoded.get("function") == function and
                        decoded.get("register_start") == first and decoded.get("register_count") == count and
                        decoded.get("exception") is exception and decoded.get("exception_code") == (raw[2] if exception else 0),
                        "translated request fields differ")
                require(status == ("EXCEPTION" if exception else "OK") and item["decode_detail"] == (raw[2] if exception else 0) and
                        item["frame_error"] == (10 if exception else 0),
                        "exception evidence differs")
                if kind == "RX":
                    require(item["transaction"] > 0 and 0 < item["expected_request_sequence"] < item["sequence"] and raw[0] == address, "unbound response")
                    if exception:
                        require(len(raw) == 5, "exception length differs")
                        values = []
                    elif function == 3:
                        require(raw[1] == 3 and len(raw) == 5 + 2 * count and raw[2] == 2 * count, "read response shape differs")
                        values = [int.from_bytes(raw[i:i+2], "big") for i in range(3, 3 + 2 * count, 2)]
                    else:
                        require(raw == request if function == 6 else raw[:-2] == request[:6] and len(raw) == 8,
                                "write echo differs")
                        values = [int.from_bytes(request[4:6], "big")] if function == 6 else []
                else:
                    require(not expected and not item["expected_request_sequence"] and not exception, "TX invents response provenance")
                    values = [] if function == 3 else [int.from_bytes(request[4:6], "big")] if function == 6 else [
                        int.from_bytes(request[i:i+2], "big") for i in range(7, 7 + 2 * count, 2)]
                require(decoded.get("words") == values and all(type(v) is int for v in decoded["words"]),
                        "translated values differ")
                require(decoded.get("function_name") == {3:"read_registers", 6:"write_register", 16:"write_registers"}[function],
                        "function name differs")
                names = decoded.get("register_names")
                require(isinstance(names, list) and len(names) == count and all(name is None or
                        (isinstance(name, str) and re.fullmatch(r"[A-Z][A-Z0-9_]*", name)) for name in names),
                        "invalid register names")
        self.traffic_sequence = item["sequence"]
        self.emit("traffic", response=item)

    def _check_motion_profile(self, handle: Command, item: dict) -> None:
        """Check the position-parameter snapshot and its explicit checked restoration."""
        action = motion_profile_arguments(handle.host_args)
        def require(condition, message):
            if not condition:
                raise BenchError("motion-profile: " + message)
        def integer(value, low=0, high=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and low <= value <= high
        if "phase" not in item:
            require(not item["ok"] and item.get("result") in ("unavailable", "invalid_arguments", "output_capacity"),
                    "reply omitted profile evidence without an explicit refusal")
            return
        require(item.get("result") == "accepted" if item["ok"] else
                item.get("result") in ("busy", "recovery_required", "unavailable", "failed", "invalid"),
                "invalid command disposition")
        require(item.get("request") == action, "request differs")
        for key in ("pending", "saved", "restored", "session_ok",
                    "tx_complete", "closure_qualified", "execution_unknown", "write_tx_complete",
                    "write_closure_qualified", "write_execution_unknown", "restore_unsettled"):
            require(type(item.get(key)) is bool, "invalid " + key)
        require(integer(item.get("phase"), 0, 3) and integer(item.get("address"), 0, 247), "invalid phase/target")
        for key in ("configuration_generation", "serial_generation", "write_configuration_generation",
                    "write_serial_generation", "write_binding_generation"):
            require(integer(item.get(key), 0, 0xFFFFFFFF), "invalid " + key)
        for key in ("deadline_us", "delivered_us", "tx_accepted", "closure_earliest_us", "closure_latest_us",
                    "write_tx_accepted", "write_closure_earliest_us", "write_closure_latest_us", "write_delivered_us", "write_deadline_us"):
            require(integer(item.get(key)), "invalid " + key)
        require(item.get("error") in ("none", "admission", "transaction", "readback_admission", "decode", "readback_mismatch", "stationary_required"),
                "invalid session error")
        for key in ("original", "current"):
            require(isinstance(item.get(key), list) and len(item[key]) == 6 and
                    all(integer(word, 0, 65535) for word in item[key]), "invalid " + key + " words")
        frames = {}
        for key, capacity in (("tx_hex", 19), ("rx_hex", 64), ("write_tx_hex", 19), ("write_reply_hex", 8)):
            value = item.get(key)
            require(isinstance(value, str) and re.fullmatch(r"(?:[0-9a-fA-F]{2})*", value) is not None and
                    len(value) <= capacity * 2, "invalid " + key)
            frames[key] = bytes.fromhex(value)
        phase = item["phase"]
        require(not item["pending"] or (phase in (1, 2, 3) and not item["session_ok"] and not item["restored"]),
                "invalid pending session")
        require(not item["session_ok"] or (not item["pending"] and item["saved"] and phase in (1, 3) and item["error"] == "none"),
                "invalid successful session")
        require(item["restored"] == (item["session_ok"] and phase == 3), "restoration flag differs")
        if item["ok"] and action in ("read", "restore"):
            require(item["pending"] and (phase in (1, 3) if action == "read" else phase == 2),
                    "accepted request did not start its explicit phase")
        forgotten = item["ok"] and action == "forget"
        if forgotten:
            require(phase == 0 and item["address"] == 0 and item["error"] == "none" and
                    item["original"] == item["current"] == [0] * 6 and not any(frames.values()) and
                    all(item[key] == 0 for key in ("configuration_generation", "serial_generation", "deadline_us",
                        "delivered_us", "tx_accepted", "closure_earliest_us", "closure_latest_us", "write_tx_accepted",
                        "write_closure_earliest_us", "write_closure_latest_us", "write_delivered_us", "write_deadline_us",
                        "write_configuration_generation", "write_serial_generation", "write_binding_generation")) and
                    not any(item[key] for key in ("pending", "saved", "restored", "session_ok", "tx_complete",
                        "closure_qualified", "execution_unknown", "write_tx_complete", "write_closure_qualified", "write_execution_unknown", "restore_unsettled")),
                    "forgotten snapshot retained state or wire evidence")
        address = item["address"]
        read_prefix = bytes((address, 3, 0, 0x20, 0, 6))
        write_prefix = bytes((address, 16, 0, 0x21, 0, 5, 10)) + b"".join(word.to_bytes(2, "big") for word in item["original"][1:])
        if phase:
            require(address >= 1 and item["configuration_generation"] and item["serial_generation"] and item["deadline_us"],
                    "session lacks endpoint/budget")
            tx = frames["tx_hex"]
            require(tx[:-2] == (write_prefix if phase == 2 else read_prefix) and wire_crc(tx) == 0, "request frame differs")
            require(phase == 1 or item["saved"], "restore lost its original snapshot")
        else:
            require(not item["pending"] and not item["saved"] and not item["session_ok"] and
                    not any(frames.values()), "empty profile publishes wire evidence")
        for prefix, tx_key in (("", "tx_hex"), ("write_", "write_tx_hex")):
            accepted, complete = item[prefix + "tx_accepted"], item[prefix + "tx_complete"]
            require(accepted <= len(frames[tx_key]) and (not complete or accepted == len(frames[tx_key]) > 0), "invalid accepted TX")
            first, last, delivered = (item[prefix + key] for key in ("closure_earliest_us", "closure_latest_us", "delivered_us"))
            if item[prefix + "closure_qualified"]:
                require(complete and 0 < first <= last <= delivered, "invalid closure interval")
            else:
                require(first == last == 0, "unqualified interval has bounds")
        write_context = ("write_deadline_us", "write_configuration_generation", "write_serial_generation", "write_binding_generation")
        require(all(item[key] > 0 for key in write_context) if phase == 2 or frames["write_tx_hex"] else
                all(item[key] == 0 for key in write_context), "invalid retained write context")
        if frames["write_tx_hex"]:
            write = frames["write_tx_hex"]; response = frames["write_reply_hex"]
            require(write[:-2] == write_prefix and wire_crc(write) == 0, "retained restoration request differs")
            if not item["write_execution_unknown"] and item["write_tx_accepted"]:
                echo = response[:-2] == bytes((address, 16, 0, 0x21, 0, 5)) and len(response) == 8
                exception = len(response) == 5 and response[:2] == bytes((address, 0x90))
                require((echo or exception) and wire_crc(response) == 0 and item["write_tx_complete"] and
                        item["write_closure_qualified"] and item["write_closure_latest_us"] <= item["write_deadline_us"],
                        "known restoration outcome lacks checked on-time reply")
            if phase == 3 and item["closure_qualified"]:
                require(item["write_delivered_us"] <= item["closure_earliest_us"], "readback precedes restoration")
        if phase == 3:
            require(frames["write_tx_hex"] and item["write_tx_accepted"], "readback lacks retained restoration attempt")
        elif phase == 2 and frames["write_tx_hex"]:
            require(not item["pending"] and not item["session_ok"] and item["error"] == "transaction" and
                    frames["write_tx_hex"] == frames["tx_hex"] and
                    frames["write_reply_hex"] == frames["rx_hex"][:8], "failed restoration lost its write evidence")
            for key in ("tx_accepted", "tx_complete", "closure_qualified", "closure_earliest_us", "closure_latest_us",
                        "delivered_us", "execution_unknown"):
                require(item["write_" + key] == item[key], "failed restoration evidence differs")
        elif not frames["write_tx_hex"]:
            require(not frames["write_tx_hex"] and not frames["write_reply_hex"] and
                    not item["write_tx_accepted"] and not item["write_tx_complete"] and not item["write_closure_qualified"] and
                    not item["write_delivered_us"] and not item["write_execution_unknown"], "unexpected retained restoration proof")
        require(not item["restore_unsettled"] or (item["saved"] and
                (item["write_tx_accepted"] > 0 or (phase == 2 and item["pending"]))),
                "unsettled restoration lacks transmitted write")
        require(not item["write_execution_unknown"] or item["execution_unknown"],
                "historical unknown write was relabelled known")
        if phase == 2 and not item["pending"] and item["write_tx_accepted"]:
            require(item["restore_unsettled"], "failed transmitted restoration lost its interlock")
        if item["session_ok"] or item["error"] == "stationary_required":
            rx = frames["rx_hex"]
            require(item["tx_complete"] and item["closure_qualified"] and
                    (not item["execution_unknown"] or item["write_execution_unknown"]) and
                    item["closure_latest_us"] <= item["deadline_us"] and len(rx) == 17 and
                    rx[:3] == bytes((address, 3, 12)) and wire_crc(rx) == 0 and
                    item["current"] == [int.from_bytes(rx[i:i+2], "big") for i in range(3, 15, 2)],
                    "successful snapshot lacks checked FC03 payload")
            require(not item["restored"] or item["current"] == item["original"], "restoration differs from original")
            if item["error"] == "stationary_required":
                require(phase == 3 and not item["session_ok"] and not item["restored"] and item["restore_unsettled"] and
                        item["current"] == item["original"], "stationary refusal lacks matching readback/interlock")
        old = self.motion_profile
        if old and old["saved"] and not forgotten:
            require(item["saved"] and item["original"] == old["original"] and item["address"] == old["address"] and
                    (item["serial_generation"] == old["serial_generation"] or
                     (old["restore_unsettled"] and item["ok"] and action == "read" and phase == 3)),
                    "retained original/binding changed")
        if old and old["restore_unsettled"]:
            require(not forgotten and not (item["ok"] and action == "restore"), "unsettled restoration was forgotten or replayed")
            require(not (item["ok"] and action == "read") or phase == 3,
                    "unsettled restoration bypassed read-only reconciliation")
            require(item["restore_unsettled"] or (item["session_ok"] and phase == 3 and
                    item["current"] == item["original"] and item["closure_earliest_us"] >= old["write_delivered_us"]),
                    "restoration interlock cleared without fresh matching readback")
        if old and (old["write_tx_hex"] or (old["phase"] == 2 and old["pending"])) and not forgotten and not (item["ok"] and action == "restore"):
            require(all(item[key] == old[key] for key in write_context), "immutable restoration context changed")
        if old and old["write_tx_hex"] and not (old["phase"] == 2 and old["pending"]) and not forgotten and not (item["ok"] and action == "restore"):
            keys = (*write_context, "write_tx_hex", "write_reply_hex", "write_tx_accepted", "write_tx_complete",
                    "write_closure_qualified", "write_closure_earliest_us", "write_closure_latest_us",
                    "write_delivered_us", "write_execution_unknown")
            require(all(item[key] == old[key] for key in keys), "immutable restoration evidence changed")
        self.motion_profile = json.loads(json.dumps(item))

    @staticmethod
    def _check_useaddr(handle: Command, item: dict) -> None:
        """Local selection returns no bus operation or admitted transaction."""
        if item["ok"]:
            if (item.get("result") != "done" or type(item.get("address")) is not int or
                    item["address"] != handle.address or type(item.get("operation_id")) is not int or item["operation_id"] != 0):
                raise BenchError("useaddr local selection reply differs from request")
        elif item.get("result") not in ("busy", "invalid", "unavailable", "ids_exhausted"):
            raise BenchError("useaddr refusal result is invalid")
        if (("address" in item and (type(item["address"]) is not int or item["address"] != handle.address)) or
                ("operation_id" in item and (type(item["operation_id"]) is not int or item["operation_id"] != 0)) or
                any(key in item for key in ("command_id", "tx_bytes", "rx_bytes", "tx_hex", "rx_hex")) or
                ("bus_traffic" in item and item["bus_traffic"] is not False)):
            raise BenchError("useaddr reply claims a bus operation or different target")

    @staticmethod
    def _check_wiring(handle: Command, item: dict) -> None:
        """A local declaration/query carries bounded context and never an operation."""
        requested = wiring_arguments(handle.host_args)
        def require(condition, message):
            if not condition:
                raise BenchError("wiring: " + message)
        def integer(value, low, high):
            return type(value) is int and low <= value <= high
        require(item.get("result") == "done" if item["ok"] else
                item.get("result") in ("busy", "invalid", "ids_exhausted"), "invalid disposition")
        require(item.get("bus_traffic") is False, "local command claims bus traffic")
        require(integer(item.get("target"), 1, 0xFFFFFFFF) and integer(item.get("address"), 1, 247) and
                integer(item.get("generation"), 1, 0xFFFFFFFF) and
                integer(item.get("configuration_generation"), 0, 0xFFFFFFFF), "invalid target/generation")
        for key, capacity in (("inputs", 4), ("outputs", 2)):
            values = item.get(key)
            require(isinstance(values, list) and len(values) == capacity and
                    all(integer(value, 0, 2) for value in values), "invalid " + key + " declarations")
        require(not any(key in item for key in ("operation_id", "command_id", "tx_bytes", "rx_bytes",
                    "tx_hex", "rx_hex", "read_kind", "tx_accepted")), "local command claims wire/admission evidence")
        if item["ok"] and requested:
            terminal, disposition = requested
            values = item["inputs" if terminal[0] == "x" else "outputs"]
            require(values[int(terminal[1])] == disposition, "declaration differs from requested terminal")

    @staticmethod
    def _check_admission_refusal(handle: Command, item: dict) -> None:
        if type(item.get("result")) is not str or item["result"] not in ADMISSION_FAILURES:
            raise BenchError("admission refusal lacks a documented disposition")
        if "operation_id" in item and (type(item["operation_id"]) is not int or item["operation_id"] != 0):
            raise BenchError("admission refusal claims an admitted operation")
        if "address" in item and handle.command != "recover" and (
                type(item["address"]) is not int or not 1 <= item["address"] <= 247 or
                (handle.address is not None and item["address"] != handle.address)):
            raise BenchError("admission refusal target differs from request")

    def _check_discovery(self, handle: Command, item: dict) -> None:
        expected = discovery_arguments(handle.host_args or ())
        def require(condition, message):
            if not condition:
                raise BenchError("discovery: " + message)
        def number(value, low=0, high=0xFFFFFFFFFFFFFFFF):
            return type(value) is int and low <= value <= high
        action = {"begin": 0, "cancel": 1, "restore": 2, "finish": 3, "inspect": -1}[expected["action"]]
        if "action" not in item and not item["ok"]:
            return  # Explicit unavailable/grammar refusal has no invented scan state.
        require(type(item.get("action")) is int and item["action"] == action, "action correlation differs")
        scan = item.get("scan")
        if scan is None:
            require(not item["ok"] or action == -1, "successful action lacks retained context")
            return
        require(isinstance(scan, dict), "context is invalid")
        require(all(number(scan.get(k)) for k in ("operation_id", "phase", "outcome", "requests", "count", "tuple_index", "address",
                                                 "started_us", "deadline_us", "finished_us", "original_serial_generation")), "progress fields invalid")
        require(scan["phase"] <= 5 and scan["outcome"] <= 9 and scan["operation_id"] <= 0xFFFFFFFF,
                "unknown phase/outcome or operation ID")
        require(all(type(scan.get(k)) is bool for k in ("owned", "restored", "released", "cancel_requested")), "state flags invalid")
        if scan["phase"] == 0:
            require(scan["operation_id"] == scan["requests"] == scan["count"] == 0 and not scan["owned"], "empty state carries operations")
            return
        require(scan["operation_id"] > 0 and scan["deadline_us"] > scan["started_us"], "missing immutable deadline/correlation")
        target = scan.get("original_target")
        require(isinstance(target, list) and len(target) == 3 and number(target[0], 1, 0xFFFFFFFF) and
                number(target[1], 1, 247) and number(target[2], 1, 0xFFFFFFFF), "original binding invalid")
        original = scan.get("original_tuple")
        def tuple_valid(value):
            return (isinstance(value, dict) and type(value.get("baud")) is int and value["baud"] in HOST_BAUDS
                    and value.get("format") in HOST_FORMATS)
        require(tuple_valid(original) and scan["original_serial_generation"] > 0, "original host evidence invalid")
        settings = scan.get("settings")
        require(isinstance(settings, dict) and number(settings.get("first"), 1, 247) and number(settings.get("last"), settings["first"], 247), "address range invalid")
        require(number(settings.get("query_ms"), 1, 5000) and number(settings.get("overall_ms"), 1, 60000) and
                number(settings.get("request_limit"), 1, 256) and number(settings.get("result_limit"), 1, 8) and
                type(settings.get("identity")) is bool, "budgets invalid")
        tuples = settings.get("tuples")
        require(isinstance(tuples, list) and 1 <= len(tuples) <= 4 and all(tuple_valid(t) for t in tuples) and
                len({(t["baud"], t["format"]) for t in tuples}) == len(tuples), "tuple candidates invalid")
        require(scan["deadline_us"] - scan["started_us"] == settings["overall_ms"] * 1000, "overall deadline differs from budget")
        if action == 0 and item["ok"]:
            for key, field in (("first", "first"), ("last", "last"), ("query-ms", "query_ms"), ("overall-ms", "overall_ms"), ("requests", "request_limit"), ("results", "result_limit")):
                require(key not in expected or settings[field] == expected[key], "accepted candidate/budget differs")
            require(settings["identity"] == expected["identity"] and (not expected["tuples"] or tuples == expected["tuples"]), "accepted tuple/refinement differs")
        require(scan["requests"] <= settings["request_limit"] and scan["count"] <= settings["result_limit"] and
                scan["count"] <= scan["requests"] and scan["tuple_index"] <= len(tuples), "count exceeds retained budgets")
        require(scan.get("evidence_columns") == DISCOVERY_EVIDENCE_COLUMNS, "wire columns differ")
        findings = scan.get("findings")
        require(isinstance(findings, list) and len(findings) == scan["count"], "retained findings count differs")
        ids = set()
        def evidence(row, count, *, unused=False):
            require(isinstance(row, list) and len(row) == len(DISCOVERY_EVIDENCE_COLUMNS), "wire evidence shape invalid")
            e = dict(zip(DISCOVERY_EVIDENCE_COLUMNS, row))
            require(all(number(e[k]) for k in ("first", "count", "event", "received_length", "attempted_us", "earliest_us", "latest_us", "delivered_us", "tx_accepted", "frame_error")) and
                    all(type(e[k]) is bool for k in ("qualified", "execution_unknown")) and
                    type(e["transport_detail"]) is int and type(e["detail"]) is int and isinstance(e["status"], str), "wire evidence fields invalid")
            require(e["event"] <= 3 and e["tx_accepted"] <= 8 and isinstance(e["raw_hex"], str) and
                    re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,37}", e["raw_hex"]) is not None, "wire bounds invalid")
            raw = bytes.fromhex(e["raw_hex"])
            require(len(raw) == min(e["received_length"], 37), "copied prefix length differs")
            if unused:
                require(row == [0,0,0,"",0,False,0,0,0,0,0,0,False,"OK",0,0],
                        "pending/unattempted identity carries settled evidence")
            else:
                require(e["first"] == 0 and e["count"] == count, "query uses unreviewed register window")
                require(e["attempted_us"] <= e["delivered_us"], "service precedes attempt")
                if e["qualified"]:
                    require(e["event"] == 0 and e["attempted_us"] <= e["earliest_us"] <= e["latest_us"] <= e["delivered_us"], "closure bounds invalid")
                else:
                    require(e["earliest_us"] == e["latest_us"] == 0, "unqualified event invents closure bounds")
                if e["event"] != 0:
                    require((e["status"],e["detail"],e["frame_error"]) == ("ILLEGAL_VALUE",{1:11,2:12,3:10}[e["event"]],0),
                            "local event status differs from public read failure")
            return e, raw
        for f in findings:
            require(isinstance(f, dict) and f.get("profile") == "ess_rs" and f.get("collision_excluded") is False, "unreviewed profile or uniqueness claim")
            endpoint = f.get("target")
            require(isinstance(endpoint, list) and len(endpoint) == 3 and number(endpoint[0], 1, 0xFFFFFFFF) and endpoint[0] == endpoint[1] and endpoint[2] == target[2] and
                    number(endpoint[1], settings["first"], settings["last"]), "finding binding outside candidate set")
            op = f.get("operation_id")
            require(number(op, 1, 0xFFFFFFFF) and op not in ids, "attempt correlation reused")
            ids.add(op)
            serial = f.get("serial")
            require(isinstance(serial, list) and len(serial) == 5 and serial[0] is True and number(serial[1], 1, 115200) and type(serial[2]) is int and serial[2] == 8 and
                    number(serial[3], 1, 3) and number(serial[4], 1, 2), "actual host tuple invalid")
            fmt = "8N2" if serial[3:] == [1, 2] else {1: "8N1", 2: "8E1", 3: "8O1"}.get(serial[3]) if serial[4] == 1 else None
            require(dict(baud=serial[1], format=fmt) in tuples, "finding tuple outside candidate set")
            prefix = bytes((endpoint[1], 3, 0, 0, 0, 1))
            require(isinstance(f.get("tx_hex"), str) and f["tx_hex"].lower() == (prefix + wire_crc(prefix).to_bytes(2, "little")).hex(), "emitted query differs from public minimal probe")
            require(number(f.get("started_us"), scan["started_us"], scan["deadline_us"]) and
                    number(f.get("deadline_us"), f["started_us"] + 1, scan["deadline_us"]), "attempt deadline outside scan")
            require(number(f.get("outcome"), 1, 9) and number(f.get("confidence"), 0, 2) and type(f.get("raw_model_known")) is bool and number(f.get("raw_model"), 0, 65535), "probe outcome invalid")
            e, raw = evidence(f.get("probe"), 1)
            require(e["attempted_us"] == f["started_us"], "probe request/evidence correlation differs")
            if f["outcome"] in (1, 2):
                require(e["event"] == 0 and e["qualified"] and e["tx_accepted"] == 8 and
                        e["latest_us"] <= f["deadline_us"] and wire_crc(raw) == 0, "responder lacks checked on-time frame")
            if e["event"] == 0 and e["qualified"] and e["latest_us"] <= f["deadline_us"]:
                status, detail, frame_error = _reply_status(raw, e["received_length"], endpoint[1], 3, 7)
                require((e["status"], e["detail"], e["frame_error"]) == (status, detail, frame_error), "checked wire failure/status differs")
                expected_outcome = 1 if status == "OK" else 2 if status == "EXCEPTION" else 4 if frame_error == 3 else 3
                require(f["outcome"] == expected_outcome, "checked mismatch/malformed classification differs")
            elif e["event"] == 0:
                detail = 10 if e["qualified"] else 13
                require((e["status"],e["detail"],e["frame_error"]) == ("ILLEGAL_VALUE",detail,0) and
                        f["outcome"] == (7 if e["qualified"] else 9), "late/unqualified probe classification differs")
            elif e["event"] == 2:
                require(f["outcome"] == 6, "cancelled probe classification differs")
            elif e["event"] == 3:
                require(e["delivered_us"] >= f["deadline_us"] and f["outcome"] == (7 if raw else 5),
                        "deadline probe classification differs")
            if f["outcome"] == 1:
                require(len(raw) == 7 and raw[:3] == bytes((endpoint[1], 3, 2)) and e["status"] == "OK" and
                        f["confidence"] == 2 and f["raw_model_known"] and f["raw_model"] == int.from_bytes(raw[3:5], "big"), "model evidence differs")
            elif f["outcome"] == 2:
                require(len(raw) == 5 and raw[:2] == bytes((endpoint[1], 0x83)) and e["status"] == "EXCEPTION" and
                        e["detail"] == raw[2] and f["confidence"] == 1 and not f["raw_model_known"], "checked exception differs")
            else:
                require(f["confidence"] == 0 and not f["raw_model_known"], "failed probe claims responsiveness")
            require(all(type(f.get(k)) is bool for k in ("identity_attempted", "identity_known", "identity_ambiguous")), "identity flags invalid")
            values = f.get("identity")
            require(isinstance(values, list) and len(values) == 4 and all(number(v, 0, 65535) for v in values), "identity values invalid")
            identity_row = f.get("identity_evidence")
            pending_identity = (f["identity_attempted"] and not f["identity_known"] and isinstance(identity_row, list) and
                                len(identity_row) == 16 and identity_row[1] == identity_row[4] == 0)
            ie, iraw = evidence(identity_row, 4, unused=not f["identity_attempted"] or pending_identity)
            if pending_identity:
                require(scan["phase"] == 2 and f is findings[-1], "pending refinement outside active identity phase")
            if f["identity_attempted"]:
                require(settings["identity"] and f["outcome"] == 1, "identity refinement lacks successful probe")
                admission=f.get("identity_admission")
                require(isinstance(admission,list) and len(admission)==2 and number(admission[0], op + 1, 0xFFFFFFFF) and admission[0] not in ids and
                        number(admission[1], f["started_us"] + 1, scan["deadline_us"]), "identity correlation/deadline invalid")
                ids.add(admission[0])
                if not pending_identity:
                    require(admission[1] == min(scan["deadline_us"], ie["attempted_us"] + settings["query_ms"] * 1000), "identity immutable deadline differs from preparation")
                    if ie["event"] == 0:
                        require((ie["status"],ie["detail"],ie["frame_error"]) ==
                                _reply_status(iraw,ie["received_length"],endpoint[1],3,13),
                                "identity codec evidence differs from retained RX")
                    elif ie["event"] == 3:
                        require(ie["delivered_us"] >= admission[1], "identity deadline event precedes deadline")
                    known = ie["event"] == 0 and ie["qualified"] and ie["status"] == "OK" and ie["latest_us"] <= admission[1]
                    require(f["identity_known"] == known, "identity success classification differs from checked closure")
            else:
                require(f.get("identity_admission") == [0,0], "unattempted identity has admission correlation")
            if f["identity_known"]:
                require(f["identity_attempted"] and ie["event"] == 0 and ie["qualified"] and ie["status"] == "OK" and
                        len(iraw) == 13 and iraw[:3] == bytes((endpoint[1], 3, 8)) and wire_crc(iraw) == 0 and
                        values == [int.from_bytes(iraw[i:i+2], "big") for i in range(3, 11, 2)] and
                        f["identity_ambiguous"] == (values[0] != f["raw_model"] or values[2] != endpoint[1]), "identity evidence or mismatch ambiguity differs")
                require(ie["attempted_us"] < scan["deadline_us"] and ie["latest_us"] <= f["identity_admission"][1] and ie["tx_accepted"] == 8,
                        "identity closure exceeds its immutable query/scan deadline")
            else:
                require(values == [0, 0, 0, 0] and not f["identity_ambiguous"], "failed/unattempted refinement publishes identity")
        require(scan["requests"] >= len(findings) + sum(f["identity_attempted"] for f in findings), "identity attempts exceed admitted request budget")
        if scan["phase"] == 5:
            require(scan["outcome"] != 0 and not scan["owned"] and scan["restored"] and scan["finished_us"] >= scan["started_us"], "terminal scan lacks restoration")
        if scan["phase"] == 4:
            require(scan["owned"] and not scan["restored"], "interlock lost ownership")
        if scan["released"]:
            require(scan["phase"] == 5, "released unfinished scan")
        old = self.discovery
        if old and old["operation_id"] == scan["operation_id"]:
            for key in ("started_us", "deadline_us", "settings", "original_target", "original_tuple", "original_serial_generation"):
                require(scan[key] == old[key], "retained immutable scan context changed")
            require(scan["requests"] >= old["requests"] and scan["count"] >= old["count"], "partial progress disappeared")
            for before, after in zip(old["findings"], findings):
                for key in ("profile", "target", "operation_id", "serial", "tx_hex", "started_us", "deadline_us", "outcome", "confidence", "raw_model_known", "raw_model", "probe"):
                    require(before[key] == after[key], "retained attempt evidence changed")
                if before["identity_attempted"]:
                    require(after["identity_attempted"], "identity attempt disappeared")
                    require(before["identity_admission"] == after["identity_admission"],
                            "retained identity correlation changed")
                    if before["identity_evidence"][1]:
                        require(before == after, "settled identity evidence changed")
        elif old and item["ok"]:
            require(action == 0 and old["released"] and scan["operation_id"] > old["operation_id"], "new scan replaced retained findings")
        self.discovery = json.loads(json.dumps(scan))
        # The scan owns host reconfiguration and restoration; its historical
        # original generation is not the restored adapter's current generation.
        # Until an explicit host query refreshes that evidence, do not carry a
        # prior generation into admission checks for the next ordinary request.
        self.serial = None

    def _complete(self, handle: Command, item: dict) -> None:
        if self.clock() >= handle.deadline:
            raise BenchError("command response deadline expired; command was not replayed")
        if handle.command == "load" and item["ok"]:
            check_load_reply(item, handle.load)
        if handle.command == "memory" and item["ok"]:
            if item.get("valid") is not True:
                raise BenchError("memory measurements are unavailable")
            check_counts(item, MEMORY_FIELDS, "memory")
        if handle.command in ("status", "health") and item["ok"]:
            self._check_cached_state(item)
        if handle.command in ("axis", "prepare") and (item["ok"] or "code" in item):
            self._check_axis(item, handle.command == "prepare")
        if handle.command == "host":
            self._check_host(handle, item)
        if handle.command == "communication":
            self._check_communication(handle, item)
        if handle.command == "persistence":
            self._check_persistence(handle, item)
        if handle.command == "debug":
            self._check_debug(handle, item)
        if handle.command == "motion-profile":
            self._check_motion_profile(handle, item)
        if handle.command == "useaddr":
            self._check_useaddr(handle, item)
        if handle.command == "wiring":
            self._check_wiring(handle, item)
        if handle.command == "profile-list" and item["ok"]:
            expected = dict(manufacturer="stepperonline", manufacturer_name="STEPPERONLINE", profile="ess_rs", name="ESS-RS",
                            probe=True, identity=True, nonchanging=True, exact_model=False, firmware=False,
                            minimum_address=1, maximum_address=247, probe_first=0, probe_count=1, identity_first=0, identity_count=4)
            if item.get("bus_traffic") is not False or item.get("profiles") != [expected]:
                raise BenchError("local discovery inventory differs from implemented profile capabilities")
        if handle.command == "discover":
            self._check_discovery(handle, item)
        if handle.command in ("status", "config") and item["ok"]:
            self._check_host_serial(item, handle.serial)
            if handle.command == "config" and handle.serial is not None and handle.serial["known"]:
                if item.get("baud") != handle.serial["baud"] or item.get("format") != handle.serial["format"]:
                    raise BenchError("config differs from current host tuple")
        if handle.command == "monitor" and item["ok"]:
            if type(item.get("enabled")) is not bool:
                raise BenchError("monitor enabled state is invalid")
            check_counts(item, ("interval_ms", "count", "remaining", "operation_id", "next_due_us", "admitted", "rejected", "cancelled"), "monitor")
            if item["remaining"] > item["count"] or item["count"] > 1000 or (item["enabled"] and not 100 <= item["interval_ms"] <= 60000):
                raise BenchError("monitor finite budget is inconsistent")
        if handle.command == "status" and item["ok"]:
            uptime = item.get("uptime_ms")
            if type(uptime) is not int or not 0 <= uptime <= 0xFFFFFFFFFFFFFFFF:
                raise BenchError("status lacks a valid monotonic uptime")
            if self.uptime_ms is not None and uptime < self.uptime_ms:
                raise BenchError("device uptime regressed; possible reset")
            self.uptime_ms = uptime
        if handle.command == "release" and item["ok"]:
            original = self.operations.get(handle.operation_id)
            if original is not None:
                if original.terminal is None:
                    raise BenchError("release succeeded before operation terminal")
                original.released = True
                del self.operations[handle.operation_id]
        handle.terminal = item
        self.emit("complete", id=handle.id, command=handle.command,
                  operation_id=handle.operation_id,
                  duration_s=round(self.clock() - handle.started, 6), ok=item["ok"])

    def _dispatch(self, item: dict) -> None:
        if item.get("type") == "traffic":
            self._check_traffic(item)
            return
        request_id = item.get("id")
        handle = self.pending.get(request_id) if type(request_id) is int else None
        if (handle is None or item.get("command") != handle.command
                or item.get("profile") != "ess_rs" or type(item.get("ok")) is not bool):
            raise BenchError("unsolicited reply ID, command, profile or result does not match request")
        if handle.terminal is not None:
            raise BenchError("duplicate or unsolicited terminal response")
        if self.clock() >= handle.deadline:
            raise BenchError("command response deadline expired; command was not replayed")
        self.emit("reply", response=item)
        asynchronous = handle.command in (*READ_COMMANDS, *ACTION_COMMANDS, "recover", *MOVE_COMMANDS, "velocity", "driver", "io", "segment", "control", "tuning", "home")
        if asynchronous:
            if item.get("type") == "reply" and not handle.accepted:
                if not item["ok"]:
                    self._check_admission_refusal(handle, item)
                    self._complete(handle, item)
                    return
                if item.get("result") != "accepted":
                    raise BenchError(f"{handle.command} acceptance is not explicit")
                if handle.command in (*READ_COMMANDS, *ACTION_COMMANDS, *MOVE_COMMANDS, "velocity", "driver", "io", "segment", "control", "tuning", "home"):
                    address = item.get("address")
                    if (type(address) is not int or not 1 <= address <= 247
                            or (handle.address is not None and address != handle.address)):
                        raise BenchError("probe acceptance address does not match request")
                    handle.address = address
                if item.get("read_kind") != TYPED_READS.get(handle.command):
                    raise BenchError("typed-read acceptance kind does not match request")
                operation_id = item.get("operation_id")
                if not self._operation_id(operation_id) or operation_id <= self.last_operation_id:
                    raise BenchError("accepted operation ID is missing, reused or not monotonic")
                if len(self.operations) == MAX_OPERATIONS:
                    raise BenchError("accepted operation exceeds retained result limit")
                quota = operation_quota(handle.command)
                same_kind = sum(operation_quota(original.command) == quota for original in self.operations.values())
                if same_kind >= (MAX_PROBES if quota == "ordinary" else 1):
                    raise BenchError("accepted operation exceeds its retained result quota")
                handle.operation_id = operation_id
                handle.accepted = True
                self.operations[operation_id] = handle
                self.last_operation_id = operation_id
                return
            expected_type = {"probe": "probe", "capture-read": "capture_read", "recover": "recovery",
                             "read-identity": "read", "read-config": "read", "read-state": "read",
                             **dict.fromkeys(ACTION_COMMANDS, "action"), **dict.fromkeys(MOVE_COMMANDS, "move"), "velocity": "velocity", "driver": "driver", "io": "io", "segment": "segment", "control": "control", "tuning": "tuning", "home": "home"}[handle.command]
            if item.get("type") != expected_type or not handle.accepted:
                raise BenchError(f"{handle.command} response sequence is invalid")
            if (not self._operation_id(item.get("operation_id"))
                    or item["operation_id"] != handle.operation_id
                    or type(item.get("command_id")) is not int or item["command_id"] != handle.id):
                raise BenchError("terminal operation ID or original command ID does not match acceptance")
            self._check_host_serial(item, handle.serial)
            if handle.command == "probe":
                self._check_probe(item, handle.address)
            elif handle.command == "capture-read":
                self._check_capture_read(item, handle.address)
            elif handle.command in TYPED_READS:
                self._check_typed_read(item, TYPED_READS[handle.command], handle.address)
            elif handle.command in ACTION_COMMANDS:
                self._check_action(item, handle.command, handle.address, handle.stop_policy)
            elif handle.command == "velocity":
                self._check_velocity(item, handle.address, handle.velocity_args)
            elif handle.command == "home":
                self._check_home(item, handle.address, handle.home_args)
            elif handle.command in ("driver", "io", "segment", "control", "tuning"):
                self._check_driver(item, handle.address, handle.driver_args, "tuning" if handle.command == "tuning" else "segment" if handle.command == "segment" else "control_settings" if handle.command == "control" else "io" if handle.command == "io" else "drive")
            elif handle.command in MOVE_COMMANDS:
                self._check_move(item, handle.address, handle.move_args, handle.command[5:])
            else:
                self._check_recovery(item)
        else:
            if item.get("type") != "reply":
                raise BenchError("unexpected structured console event")
            if handle.command in ("result", "release", "cancel"):
                operation_id = item.get("operation_id")
                if (item["ok"] or operation_id is not None) and (
                        not self._operation_id(operation_id) or operation_id != handle.operation_id):
                    raise BenchError("host control operation ID does not match request")
                if handle.command == "result" and operation_id is not None:
                    original = self.operations.get(operation_id)
                    self._check_host_serial(item, original.serial if original else None)
                    original_id = item.get("command_id")
                    if (not self._operation_id(original_id)
                            or (original is not None and original_id != original.id)):
                        raise BenchError("result original command ID does not match retained operation")
                    recovery = item.get("recovery", False)
                    capture_read = item.get("capture_read", False)
                    read_kind = item.get("read_kind")
                    action_kind = item.get("action_kind")
                    move_kind = item.get("move_kind")
                    velocity = item.get("velocity", False)
                    driver = item.get("driver", False)
                    home = item.get("home", False)
                    if (type(home) is not bool or (home and (driver or velocity or move_kind is not None or action_kind is not None or recovery or capture_read or read_kind is not None)) or
                            (original is not None and home != (original.command == "home"))):
                        raise BenchError("result home kind does not match retained operation")
                    if (type(driver) is not bool or (driver and (velocity or move_kind is not None or action_kind is not None or recovery or capture_read or read_kind is not None)) or
                            (original is not None and driver != (original.command in ("driver", "io", "segment", "control", "tuning")))):
                        raise BenchError("result driver kind does not match retained operation")
                    if type(velocity) is not bool or (velocity and (move_kind is not None or action_kind is not None or recovery or capture_read or read_kind is not None)) or (original is not None and velocity != (original.command == "velocity")):
                        raise BenchError("result velocity kind does not match retained operation")
                    stop_policy = item.get("stop_policy")
                    if (move_kind is not None and "move-" + move_kind not in MOVE_COMMANDS) or (
                            move_kind is not None and (action_kind is not None or recovery or capture_read or read_kind is not None)) or (
                            original is not None and move_kind != (original.command[5:] if original.command in MOVE_COMMANDS else None)):
                        raise BenchError("result move kind does not match retained operation")
                    if (action_kind is not None and action_kind not in ACTION_KINDS.values()) or (
                            action_kind == "stop" and stop_policy not in ("normal", "direct")) or (
                            action_kind != "stop" and not velocity and stop_policy is not None) or (
                            action_kind is not None and (recovery or capture_read or read_kind is not None)) or (
                            original is not None and action_kind != ACTION_KINDS.get(original.command)) or (
                            not velocity and stop_policy != (original.stop_policy if original else stop_policy)):
                        raise BenchError("result action kind or policy does not match retained operation")
                    if (type(recovery) is not bool or
                            type(capture_read) is not bool or (recovery and capture_read) or
                            (read_kind is not None and (type(read_kind) is not str or read_kind not in TYPED_WINDOWS)) or
                            (read_kind is not None and (recovery or capture_read)) or
                            (original is not None and (recovery != (original.command == "recover") or
                             capture_read != (original.command == "capture-read") or
                             read_kind != TYPED_READS.get(original.command)))):
                        raise BenchError("result kind does not match retained operation")
                    if item.get("result") == "pending":
                        if item["ok"] is not True or type(item.get("recovery")) is not bool:
                            raise BenchError("pending result lacks a valid lifecycle")
                        if original is not None and original.terminal is not None:
                            raise BenchError("completed retained operation regressed to pending")
                    elif home:
                        self._check_home(item, original.address if original else None, original.home_args if original else None)
                    elif driver:
                        self._check_driver(item, original.address if original else None, original.driver_args if original else None, ("tuning" if original.command == "tuning" else "segment" if original.command == "segment" else "control_settings" if original.command == "control" else "io" if original.command == "io" else "drive") if original else None)
                    elif velocity:
                        self._check_velocity(item, original.address if original else None, original.velocity_args if original else None)
                    elif move_kind is not None:
                        self._check_move(item, original.address if original else None, original.move_args if original else None, move_kind)
                    elif action_kind is not None:
                        action_command = next(name for name, kind in ACTION_KINDS.items() if kind == action_kind)
                        self._check_action(item, action_command, original.address if original else None, stop_policy)
                    elif recovery:
                        self._check_recovery(item)
                    elif capture_read:
                        self._check_capture_read(item, original.address if original else None)
                    elif read_kind is not None:
                        self._check_typed_read(item, read_kind, original.address if original else None)
                    else:
                        self._check_probe(item, original.address if original else None)
                    if original is not None and original.terminal is not None:
                        routing = {"type", "id", "command", "recovery"}
                        retained = {key: value for key, value in original.terminal.items() if key not in routing}
                        inspected = {key: value for key, value in item.items() if key not in routing}
                        if inspected != retained:
                            raise BenchError("result changed immutable retained terminal")
        self._complete(handle, item)

    def _consume(self, data: bytes) -> None:
        for raw in self._lines(data):
            item = self._decode(raw)
            traffic = item is not None and item.get("type") == "traffic"
            for handle in self.pending.values():
                if handle.terminal is None:
                    if traffic:
                        handle.traffic_bytes += len(raw) + 1
                        if handle.traffic_bytes > MAX_TRAFFIC_INPUT:
                            raise BenchError("traffic exceeds command diagnostic limit")
                    else:
                        handle.input_bytes += len(raw) + 1
                        if handle.input_bytes > MAX_INPUT:
                            raise BenchError("command response exceeds input limit")
            if item is not None:
                self._dispatch(item)

    def _check_pending(self, deadline: float) -> None:
        total = 0
        first_traffic = self.traffic_sequence
        while True:
            if self.clock() >= deadline:
                raise BenchError("command deadline expired while collecting pending diagnostics")
            self._check_deadlines()
            data = self._read()
            total += len(data)
            if total > (MAX_TRAFFIC_INPUT if self.traffic_sequence != first_traffic else MAX_INPUT):
                raise BenchError("pending diagnostics exceed input limit")
            self._consume(data)
            if not data:
                active = any(handle.terminal is None for handle in self.pending.values())
                if self._diagnostic_line_pending() and not active:
                    self.sleep(0.005)
                    continue
                if self.buffer and not active:
                    raise BenchError("incomplete pending line before command")
                return

    def begin(self, command: str, *, timeout_s: float = 3.0,
              address: int | None = None, load: tuple[int, int, int] | None = None,
              operation_id: int | None = None, monitor: tuple[int, int] | bool | None = None,
              host_args: tuple[str, ...] | None = None, stop_policy: str | None = None,
              move_args: tuple[str, ...] | None = None, velocity_args: tuple[str, ...] | None = None,
              driver_args: tuple[str, ...] | None = None, home_args: tuple[str, ...] | None = None) -> Command:
        """Send once and collect admission/local reply; bus completion can stay pending.

        Up to eleven handles (eight ordinary operations, recovery, stop and a local query) may be
        outstanding. Accepted operation IDs stay retained until explicit release.
        No command, including recovery, is retried after any framing failure.
        """
        positive(timeout_s, "command timeout")
        if command not in COMMANDS:
            raise ValueError("command is not in the explicit harness inventory")
        if (command == "stop" and stop_policy not in ("normal", "direct")) or (command != "stop" and stop_policy is not None):
            raise ValueError("stop requires explicit normal or direct policy")
        if command == "host":
            host_args = () if host_args is None else host_args
            host_arguments(host_args)
        elif command == "communication":
            host_args = () if host_args is None else host_args
            communication_arguments(host_args)
        elif command == "persistence":
            host_args = () if host_args is None else host_args
            persistence_arguments(host_args)
        elif command == "debug":
            host_args = () if host_args is None else host_args
            debug_arguments(host_args)
        elif command == "motion-profile":
            motion_profile_arguments(host_args)
        elif command == "wiring":
            host_args = () if host_args is None else host_args
            wiring_arguments(host_args)
        elif command == "discover":
            host_args = () if host_args is None else host_args
            discovery_arguments(host_args)
        elif host_args is not None:
            if (command not in ("axis", "prepare") or not isinstance(host_args, tuple) or
                    not 1 <= len(host_args) <= (9 if command == "prepare" else 8) or any(type(token) is not str or not token or
                    any(ord(char) < 33 or ord(char) > 126 for char in token) for token in host_args)):
                raise ValueError("host preparation requires bounded ASCII tokens without whitespace")
        elif command in ("axis", "prepare"):
            raise ValueError("axis/prepare require explicit host-only arguments")
        if command in MOVE_COMMANDS:
            move_arguments(command[5:], move_args)
        elif move_args is not None:
            raise ValueError("move arguments are only valid for finite moves")
        if command == "velocity":
            velocity_arguments(velocity_args)
        elif velocity_args is not None:
            raise ValueError("velocity arguments require velocity command")
        if command in ("driver", "io", "segment", "control", "tuning"):
            tuning_arguments(driver_args) if command == "tuning" else segment_arguments(driver_args) if command == "segment" else driver_arguments(driver_args, "control_settings" if command == "control" else "io" if command == "io" else "drive")
        elif driver_args is not None:
            raise ValueError("driver arguments require driver command")
        if command == "home":
            home_arguments(home_args)
        elif home_args is not None:
            raise ValueError("home arguments require home command")
        health_check = command == "health-check"
        if health_check:
            command = "read-state"  # The wire alias returns canonical read-state records.
        if monitor is not None:
            if command != "monitor" or (monitor is not False and
                (not isinstance(monitor, tuple) or len(monitor) != 2 or
                 type(monitor[0]) is not int or type(monitor[1]) is not int or
                 not 100 <= monitor[0] <= 60000 or not 1 <= monitor[1] <= 1000)):
                raise ValueError("monitor requires off or interval 100..60000/count 1..1000")
        if command == "useaddr" and address is None:
            raise ValueError("useaddr requires an explicit ESS address within 1..247")
        if address is not None and (command not in (*READ_COMMANDS, *ACTION_COMMANDS, *MOVE_COMMANDS, "velocity", "driver", "io", "segment", "control", "tuning", "home", "useaddr") or type(address) is not int
                                    or not 1 <= address <= 247):
            raise ValueError("an ESS probe address must be an integer within 1..247")
        if load is not None:
            if command != "load":
                raise ValueError("load settings are only valid for the load command")
            check_load(load)
        controls = command in ("result", "release", "cancel")
        if controls != (operation_id is not None) or (controls and not self._operation_id(operation_id)):
            raise ValueError("result/release/cancel require an integer operation ID within 1..4294967295")
        if not self.synchronized:
            raise BenchError("console framing failed; session cannot be reused")
        if not self.identified and command != "version":
            raise BenchError("identify the standalone firmware before issuing commands")
        if len(self.pending) == MAX_COMMANDS:
            raise BenchError("outstanding command limit reached; wait for a retained handle")
        if self.next_id > 0xFFFFFFFF:
            raise BenchError("request IDs exhausted; start a new inspected session")
        if host_args is not None and len(f"@{self.next_id} {command} {' '.join(host_args)}") >= 128:
            raise ValueError("host command exceeds the console line bound")
        if move_args is not None and len(f"@{self.next_id} move {command[5:]} {' '.join(move_args)}" + ("" if address is None else f" {address}")) >= 128:
            raise ValueError("move command exceeds the console line bound")
        if velocity_args is not None and len(f"@{self.next_id} velocity {' '.join(velocity_args)}" + ("" if address is None else f" {address}")) >= 128:
            raise ValueError("velocity command exceeds the console line bound")
        if driver_args is not None and len(f"@{self.next_id} profile ess_rs {command} {' '.join(driver_args)}" + ("" if address is None else f" {address}")) >= 128:
            raise ValueError("driver command exceeds the console line bound")
        request_id = self.next_id
        self.next_id += 1
        started = self.clock()
        deadline = started + timeout_s
        try:
            self._check_pending(deadline)
            if self.clock() >= deadline:
                raise BenchError("command deadline expired before transmission")
            handle = Command(self, request_id, command, started, deadline, address, load, operation_id)
            handle.stop_policy = stop_policy
            handle.move_args = move_args
            handle.velocity_args = velocity_args
            handle.driver_args = driver_args
            handle.home_args = home_args
            handle.host_args = host_args
            if self.serial is not None:
                known = self.serial["active_known"] and not self.serial["blocked"]
                handle.serial = dict(known=known, baud=self.serial["active"]["baud"] if known else 0,
                                     format=self.serial["active"]["format"] if known else "unknown",
                                     generation=self.serial["serial_generation"] if known else 0)
            self.pending[request_id] = handle
            suffix = "" if address is None else f" {address}"
            if load is not None:
                suffix = " " + " ".join(str(value) for value in load)
            if operation_id is not None:
                suffix = f" {operation_id}"
            if monitor is not None:
                suffix = " off" if monitor is False else f" {monitor[0]} {monitor[1]}"
            if host_args is not None:
                suffix = " " + " ".join(host_args)
            if stop_policy is not None:
                suffix = " " + stop_policy + suffix
            if move_args is not None:
                suffix = " " + " ".join(move_args) + suffix
            if velocity_args is not None:
                suffix = " " + " ".join(velocity_args) + suffix
            if home_args is not None:
                suffix = " " + " ".join(home_args) + suffix
            if driver_args is not None:
                suffix = " " + " ".join(driver_args) + suffix
            wire_command = "health check" if health_check else "read " + TYPED_READS[command] if command in TYPED_READS else command
            if command == "profile-list": wire_command = "profile list"
            if command in MOVE_COMMANDS: wire_command = "move " + command[5:]
            if command in ("driver", "io", "segment", "control", "tuning", "communication", "persistence"): wire_command = "profile ess_rs " + command
            payload = f"@{request_id} {wire_command}{suffix}\n".encode("ascii")
            if len(payload) > 128:
                raise ValueError("command exceeds the console line bound")
            self.emit("send", id=request_id, command=command, address=address,
                      load=load, operation_id=operation_id)
            if self.port.write(payload) != len(payload):
                raise BenchError("short serial command write; command was not replayed")
            while not handle.accepted and handle.terminal is None:
                self._check_deadlines()
                data = self._read()
                self._consume(data)
                if not data:
                    self.sleep(0.005)
            self._trailing()
            return handle
        except BaseException:
            self.synchronized = False
            raise

    def _trailing(self) -> None:
        active = any(handle.terminal is None for handle in self.pending.values())
        if self.buffer and not active and not self._diagnostic_line_pending():
            raise BenchError("terminal response has an incomplete trailing line")

    def wait(self, handle: Command, *, release: bool = False) -> dict:
        """Collect a handle's original terminal while routing other admitted records.

        Default retention supports result/cancel/release scenarios. release=True
        sends one separate correlated release after completion, never motor I/O.
        """
        if not isinstance(handle, Command) or handle.owner is not self:
            raise ValueError("command handle belongs to another console")
        if not self.synchronized:
            raise BenchError("console framing failed; session cannot be reused")
        interrupt_safe = False
        try:
            while handle.terminal is None:
                self._check_deadlines()
                data = self._read()
                self._consume(data)
                if not data:
                    # An idle pause has no read/write in progress and all bytes
                    # are retained. A finite campaign can issue its explicit
                    # cleanup stop while routing this admitted operation.
                    interrupt_safe = True
                    self.sleep(0.005)
                    interrupt_safe = False
            self._trailing()
            self.pending.pop(handle.id, None)
            if release and handle.accepted and not handle.released:
                remaining = handle.deadline - self.clock()
                if remaining <= 0:
                    raise BenchError("command deadline expired before result release")
                acknowledgement = self.command("release", operation_id=handle.operation_id, timeout_s=remaining)
                if not acknowledgement["ok"]:
                    raise BenchError("explicit result release was rejected")
            return handle.terminal
        except KeyboardInterrupt:
            if not interrupt_safe:
                self.synchronized = False
            raise
        except BaseException:
            self.synchronized = False
            raise

    def command(self, command: str, *, timeout_s: float = 3.0,
                address: int | None = None, load: tuple[int, int, int] | None = None,
                operation_id: int | None = None, monitor: tuple[int, int] | bool | None = None,
                host_args: tuple[str, ...] | None = None, stop_policy: str | None = None,
                move_args: tuple[str, ...] | None = None, velocity_args: tuple[str, ...] | None = None,
                driver_args: tuple[str, ...] | None = None, home_args: tuple[str, ...] | None = None) -> dict:
        """Send once, wait for its terminal, then explicitly release admitted results."""
        handle = self.begin(command, timeout_s=timeout_s, address=address, load=load,
                            operation_id=operation_id, monitor=monitor, host_args=host_args, stop_policy=stop_policy, move_args=move_args, velocity_args=velocity_args, driver_args=driver_args, home_args=home_args)
        return self.wait(handle, release=True)

    def identify(self, *, timeout_s: float = 3.0) -> dict:
        response = self.command("version", timeout_s=timeout_s)
        if (not response["ok"] or response.get("product") != "MotorControl-RS"
                or type(response.get("protocol")) is not int
                or response["protocol"] != 2
                or type(response.get("outstanding_capacity")) is not int
                or response["outstanding_capacity"] != MAX_OPERATIONS):
            self.synchronized = False
            raise BenchError("port is not the supported MotorControl-RS probe console")
        self.identified = True
        return response


def successful(console: Console, command: str, timeout_s: float,
               address: int | None = None,
               load: tuple[int, int, int] | None = None) -> dict:
    result = console.command(command, timeout_s=timeout_s, address=address, load=load)
    if not result["ok"]:
        raise BenchError(f"{command} failed: {result.get('result', result.get('transport'))}")
    return result


def campaign(
    console: Console,
    mode: str,
    *,
    count: int,
    interval_s: float,
    timeout_s: float,
    address: int = 1,
    load: tuple[int, int, int] | None = None,
    read_command: str = "probe",
    typed_kind: str = "both",
    sleeper: Callable[[float], None] = time.sleep,
) -> None:
    """Finite work, no recovery and no replay. Watch never issues a probe.

    Load campaigns retain cached diagnostics after a complete failed probe,
    then stop. A framing failure ends communication immediately. Fixture load
    stays explicitly configured, including after a failure or interruption;
    the harness never sends cleanup or recovery commands behind the operator.
    """
    if mode == "state-health":
        if load is not None or read_command != "probe":
            raise ValueError("state-health uses only its reviewed state reads")
        state_health_campaign(console, count=count, interval_s=interval_s, timeout_s=timeout_s, address=address)
        return
    if mode == "typed-read":
        if type(count) is not int or count != 1 or interval_s != 0 or load is not None or read_command != "probe":
            raise ValueError("typed-read is one explicit configuration/identity scenario")
        typed_read_campaign(console, kind=typed_kind, timeout_s=timeout_s, address=address)
        return
    if mode not in {"probe", "capture-read", "stress", "watch", "load"}:
        raise ValueError("unknown campaign mode")
    if mode == "capture-read":
        read_command = "capture-read"
    if read_command not in ("probe", "capture-read") or (read_command != "probe" and mode not in ("capture-read", "load")):
        raise ValueError("capture-read requires its named campaign or load mode")
    if type(count) is not int or not 1 <= count <= 1_000_000 or (mode in READ_COMMANDS and count != 1):
        raise ValueError("invalid campaign count")
    if not math.isfinite(interval_s) or not 0 <= interval_s <= 60:
        raise ValueError("interval must be finite and within 0..60 seconds")
    positive(timeout_s, "command timeout")
    if type(address) is not int or not 1 <= address <= 247:
        raise ValueError("ESS probe address must be within 1..247")
    if mode == "load":
        check_load(load)
    elif load is not None:
        raise ValueError("load settings require a load campaign")

    attempted = passed = completed = 0
    latency_total = 0
    latency_min = latency_max = None
    latest_load = latest_memory = None
    failure = None
    try:
        if mode == "load":
            latest_load = successful(console, "load", timeout_s, load=load)
        successful(console, "stats", timeout_s)
        if latest_load is not None and latest_load["sample_gap_exceeded"]:
            raise BenchError("capture sample gap exceeded; load qualification failed")
        for iteration in range(count):
            console.emit("iteration", number=iteration + 1, mode=mode)
            probe = None
            if mode != "watch":
                attempted += 1
                probe = console.command(read_command, timeout_s=timeout_s, address=address)
                if probe["ok"]:
                    passed += 1
                    latency = probe["duration_us"]
                    latency_total += latency
                    latency_min = latency if latency_min is None else min(latency_min, latency)
                    latency_max = latency if latency_max is None else max(latency_max, latency)
                elif mode != "load":
                    raise BenchError(f"probe failed: {probe.get('result', probe.get('transport'))}")
            for command in ("status", "health", "memory"):
                report = successful(console, command, timeout_s)
                if command == "memory":
                    latest_memory = report
            if mode == "load":
                latest_load = successful(console, "load", timeout_s)
                check_load_reply(latest_load, load)
                if latest_load["sample_gap_exceeded"]:
                    successful(console, "stats", timeout_s)
                    raise BenchError("capture sample gap exceeded; load qualification failed")
                if probe is not None and not probe["ok"]:
                    successful(console, "stats", timeout_s)
                    raise BenchError(f"probe failed: {probe.get('result', probe.get('transport'))}")
            completed += 1
            if iteration + 1 < count and interval_s:
                sleeper(interval_s)
        successful(console, "stats", timeout_s)
        if mode == "load" and latest_load is not None:
            if (load[0] or load[2]) and not latest_load["work_iterations"]:
                raise BenchError("load window contained no competing task iterations")
            if load[0] and not latest_load["work_us"]:
                raise BenchError("requested CPU workload performed no measured work")
            if load[2] and not latest_load["console_lines"]:
                raise BenchError("console workload produced no complete lines")
            if load[1] and latest_load["owner_gap_max_us"] < load[1]:
                raise BenchError("requested owner delay was not observed during active service")
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        console.emit("summary", mode=mode, read_command=read_command, iterations=completed, requested_count=count,
                     probes_attempted=attempted, probes_passed=passed,
                     probes_failed=attempted - passed,
                     latency_us={"min": latency_min, "max": latency_max,
                                 "mean": latency_total / passed if passed else None},
                     load_settings=load, last_load=latest_load, last_memory=latest_memory,
                     ok=failure is None and completed == count, error=failure)


def typed_read_campaign(console: Console, *, kind: str, timeout_s: float, address: int) -> None:
    """One reviewed read per selected kind, retained inspection, explicit release.

    A missing/malformed response ends the session without retry or recovery.
    Inspection does not refresh observations, change settings or send motor traffic.
    """
    if kind not in ("identity", "config", "state", "both"):
        raise ValueError("typed read kind must be identity, config, state or both")
    positive(timeout_s, "command timeout")
    if type(address) is not int or not 1 <= address <= 247:
        raise ValueError("ESS read address must be within 1..247")
    kinds = ("identity", "config") if kind == "both" else (kind,)
    attempted = completed = 0
    failure = None
    try:
        successful(console, "caps", timeout_s)
        successful(console, "stats", timeout_s)
        for selected in kinds:
            attempted += 1
            handle = console.begin("read-" + selected, timeout_s=timeout_s, address=address)
            terminal = console.wait(handle)
            if not terminal["ok"]:
                raise BenchError(f"typed {selected} failed: {terminal.get('outcome', terminal.get('result'))}")
            # Each control uses the same remaining host budget; diagnostic queries
            # cannot renew the original operation's deadline.
            remaining = handle.deadline - console.clock()
            positive(remaining, "remaining typed-read inspection budget")
            inspected = console.command("result", operation_id=handle.operation_id, timeout_s=remaining)
            if not inspected["ok"]:
                raise BenchError("typed read retained inspection was rejected")
            remaining = handle.deadline - console.clock()
            positive(remaining, "remaining typed-read release budget")
            released = console.command("release", operation_id=handle.operation_id, timeout_s=remaining)
            if not released["ok"]:
                raise BenchError("explicit typed read release was rejected")
            completed += 1
        for command in ("status", "health", "memory", "drv", "stats"):
            successful(console, command, timeout_s)
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        console.emit("summary", mode="typed-read", read_kind=kind, reads_attempted=attempted,
                     reads_passed=completed, reads_failed=attempted - completed,
                     ok=failure is None and completed == len(kinds), error=failure)


def persistence_campaign(console: Console, *, kind: str, timeout_s: float,
                         execute: bool = False, verify: bool = False, finish: bool = False) -> dict:
    """Finite explicit workflow. Failure leaves retained state without cleanup writes."""
    persistence_arguments(("plan",kind))
    positive(timeout_s,"persistence deadline")
    if (verify or finish) and not execute or finish and not verify:
        raise ValueError("verification requires execution; finish requires explicit verification")
    deadline=console.clock()+timeout_s
    writes=reads=0
    failure=None
    operation_id=None
    def command(tokens):
        nonlocal operation_id
        remaining=deadline-console.clock()
        if remaining <= 0:
            raise BenchError("persistence procedure deadline; retained session requires inspection")
        result=console.command("persistence",host_args=tokens,timeout_s=remaining)
        if not result["ok"]:
            raise BenchError("persistence command refused: "+str(result.get("result")))
        context=result.get("context") or {}
        if tokens[0] == "begin":
            operation_id=context.get("operation_id")
            if not Console._operation_id(operation_id):
                raise BenchError("persistence begin omitted session identity")
        elif operation_id is not None and context.get("operation_id") != operation_id:
            raise BenchError("persistence session changed; no further work attempted")
        return result
    def settled(result):
        for _ in range(100):
            if not result["pending"]:return result
            remaining=deadline-console.clock()
            if remaining <= 0:break
            console.sleep(min(.01,remaining))
            result=command(("inspect",))
        raise BenchError("persistence pending bound reached; no replay or automatic cleanup")
    try:
        result=command(("plan",kind))
        console.emit("persistence_plan",plan=result.get("plan"),route_ready=result.get("route_ready"),execute=execute)
        if not execute:return result
        if not result["route_ready"]:
            raise BenchError("persistence backup/recommissioning route unavailable; no write attempted")
        reads+=1
        result=settled(command(("snapshot",)))
        if not result["snapshot_known"]:raise BenchError("persistence snapshot failed; no write attempted")
        writes+=1
        result=settled(command(("begin",kind)))
        c=result.get("context") or {}
        if c.get("state") not in (2,3) or c.get("outcome") not in (1,7):
            raise BenchError("persistence write failed/unknown; inspect and direct recovery explicitly")
        if verify:
            reads+=1
            result=settled(command(("verify",)))
            if not (result.get("context") or {}).get("verification_known"):
                raise BenchError("persistence verification failed; retained state requires explicit recovery")
            if finish:result=command(("finish",))
        return result
    except BaseException as exc:
        failure=str(exc)
        raise
    finally:
        console.emit("summary",mode="persistence-check",ok=failure is None,failure=failure,
                     write_attempts=writes,read_sequences=reads,auto_recovery=False,
                     restart_performed=False,write_replayed=False,all_parameter_durability=False)


def communication_campaign(console: Console, *, field: str, value: str, address: int,
                           timeout_s: float, execute: bool = False,
                           confirm: str | None = None, finish: bool = False) -> dict:
    """Preview first, optionally one write and one explicitly selected read.

    Failed/uncertain writes retain the session for separately directed recovery.
    Polls inspect cached session state only, with both count and elapsed bounds.
    No cleanup operation is invented on timeout, interruption or failed setup.
    """
    tokens = ("plan", field, value, str(address))
    communication_arguments(tokens)
    positive(timeout_s, "communication deadline")
    if confirm not in (None, "before", "requested") or (finish and confirm is None):
        raise ValueError("finish requires an explicit before/requested confirmation")
    if not execute and (confirm is not None or finish):
        raise ValueError("confirmation/finish require explicit execution")
    deadline = console.clock() + timeout_s
    writes = reads = 0
    failure = None
    operation_id = None
    def command(args):
        nonlocal operation_id
        remaining = deadline - console.clock()
        if remaining <= 0:
            raise BenchError("communication procedure deadline; retained session requires inspection")
        result = console.command("communication", host_args=args, timeout_s=remaining)
        if not result["ok"]:
            raise BenchError("communication command refused: " + str(result.get("result")))
        context = result.get("context") or {}
        if args[0] == "begin":
            operation_id = context.get("operation_id")
            if not Console._operation_id(operation_id):
                raise BenchError("communication begin omitted session identity")
        elif operation_id is not None and context.get("operation_id") != operation_id:
            raise BenchError("communication procedure session changed; no further work attempted")
        return result
    def settled(result):
        step = (result.get("context") or {}).get("step")
        for _ in range(100):
            if not result["pending"]:
                return result
            remaining = deadline - console.clock()
            if remaining <= 0:
                break
            console.sleep(min(0.01, remaining))
            result = command(("inspect",))
            if (result.get("context") or {}).get("step") != step:
                raise BenchError("communication procedure attempt changed; no further work attempted")
        raise BenchError("communication pending bound reached; no command replay or automatic cleanup")
    try:
        result = command(tokens)
        console.emit("communication_plan", plan=result.get("plan"), route_ready=result.get("route_ready"), execute=execute)
        if not execute:
            return result
        if not result.get("route_ready"):
            raise BenchError("communication route/restart fixture is unavailable; no write attempted")
        writes += 1  # Counts attempts even when the console response is lost.
        result = settled(command(("begin", field, value, str(address))))
        context = result.get("context") or {}
        if context.get("step") != 0 or context.get("state") != 2 or context.get("status") != "OK":
            raise BenchError("write outcome is not confirmed success; inspect retained context and direct recovery explicitly")
        if confirm is not None:
            command(("host", confirm))
            reads += 1
            result = settled(command(("confirm", confirm)))
            context = result.get("context") or {}
            if context.get("state") != 2 or context.get("status") != "OK" or not context.get("observed_active_known"):
                raise BenchError("confirmation failed; retained endpoint candidates require explicit recovery")
            if finish:
                result = command(("finish",))
        return result
    except BaseException as exc:
        failure = str(exc)
        raise
    finally:
        console.emit("summary", mode="communication-check", ok=failure is None, failure=failure,
                     write_attempts=writes, read_attempts=reads, auto_recovery=False,
                     save_sent=False, restart_performed=False, write_replayed=False)


def host_check_campaign(console: Console, *, baud: int, fmt: str, timeout_s: float, address: int) -> None:
    """Explicit finite host mismatch, one read, recovery, restore and one final read.

    Recovery is a declared step only after the expected no-response terminal.
    An unexpected failure never causes a read retry or automatic bus recovery.
    """
    tokens = ("set", str(baud), fmt)
    host_arguments(tokens)
    positive(timeout_s, "host check timeout")
    changed = False; restored = False; restoration_attempted = False; failure = None; mismatch = None
    reads = 0; recoveries = 0

    def read_once():
        nonlocal reads
        reads += 1
        handle = console.begin("probe", timeout_s=timeout_s, address=address)
        try:
            terminal = console.wait(handle)
            if handle.accepted:
                console.command("result", operation_id=handle.operation_id, timeout_s=timeout_s)
            return terminal
        finally:
            if handle.accepted and handle.terminal is not None and not handle.released and console.synchronized:
                release = console.command("release", operation_id=handle.operation_id, timeout_s=timeout_s)
                if not release["ok"]:
                    raise BenchError("host check result release failed")

    try:
        initial = console.command("host", timeout_s=timeout_s)
        if (not initial["ok"] or not initial.get("active_known") or initial.get("blocked")
                or initial["active"] != initial["original"]):
            raise BenchError("host check requires the original working tuple active")
        if initial["active"] == dict(baud=baud, format=fmt):
            raise ValueError("host check requires a deliberate different tuple")
        if baud not in initial["supported_bauds"] or fmt not in initial["supported_formats"]:
            raise ValueError("host check tuple is unsupported by selected adapter")
        if not read_once()["ok"]:
            raise BenchError("host check baseline probe failed; mismatch was not attempted")
        selected = console.command("host", host_args=tokens, timeout_s=timeout_s)
        changed = selected.get("active") != initial["original"] or not selected.get("active_known", False)
        if not selected["ok"]:
            raise BenchError("host check tuple setup failed")
        mismatch = read_once()
        if (mismatch["ok"] is not False or mismatch.get("transport") != "NO_RESPONSE"
                or mismatch.get("outcome") != "transport" or mismatch.get("codec") != "NOT_CHECKED"
                or mismatch.get("execution_unknown") is not True
                or type(mismatch.get("tx_bytes")) is not int or mismatch["tx_bytes"] != 8
                or type(mismatch.get("rx_bytes")) is not int or mismatch["rx_bytes"] != 0
                or mismatch.get("raw_model") is not None or mismatch.get("timing_valid") is not False):
            raise BenchError("host mismatch did not produce the expected read-only no-response evidence")
        console.emit("host_recovery_step", reason="expected_mismatch_no_response", automatic_retry=False)
        recoveries += 1
        recovered = console.command("recover", timeout_s=timeout_s)
        if not recovered["ok"]:
            raise BenchError("explicit host mismatch recovery failed")
        restoration_attempted = True
        restoration = console.command("host", host_args=("restore",), timeout_s=timeout_s)
        restored = restoration["ok"] and restoration.get("active") == initial["original"]
        if not restored:
            raise BenchError("host original tuple restoration failed")
        if not read_once()["ok"]:
            raise BenchError("host restored probe failed; read was not replayed")
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        if changed and not restoration_attempted and console.synchronized:
            try:
                restoration_attempted = True
                restoration = console.command("host", host_args=("restore",), timeout_s=timeout_s)
                restored = restoration["ok"] and restoration.get("active") == initial["original"]
                if not restored and failure is None:
                    raise BenchError("host original tuple cleanup failed")
            except BaseException as exc:
                console.emit("host_restore_failure", error=str(exc) or "interrupted")
                if failure is None:
                    failure = str(exc) or "interrupted"
                    raise
        console.emit("summary", mode="host-check", ok=failure is None, reads_attempted=reads,
                     recoveries_attempted=recoveries, restored=restored,
                     restoration_attempted=restoration_attempted,
                     requested_host=dict(baud=baud, format=fmt), mismatch_result=mismatch,
                     motor_writes=0, automatic_retries=0, error=failure)


def driver_read_campaign(console: Console, *, timeout_s: float, address: int, command: str = "driver",
                         driver_args: tuple[str, ...] = ("read",)) -> None:
    """One settings attempt, immutable inspection and release; never replay/rollback."""
    if command not in ("driver", "io", "segment", "control", "tuning"): raise ValueError("unknown settings command")
    candidate = tuning_arguments(driver_args)[1] if command == "tuning" else segment_arguments(driver_args)[2] if command == "segment" else driver_arguments(driver_args, "control_settings" if command == "control" else "io" if command == "io" else "drive")
    positive(timeout_s, "driver read timeout")
    handle = None; terminal = None; inspected = None; failure = None
    try:
        handle = console.begin(command, driver_args=driver_args, address=address, timeout_s=timeout_s)
        terminal = console.wait(handle)
        if handle.accepted:
            inspected = console.command("result", operation_id=handle.operation_id, timeout_s=timeout_s)
            if not inspected["ok"]:
                raise BenchError("driver retained result inspection failed")
        if not terminal["ok"]:
            raise BenchError("driver read rejected or failed: " + str(terminal.get("result", terminal.get("outcome"))))
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        release_error = None
        try:
            if handle is not None and handle.accepted and handle.terminal is not None and not handle.released and console.synchronized:
                release = console.command("release", operation_id=handle.operation_id, timeout_s=timeout_s)
                if not release["ok"]: raise BenchError("driver result release rejected")
        except BaseException as exc:
            release_error = str(exc) or "interrupted"
            if failure is None: failure = release_error; raise
        finally:
            console.emit("summary", mode=command + ("-set" if candidate else "-read"),
                         reads_attempted=0 if candidate else 1, settings_fields_requested=len(candidate), ok=failure is None,
                         driver_result=terminal, inspected_result=inspected,
                         motor_writes=(sum(row[3] and row[7] > 0 for row in terminal.get("evidence", [])) if candidate and terminal else None if candidate else 0), error=failure,
                         release_error=release_error)


def drive_reported_stopped(state: dict) -> bool:
    """Checked drive reports, not an independent shaft or exact sample time."""
    blocks = {block.get("block"): block for block in state.get("state_blocks", [])}
    motion, feedback = blocks.get(0, {}), blocks.get(2, {})
    return (state.get("ok") is True and motion.get("running") is False and
            motion.get("raw_alarm") == 0 and motion.get("alarm_flag") is False and
            type(feedback.get("raw_speed")) is int and feedback["raw_speed"] == 0)


def observe_stopped(console: Console, *, address: int, timeout_s: float,
                    attempts: int = 10, interval_s: float = .05, on_sample=None) -> dict:
    """Bounded read-only settlement; never repeat a motor stop or motion write."""
    if type(attempts) is not int or not 1 <= attempts <= 10:
        raise ValueError("standstill observation budget must be within 1..10")
    if not math.isfinite(interval_s) or not 0 <= interval_s <= .05:
        raise ValueError("standstill observation interval must be within 0...05s")
    positive(timeout_s, "standstill observation timeout")
    deadline = console.clock() + timeout_s
    for index in range(attempts):
        remaining = deadline - console.clock()
        if remaining <= 0:
            break
        state = console.command("read-state", address=address, timeout_s=remaining)
        if on_sample is not None:
            on_sample(state)
        if not state["ok"]:
            raise BenchError("standstill observation was rejected or failed")
        if drive_reported_stopped(state):
            return state
        if index + 1 < attempts:
            console.sleep(min(interval_s, max(0, deadline - console.clock())))
    raise BenchError("zero-speed/non-running observation bound exhausted")


def move_campaign(console: Console, *, move_args: tuple[str, ...] | None, cleanup_stop: str,
                  timeout_s: float, address: int, command: str = "move-relative",
                  velocity_args: tuple[str, ...] | None = None, home_args: tuple[str, ...] | None = None) -> None:
    """One finite motion attempt, explicit stop, final reports and local release.

    Cleanup never replays the move or converts an uncertain outcome to success.
    Broken framing prevents further commands and leaves cleanup explicitly unknown.
    Drive status is not an independent physical shaft observation.
    """
    label = "home" if command == "home" else "velocity" if command == "velocity" else "move"
    if cleanup_stop not in ("normal", "direct"):
        raise ValueError(f"finite {label} requires an explicit cleanup stop policy")
    positive(timeout_s, f"{label} timeout")
    handle = None
    request_id = console.next_id
    terminal = inspected = stopped = final_state = final_health = None
    cleanup = "not_required"
    failure = None
    try:
        handle = console.begin(command, move_args=move_args, velocity_args=velocity_args, home_args=home_args, address=address, timeout_s=timeout_s)
        if handle.accepted:
            cleanup = "unknown"
        terminal = console.wait(handle)
        if not terminal["ok"]:
            raise BenchError(f"{label} rejected or failed: " + str(terminal.get("result", terminal.get("outcome"))))
        inspected = console.command("result", operation_id=handle.operation_id, timeout_s=timeout_s)
        if not inspected["ok"]:
            raise BenchError(f"retained {label} inspection failed")
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        cleanup_error = None
        if handle is None:
            handle = console.pending.get(request_id)
        if handle is not None and (handle.accepted or (handle.terminal is None and not console.synchronized)):
            cleanup = "unknown"
            if console.synchronized and handle.accepted:
                try:
                    stopped = console.command("stop", stop_policy=cleanup_stop, address=address, timeout_s=timeout_s)
                    if not stopped["ok"]:
                        raise BenchError("cleanup stop was rejected or failed")
                    final_state = observe_stopped(console, address=address, timeout_s=timeout_s)
                    final_health = console.command("health", timeout_s=timeout_s)
                    if not final_state["ok"] or not final_health["ok"]:
                        raise BenchError("cleanup state/health read failed")
                    cleanup = "drive_reported_standstill"
                    if handle.terminal is not None and not handle.released:
                        released = console.command("release", operation_id=handle.operation_id, timeout_s=timeout_s)
                        if not released["ok"]:
                            raise BenchError(f"local {label} result release failed")
                except BaseException as exc:
                    cleanup_error = str(exc) or "cleanup interrupted"
            else:
                cleanup_error = "framing unavailable; no cleanup command sent"
        if terminal is None and handle is not None:
            terminal = handle.terminal  # It may have arrived interleaved with cleanup.
        console.emit("summary", mode=command,
                     **({"homes_attempted": 1, "home_result": terminal} if command == "home" else
                        {"velocities_attempted": 1, "velocity_result": terminal} if command == "velocity" else
                        {"moves_attempted": 1, "move_result": terminal}),
                     retained_inspection=inspected, cleanup_policy=cleanup_stop, cleanup=cleanup,
                     stop_result=stopped, final_state=final_state, final_health=final_health,
                     physical_observation="not_supplied", ok=failure is None and cleanup_error is None,
                     error=failure, cleanup_error=cleanup_error)
        if failure is None and cleanup_error is not None:
            raise BenchError(cleanup_error)


def velocity_campaign(console: Console, *, velocity_args: tuple[str, ...], cleanup_stop: str,
                      timeout_s: float, address: int) -> None:
    """One finite velocity attempt; explicit stop cleanup on success or failure.

    Firmware owns the admitted duration; this host never sends a replay or a
    keepalive loop. Broken framing retains unknown stop and prevents more I/O.
    """
    parsed = velocity_arguments(velocity_args)
    if timeout_s * 1000000 <= parsed["duration_us"]:
        raise ValueError("velocity command timeout must exceed finite duration and leave stop time")
    move_campaign(console, command="velocity", move_args=None, velocity_args=velocity_args,
                  cleanup_stop=cleanup_stop, timeout_s=timeout_s, address=address)


def state_health_campaign(console: Console, *, count: int, interval_s: float, timeout_s: float, address: int) -> None:
    """Stationary non-consuming checks; no motor setting/action or hidden replay."""
    if type(count) is not int or not 1 <= count <= 1_000_000:
        raise ValueError("invalid campaign count")
    if not math.isfinite(interval_s) or not 0 <= interval_s <= 60:
        raise ValueError("interval must be finite and within 0..60 seconds")
    positive(timeout_s, "command timeout")
    if type(address) is not int or not 1 <= address <= 247:
        raise ValueError("ESS read address must be within 1..247")
    polling = console.command("monitor", timeout_s=timeout_s)
    if not polling["ok"] or polling["enabled"]:
        raise BenchError("disable observation polling explicitly before this stationary campaign")
    typed_read_campaign(console, kind="config", timeout_s=timeout_s, address=address)
    passed = 0
    failure = None
    try:
        for index in range(count):
            handle = console.begin("health-check", address=address, timeout_s=timeout_s)
            terminal = console.wait(handle)
            if not terminal["ok"]:
                raise BenchError("health check failed: " + str(terminal.get("outcome")))
            for control in ("result", "release"):
                remaining = handle.deadline - console.clock()
                positive(remaining, "remaining state-read " + control + " budget")
                result = console.command(control, operation_id=handle.operation_id, timeout_s=remaining)
                if not result["ok"]:
                    raise BenchError("state-read " + control + " was rejected")
            status = successful(console, "status", timeout_s)
            health = successful(console, "health", timeout_s)
            if "state_blocks" not in status or "state_blocks" not in health:
                raise BenchError("state observation cache is unavailable")
            for block_index, (previous, later) in enumerate(zip(status["state_blocks"], health["state_blocks"])):
                step = terminal["steps"][block_index]
                expected = terminal["state_blocks"][block_index]
                for cached in (previous, later):
                    if (not cached["valid"] or cached["value"] != expected
                            or any(cached[key] != terminal[key] for key in ("target", "address", "generation"))
                            or cached["operation_id"] != handle.operation_id
                            or cached["last_success_us"] != step["latest_us"]
                            or cached["observed_latest_us"] != step["latest_us"]
                            or cached["delivered_us"] != step["delivered_us"]
                            or not step["attempted_us"] <= cached["observed_earliest_us"] <= step["earliest_us"]):
                        raise BenchError("state observation cache does not match completed refresh")
                for key in ("value", "last_success_us", "observed_earliest_us", "observed_latest_us", "delivered_us"):
                    if previous[key] != later[key]:
                        raise BenchError("passive health query changed observation evidence")
                if later["age_us"] < previous["age_us"]:
                    raise BenchError("passive health query rejuvenated observation age")
            passed += 1
            if index + 1 < count:
                console.sleep(interval_s)
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        console.emit("summary", mode="state-health", checks_attempted=passed + (failure is not None),
                     checks_passed=passed, ok=failure is None and passed == count, error=failure)


def discovery_campaign(console, *, tokens: tuple[str, ...], timeout_s: float) -> dict:
    """One scan, finite local inspections, no retry or automatic recovery."""
    selected = discovery_arguments(tokens)
    if selected["action"] != "begin":
        raise ValueError("discovery campaign requires bounded scan candidates")
    limit = selected.get("overall-ms", 5000) / 1000 + timeout_s
    deadline = console.clock() + limit
    scans = 0
    result = console.command("discover", host_args=tokens, timeout_s=timeout_s)
    if not result["ok"]:
        raise BenchError("discovery admission refused: " + str(result.get("result")))
    scan = result["scan"]
    # Polls are local cached inspection, bounded independently of motor requests.
    # A runner/restore interlock needs the operator's explicit repair sequence.
    while scan["phase"] not in (4, 5):
        if console.clock() >= deadline or scans >= 1500:
            if console.synchronized:
                console.command("discover", host_args=("cancel",), timeout_s=timeout_s)
            raise BenchError("discovery inspection budget exhausted; cancellation requested without replay")
        console.sleep(.05)
        result = console.command("discover", host_args=("inspect",), timeout_s=min(timeout_s, max(.001, deadline-console.clock())))
        if not result["ok"]:
            raise BenchError("discovery inspection failed")
        scan = result["scan"]
        scans += 1
    console.emit("discovery_summary", scan=scan, inspections=scans, restored=scan["restored"],
                 interlocked=scan["phase"] == 4, ok=scan["phase"] == 5 and scan["outcome"] == 1)
    if scan["phase"] == 4:
        raise BenchError("discovery stopped with retained partial results and host/transport interlock; explicit repair required")
    if scan["outcome"] != 1:
        raise BenchError("discovery did not complete its requested candidates; partial findings retained: " + str(scan["outcome"]))
    finished = console.command("discover", host_args=("finish",), timeout_s=timeout_s)
    if not finished["ok"]:
        raise BenchError("discovery findings release refused")
    return scan


def open_port(name: str, baud: int, timeout_s: float):
    try:
        import serial
    except ImportError as exc:
        raise BenchError("pyserial is required: python -m pip install pyserial") from exc
    port = serial.Serial()
    port.port = name
    port.baudrate = baud
    port.timeout = 0
    port.write_timeout = min(timeout_s, 2.0)
    port.rtscts = False
    port.dsrdtr = False
    port.dtr = False
    port.rts = False
    port.open()
    return port


def arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="standalone console port, e.g. COM13")
    parser.add_argument("--baud", type=int, default=115200, help="USB console baud, not motor baud")
    parser.add_argument("--address", type=int, default=1, help="ESS probe address 1..247; no scan")
    parser.add_argument("--log", type=Path, required=True, help="new JSONL evidence file")
    parser.add_argument("--timeout", type=float, default=3.0, help="each command deadline in seconds")
    parser.add_argument("--startup", type=float, default=0.5, help="bounded boot-log collection in seconds")
    parser.add_argument("--debug", choices=("off", "raw", "decoded"), default=None,
                        help="observe the ordinary campaign on this connection, then restore the previous display mode")
    sub = parser.add_subparsers(dest="mode", required=True)
    sub.add_parser("profile-list", help="compiled manufacturer/profile inventory; no motor traffic")
    discovery_help = ("[profile ess_rs | manufacturer stepperonline] [addresses FIRST LAST] "
                      "[tuple BAUD FORMAT] (up to four distinct reviewed tuples) [query-ms 1..5000] "
                      "[overall-ms 1..60000] [requests 1..256] [results 1..8] [identity]. "
                      "Defaults: selected endpoint/current tuple, 500 ms/query, 5000 ms overall, "
                      "16 requests, 8 results, no identity refinement or retries. Console input is "
                      "128 bytes/20 tokens including correlation and command. "
                      "Controls: inspect, cancel, restore, finish. Interlocks require explicit repair.")
    discover = sub.add_parser("discover", help="one bounded scan action; inspect/cancel/restore/finish retain evidence", description=discovery_help)
    discover.add_argument("discovery_tokens", nargs="*")
    discovery_check = sub.add_parser("discovery-check", help="one finite ESS scan, bounded inspection and explicit result release", description=discovery_help)
    discovery_check.add_argument("discovery_tokens", nargs="*")
    sub.add_parser("probe", help="one model-register read and cached observations")
    host = sub.add_parser("host", help="one host-only serial query or explicit tuple change; no motor writes")
    host.add_argument("host_tokens", nargs="*")
    communication = sub.add_parser("communication", help="one explicit communication session action; no retries/save/restart")
    communication.add_argument("communication_tokens", nargs="*")
    persistence = sub.add_parser("persistence", help="one explicit save/restore session action; no retries or restart")
    persistence.add_argument("persistence_tokens", nargs="*")
    persistence_check = sub.add_parser("persistence-check", help="finite plan, optional one persistence write and explicit read verification")
    persistence_check.add_argument("persistence_kind", choices=("save","factory-restore"))
    persistence_check.add_argument("--execute", action="store_true")
    persistence_check.add_argument("--verify", action="store_true")
    persistence_check.add_argument("--finish", action="store_true")
    debug = sub.add_parser("debug", help="query or select non-consuming traffic display")
    debug.add_argument("debug_mode", nargs="?", choices=("off", "raw", "decoded"))
    motion_profile = sub.add_parser("motion-profile", help="read position parameters, inspect or restore the original snapshot")
    motion_profile.add_argument("motion_profile_action", choices=("read", "inspect", "restore"))
    commissioning = sub.add_parser("communication-check", help="finite preview, optional one write and explicitly selected confirmation")
    commissioning.add_argument("field", choices=("address", "baud", "format"))
    commissioning.add_argument("value")
    commissioning.add_argument("--execute", action="store_true", help="attempt the previewed write when actual prerequisites and route exist")
    commissioning.add_argument("--confirm", choices=("before", "requested"), help="explicit host selection and one read after successful write")
    commissioning.add_argument("--finish", action="store_true", help="release commissioning ownership only after successful chosen confirmation")
    host_check = sub.add_parser("host-check", help="finite deliberate host mismatch/read/recover/restore; no read replay")
    host_check.add_argument("--baud", dest="host_baud", type=int, required=True, choices=HOST_BAUDS)
    host_check.add_argument("--fmt", required=True, choices=HOST_FORMATS)
    sub.add_parser("capture-read", help="one fixed 0x0130/16-word timing-fixture read")
    for name in ACTION_COMMANDS:
        action = sub.add_parser(name, help="one explicit action attempt; never retried")
        if name == "stop":
            action.add_argument("stop_policy", choices=("normal", "direct"))
    typed = sub.add_parser("typed-read", help="typed identity/configuration/state reads, retained inspection and release")
    typed.add_argument("--kind", choices=("identity", "config", "state", "both"), default="both")
    for name in MOVE_COMMANDS:
        move = sub.add_parser(name, help="one finite move with explicit stop cleanup; never replayed")
        move.add_argument("value", help="exact integer/decimal/fraction coordinate")
        move.add_argument("unit", choices=("turn", "deg", "rad") if name == "move-angle" else ("steps", "fullsteps", "counts", "turn", "deg", "rad", "mm"))
        move.add_argument("frame", choices=("motor", "load") if name == "move-angle" else ("native", "motor", "load"))
        if name == "move-angle":
            move.add_argument("path", choices=("positive", "negative", "shortest"))
            move.add_argument("tie", choices=("reject", "positive", "negative"))
        move.add_argument("native_rpm", help="positive exact integer native motor rpm")
        move.add_argument("ramp", choices=("configured",))
        move.add_argument("--round", choices=("exact", "nearest", "zero", "floor", "ceil"))
        move.add_argument("--maximum-error", default="0", help="exact maximum native quantization error")
        move.add_argument("--approximation-error", help="explicit positive native error allowance for radians")
        if name == "move-relative": move.add_argument("--basis", choices=("actual", "commanded", "queued"))
        move.add_argument("--cleanup-stop", required=True, choices=("normal", "direct"))
    home = sub.add_parser("home", help="one current-position reference attempt with explicit stop cleanup")
    home.add_argument("method", choices=("35",))
    home.add_argument("search_native")
    home.add_argument("return_native")
    home.add_argument("ramp_native")
    home.add_argument("offset", choices=("zero",))
    home.add_argument("--cleanup-stop", required=True, choices=("normal", "direct"))
    velocity = sub.add_parser("velocity", help="one finite signed velocity with explicit stop cleanup; never replayed")
    sub.add_parser("driver-read", help="one checked four-window drive-settings read with retained inspection; no writes")
    segment = sub.add_parser("segment", help="indexed stored record read/update; no external execution")
    segment.add_argument("segment_tokens", nargs="+")
    tuning = sub.add_parser("tuning", help="one named native tuning read or exact qualified setting attempt; no gain sweep or replay")
    tuning.add_argument("tuning_tokens", nargs="+", help="filters|current-loop|la|collision read|set FIELD INTEGER ...")
    control = sub.add_parser("control", help="one explicit stopped native control read or settings attempt; never replayed")
    control.add_argument("control_tokens", nargs="+", help="read | set FIELD INTEGER ...; algorithm open-loop|algorithm-1")
    io = sub.add_parser("io", help="one explicit typed IO read or settings attempt with retained inspection; never replayed")
    io.add_argument("io_tokens", nargs="+", help="read | set FIELD VALUE ...; none assigns documented function zero")
    velocity.add_argument("value", help="exact integer/decimal/fraction velocity")
    velocity.add_argument("unit", choices=("rpm", "steps/s", "fullsteps/s", "counts/s", "turn/s", "turns/s", "deg/s", "rad/s", "mm/s"))
    velocity.add_argument("frame", choices=("native", "motor", "load"))
    velocity.add_argument("duration_ms", type=int)
    velocity.add_argument("ramp", choices=("configured",))
    velocity.add_argument("stop", choices=("normal", "direct"))
    velocity.add_argument("--round", choices=("exact", "nearest", "zero", "floor", "ceil"))
    velocity.add_argument("--maximum-error", default="0")
    velocity.add_argument("--approximation-error")
    velocity.add_argument("--cleanup-stop", required=True, choices=("normal", "direct"))
    for mode, default_count, default_interval, description in (
        ("stress", 100, 0.1, "explicit repeated probes"),
        ("state-health", 5, 0.1, "stationary state/health refresh, retained checks and passive cache age"),
        ("watch", 60, 1.0, "cached status/health/memory only; no motor traffic"),
        ("load", 100, 0.1, "read-only probes with explicit competing host workload"),
    ):
        child = sub.add_parser(mode, help=description)
        child.add_argument("--count", type=int, default=default_count)
        child.add_argument("--interval", type=float, default=default_interval)
        if mode == "load":
            child.add_argument("--capture-read", action="store_true",
                               help="use fixed 37-byte read replies instead of model probes")
            child.add_argument("--work-us", type=int, default=0,
                               help="competing task work per 10 ms period, 0..5000 us")
            child.add_argument("--owner-delay-us", type=int, default=0,
                               help="active transaction service delay, 0..20000 us")
            child.add_argument("--console-bytes", type=int, default=0,
                               help="competing console payload per 10 ms, 0..256 bytes")
    result = parser.parse_args(argv)
    if result.mode == "debug" and result.debug is not None:
        parser.error("use either the debug command or --debug with an ordinary campaign")
    if not 1 <= result.baud <= 4_000_000:
        parser.error("baud must be within 1..4000000")
    if not 1 <= result.address <= 247:
        parser.error("address must be within 1..247")
    if not math.isfinite(result.timeout) or not 0.1 <= result.timeout <= 60:
        parser.error("timeout must be finite and within 0.1..60 seconds")
    if not math.isfinite(result.startup) or not 0.01 <= result.startup <= 60:
        parser.error("startup must be finite and within 0.01..60 seconds")
    result.count = getattr(result, "count", 1)
    result.interval = getattr(result, "interval", 0.0)
    if not 1 <= result.count <= 1_000_000:
        parser.error("count must be within 1..1000000")
    if not math.isfinite(result.interval) or not 0 <= result.interval <= 60:
        parser.error("interval must be finite and within 0..60 seconds")
    result.move_args = None
    result.host_args = None
    if result.mode == "debug":
        result.host_args = (result.debug_mode,) if result.debug_mode else ()
    if result.mode == "motion-profile":
        result.host_args = (result.motion_profile_action,)
    if result.mode == "communication":
        result.host_args = tuple(result.communication_tokens)
        try: communication_arguments(result.host_args)
        except ValueError as exc: parser.error(str(exc))
    if result.mode in ("discover", "discovery-check"):
        result.host_args=tuple(result.discovery_tokens)
        try:
            selected=discovery_arguments(result.host_args)
            if result.mode == "discovery-check" and selected["action"] != "begin":
                raise ValueError("discovery-check starts one scan; controls use discover")
        except ValueError as exc:parser.error(str(exc))
    if result.mode == "persistence":
        result.host_args=tuple(result.persistence_tokens)
        try:persistence_arguments(result.host_args)
        except ValueError as exc:parser.error(str(exc))
    if result.mode == "persistence-check":
        if (result.verify or result.finish) and not result.execute or result.finish and not result.verify:
            parser.error("--verify requires --execute; --finish requires --verify")
    if result.mode == "communication-check":
        try: communication_arguments(("plan", result.field, result.value, str(result.address)))
        except ValueError as exc: parser.error(str(exc))
        if (result.confirm or result.finish) and not result.execute:
            parser.error("--confirm/--finish require --execute")
        if result.finish and not result.confirm:
            parser.error("--finish requires --confirm")
    if result.mode == "host":
        result.host_args = tuple(result.host_tokens)
        try: host_arguments(result.host_args)
        except ValueError as exc: parser.error(str(exc))
    if result.mode in MOVE_COMMANDS:
        tokens = [result.value, result.unit, result.frame]
        if result.mode == "move-angle": tokens.extend((result.path, result.tie))
        tokens.extend((result.native_rpm, result.ramp))
        if getattr(result, "basis", None): tokens.extend(("basis", result.basis))
        if result.round: tokens.extend(("round", result.round, result.maximum_error))
        elif result.maximum_error != "0" or result.approximation_error:
            parser.error("numerical allowances require an explicit --round policy")
        if result.approximation_error: tokens.extend(("approx", result.approximation_error))
        result.move_args = tuple(tokens)
        try: move_arguments(result.mode[5:], result.move_args)
        except ValueError as exc: parser.error(str(exc))
    result.home_args = None
    if result.mode == "home":
        result.home_args = (result.method, result.search_native, result.return_native, result.ramp_native, result.offset)
        try: home_arguments(result.home_args)
        except ValueError as exc: parser.error(str(exc))
    result.velocity_args = None
    if result.mode == "velocity":
        tokens = [result.value, result.unit, result.frame, str(result.duration_ms), result.ramp, result.stop]
        if result.round: tokens.extend(("round", result.round, result.maximum_error))
        elif result.maximum_error != "0" or result.approximation_error:
            parser.error("numerical allowances require explicit --round policy")
        if result.approximation_error: tokens.extend(("approx", result.approximation_error))
        result.velocity_args = tuple(tokens)
        try:
            parsed = velocity_arguments(result.velocity_args)
            if result.timeout * 1000000 <= parsed["duration_us"]: raise ValueError("timeout must exceed velocity duration and leave stop time")
        except ValueError as exc: parser.error(str(exc))
    result.driver_args = None
    if result.mode == "segment":
        result.driver_args = tuple(result.segment_tokens)
        try: segment_arguments(result.driver_args)
        except ValueError as exc: parser.error(str(exc))
    if result.mode == "tuning":
        result.driver_args = tuple(result.tuning_tokens)
        try: tuning_arguments(result.driver_args)
        except ValueError as exc: parser.error(str(exc))
    if result.mode == "control":
        result.driver_args = tuple(result.control_tokens)
        try: driver_arguments(result.driver_args, "control_settings")
        except ValueError as exc: parser.error(str(exc))
    if result.mode == "io":
        result.driver_args = tuple(result.io_tokens)
        try: driver_arguments(result.driver_args, "io")
        except ValueError as exc: parser.error(str(exc))
    result.load = None
    if result.mode == "load":
        result.load = (result.work_us, result.owner_delay_us, result.console_bytes)
        try:
            check_load(result.load)
        except ValueError as exc:
            parser.error(str(exc))
    return result


def main(argv: list[str] | None = None) -> int:
    args = arguments(argv)
    port = None
    try:
        with args.log.open("x", encoding="utf-8", newline="\n") as stream:
            evidence = Evidence(stream)
            evidence("session", port=args.port, console_baud=args.baud, mode=args.mode,
                     address=args.address, requested_count=args.count, timeout_s=args.timeout,
                     load_settings=args.load)
            try:
                port = open_port(args.port, args.baud, args.timeout)
                console = Console(port, on_event=evidence)
                console.drain_startup(args.startup)
                console.identify(timeout_s=args.timeout)
                with debug_session(console, args.debug, args.timeout):
                    if args.mode == "debug":
                        result = console.command("debug", host_args=args.host_args, timeout_s=args.timeout)
                        if not result["ok"]:
                            raise BenchError("debug command failed")
                    elif args.mode == "motion-profile":
                        result = console.command("motion-profile", host_args=args.host_args, timeout_s=args.timeout)
                        if not result["ok"]:
                            raise BenchError("motion-profile command failed: " + str(result.get("result")))
                    elif args.mode in ("discover", "profile-list"):
                        result = console.command(args.mode, host_args=args.host_args if args.mode=="discover" else None, timeout_s=args.timeout)
                        if not result["ok"]:
                            raise BenchError("discovery command refused: " + str(result.get("result")))
                    elif args.mode == "discovery-check":
                        discovery_campaign(console,tokens=args.host_args,timeout_s=args.timeout)
                    elif args.mode == "communication":
                        result = console.command("communication", host_args=args.host_args, timeout_s=args.timeout)
                        if not result["ok"]:
                            raise BenchError("communication command failed: " + str(result.get("result")))
                    elif args.mode == "persistence":
                        result=console.command("persistence",host_args=args.host_args,timeout_s=args.timeout)
                        if not result["ok"]:raise BenchError("persistence command refused")
                    elif args.mode == "persistence-check":
                        persistence_campaign(console,kind=args.persistence_kind,timeout_s=args.timeout,
                            execute=args.execute,verify=args.verify,finish=args.finish)
                    elif args.mode == "communication-check":
                        communication_campaign(console, field=args.field, value=args.value, address=args.address,
                                               timeout_s=args.timeout, execute=args.execute, confirm=args.confirm, finish=args.finish)
                    elif args.mode == "host":
                        result = console.command("host", host_args=args.host_args, timeout_s=args.timeout)
                        if not result["ok"]:
                            raise BenchError("host command failed: " + str(result.get("result")))
                    elif args.mode == "host-check":
                        host_check_campaign(console, baud=args.host_baud, fmt=args.fmt,
                                            timeout_s=args.timeout, address=args.address)
                    elif args.mode in ACTION_COMMANDS:
                        result = console.command(args.mode, timeout_s=args.timeout, address=args.address,
                                                 stop_policy=getattr(args, "stop_policy", None))
                        if not result["ok"]:
                            raise BenchError("action rejected or failed: " + str(result.get("result", result.get("outcome"))))
                    elif args.mode == "home":
                        move_campaign(console, command="home", move_args=None, home_args=args.home_args,
                                      cleanup_stop=args.cleanup_stop, timeout_s=args.timeout, address=args.address)
                    elif args.mode == "driver-read":
                        driver_read_campaign(console, timeout_s=args.timeout, address=args.address)
                    elif args.mode in ("io", "segment", "control", "tuning"):
                        driver_read_campaign(console, timeout_s=args.timeout, address=args.address, command=args.mode, driver_args=args.driver_args)
                    elif args.mode == "velocity":
                        velocity_campaign(console, velocity_args=args.velocity_args, cleanup_stop=args.cleanup_stop,
                                          timeout_s=args.timeout, address=args.address)
                    elif args.mode in MOVE_COMMANDS:
                        move_campaign(console, command=args.mode, move_args=args.move_args,
                                      cleanup_stop=args.cleanup_stop, timeout_s=args.timeout, address=args.address)
                    else:
                        campaign(console, args.mode, count=args.count, interval_s=args.interval,
                                 timeout_s=args.timeout, address=args.address, load=args.load,
                                 typed_kind=getattr(args, "kind", "both"),
                                 read_command="capture-read" if getattr(args, "capture_read", False) else "probe")
            except (Exception, KeyboardInterrupt) as exc:
                evidence("failure", error=str(exc) or "interrupted", ok=False)
                raise
            finally:
                if port is not None:
                    port.close()
    except KeyboardInterrupt:
        print("Interrupted; no recovery or command replay was attempted.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"Bench run stopped: {exc}", file=sys.stderr)
        return 1
    print(f"Completed {args.mode}: {args.count} iteration(s). Evidence: {args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

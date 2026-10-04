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
from datetime import datetime, timezone
import json
import math
from fractions import Fraction
from pathlib import Path
import re
import sys
import time


MAX_LINE = 4608
MAX_INPUT = 32768
COMMANDS = frozenset({"version", "config", "probe", "capture-read", "status", "health", "memory", "stats", "load",
                      "drv", "result", "release", "cancel", "recover", "reset", "caps",
                      "read-identity", "read-config", "read-state", "health-check", "monitor", "axis", "prepare", "enable", "motor-release", "alarm-clear", "stop", "position-clear", "move-relative", "move-absolute", "move-angle", "velocity", "driver", "home"})
MAX_COMMANDS = 11  # Eight ordinary operations, recovery, reserved stop and local query.
MAX_OPERATIONS = 10  # Eight ordinary operations, one recovery and one reserved stop.
MAX_PROBES = 8
TYPED_READS = {"read-identity": "identity", "read-config": "config", "read-state": "state"}
READ_COMMANDS = ("probe", "capture-read", *TYPED_READS)
ACTION_COMMANDS = ("enable", "motor-release", "alarm-clear", "stop", "position-clear")
MOVE_COMMANDS = ("move-relative", "move-absolute", "move-angle")
ACTION_KINDS = {"enable": "enable", "motor-release": "release", "alarm-clear": "clear_alarm", "stop": "stop", "position-clear": "clear_position"}
DRIVER_FIELDS = ("direction", "subdivision", "word-order", "soft-limit", "over-limit", "interruption", "position-mode", "positive-limit", "negative-limit")
DRIVER_REGISTERS = (0x10, 0x11, 0x19, 0x18, 0x17, 0x51, 0x50)
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


def driver_arguments(arguments: tuple[str, ...]) -> dict:
    """Strict typed profile grammar; value semantics remain in the public API."""
    if (not isinstance(arguments, tuple) or not 1 <= len(arguments) <= 19 or
            any(type(token) is not str or not token or any(ord(c) < 33 or ord(c) > 126 for c in token) for token in arguments)):
        raise ValueError("driver requires bounded ASCII tokens")
    if arguments == ("read",):
        return {}
    if arguments[0] != "set" or len(arguments) < 3 or len(arguments) % 2 != 1:
        raise ValueError("driver requires read or complete set field/integer pairs")
    result = {}
    for index in range(1, len(arguments), 2):
        field, value = arguments[index:index + 2]
        if field not in DRIVER_FIELDS or field in result or not re.fullmatch(r"-?[0-9]+", value):
            raise ValueError("driver field is unknown, repeated or not an integer")
        number = int(value)
        if not (-2**63 <= number < 2**63 if field in ("positive-limit", "negative-limit") else 0 <= number <= 65535):
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


def wire_crc(data: bytes) -> int:
    """Check retained read-only fixture bytes independently of firmware parsing."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


class Evidence:
    """Stream evidence to an exclusive new file; do not retain a growing list."""

    def __init__(self, stream, clock: Callable[[], float] = time.monotonic):
        self.stream = stream
        self.clock = clock
        self.started = clock()

    def __call__(self, event: str, **fields) -> None:
        record = {
            "event": event,
            "utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "elapsed_s": round(self.clock() - self.started, 6),
            **fields,
        }
        self.stream.write(json.dumps(record, ensure_ascii=True, allow_nan=False) + "\n")
        self.stream.flush()


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
        self.released = False
        self.stop_policy = None
        self.move_args = None
        self.velocity_args = None
        self.driver_args = None
        self.home_args = None


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
                        raise BenchError("unexpected structured reply during startup")
                if not data:
                    self.sleep(0.005)
            if self.buffer and not self._load_line_pending():
                raise BenchError("startup ended with an incomplete line")
        except Exception:
            self.synchronized = False
            raise

    def _load_line_pending(self) -> bool:
        """Only fixture text may straddle a completed JSON reply."""
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
        Console._check_read_evidence(item, address, 7)

    @staticmethod
    def _check_read_evidence(item: dict, address: int | None, reply_size: int) -> None:
        actual = item.get("address")
        if (type(actual) is not int or not 1 <= actual <= 247
                or (address is not None and actual != address)):
            raise BenchError("probe terminal address does not match acceptance")
        if not item["ok"]:
            return
        if (item.get("transport") != "FRAME" or item.get("codec") != "OK"
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
        require(integer(item.get("effective_native"), -2**31, 2**31 - 1) and
                integer(item.get("displacement_native"), -2**63, 2**63 - 1) and item["displacement_native"] != 0 and
                (kind != "relative" or item["displacement_native"] == item["effective_native"]) and item["zero_displacement"] is False and
                integer(item.get("endpoint_native"), -2**63, 2**63 - 1), "effective target is invalid")
        require(integer(item.get("native_rpm"), 1, 3000) and item.get("ramp") == "configured", "speed or ramp is invalid")
        words = item.get("staging_words")
        require(isinstance(words, list) and len(words) == 5 and all(integer(word, 0, 65535) for word in words) and
                words[0] <= 2000 and words[1] <= 2000 and words[2] == item["native_rpm"], "staged parameter words are inconsistent")
        encoded = item["effective_native"] & 0xFFFFFFFF
        require(words[3:] in ([encoded >> 16, encoded & 65535], [encoded & 65535, encoded >> 16]), "staged target differs from effective target")
        prerequisites = item.get("prerequisites")
        require(isinstance(prerequisites, dict) and all(prerequisites.get(key) == item[key] for key in
                ("target", "generation", "configuration_generation")), "qualification binding differs")
        require(all(prerequisites.get(key) is True for key in ("command_units_verified", "configured_ramp_verified",
                "serial_inputs_permit", "readiness_qualified", "word_order_known", "start_speed_known")) and
                type(prerequisites.get("negative_encoding_verified")) is bool and
                (item["effective_native"] >= 0 or prerequisites["negative_encoding_verified"]) and
                (kind != "relative" or prerequisites.get("relative_basis_verified") is True), "qualification is missing")
        require(integer(prerequisites.get("observed_us")) and integer(prerequisites.get("maximum_age_us"), 1) and
                prerequisites["observed_us"] <= item["started_us"] and
                item["started_us"] - prerequisites["observed_us"] < prerequisites["maximum_age_us"] and
                prerequisites.get("raw_alarm") == 0 and integer(prerequisites.get("raw_motion"), 0, 65535) and
                not prerequisites["raw_motion"] & 0x7C and integer(prerequisites.get("word_order"), 0, 1) and
                integer(prerequisites.get("start_speed"), 0, item["native_rpm"]), "readiness or configured speed evidence is inconsistent")
        write_deadline = min(item["deadline_us"], 0xFFFFFFFFFFFFFFFF,
                             prerequisites["observed_us"] + prerequisites["maximum_age_us"])
        reference = item.get("reference")
        require(isinstance(reference, dict) and type(reference.get("native_known")) is bool and
                all(integer(reference.get(key), 0, 0xFFFFFFFF) for key in ("target", "generation", "configuration_generation")) and
                integer(reference.get("native_position"), -2**63, 2**63 - 1) and integer(reference.get("basis"), 0, 2) and
                integer(reference.get("source"), 0, 4) and integer(reference.get("observed_us")) and integer(reference.get("maximum_age_us")),
                "reference provenance is invalid")
        if kind != "relative" or reference["native_known"]:
            require(reference["native_known"] and all(reference[key] == item[key] for key in ("target", "generation", "configuration_generation")) and
                    reference["basis"] == 0 and reference["source"] != 0 and reference["maximum_age_us"] > 0 and
                    reference["observed_us"] <= item["started_us"] < min(0xFFFFFFFFFFFFFFFF,
                        reference["observed_us"] + reference["maximum_age_us"]), "reference is stale or mismatched")
            require(item["endpoint_known"] and item["endpoint_native"] - reference["native_position"] == item["displacement_native"] and
                    (kind == "relative" or item["effective_native"] == item["endpoint_native"]), "reference endpoint differs")
            write_deadline = min(write_deadline, 0xFFFFFFFFFFFFFFFF, reference["observed_us"] + reference["maximum_age_us"])
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
            require(item["execution"] == "acknowledged" and item["polls"] >= 1 and observed["step"] == item["polls"] + 1 and
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
                require(item["execution"] == "acknowledged" and failure["step"] in (item["polls"] + 1, item["polls"] + 2),
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
            elif not failure["response_confirmed"]:
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
    def _check_driver(item: dict, address: int | None, arguments: tuple[str, ...] | None) -> None:
        """Retain exact partial progress; acknowledgement is not activation."""
        def require(condition, message):
            if not condition: raise BenchError("driver " + message)

        def integer(value, maximum=2**64 - 1):
            return type(value) is int and 0 <= value <= maximum

        require(item.get("driver") is True and item.get("driver_kind") in ("read", "update") and
                item.get("velocity", False) is False and all(item.get(k) is None for k in ("read_kind", "move_kind", "action_kind")) and
                item.get("recovery", False) is False and item.get("capture_read", False) is False, "kind differs")
        require(integer(item.get("address"), 247) and item["address"] >= 1 and (address is None or item["address"] == address), "target differs")
        require(all(Console._operation_id(item.get(k)) for k in ("target", "generation", "configuration_generation")), "generation is invalid")
        require(all(integer(item.get(k)) for k in ("started_us", "deadline_us", "stationary_valid_until_us", "serviced_us", "completed_steps", "fields", "effects")) and
                item["started_us"] < item["deadline_us"] and item["serviced_us"] >= item["started_us"] and
                item["started_us"] < item["stationary_valid_until_us"] <= item["deadline_us"] and
                item["completed_steps"] <= 14 and item["fields"] <= 127 and item["effects"] & ~item["fields"] == 0, "budget or field mask differs")
        require(type(item.get("uncertain")) is bool and item.get("atomic") is False and item.get("active_settings_known") is False, "activation/rollback claim is invalid")
        require(item.get("state") == ("succeeded" if item["ok"] else "failed") and
                item.get("outcome") in ({"success"} if item["ok"] else {"reply_error", "transport_error", "cancelled", "deadline", "timing_unqualified", "unconfirmed_response", "readback_mismatch"}) and
                item.get("status") in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"} and
                (item["ok"] == (item["status"] == "OK")) and type(item.get("detail")) is int, "terminal result differs")
        parsed = None if arguments is None else driver_arguments(arguments)
        if parsed is not None:
            mask = sum(1 << DRIVER_FIELDS.index(k) for k in parsed)
            require(item["fields"] == mask and item["driver_kind"] == ("update" if parsed else "read"), "candidate differs from command")
        require(item.get("progress_columns") == DRIVER_PROGRESS_COLUMNS and item.get("evidence_columns") == DRIVER_EVIDENCE_COLUMNS, "evidence schema differs")
        progress, evidence = item.get("progress"), item.get("evidence")
        require(isinstance(progress, list) and len(progress) <= 7 and isinstance(evidence, list) and len(evidence) <= 14, "storage bounds exceeded")
        fields = {}
        for row in progress:
            require(isinstance(row, list) and len(row) == len(DRIVER_PROGRESS_COLUMNS), "progress row is invalid")
            p = dict(zip(DRIVER_PROGRESS_COLUMNS, row)); field = p["field"]
            require(integer(field, 64) and field and field & (field - 1) == 0 and field not in fields and item["fields"] & field and
                    p["register"] == DRIVER_REGISTERS[field.bit_length() - 1], "progress field differs")
            require(all(integer(p[k], 65535) for k in ("previous", "requested", "readback", "active")) and
                    all(type(p[k]) is bool for k in ("acknowledged", "readback_known", "active_known")) and
                    p["active_known"] is False and p["execution"] in ("not_transmitted", "acknowledged", "rejected", "unknown"), "progress values are invalid")
            require(400 <= p["requested"] <= 51200 if field == 2 else p["requested"] <= 1, "candidate is outside reviewed range")
            if parsed is not None: require(p["requested"] == parsed[DRIVER_FIELDS[field.bit_length() - 1]], "requested value changed")
            fields[field] = p
        require(sum(fields) == item["fields"], "selected field progress is missing")
        steps = []
        for index, row in enumerate(evidence):
            require(isinstance(row, list) and len(row) == len(DRIVER_EVIDENCE_COLUMNS), "evidence row is invalid")
            e = dict(zip(DRIVER_EVIDENCE_COLUMNS, row))
            require(e["step"] == index and all(integer(e[k], limit) for k, limit in (("register", 65535), ("count", 4), ("event", 3), ("received_length", 2**32 - 1), ("tx_accepted", 8), ("frame_error", 255))) and e["count"] >= 1 and
                    all(type(e[k]) is bool for k in ("write", "tx_complete", "response_confirmed", "qualified", "execution_unknown")) and
                    all(integer(e[k]) for k in ("earliest_us", "latest_us", "delivered_us", "attempted_us")) and
                    all(type(e[k]) is int and -2**31 <= e[k] < 2**31 for k in ("detail", "transport_detail")) and
                    e["status"] in {"OK", "INVALID_CONFIG", "ILLEGAL_VALUE", "UNSUPPORTED", "CRC_ERROR", "FRAME_ERROR", "EXCEPTION"}, "evidence values are invalid")
            require(isinstance(e["raw_hex"], str) and re.fullmatch(r"(?:[0-9a-fA-F]{2}){0,13}", e["raw_hex"]) is not None, "raw frame is malformed")
            raw = bytes.fromhex(e["raw_hex"])
            require(len(raw) == min(e["received_length"], 13) and item["started_us"] <= e["attempted_us"] <= e["delivered_us"] <= item["serviced_us"] and
                    (not e["tx_complete"] or e["tx_accepted"] == 8) and (e["event"] == 0 or not e["response_confirmed"]), "transport evidence differs")
            require((e["qualified"] and e["event"] == 0 and e["attempted_us"] <= e["earliest_us"] <= e["latest_us"] <= e["delivered_us"]) or
                    (not e["qualified"] and e["earliest_us"] == e["latest_us"] == 0), "closure bounds differ")
            if item["driver_kind"] == "read":
                require(index < 4 and not e["write"] and (e["register"], e["count"]) == ((0x10, 2), (0x17, 3), (0x37, 4), (0x50, 2))[index], "read window differs")
            else:
                selected = list(fields.values())
                if 8 in fields and fields[8]["requested"] == 0:
                    selected = [fields[8]] + [p for p in selected if p["field"] != 8]
                require(index // 2 < len(selected) and e["register"] == selected[index // 2]["register"] and e["count"] == 1 and
                        e["write"] == (index % 2 == 0), "update sequence differs")
            if e["event"] == 0 and e["status"] == "OK":
                require(e["tx_complete"] and e["detail"] == e["frame_error"] == 0 and wire_crc(raw) == 0 and raw[0] == item["address"], "checked frame differs")
                if e["write"]:
                    p = next(p for p in fields.values() if p["register"] == e["register"])
                    require(len(raw) == 8 and raw[1] == 6 and int.from_bytes(raw[2:4], "big") == e["register"] and
                            int.from_bytes(raw[4:6], "big") == p["requested"], "write echo differs")
                else:
                    require(len(raw) == 5 + 2 * e["count"] and raw[1:3] == bytes((3, 2 * e["count"])), "read payload differs")
            if e["status"] == "EXCEPTION":
                require(e["event"] == 0 and len(raw) == 5 and raw[:2] == bytes((item["address"], 0x86 if e["write"] else 0x83)) and
                        wire_crc(raw) == 0 and e["detail"] == raw[2], "exception differs")
            steps.append((e, raw))
        effects = 0
        uncertain = False
        for p in fields.values():
            rows = [(e, raw) for e, raw in steps if e["register"] == p["register"]]
            acknowledged = any(e["write"] and e["event"] == 0 and e["status"] == "OK" and e["qualified"] and e["response_confirmed"] for e, raw in rows)
            readbacks = [(e, raw) for e, raw in rows if not e["write"] and e["event"] == 0 and e["status"] == "OK" and e["qualified"] and e["response_confirmed"] and e["latest_us"] <= item["deadline_us"]]
            require(p["acknowledged"] == acknowledged and p["readback_known"] == bool(readbacks), "acknowledgement/readback provenance differs")
            if readbacks: require(p["readback"] == int.from_bytes(readbacks[-1][1][3:5], "big"), "readback value differs")
            writes = [e for e, raw in rows if e["write"]]
            execution = "not_transmitted"
            for e in writes:
                if e["tx_accepted"] or e["execution_unknown"]:
                    effects |= p["field"]; execution = "unknown"
                if e["event"] == 0 and e["qualified"] and e["response_confirmed"]:
                    if e["status"] == "OK": execution = "acknowledged"
                    elif e["status"] == "EXCEPTION" and 1 <= e["detail"] <= 7: execution = "rejected"
            require(p["execution"] == execution, "execution differs from write evidence")
            uncertain |= execution == "unknown" or (execution == "acknowledged" and (not p["readback_known"] or p["readback"] != p["requested"]))
        require(item["effects"] == effects and item["uncertain"] == uncertain, "changed-setting mask or uncertainty differs")
        total = 4 if item["driver_kind"] == "read" else 2 * len(fields)
        successful_steps = 0
        for e, raw in steps:
            deadline = item["stationary_valid_until_us"] if e["write"] else item["deadline_us"]
            good = e["event"] == 0 and e["qualified"] and e["response_confirmed"] and e["latest_us"] <= deadline and e["status"] == "OK"
            if good and item["driver_kind"] == "update" and not e["write"]:
                p = next(p for p in fields.values() if p["register"] == e["register"])
                good = int.from_bytes(raw[3:5], "big") == p["requested"]
            successful_steps += good
        require(item["completed_steps"] == successful_steps and len(steps) <= total and
                (item["ok"] or item["completed_steps"] < total), "completed progress contradicts terminal state")
        if item["ok"]:
            require(not item["uncertain"] and item["detail"] == 0 and item["completed_steps"] == len(steps) == (4 if item["driver_kind"] == "read" else 2 * len(fields)) and
                    all(e["qualified"] and e["response_confirmed"] and e["status"] == "OK" and e["latest_us"] <= item["deadline_us"] for e, raw in steps), "success lacks complete checked evidence")
            require(all(p["acknowledged"] and p["readback_known"] and p["requested"] == p["readback"] and p["execution"] == "acknowledged" for p in fields.values()), "success lacks matching readback")
        if item["driver_kind"] == "read" and item["ok"]:
            v = item.get("observation")
            require(isinstance(v, dict) and v.get("signed_limits") == "unresolved" and v.get("native_scale") == "unresolved", "limit resolution was fabricated")
            words = [[int.from_bytes(raw[i:i+2], "big") for i in range(3, len(raw) - 2, 2)] for e, raw in steps]
            values = [words[0][0], words[0][1], words[1][2], words[1][1], words[1][0], words[3][1], words[3][0]]
            require(v.get("raw") == values and v.get("positive_words") == words[2][:2] and v.get("negative_words") == words[2][2:], "observation differs from raw replies")
            known = sum(1 << i for i, value in enumerate(values) if (400 <= value <= 51200 if i == 1 else value <= 1))
            require(v.get("known_fields") == known, "unknown enums were normalized")
            require(v.get("pair_known") == (values[2] in (0, 1)), "pair word order differs")
            if v["pair_known"]:
                pair = lambda w: (w[0] << 16 | w[1]) if values[2] == 0 else (w[1] << 16 | w[0])
                require(v.get("positive_bits") == pair(words[2][:2]) and v.get("negative_bits") == pair(words[2][2:]), "paired bits differ")
            else: require(v.get("positive_bits") == v.get("negative_bits") == 0, "unknown word order was decoded")
        else: require(item.get("observation") is None, "partial update publishes a whole observation")
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
        require(isinstance(p, dict) and uint(p.get("observed_us")) and uint(p.get("maximum_age_us")) and p["maximum_age_us"] > 0 and
                p["observed_us"] <= item["started_us"] < p["observed_us"] + p["maximum_age_us"] and
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
                late = e["latest_us"] > min(item["deadline_us"], p["observed_us"] + p["maximum_age_us"])
                require(not late or (not item["ok"] and item["outcome"] == "deadline" and item["failure_evidence"] == item[name]), "write exceeds readiness without retained deadline")
        require(item["staging_applied"] == (item["setup_execution"] == "acknowledged"), "staging application differs")
        stage, _ = evidence["staging_evidence"]
        trigger, _ = evidence["trigger_evidence"]
        if trigger["tx_accepted"] or trigger["execution_unknown"]:
            cap = min(item["deadline_us"], p["observed_us"] + p["maximum_age_us"])
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
                cap = min(item["deadline_us"], p["observed_us"] + p["maximum_age_us"]) if failure["step"] < 2 else item["deadline_us"]
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
            require(item["execution"] == "acknowledged" and item["polls"] >= 1
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
                require(item["execution"] == "acknowledged", "observation failure precedes acknowledgement")
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
            elif not failure["response_confirmed"]:
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
            require(serial["baud"] > 0 and 5 <= serial["data_bits"] <= 8
                    and 1 <= serial["parity"] <= 3 and serial["stop_bits"] in (1, 2), "active tuple is invalid")
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
                received = step["received_length"]
                if received < 5 or received > 5 + 2 * count:
                    error = 2  # Exact response-length contract, before header access.
                elif rx[0] != item["address"]:
                    error = 3
                elif rx[1] not in (3, 0x83):
                    error = 4
                elif received != (5 if rx[1] == 0x83 else 5 + 2 * count):
                    error = 2
                elif rx[1] == 3 and rx[2] != 2 * count:
                    error = 5
                elif wire_crc(rx) != 0:
                    error = 6
                else:
                    error = 10 if rx[1] == 0x83 else 0
                expected_status = {0: "OK", 6: "CRC_ERROR", 10: "EXCEPTION"}.get(error, "FRAME_ERROR")
                expected_detail = rx[2] if error == 10 else error
                require(step["frame_error"] == error and step["status"] == expected_status
                        and step["detail"] == expected_detail, "codec evidence differs from retained RX")
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
                    and block["fresh"] == (expected_current and age <= item["stale_after_ms"] * 1000), "observation age or generation is inconsistent")
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
                expected = "unknown" if not same else "stale" if item["now_us"] - item["communication_earliest_us"] > item["stale_after_ms"] * 1000 else "current"
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
        asynchronous = handle.command in (*READ_COMMANDS, *ACTION_COMMANDS, "recover", *MOVE_COMMANDS, "velocity", "driver", "home")
        if asynchronous:
            if item.get("type") == "reply" and not handle.accepted:
                if not item["ok"]:
                    self._complete(handle, item)
                    return
                if item.get("result") != "accepted":
                    raise BenchError(f"{handle.command} acceptance is not explicit")
                if handle.command in (*READ_COMMANDS, *ACTION_COMMANDS, *MOVE_COMMANDS, "velocity", "driver", "home"):
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
                             **dict.fromkeys(ACTION_COMMANDS, "action"), **dict.fromkeys(MOVE_COMMANDS, "move"), "velocity": "velocity", "driver": "driver", "home": "home"}[handle.command]
            if item.get("type") != expected_type or not handle.accepted:
                raise BenchError(f"{handle.command} response sequence is invalid")
            if (not self._operation_id(item.get("operation_id"))
                    or item["operation_id"] != handle.operation_id
                    or type(item.get("command_id")) is not int or item["command_id"] != handle.id):
                raise BenchError("terminal operation ID or original command ID does not match acceptance")
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
            elif handle.command == "driver":
                self._check_driver(item, handle.address, handle.driver_args)
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
                            (original is not None and driver != (original.command == "driver"))):
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
                        self._check_driver(item, original.address if original else None, original.driver_args if original else None)
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
        for handle in self.pending.values():
            if handle.terminal is None:
                handle.input_bytes += len(data)
                if handle.input_bytes > MAX_INPUT:
                    raise BenchError("command response exceeds input limit")
        for raw in self._lines(data):
            item = self._decode(raw)
            if item is not None:
                self._dispatch(item)

    def _check_pending(self, deadline: float) -> None:
        total = 0
        while True:
            if self.clock() >= deadline:
                raise BenchError("command deadline expired while collecting pending diagnostics")
            self._check_deadlines()
            data = self._read()
            total += len(data)
            if total > MAX_INPUT:
                raise BenchError("pending diagnostics exceed input limit")
            self._consume(data)
            if not data:
                active = any(handle.terminal is None for handle in self.pending.values())
                if self._load_line_pending() and not active:
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
        if host_args is not None:
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
        if command == "driver":
            driver_arguments(driver_args)
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
        if address is not None and (command not in (*READ_COMMANDS, *ACTION_COMMANDS, *MOVE_COMMANDS, "velocity", "driver", "home") or type(address) is not int
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
        if driver_args is not None and len(f"@{self.next_id} profile ess_rs driver {' '.join(driver_args)}" + ("" if address is None else f" {address}")) >= 128:
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
            if command in MOVE_COMMANDS: wire_command = "move " + command[5:]
            if command == "driver": wire_command = "profile ess_rs driver"
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
        if self.buffer and not active and not self._load_line_pending():
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
            if load[2] and not latest_load["console_lines"]:
                raise BenchError("console workload produced no complete lines")
    except Exception as exc:
        failure = str(exc)
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
    except Exception as exc:
        failure = str(exc)
        raise
    finally:
        console.emit("summary", mode="typed-read", read_kind=kind, reads_attempted=attempted,
                     reads_passed=completed, reads_failed=attempted - completed,
                     ok=failure is None and completed == len(kinds), error=failure)


def driver_read_campaign(console: Console, *, timeout_s: float, address: int) -> None:
    """One four-window settings read, immutable inspection and explicit release."""
    positive(timeout_s, "driver read timeout")
    handle = None; terminal = None; inspected = None; failure = None
    try:
        handle = console.begin("driver", driver_args=("read",), address=address, timeout_s=timeout_s)
        terminal = console.wait(handle)
        if handle.accepted:
            inspected = console.command("result", operation_id=handle.operation_id, timeout_s=timeout_s)
        if not terminal["ok"]:
            raise BenchError("driver read rejected or failed: " + str(terminal.get("result", terminal.get("outcome"))))
    except BaseException as exc:
        failure = str(exc) or "interrupted"
        raise
    finally:
        try:
            if handle is not None and handle.accepted and handle.terminal is not None and not handle.released and console.synchronized:
                release = console.command("release", operation_id=handle.operation_id, timeout_s=timeout_s)
                if not release["ok"]: raise BenchError("driver result release rejected")
        except BaseException as exc:
            if failure is None: failure = str(exc) or "interrupted"; raise
        finally:
            console.emit("summary", mode="driver-read", reads_attempted=1, ok=failure is None,
                         driver_result=terminal, inspected_result=inspected,
                         motor_writes=0, error=failure)


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
                    final_state = console.command("read-state", address=address, timeout_s=timeout_s)
                    final_health = console.command("health", timeout_s=timeout_s)
                    if not final_state["ok"] or not final_health["ok"]:
                        raise BenchError("cleanup state/health read failed")
                    motion = next((value for value in final_state["state_blocks"] if value["block"] == 0), None)
                    if motion is None or motion.get("running") is not False:
                        raise BenchError("cleanup final drive report does not show non-running")
                    cleanup = "drive_reported_nonrunning"
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
    except Exception as exc:
        failure = str(exc)
        raise
    finally:
        console.emit("summary", mode="state-health", checks_attempted=passed + (failure is not None),
                     checks_passed=passed, ok=failure is None and passed == count, error=failure)


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
    sub = parser.add_subparsers(dest="mode", required=True)
    sub.add_parser("probe", help="one model-register read and cached observations")
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
                if args.mode in ACTION_COMMANDS:
                    result = console.command(args.mode, timeout_s=args.timeout, address=args.address,
                                             stop_policy=getattr(args, "stop_policy", None))
                    if not result["ok"]:
                        raise BenchError("action rejected or failed: " + str(result.get("result", result.get("outcome"))))
                elif args.mode == "home":
                    move_campaign(console, command="home", move_args=None, home_args=args.home_args,
                                  cleanup_stop=args.cleanup_stop, timeout_s=args.timeout, address=args.address)
                elif args.mode == "driver-read":
                    driver_read_campaign(console, timeout_s=args.timeout, address=args.address)
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

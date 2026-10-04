#!/usr/bin/env python3
"""Bounded, read-only tests of the standalone MotorControl-RS JSONL console.

Only reviewed probes and typed reads create motor-bus traffic. Status, health and memory are cached
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
from pathlib import Path
import re
import sys
import time


MAX_LINE = 4096
MAX_INPUT = 32768
COMMANDS = frozenset({"version", "config", "probe", "capture-read", "status", "health", "memory", "stats", "load",
                      "drv", "result", "release", "cancel", "recover", "reset", "caps",
                      "read-identity", "read-config", "read-state", "health-check", "monitor"})
MAX_COMMANDS = 10  # Eight probes, one recovery and one interleaved local report.
MAX_OPERATIONS = 9  # Firmware retains eight ordinary results plus one recovery.
MAX_PROBES = 8
TYPED_READS = {"read-identity": "identity", "read-config": "config", "read-state": "state"}
READ_COMMANDS = ("probe", "capture-read", *TYPED_READS)
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
            require(isinstance(block, dict) and type(block.get("block")) is int and block["block"] == index
                    and block.get("config_operation_id") == config["operation_id"], "block identity is inconsistent")
            if index == 0:
                alarm, motion = words[:2]
                expected = dict(raw_alarm=alarm, alarm_known=alarm in (0, 1, 2, 3, 5), raw_motion=motion,
                                unknown_motion_bits=motion & 0xFF80, in_position=bool(motion & 1),
                                homing_complete=bool(motion & 2), running=bool(motion & 4), alarm_flag=bool(motion & 8),
                                released=bool(motion & 16), enabled=not bool(motion & 16),
                                positive_soft_limit=bool(motion & 32), negative_soft_limit=bool(motion & 64))
            elif index == 1:
                inputs, outputs = words[2:4]
                expected = dict(raw_inputs=inputs, raw_outputs=outputs, unknown_input_bits=inputs & 0xFFF0,
                                unknown_output_bits=outputs & 0xFFFC,
                                inputs=[bool(inputs & (1 << bit)) for bit in range(4)],
                                outputs=[bool(outputs & (1 << bit)) for bit in range(2)], levels="logical_valid_not_voltage")
            else:
                first, second, speed = words[4:7]
                pair_known = config["word_order_known"]
                raw_position = ((first << 16 | second) if config["word_order"] == 0 else (second << 16 | first)) if pair_known else 0
                expected = dict(position_words=[first, second], raw_speed=speed, pair_known=pair_known,
                                raw_position=raw_position, position_source=config["algorithm"] if config["algorithm_known"] else 0,
                                word_order_resolution=0 if pair_known else 9,
                                position_source_resolution=0 if config["algorithm_known"] else 10,
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
                               "observed_earliest_us", "observed_latest_us", "delivered_us"), "cached state block")
            require(block["last_attempt_us"] <= item["now_us"], "last attempt is in the future")
            require(type(block.get("last_attempt_status")) is str and type(block.get("last_attempt_detail")) is int, "attempt error is unavailable")
            if not block["valid"]:
                require(not block["current"] and not block["fresh"] and block.get("value") is None
                        and block.get("age_us") is None and block.get("source") == "absent", "absent feedback was fabricated")
                continue
            require(isinstance(block.get("value"), dict) and block["value"].get("block") == index
                    and block.get("source") == "checked_rtu_register", "valid observation source is invalid")
            require(0 <= block["observed_earliest_us"] <= block["observed_latest_us"] <= block["delivered_us"] <= item["now_us"]
                    and block["last_success_us"] == block["observed_latest_us"], "observation timing bounds are invalid")
            expected_current = all(block[key] == item["selected_" + key] for key in ("target", "address", "generation"))
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
        asynchronous = handle.command in (*READ_COMMANDS, "recover")
        if asynchronous:
            if item.get("type") == "reply" and not handle.accepted:
                if not item["ok"]:
                    self._complete(handle, item)
                    return
                if item.get("result") != "accepted":
                    raise BenchError(f"{handle.command} acceptance is not explicit")
                if handle.command in READ_COMMANDS:
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
                same_kind = sum((original.command in READ_COMMANDS) == (handle.command in READ_COMMANDS)
                                for original in self.operations.values())
                if same_kind >= (MAX_PROBES if handle.command in READ_COMMANDS else 1):
                    raise BenchError("accepted operation exceeds its retained result quota")
                handle.operation_id = operation_id
                handle.accepted = True
                self.operations[operation_id] = handle
                self.last_operation_id = operation_id
                return
            expected_type = {"probe": "probe", "capture-read": "capture_read", "recover": "recovery",
                             "read-identity": "read", "read-config": "read", "read-state": "read"}[handle.command]
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
              operation_id: int | None = None, monitor: tuple[int, int] | bool | None = None) -> Command:
        """Send once and collect admission/local reply; bus completion can stay pending.

        Up to ten handles (eight probes, one recovery and one local query) may be
        outstanding. Accepted operation IDs stay retained until explicit release.
        No command, including recovery, is retried after any framing failure.
        """
        positive(timeout_s, "command timeout")
        if command not in COMMANDS:
            raise ValueError("command is not in the read-only/host-control harness inventory")
        health_check = command == "health-check"
        if health_check:
            command = "read-state"  # The wire alias returns canonical read-state records.
        if monitor is not None:
            if command != "monitor" or (monitor is not False and
                (not isinstance(monitor, tuple) or len(monitor) != 2 or
                 type(monitor[0]) is not int or type(monitor[1]) is not int or
                 not 100 <= monitor[0] <= 60000 or not 1 <= monitor[1] <= 1000)):
                raise ValueError("monitor requires off or interval 100..60000/count 1..1000")
        if address is not None and (command not in READ_COMMANDS or type(address) is not int
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
        request_id = self.next_id
        self.next_id += 1
        started = self.clock()
        deadline = started + timeout_s
        try:
            self._check_pending(deadline)
            if self.clock() >= deadline:
                raise BenchError("command deadline expired before transmission")
            handle = Command(self, request_id, command, started, deadline, address, load, operation_id)
            self.pending[request_id] = handle
            suffix = "" if address is None else f" {address}"
            if load is not None:
                suffix = " " + " ".join(str(value) for value in load)
            if operation_id is not None:
                suffix = f" {operation_id}"
            if monitor is not None:
                suffix = " off" if monitor is False else f" {monitor[0]} {monitor[1]}"
            wire_command = "health check" if health_check else "read " + TYPED_READS[command] if command in TYPED_READS else command
            payload = f"@{request_id} {wire_command}{suffix}\n".encode("ascii")
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
        except Exception:
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
        try:
            while handle.terminal is None:
                self._check_deadlines()
                data = self._read()
                self._consume(data)
                if not data:
                    self.sleep(0.005)
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
        except Exception:
            self.synchronized = False
            raise

    def command(self, command: str, *, timeout_s: float = 3.0,
                address: int | None = None, load: tuple[int, int, int] | None = None,
                operation_id: int | None = None, monitor: tuple[int, int] | bool | None = None) -> dict:
        """Send once, wait for its terminal, then explicitly release admitted results."""
        handle = self.begin(command, timeout_s=timeout_s, address=address, load=load,
                            operation_id=operation_id, monitor=monitor)
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
    typed = sub.add_parser("typed-read", help="typed identity/configuration/state reads, retained inspection and release")
    typed.add_argument("--kind", choices=("identity", "config", "state", "both"), default="both")
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

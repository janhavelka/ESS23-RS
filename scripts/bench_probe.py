#!/usr/bin/env python3
"""Bounded, read-only tests of the standalone MotorControl-RS JSONL console.

Only ``probe`` creates motor-bus traffic. Status, health and memory are cached
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
COMMANDS = frozenset({"version", "probe", "status", "health", "memory", "stats", "load",
                      "drv", "result", "release", "cancel", "recover", "reset"})
MAX_COMMANDS = 10  # Eight probes, one recovery and one interleaved local report.
MAX_OPERATIONS = 9  # Firmware retains eight ordinary results plus one recovery.
MAX_PROBES = 8
LOAD_FIELDS = ("workload_us", "owner_delay_us", "console_bytes")
LOAD_LIMITS = (5000, 20000, 256)
LOAD_COUNTERS = (
    "elapsed_us", "work_us", "work_iterations", "console_lines", "console_dropped",
    "capture_us", "capture_samples", "owner_gap_max_us", "capture_gap_max_us",
    "work_stack_free_bytes",
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
        actual = item.get("address")
        if (type(actual) is not int or not 1 <= actual <= 247
                or (address is not None and actual != address)):
            raise BenchError("probe terminal address does not match acceptance")
        if not item["ok"]:
            return
        model = item.get("raw_model")
        if (item.get("transport") != "FRAME" or item.get("codec") != "OK"
                or item.get("outcome") != "success" or item.get("execution_unknown") is not False
                or type(model) is not int or not 0 <= model <= 65535
                or type(item.get("tx_bytes")) is not int or item["tx_bytes"] != 8
                or type(item.get("rx_bytes")) is not int or item["rx_bytes"] != 7
                or type(item.get("duration_us")) is not int
                or not 0 <= item["duration_us"] <= 0xFFFFFFFFFFFFFFFF
                or item.get("timing_valid") is not True
                or item.get("raw_truncated") is not False):
            raise BenchError("successful probe lacks consistent result evidence")
        check_counts(item, ("observed_earliest_us", "observed_latest_us", "delivered_us"), "probe")
        if not item["observed_earliest_us"] <= item["observed_latest_us"] <= item["delivered_us"]:
            raise BenchError("probe observation and delivery bounds are inconsistent")

    @staticmethod
    def _check_recovery(item: dict) -> None:
        outcome = item.get("outcome")
        if (item.get("recovery") is not True
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
        asynchronous = handle.command in ("probe", "recover")
        if asynchronous:
            if item.get("type") == "reply" and not handle.accepted:
                if not item["ok"]:
                    self._complete(handle, item)
                    return
                if item.get("result") != "accepted":
                    raise BenchError(f"{handle.command} acceptance is not explicit")
                if handle.command == "probe":
                    address = item.get("address")
                    if (type(address) is not int or not 1 <= address <= 247
                            or (handle.address is not None and address != handle.address)):
                        raise BenchError("probe acceptance address does not match request")
                    handle.address = address
                operation_id = item.get("operation_id")
                if not self._operation_id(operation_id) or operation_id <= self.last_operation_id:
                    raise BenchError("accepted operation ID is missing, reused or not monotonic")
                if len(self.operations) == MAX_OPERATIONS:
                    raise BenchError("accepted operation exceeds retained result limit")
                same_kind = sum(original.command == handle.command for original in self.operations.values())
                if same_kind >= (MAX_PROBES if handle.command == "probe" else 1):
                    raise BenchError("accepted operation exceeds its retained result quota")
                handle.operation_id = operation_id
                handle.accepted = True
                self.operations[operation_id] = handle
                self.last_operation_id = operation_id
                return
            expected_type = "probe" if handle.command == "probe" else "recovery"
            if item.get("type") != expected_type or not handle.accepted:
                raise BenchError(f"{handle.command} response sequence is invalid")
            if (not self._operation_id(item.get("operation_id"))
                    or item["operation_id"] != handle.operation_id
                    or type(item.get("command_id")) is not int or item["command_id"] != handle.id):
                raise BenchError("terminal operation ID or original command ID does not match acceptance")
            if handle.command == "probe":
                self._check_probe(item, handle.address)
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
                    if item.get("result") == "pending":
                        if item["ok"] is not True or type(item.get("recovery")) is not bool:
                            raise BenchError("pending result lacks a valid lifecycle")
                    elif item.get("recovery") is True:
                        self._check_recovery(item)
                    else:
                        self._check_probe(item, original.address if original else None)
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
              operation_id: int | None = None) -> Command:
        """Send once and collect admission/local reply; bus completion can stay pending.

        Up to ten handles (eight probes, one recovery and one local query) may be
        outstanding. Accepted operation IDs stay retained until explicit release.
        No command, including recovery, is retried after any framing failure.
        """
        positive(timeout_s, "command timeout")
        if command not in COMMANDS:
            raise ValueError("command is not in the read-only/host-control harness inventory")
        if address is not None and (command != "probe" or type(address) is not int
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
            payload = f"@{request_id} {command}{suffix}\n".encode("ascii")
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
                operation_id: int | None = None) -> dict:
        """Send once, wait for its terminal, then explicitly release admitted results."""
        handle = self.begin(command, timeout_s=timeout_s, address=address, load=load,
                            operation_id=operation_id)
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
    sleeper: Callable[[float], None] = time.sleep,
) -> None:
    """Finite work, no recovery and no replay. Watch never issues a probe.

    Load campaigns retain cached diagnostics after a complete failed probe,
    then stop. A framing failure ends communication immediately. Fixture load
    stays explicitly configured, including after a failure or interruption;
    the harness never sends cleanup or recovery commands behind the operator.
    """
    if mode not in {"probe", "stress", "watch", "load"}:
        raise ValueError("unknown campaign mode")
    if type(count) is not int or not 1 <= count <= 1_000_000 or (mode == "probe" and count != 1):
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
        for iteration in range(count):
            console.emit("iteration", number=iteration + 1, mode=mode)
            probe = None
            if mode != "watch":
                attempted += 1
                probe = console.command("probe", timeout_s=timeout_s, address=address)
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
        console.emit("summary", mode=mode, iterations=completed, requested_count=count,
                     probes_attempted=attempted, probes_passed=passed,
                     probes_failed=attempted - passed,
                     latency_us={"min": latency_min, "max": latency_max,
                                 "mean": latency_total / passed if passed else None},
                     load_settings=load, last_load=latest_load, last_memory=latest_memory,
                     ok=failure is None and completed == count, error=failure)


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
    for mode, default_count, default_interval, description in (
        ("stress", 100, 0.1, "explicit repeated probes"),
        ("watch", 60, 1.0, "cached status/health/memory only; no motor traffic"),
        ("load", 100, 0.1, "read-only probes with explicit competing host workload"),
    ):
        child = sub.add_parser(mode, help=description)
        child.add_argument("--count", type=int, default=default_count)
        child.add_argument("--interval", type=float, default=default_interval)
        if mode == "load":
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
                         timeout_s=args.timeout, address=args.address, load=args.load)
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

#!/usr/bin/env python3
"""Bounded, read-only tests of the standalone MotorControl-RS JSONL console.

Only ``probe`` creates motor-bus traffic. Status, health and memory are cached
host reports. A lost or malformed reply stops the run; nothing is replayed and
host recovery is never automatic. Python 3.10+; pyserial is needed only for a
real port. See ``--help`` for finite probe, stress and watch runs.
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
COMMANDS = frozenset({"version", "probe", "status", "health", "memory", "stats"})
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


class Console:
    """One owner, one command at a time, correlated replies, fixed input bounds.

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
            if self.buffer:
                raise BenchError("startup ended with an incomplete line")
        except Exception:
            self.synchronized = False
            raise

    def _check_pending(self) -> None:
        total = 0
        while True:
            data = self._read()
            total += len(data)
            if total > MAX_INPUT:
                raise BenchError("pending diagnostics exceed input limit")
            for raw in self._lines(data):
                if self._decode(raw) is not None:
                    raise BenchError("unsolicited structured reply before command")
            if not data:
                break
        if self.buffer:
            raise BenchError("incomplete pending line before command")

    def command(self, command: str, *, timeout_s: float = 3.0,
                address: int | None = None) -> dict:
        """Send exactly once and return the terminal response, logging acceptance."""
        positive(timeout_s, "command timeout")
        if command not in COMMANDS:
            raise ValueError("command is not in the read-only harness inventory")
        if address is not None and (command != "probe" or type(address) is not int
                                    or not 1 <= address <= 247):
            raise ValueError("an ESS probe address must be an integer within 1..247")
        if not self.synchronized:
            raise BenchError("console framing failed; session cannot be reused")
        if not self.identified and command != "version":
            raise BenchError("identify the standalone firmware before issuing commands")
        if self.next_id > 0xFFFFFFFF:
            raise BenchError("request IDs exhausted; start a new inspected session")
        request_id = self.next_id
        self.next_id += 1
        self.synchronized = False
        started = self.clock()
        deadline = started + timeout_s
        try:
            self._check_pending()
            if self.clock() >= deadline:
                raise BenchError("command deadline expired before transmission")
            suffix = "" if address is None else f" {address}"
            payload = f"@{request_id} {command}{suffix}\n".encode("ascii")
            self.emit("send", id=request_id, command=command, address=address)
            if self.port.write(payload) != len(payload):
                raise BenchError("short serial command write; command was not replayed")
            total = 0
            accepted = False
            probe_address = None
            terminal = None
            while self.clock() < deadline:
                data = self._read()
                total += len(data)
                if total > MAX_INPUT:
                    raise BenchError("command response exceeds input limit")
                for raw in self._lines(data):
                    item = self._decode(raw)
                    if item is None:
                        continue
                    if terminal is not None:
                        raise BenchError("duplicate or unsolicited terminal response")
                    if (type(item.get("id")) is not int or item["id"] != request_id
                            or item.get("command") != command
                            or item.get("profile") != "ess_rs"
                            or type(item.get("ok")) is not bool):
                        raise BenchError("reply ID, command, profile or result does not match request")
                    self.emit("reply", response=item)
                    if command == "probe":
                        if item.get("type") == "reply" and not accepted:
                            if item["ok"]:
                                if item.get("result") != "accepted":
                                    raise BenchError("probe acceptance is not explicit")
                                probe_address = item.get("address")
                                if (type(probe_address) is not int or not 1 <= probe_address <= 247
                                        or (address is not None and probe_address != address)):
                                    raise BenchError("probe acceptance address does not match request")
                                accepted = True
                                continue
                            terminal = item
                        elif item.get("type") == "probe" and accepted:
                            if (type(item.get("address")) is not int
                                    or item["address"] != probe_address):
                                raise BenchError("probe terminal address does not match acceptance")
                            if item["ok"]:
                                model = item.get("raw_model")
                                if (item.get("transport") != "FRAME" or item.get("codec") != "OK"
                                        or type(model) is not int or not 0 <= model <= 65535
                                        or type(item.get("tx_bytes")) is not int or item["tx_bytes"] != 8
                                        or type(item.get("rx_bytes")) is not int or item["rx_bytes"] != 7
                                        or item.get("timing_valid") is not True
                                        or item.get("raw_truncated") is not False):
                                    raise BenchError("successful probe lacks consistent result evidence")
                            terminal = item
                        else:
                            raise BenchError("probe response sequence is invalid")
                    elif item.get("type") == "reply":
                        terminal = item
                    else:
                        raise BenchError("unexpected structured console event")
                if terminal is not None:
                    if self.clock() >= deadline:
                        raise BenchError("command response deadline expired; command was not replayed")
                    if self.buffer:
                        raise BenchError("terminal response has an incomplete trailing line")
                    if command == "status" and terminal["ok"]:
                        uptime = terminal.get("uptime_ms")
                        if type(uptime) is not int or uptime < 0:
                            raise BenchError("status lacks a valid monotonic uptime")
                        if self.uptime_ms is not None and uptime < self.uptime_ms:
                            raise BenchError("device uptime regressed; possible reset")
                        self.uptime_ms = uptime
                    self.synchronized = True
                    self.emit("complete", id=request_id, command=command,
                              duration_s=round(self.clock() - started, 6),
                              ok=terminal["ok"])
                    return terminal
                if not data:
                    self.sleep(0.005)
            raise BenchError("command response deadline expired; command was not replayed")
        except Exception:
            self.synchronized = False
            raise

    def identify(self, *, timeout_s: float = 3.0) -> dict:
        response = self.command("version", timeout_s=timeout_s)
        if (not response["ok"] or response.get("product") != "MotorControl-RS"
                or type(response.get("protocol")) is not int
                or response["protocol"] != 1):
            self.synchronized = False
            raise BenchError("port is not the supported MotorControl-RS probe console")
        self.identified = True
        return response


def successful(console: Console, command: str, timeout_s: float,
               address: int | None = None) -> dict:
    result = console.command(command, timeout_s=timeout_s, address=address)
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
    sleeper: Callable[[float], None] = time.sleep,
) -> None:
    """Finite work, no recovery and no replay. Watch never issues a probe."""
    if mode not in {"probe", "stress", "watch"}:
        raise ValueError("unknown campaign mode")
    if not 1 <= count <= 1_000_000 or (mode == "probe" and count != 1):
        raise ValueError("invalid campaign count")
    if not math.isfinite(interval_s) or not 0 <= interval_s <= 60:
        raise ValueError("interval must be finite and within 0..60 seconds")
    positive(timeout_s, "command timeout")
    if type(address) is not int or not 1 <= address <= 247:
        raise ValueError("ESS probe address must be within 1..247")
    successful(console, "stats", timeout_s)
    for iteration in range(count):
        console.emit("iteration", number=iteration + 1, mode=mode)
        if mode != "watch":
            successful(console, "probe", timeout_s, address)
        for command in ("status", "health", "memory"):
            successful(console, command, timeout_s)
        if iteration + 1 < count and interval_s:
            sleeper(interval_s)
    successful(console, "stats", timeout_s)
    console.emit("summary", mode=mode, iterations=count, ok=True)


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
    for mode, default_count, default_interval in (("stress", 100, 0.1), ("watch", 60, 1.0)):
        child = sub.add_parser(mode, help="explicit repeated probes" if mode == "stress"
                               else "cached status/health/memory only; no motor traffic")
        child.add_argument("--count", type=int, default=default_count)
        child.add_argument("--interval", type=float, default=default_interval)
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
    return result


def main(argv: list[str] | None = None) -> int:
    args = arguments(argv)
    port = None
    try:
        with args.log.open("x", encoding="utf-8", newline="\n") as stream:
            evidence = Evidence(stream)
            evidence("session", port=args.port, console_baud=args.baud, mode=args.mode,
                     address=args.address, requested_count=args.count, timeout_s=args.timeout)
            try:
                port = open_port(args.port, args.baud, args.timeout)
                console = Console(port, on_event=evidence)
                console.drain_startup(args.startup)
                console.identify(timeout_s=args.timeout)
                campaign(console, args.mode, count=args.count, interval_s=args.interval,
                         timeout_s=args.timeout, address=args.address)
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

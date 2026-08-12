#!/usr/bin/env python3
"""
Headless Tibia 7.72 load client for the local Nekiro TFS test server.

It implements only the protocol pieces required by the load test:
RSA/XTEA login, framed packet receive/discard, keepalive and rate-limited
movement/follow/attack actions. It intentionally has no renderer, audio,
sprites, UI, map parser or CAM recorder.
"""

from __future__ import annotations

import argparse
import asyncio
import csv
import ctypes
import json
import math
import os
import random
import socket
import struct
import subprocess
import sys
import time
from array import array
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any


RSA_MODULUS = int(
    "109120132967399429278860960508995541528237502902798129123468757937266291492576446330739696001110603907230888610072655818825358503429057592827629436413108566029093628212635953836686562675849720620786279431090218017681061521755056710823876476444260558147179707119674283982419152118103759076030616683978566631413"
)
RSA_EXPONENT = 65537
PROTOCOL_GAME = 0x0A
CLIENT_OS_WINDOWS = 2
CLIENT_VERSION = 772
XTEA_DELTA = 0x9E3779B9
UINT32_MASK = 0xFFFFFFFF

OPCODE_LOGOUT = 0x14
OPCODE_PING_BACK_REQUEST = 0x1D
OPCODE_PING = 0x1E
OPCODE_WALK_NORTH = 0x65
OPCODE_WALK_EAST = 0x66
OPCODE_WALK_SOUTH = 0x67
OPCODE_WALK_WEST = 0x68
OPCODE_FIGHT_MODES = 0xA0
OPCODE_ATTACK = 0xA1
OPCODE_FOLLOW = 0xA2


def _u16_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 0xFFFF:
        raise ValueError("string is too long for the protocol")
    return struct.pack("<H", len(encoded)) + encoded


def xtea_encrypt(data: bytes, key: tuple[int, int, int, int]) -> bytes:
    if len(data) % 8:
        raise ValueError("XTEA input must be a multiple of 8 bytes")
    output = bytearray(data)
    for offset in range(0, len(output), 8):
        left, right = struct.unpack_from("<II", output, offset)
        total = 0
        for _ in range(32):
            left = (
                left
                + ((((right << 4) ^ (right >> 5)) + right) ^ (total + key[total & 3]))
            ) & UINT32_MASK
            total = (total + XTEA_DELTA) & UINT32_MASK
            right = (
                right
                + ((((left << 4) ^ (left >> 5)) + left) ^ (total + key[(total >> 11) & 3]))
            ) & UINT32_MASK
        struct.pack_into("<II", output, offset, left, right)
    return bytes(output)


def xtea_decrypt(data: bytes, key: tuple[int, int, int, int]) -> bytes:
    if len(data) % 8:
        raise ValueError("XTEA input must be a multiple of 8 bytes")
    output = bytearray(data)
    for offset in range(0, len(output), 8):
        left, right = struct.unpack_from("<II", output, offset)
        total = (XTEA_DELTA * 32) & UINT32_MASK
        for _ in range(32):
            right = (
                right
                - ((((left << 4) ^ (left >> 5)) + left) ^ (total + key[(total >> 11) & 3]))
            ) & UINT32_MASK
            total = (total - XTEA_DELTA) & UINT32_MASK
            left = (
                left
                - ((((right << 4) ^ (right >> 5)) + right) ^ (total + key[total & 3]))
            ) & UINT32_MASK
        struct.pack_into("<II", output, offset, left, right)
    return bytes(output)


def build_login_packet(
    account_number: int,
    character_name: str,
    password: str,
    xtea_key: tuple[int, int, int, int],
) -> bytes:
    rsa_plain = bytearray()
    rsa_plain.append(0)
    rsa_plain.extend(struct.pack("<IIII", *xtea_key))
    rsa_plain.append(0)  # gamemaster flag
    rsa_plain.extend(struct.pack("<I", account_number))
    rsa_plain.extend(_u16_string(character_name))
    rsa_plain.extend(_u16_string(password))
    if len(rsa_plain) > 128:
        raise ValueError("login credentials do not fit in the RSA block")
    rsa_plain.extend(os.urandom(128 - len(rsa_plain)))
    rsa_value = int.from_bytes(rsa_plain, "big")
    rsa_cipher = pow(rsa_value, RSA_EXPONENT, RSA_MODULUS).to_bytes(128, "big")

    body = bytearray([PROTOCOL_GAME])
    body.extend(struct.pack("<HH", CLIENT_OS_WINDOWS, CLIENT_VERSION))
    body.extend(rsa_cipher)
    return struct.pack("<H", len(body)) + body


def build_encrypted_packet(payload: bytes, key: tuple[int, int, int, int]) -> bytes:
    plain = bytearray(struct.pack("<H", len(payload)))
    plain.extend(payload)
    padding = (-len(plain)) % 8
    if padding:
        plain.extend(b"\x00" * padding)
    encrypted = xtea_encrypt(bytes(plain), key)
    return struct.pack("<H", len(encrypted)) + encrypted


def decrypt_server_packet(body: bytes, key: tuple[int, int, int, int]) -> bytes:
    plain = xtea_decrypt(body, key)
    if len(plain) < 2:
        raise ValueError("encrypted server packet is too short")
    inner_length = struct.unpack_from("<H", plain, 0)[0]
    if inner_length > len(plain) - 2:
        raise ValueError("invalid inner packet length")
    return plain[2 : 2 + inner_length]


async def read_frame(reader: asyncio.StreamReader, timeout: float | None = None) -> bytes:
    async def _read() -> bytes:
        header = await reader.readexactly(2)
        length = struct.unpack("<H", header)[0]
        if length == 0 or length > 65520:
            raise ValueError(f"invalid network frame length: {length}")
        return await reader.readexactly(length)

    if timeout is None:
        return await _read()
    return await asyncio.wait_for(_read(), timeout)


def decode_login_error(payload: bytes) -> str:
    if not payload or payload[0] != 0x14 or len(payload) < 3:
        return f"unexpected first opcode 0x{payload[0]:02X}" if payload else "empty login response"
    length = struct.unpack_from("<H", payload, 1)[0]
    return payload[3 : 3 + length].decode("utf-8", errors="replace")


def extract_add_creatures(payload: bytes, name: str) -> list[int]:
    """Extract IDs from 7.72 AddCreature records for one exact visible name."""
    encoded_name = name.encode("utf-8")
    name_length = struct.pack("<H", len(encoded_name))
    ids: list[int] = []
    search_from = 0
    while True:
        name_offset = payload.find(encoded_name, search_from)
        if name_offset < 0:
            break
        search_from = name_offset + len(encoded_name)
        # 0x61 0x00, remove-known ID, creature ID, string length, name.
        record_offset = name_offset - 12
        if (
            record_offset < 0
            or payload[record_offset : record_offset + 2] != b"\x61\x00"
            or payload[name_offset - 2 : name_offset] != name_length
        ):
            continue
        ids.append(struct.unpack_from("<I", payload, name_offset - 6)[0])
    return ids


class Samples:
    def __init__(self) -> None:
        self.values = array("d")

    def add(self, value_ms: float) -> None:
        self.values.append(value_ms)

    def summary(self) -> dict[str, float | int]:
        if not self.values:
            return {"count": 0, "avg_ms": 0.0, "p95_ms": 0.0, "p99_ms": 0.0, "max_ms": 0.0}
        ordered = sorted(self.values)

        def percentile(fraction: float) -> float:
            index = max(0, min(len(ordered) - 1, math.ceil(fraction * len(ordered)) - 1))
            return ordered[index]

        return {
            "count": len(ordered),
            "avg_ms": sum(ordered) / len(ordered),
            "p95_ms": percentile(0.95),
            "p99_ms": percentile(0.99),
            "max_ms": ordered[-1],
        }


@dataclass
class Counters:
    attempted: int = 0
    logged_in: int = 0
    login_failed: int = 0
    disconnected: int = 0
    rx_frames: int = 0
    rx_bytes: int = 0
    tx_frames: int = 0
    tx_bytes: int = 0
    actions: int = 0
    action_latency: Samples = field(default_factory=Samples)
    login_latency: Samples = field(default_factory=Samples)


class EventLog:
    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._file = path.open("a", encoding="utf-8", buffering=1)

    def write(self, event: str, **fields: Any) -> None:
        record = {
            "timestamp": datetime.now().astimezone().isoformat(timespec="milliseconds"),
            "event": event,
            **fields,
        }
        self._file.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")

    def close(self) -> None:
        self._file.close()


if os.name == "nt":
    from ctypes import wintypes

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    PROCESS_VM_READ = 0x0010

    class FILETIME(ctypes.Structure):
        _fields_ = [("low", wintypes.DWORD), ("high", wintypes.DWORD)]

    class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
            ("PrivateUsage", ctypes.c_size_t),
        ]


def _filetime_seconds(value: Any) -> float:
    return ((int(value.high) << 32) | int(value.low)) / 10_000_000.0


def sample_process(pid: int) -> tuple[float, int, int] | None:
    if os.name != "nt":
        if pid == os.getpid():
            return time.process_time(), 0, 0
        return None
    kernel32 = ctypes.windll.kernel32
    psapi = ctypes.windll.psapi
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, pid)
    if not handle:
        return None
    try:
        creation = FILETIME()
        exit_time = FILETIME()
        kernel = FILETIME()
        user = FILETIME()
        if not kernel32.GetProcessTimes(
            handle,
            ctypes.byref(creation),
            ctypes.byref(exit_time),
            ctypes.byref(kernel),
            ctypes.byref(user),
        ):
            return None
        memory = PROCESS_MEMORY_COUNTERS_EX()
        memory.cb = ctypes.sizeof(memory)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
            return None
        cpu_seconds = _filetime_seconds(kernel) + _filetime_seconds(user)
        return cpu_seconds, int(memory.WorkingSetSize), int(memory.PrivateUsage)
    finally:
        kernel32.CloseHandle(handle)


def find_tfs_pid() -> int | None:
    if os.name != "nt":
        return None
    try:
        output = subprocess.check_output(
            ["tasklist", "/FI", "IMAGENAME eq tfs.exe", "/FO", "CSV", "/NH"],
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
        )
        row = next(csv.reader(output.splitlines()), None)
        if row and len(row) >= 2 and row[0].lower() == "tfs.exe":
            return int(row[1])
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    return None


@dataclass
class ProcessDelta:
    pid: int
    last_wall: float = field(default_factory=time.perf_counter)
    last_cpu: float = 0.0

    def __post_init__(self) -> None:
        initial = sample_process(self.pid)
        self.last_cpu = initial[0] if initial else 0.0

    def sample(self) -> dict[str, float | int] | None:
        now = time.perf_counter()
        current = sample_process(self.pid)
        if current is None:
            return None
        cpu_seconds, working_set, private_bytes = current
        elapsed = max(0.001, now - self.last_wall)
        cpu_delta = max(0.0, cpu_seconds - self.last_cpu)
        self.last_wall = now
        self.last_cpu = cpu_seconds
        one_core_pct = (cpu_delta / elapsed) * 100.0
        host_pct = one_core_pct / max(1, os.cpu_count() or 1)
        return {
            "pid": self.pid,
            "cpu_one_core_pct": one_core_pct,
            "cpu_host_pct": host_pct,
            "working_set_mb": working_set / (1024 * 1024),
            "private_mb": private_bytes / (1024 * 1024),
        }


class HeadlessSession:
    def __init__(
        self,
        runner: "LoadRunner",
        index: int,
        account_number: int,
        character_name: str,
        profile: str,
    ) -> None:
        self.runner = runner
        self.index = index
        self.account_number = account_number
        self.character_name = character_name
        self.profile = profile
        self.key = tuple(random.SystemRandom().getrandbits(32) for _ in range(4))
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self.runtime_id: int | None = None
        self.target_id: int | None = None
        self.target_name: str | None = None
        self.observed_creatures: dict[str, int] = {}
        self.connected = False
        self.closing = False
        self.reader_task: asyncio.Task[None] | None = None
        self.action_task: asyncio.Task[None] | None = None
        self.keepalive_task: asyncio.Task[None] | None = None
        self.pending_action_at: float | None = None
        self.pending_action_name: str | None = None
        self._walk_step = 0

    async def login(self) -> None:
        self.runner.counters.attempted += 1
        started = time.perf_counter()
        try:
            self.reader, self.writer = await asyncio.wait_for(
                asyncio.open_connection(self.runner.args.host, self.runner.args.port),
                self.runner.args.login_timeout,
            )
            transport_socket = self.writer.get_extra_info("socket")
            if transport_socket:
                transport_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            packet = build_login_packet(
                self.account_number,
                self.character_name,
                self.runner.args.password,
                self.key,
            )
            self.writer.write(packet)
            await self.writer.drain()
            self.runner.counters.tx_frames += 1
            self.runner.counters.tx_bytes += len(packet)

            deadline = time.perf_counter() + self.runner.args.login_timeout
            payload = b""
            preliminary_opcodes: list[int] = []
            while time.perf_counter() < deadline:
                remaining = max(0.001, deadline - time.perf_counter())
                first_body = await read_frame(self.reader, remaining)
                payload = decrypt_server_packet(first_body, self.key)
                self._observe_payload(payload)
                self.runner.counters.rx_frames += 1
                self.runner.counters.rx_bytes += len(first_body) + 2
                if payload and payload[0] == 0x0A and len(payload) >= 5:
                    break
                if payload and payload[0] in (0x14, 0x16):
                    raise RuntimeError(decode_login_error(payload))
                preliminary_opcodes.append(payload[0] if payload else -1)
            else:
                raise TimeoutError(
                    f"login map packet 0x0A was not received; preliminary opcodes={preliminary_opcodes}"
                )

            latency_ms = (time.perf_counter() - started) * 1000.0
            self.runtime_id = struct.unpack_from("<I", payload, 1)[0]
            self.connected = True
            self.runner.counters.logged_in += 1
            self.runner.counters.login_latency.add(latency_ms)
            self.runner.events.write(
                "login_ok",
                account=self.account_number,
                character=self.character_name,
                profile=self.profile,
                runtime_id=self.runtime_id,
                latency_ms=round(latency_ms, 3),
                preliminary_opcodes=[
                    f"0x{opcode:02X}" if opcode >= 0 else "empty" for opcode in preliminary_opcodes
                ],
            )
            self.reader_task = asyncio.create_task(self._reader_loop(), name=f"reader-{self.character_name}")
            self.keepalive_task = asyncio.create_task(self._keepalive_loop(), name=f"keepalive-{self.character_name}")
        except Exception as exc:
            self.runner.counters.login_failed += 1
            self.runner.events.write(
                "login_failed",
                account=self.account_number,
                character=self.character_name,
                profile=self.profile,
                error=f"{type(exc).__name__}: {exc}",
            )
            await self.close(send_logout=False)

    async def start_activity(self) -> None:
        if not self.connected:
            return
        if self.profile == "attack-follow":
            # Offensive, chase enabled, and safe fight disabled. The red
            # "PvP" button in this 7.72 client maps to safe fight = false.
            # Chase makes the server follow the attacked creature; sending
            # opcode 0xA2 after an attack would instead cancel it.
            await self._send_payload(bytes([OPCODE_FIGHT_MODES, 1, 1, 0]))
            self.runner.events.write(
                "attack_follow_modes_set",
                character=self.character_name,
                fight_mode="offensive",
                chase=True,
                safe_fight=False,
            )
        if self.profile != "idle":
            self.action_task = asyncio.create_task(self._action_loop(), name=f"action-{self.character_name}")

    async def _reader_loop(self) -> None:
        assert self.reader is not None
        try:
            while not self.closing:
                body = await read_frame(self.reader)
                payload = decrypt_server_packet(body, self.key)
                self._observe_payload(payload)
                self.runner.counters.rx_frames += 1
                self.runner.counters.rx_bytes += len(body) + 2
                # Tibia 7.72 replies immediately to the server's 0x1E ping
                # with ClientPingBack (also 0x1E). This avoids a synthetic
                # timer becoming late during a login burst.
                if payload and payload[0] == OPCODE_PING:
                    await self._send_payload(bytes([OPCODE_PING]))
                if self.pending_action_at is not None:
                    latency_ms = (time.perf_counter() - self.pending_action_at) * 1000.0
                    self.runner.counters.action_latency.add(latency_ms)
                    self.pending_action_at = None
                    self.pending_action_name = None
        except asyncio.IncompleteReadError:
            if not self.closing:
                self._record_disconnect("server closed the TCP stream")
        except (ConnectionError, OSError, ValueError) as exc:
            if not self.closing:
                self._record_disconnect(f"{type(exc).__name__}: {exc}")
        finally:
            self.connected = False

    def _record_disconnect(self, reason: str) -> None:
        self.runner.counters.disconnected += 1
        self.runner.events.write(
            "disconnect",
            account=self.account_number,
            character=self.character_name,
            profile=self.profile,
            reason=reason,
        )

    async def _send_payload(self, payload: bytes, action_name: str | None = None) -> None:
        if not self.connected or self.writer is None:
            return
        packet = build_encrypted_packet(payload, self.key)
        try:
            self.writer.write(packet)
            await self.writer.drain()
            self.runner.counters.tx_frames += 1
            self.runner.counters.tx_bytes += len(packet)
            if action_name:
                self.runner.counters.actions += 1
                self.pending_action_at = time.perf_counter()
                self.pending_action_name = action_name
        except (ConnectionError, OSError) as exc:
            if not self.closing:
                self._record_disconnect(f"send failed: {type(exc).__name__}: {exc}")
            self.connected = False

    def _rate_for_profile(self) -> float:
        if self.profile == "movement":
            return self.runner.args.movement_rate
        if self.profile == "follow":
            return self.runner.args.follow_rate
        if self.profile == "attack":
            return self.runner.args.attack_rate
        if self.profile == "attack-follow":
            return self.runner.args.attack_rate
        return self.runner.args.mixed_rate

    def _observe_payload(self, payload: bytes) -> None:
        if not self.target_name:
            return
        for creature_id in extract_add_creatures(payload, self.target_name):
            self.observed_creatures[self.target_name] = creature_id
            if self.target_id is None:
                self.target_id = creature_id
                self.runner.events.write(
                    "follow_terminal_visible",
                    character=self.character_name,
                    target_name=self.target_name,
                    target_id=creature_id,
                )

    def _build_action(self) -> tuple[bytes, str] | None:
        if self.profile in ("movement", "mixed"):
            movement = (
                OPCODE_WALK_NORTH,
                OPCODE_WALK_SOUTH,
                OPCODE_WALK_EAST,
                OPCODE_WALK_WEST,
            )
            opcode = movement[self._walk_step % len(movement)]
            self._walk_step += 1
            if self.profile == "movement" or self._walk_step % 3:
                return bytes([opcode]), "movement"

        if self.target_id is None and self.target_name:
            self.target_id = self.observed_creatures.get(self.target_name)
        if self.target_id is None:
            return None
        if self.profile == "follow" or (self.profile == "mixed" and self._walk_step % 2 == 0):
            return bytes([OPCODE_FOLLOW]) + struct.pack("<I", self.target_id), "follow"
        return bytes([OPCODE_ATTACK]) + struct.pack("<I", self.target_id), "attack"

    async def _action_loop(self) -> None:
        rate = self._rate_for_profile()
        if rate <= 0:
            return
        base_delay = 1.0 / rate
        await asyncio.sleep(random.uniform(0.1, max(0.11, base_delay)))
        try:
            while self.connected and not self.closing:
                action = self._build_action()
                if action:
                    await self._send_payload(action[0], action[1])
                jitter = self.runner.args.action_jitter
                factor = random.uniform(max(0.05, 1.0 - jitter), 1.0 + jitter)
                await asyncio.sleep(base_delay * factor)
        except asyncio.CancelledError:
            pass

    async def _keepalive_loop(self) -> None:
        """Send periodic pings to prevent server-side idle timeout (30s)."""
        interval = max(1.0, self.runner.args.keepalive_interval)
        try:
            while self.connected and not self.closing:
                await asyncio.sleep(interval)
                if self.connected and not self.closing:
                    await self._send_payload(bytes([OPCODE_PING]), "keepalive")
        except asyncio.CancelledError:
            pass

    async def close(self, send_logout: bool = True) -> None:
        if self.closing:
            return
        self.closing = True
        for task in (self.action_task, self.keepalive_task):
            if task:
                task.cancel()
        if send_logout and self.connected:
            await self._send_payload(bytes([OPCODE_LOGOUT]))
            await asyncio.sleep(0.05)
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except (ConnectionError, OSError):
                pass
        if self.reader_task:
            self.reader_task.cancel()
            await asyncio.gather(self.reader_task, return_exceptions=True)
        self.connected = False


class LoadRunner:
    METRIC_FIELDS = [
        "timestamp",
        "elapsed_s",
        "attempted",
        "logged_in_total",
        "connected_now",
        "login_failed",
        "disconnected",
        "actions",
        "rx_frames",
        "rx_bytes",
        "tx_frames",
        "tx_bytes",
        "sim_pid",
        "sim_cpu_one_core_pct",
        "sim_cpu_host_pct",
        "sim_working_set_mb",
        "sim_private_mb",
        "tfs_pid",
        "tfs_cpu_one_core_pct",
        "tfs_cpu_host_pct",
        "tfs_working_set_mb",
        "tfs_private_mb",
    ]

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        self.output_dir = Path(args.output_dir).resolve() / f"{stamp}-{args.profile}-{args.count}"
        self.output_dir.mkdir(parents=True, exist_ok=False)
        self.events = EventLog(self.output_dir / "events.jsonl")
        self.counters = Counters()
        self.sessions: list[HeadlessSession] = []
        self.started_at = time.perf_counter()
        self.stop_event = asyncio.Event()
        self.sim_process = ProcessDelta(os.getpid())
        tfs_pid = args.tfs_pid or find_tfs_pid()
        self.tfs_process = ProcessDelta(tfs_pid) if tfs_pid else None
        self.metrics_file = (self.output_dir / "resources.csv").open("w", newline="", encoding="utf-8")
        self.metrics_writer = csv.DictWriter(self.metrics_file, fieldnames=self.METRIC_FIELDS)
        self.metrics_writer.writeheader()
        self.metrics_file.flush()

    def profile_for_index(self, index: int) -> str:
        if self.args.profile != "mixed":
            return self.args.profile
        choices = ("idle", "idle", "movement", "movement", "follow", "attack")
        return choices[index % len(choices)]

    async def connect_batches(self) -> None:
        for start in range(0, self.args.count, self.args.batch_size):
            batch: list[HeadlessSession] = []
            end = min(self.args.count, start + self.args.batch_size)
            for index in range(start, end):
                ordinal = self.args.start_index + index
                session = HeadlessSession(
                    self,
                    index,
                    self.args.account_start + index,
                    f"{self.args.character_prefix}{ordinal:03d}",
                    self.profile_for_index(index),
                )
                if self.args.follow_chain and index == self.args.count - 1:
                    # Set this before login: the GM can be present in the
                    # terminal session's initial map packet.
                    session.target_name = self.args.follow_terminal_name
                self.sessions.append(session)
                batch.append(session)

            if self.args.login_concurrency == 1:
                # Production-compatible path. Ban::acceptConnection rejects
                # every sixth rapid attempt from the same IP, so sockets are
                # admitted at a deterministic cadence.
                for admission_index, session in enumerate(batch):
                    await session.login()
                    is_last_overall = end == self.args.count and admission_index == len(batch) - 1
                    if not is_last_overall:
                        await asyncio.sleep(self.args.login_admission_delay)
            else:
                # Explicit load-test path. The matching TFS launcher must set
                # TFS_LOAD_TEST_BYPASS_CONNECTION_THROTTLE=1. The semaphore
                # bounds how many full login handshakes are in flight while
                # preserving a single event loop and one metrics collector.
                semaphore = asyncio.Semaphore(self.args.login_concurrency)

                async def login_concurrently(session: HeadlessSession) -> None:
                    async with semaphore:
                        await session.login()
                        if self.args.login_admission_delay > 0:
                            await asyncio.sleep(self.args.login_admission_delay)

                await asyncio.gather(*(login_concurrently(session) for session in batch))

            print(
                f"Login batch {start + 1}-{end}: "
                f"connected={sum(s.connected for s in self.sessions)} "
                f"failed={self.counters.login_failed}"
            )
            if end < self.args.count:
                await asyncio.sleep(self.args.batch_delay)

        connected = [session for session in self.sessions if session.connected and session.runtime_id is not None]
        if self.args.follow_chain:
            for index, session in enumerate(self.sessions[:-1]):
                target = self.sessions[index + 1]
                if session.connected and target.connected and target.runtime_id is not None:
                    session.target_id = target.runtime_id
                    self.events.write(
                        "follow_chain_link",
                        character=session.character_name,
                        target_character=target.character_name,
                        target_id=target.runtime_id,
                    )
                elif session.connected:
                    self.events.write(
                        "follow_chain_link_unavailable",
                        character=session.character_name,
                        target_character=target.character_name,
                    )

            terminal = self.sessions[-1]
            if terminal.connected:
                if self.args.profile == "attack-follow":
                    terminal.profile = "follow"
                self.events.write(
                    "follow_chain_terminal_waiting",
                    character=terminal.character_name,
                    target_name=terminal.target_name,
                )
        else:
            for index, session in enumerate(connected):
                if len(connected) > 1:
                    paired_index = index + 1 if index % 2 == 0 else index - 1
                    paired_index = min(paired_index, len(connected) - 1)
                    if paired_index == index:
                        paired_index = (index + 1) % len(connected)
                    session.target_id = connected[paired_index].runtime_id
        await asyncio.gather(*(session.start_activity() for session in connected))

    async def resource_loop(self) -> None:
        try:
            while not self.stop_event.is_set():
                await asyncio.sleep(self.args.metrics_interval)
                sim = self.sim_process.sample() or {}
                tfs = self.tfs_process.sample() if self.tfs_process else {}
                tfs = tfs or {}
                connected = sum(session.connected for session in self.sessions)
                row = {
                    "timestamp": datetime.now().astimezone().isoformat(timespec="seconds"),
                    "elapsed_s": round(time.perf_counter() - self.started_at, 3),
                    "attempted": self.counters.attempted,
                    "logged_in_total": self.counters.logged_in,
                    "connected_now": connected,
                    "login_failed": self.counters.login_failed,
                    "disconnected": self.counters.disconnected,
                    "actions": self.counters.actions,
                    "rx_frames": self.counters.rx_frames,
                    "rx_bytes": self.counters.rx_bytes,
                    "tx_frames": self.counters.tx_frames,
                    "tx_bytes": self.counters.tx_bytes,
                    "sim_pid": sim.get("pid", ""),
                    "sim_cpu_one_core_pct": round(float(sim.get("cpu_one_core_pct", 0)), 3),
                    "sim_cpu_host_pct": round(float(sim.get("cpu_host_pct", 0)), 3),
                    "sim_working_set_mb": round(float(sim.get("working_set_mb", 0)), 3),
                    "sim_private_mb": round(float(sim.get("private_mb", 0)), 3),
                    "tfs_pid": tfs.get("pid", ""),
                    "tfs_cpu_one_core_pct": round(float(tfs.get("cpu_one_core_pct", 0)), 3),
                    "tfs_cpu_host_pct": round(float(tfs.get("cpu_host_pct", 0)), 3),
                    "tfs_working_set_mb": round(float(tfs.get("working_set_mb", 0)), 3),
                    "tfs_private_mb": round(float(tfs.get("private_mb", 0)), 3),
                }
                self.metrics_writer.writerow(row)
                self.metrics_file.flush()
                print(
                    f"[{row['elapsed_s']:>7}s] connected={connected}/{self.args.count} "
                    f"fail={self.counters.login_failed} drop={self.counters.disconnected} "
                    f"actions={self.counters.actions} "
                    f"sim={row['sim_cpu_one_core_pct']}%core/{row['sim_working_set_mb']}MB "
                    f"tfs={row['tfs_cpu_one_core_pct']}%core/{row['tfs_working_set_mb']}MB"
                )
        except asyncio.CancelledError:
            pass

    async def stop_batches(self) -> None:
        connected = [session for session in self.sessions if session.connected]
        for start in range(0, len(connected), self.args.stop_batch_size):
            batch = connected[start : start + self.args.stop_batch_size]
            await asyncio.gather(*(session.close() for session in batch))
            if start + len(batch) < len(connected):
                await asyncio.sleep(self.args.stop_batch_delay)
        await asyncio.gather(*(session.close(send_logout=False) for session in self.sessions), return_exceptions=True)

    async def run(self) -> int:
        resource_task = asyncio.create_task(self.resource_loop(), name="resource-monitor")
        try:
            await self.connect_batches()
            if self.counters.login_failed and self.args.fail_on_login_error:
                return 2
            if self.args.duration > 0:
                try:
                    await asyncio.wait_for(self.stop_event.wait(), timeout=self.args.duration)
                except asyncio.TimeoutError:
                    pass
            else:
                await self.stop_event.wait()
            return 0
        finally:
            await self.stop_batches()
            self.stop_event.set()
            resource_task.cancel()
            await asyncio.gather(resource_task, return_exceptions=True)
            self.write_summary()
            self.metrics_file.close()
            self.events.close()

    def write_summary(self) -> None:
        summary = {
            "configuration": {
                key: ("<redacted>" if key == "password" else value)
                for key, value in vars(self.args).items()
            },
            "elapsed_s": time.perf_counter() - self.started_at,
            "attempted": self.counters.attempted,
            "logged_in": self.counters.logged_in,
            "login_failed": self.counters.login_failed,
            "disconnected": self.counters.disconnected,
            "actions": self.counters.actions,
            "rx_frames": self.counters.rx_frames,
            "rx_bytes": self.counters.rx_bytes,
            "tx_frames": self.counters.tx_frames,
            "tx_bytes": self.counters.tx_bytes,
            "login_latency": self.counters.login_latency.summary(),
            "action_response_latency_proxy": self.counters.action_latency.summary(),
            "latency_note": (
                "Action latency is the time from sending an action to the next received server frame. "
                "It is a low-cost response proxy, not a semantic acknowledgement parser."
            ),
        }
        (self.output_dir / "summary.json").write_text(
            json.dumps(summary, ensure_ascii=False, indent=2, default=str),
            encoding="utf-8",
        )
        print(f"Results: {self.output_dir}")
        print(json.dumps(summary, ensure_ascii=False, indent=2, default=str))


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Headless Tibia 7.72 load simulator")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1124)
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--account-start", type=int, default=100001)
    parser.add_argument("--start-index", type=int, default=1)
    parser.add_argument("--character-prefix", default="Teste")
    parser.add_argument("--password", default="123456")
    parser.add_argument(
        "--profile",
        choices=("idle", "movement", "follow", "attack", "attack-follow", "mixed"),
        default="idle",
    )
    parser.add_argument(
        "--follow-chain",
        action="store_true",
        help="make each session follow the next one; the final session follows --follow-terminal-name",
    )
    parser.add_argument(
        "--follow-terminal-name",
        default="",
        help="exact visible creature name for the final session of --follow-chain",
    )
    parser.add_argument("--batch-size", type=int, default=10)
    parser.add_argument("--batch-delay", type=float, default=2.0)
    parser.add_argument(
        "--login-admission-delay",
        type=float,
        default=0.60,
        help="delay between individual sockets required by the TFS per-IP anti-flood",
    )
    parser.add_argument(
        "--login-concurrency",
        type=int,
        default=1,
        help="simultaneous login handshakes; values above 1 require the dedicated TFS load-test launcher",
    )
    parser.add_argument("--stop-batch-size", type=int, default=20)
    parser.add_argument("--stop-batch-delay", type=float, default=0.5)
    parser.add_argument("--duration", type=float, default=300.0, help="seconds; 0 waits for Ctrl+C")
    parser.add_argument("--login-timeout", type=float, default=15.0)
    parser.add_argument("--keepalive-interval", type=float, default=4.0)
    parser.add_argument("--movement-rate", type=float, default=0.8, help="actions per second per movement bot")
    parser.add_argument("--follow-rate", type=float, default=0.1, help="actions per second per follow bot")
    parser.add_argument("--attack-rate", type=float, default=0.1, help="actions per second per attack bot")
    parser.add_argument("--mixed-rate", type=float, default=0.5, help="actions per second per mixed bot")
    parser.add_argument("--action-jitter", type=float, default=0.20, help="fractional random timing jitter")
    parser.add_argument("--metrics-interval", type=float, default=5.0)
    parser.add_argument("--tfs-pid", type=int, default=0)
    parser.add_argument(
        "--output-dir",
        default=str(Path(__file__).resolve().parents[2] / "performance-results" / "headless-load"),
    )
    parser.add_argument("--fail-on-login-error", action="store_true")
    args = parser.parse_args()
    if args.count < 1 or args.count > 10000:
        parser.error("--count must be between 1 and 10000")
    if args.batch_size < 1 or args.stop_batch_size < 1:
        parser.error("batch sizes must be positive")
    if args.login_concurrency < 1 or args.login_concurrency > args.count:
        parser.error("--login-concurrency must be between 1 and --count")
    if args.login_concurrency == 1 and args.login_admission_delay < 0.55:
        parser.error("sequential login requires --login-admission-delay of at least 0.55 seconds")
    if args.login_concurrency > 1 and args.login_admission_delay < 0:
        parser.error("--login-admission-delay cannot be negative")
    if not 0 <= args.action_jitter <= 0.95:
        parser.error("--action-jitter must be between 0 and 0.95")
    if min(args.movement_rate, args.follow_rate, args.attack_rate, args.mixed_rate) < 0:
        parser.error("action rates cannot be negative")
    if args.follow_chain and args.profile not in ("follow", "attack-follow"):
        parser.error("--follow-chain requires --profile follow or attack-follow")
    if args.follow_chain and not args.follow_terminal_name:
        parser.error("--follow-chain requires --follow-terminal-name")
    return args


async def async_main(args: argparse.Namespace) -> int:
    runner = LoadRunner(args)
    try:
        return await runner.run()
    except KeyboardInterrupt:
        runner.stop_event.set()
        return 130


def main() -> int:
    args = parse_arguments()
    try:
        return asyncio.run(async_main(args))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())

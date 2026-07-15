#!/usr/bin/env python3
"""Simulate up to ten Modbus RTU slaves on one USB-RS485 adapter.

The ART-Pi is the bus master.  This process owns the PC serial port and answers
unit IDs 1..10.  Edit the control JSON while it is running to make units go
offline, freeze the process values, or add response delay, for example::

    {"offline_units": [4], "dynamic_values": true, "value_period_ms": 1000}

Supported functions are FC01, FC02, FC03, FC04, FC05 and FC06.
"""

from __future__ import annotations

import argparse
import json
import signal
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Set

try:
    import serial
except ImportError as error:
    raise SystemExit(
        "pyserial is required: python -m pip install pyserial"
    ) from error


def modbus_crc(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def append_crc(data: bytes) -> bytes:
    crc = modbus_crc(data)
    return data + bytes((crc & 0xFF, crc >> 8))


def triangle_delta(step: int, phase: int, amplitude: int) -> int:
    """Return a deterministic -amplitude..+amplitude triangle wave."""
    position = (step + phase) % (amplitude * 4)
    if position <= amplitude * 2:
        return -amplitude + position
    return amplitude * 3 - position


@dataclass
class SlaveState:
    holding_overrides: Dict[int, int] = field(default_factory=dict)
    coil_overrides: Dict[int, bool] = field(default_factory=dict)
    requests: int = 0
    replies: int = 0

    def coil(self, unit_id: int, address: int) -> bool:
        return self.coil_overrides.get(address, ((unit_id + address) & 1) != 0)

    @staticmethod
    def discrete(unit_id: int, address: int, dynamic_step: int | None) -> bool:
        process_bit = 0 if dynamic_step is None else dynamic_step // 5
        return ((unit_id * 3 + address + process_bit) & 1) != 0

    def holding(self, unit_id: int, address: int, dynamic_step: int | None) -> int:
        if address in self.holding_overrides:
            return self.holding_overrides[address]
        value = unit_id * 100 + address
        if dynamic_step is not None:
            value += triangle_delta(dynamic_step, unit_id * 7 + address, 20)
        return value & 0xFFFF

    @staticmethod
    def input_register(
        unit_id: int, address: int, dynamic_step: int | None
    ) -> int:
        value = unit_id * 1000 + address
        if dynamic_step is not None:
            value += triangle_delta(dynamic_step, unit_id * 11 + address * 3, 50)
        return value & 0xFFFF


@dataclass
class ControlState:
    offline_units: Set[int] = field(default_factory=set)
    corrupt_crc_units: Set[int] = field(default_factory=set)
    response_delay_ms: int = 0
    dynamic_values: bool = True
    value_period_ms: int = 1000


class ControlFile:
    def __init__(self, path: Path):
        self.path = path
        self.last_mtime_ns = -1
        self.state = ControlState()

    def refresh(self) -> ControlState:
        try:
            stat = self.path.stat()
        except FileNotFoundError:
            self.last_mtime_ns = -1
            self.state = ControlState()
            return self.state

        if stat.st_mtime_ns == self.last_mtime_ns:
            return self.state

        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
            self.state = ControlState(
                offline_units={int(value) for value in raw.get("offline_units", [])},
                corrupt_crc_units={
                    int(value) for value in raw.get("corrupt_crc_units", [])
                },
                response_delay_ms=max(0, int(raw.get("response_delay_ms", 0))),
                dynamic_values=bool(raw.get("dynamic_values", True)),
                value_period_ms=max(100, int(raw.get("value_period_ms", 1000))),
            )
            self.last_mtime_ns = stat.st_mtime_ns
            print(
                f"control: offline={sorted(self.state.offline_units)} "
                f"corrupt_crc={sorted(self.state.corrupt_crc_units)} "
                f"delay={self.state.response_delay_ms}ms "
                f"dynamic={self.state.dynamic_values} "
                f"period={self.state.value_period_ms}ms",
                flush=True,
            )
        except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
            print(f"control file ignored: {error}", file=sys.stderr, flush=True)
        return self.state


def exception_response(unit_id: int, function: int, code: int) -> bytes:
    return append_crc(bytes((unit_id, function | 0x80, code)))


def read_bits(
    unit_id: int,
    function: int,
    address: int,
    quantity: int,
    state: SlaveState,
    dynamic_step: int | None,
) -> bytes:
    if quantity == 0 or quantity > 2000 or address + quantity > 65536:
        return exception_response(unit_id, function, 0x02)
    packed = bytearray((quantity + 7) // 8)
    for offset in range(quantity):
        value = (
            state.coil(unit_id, address + offset)
            if function == 1
            else state.discrete(unit_id, address + offset, dynamic_step)
        )
        if value:
            packed[offset // 8] |= 1 << (offset % 8)
    return append_crc(bytes((unit_id, function, len(packed))) + packed)


def read_registers(
    unit_id: int,
    function: int,
    address: int,
    quantity: int,
    state: SlaveState,
    dynamic_step: int | None,
) -> bytes:
    if quantity == 0 or quantity > 125 or address + quantity > 65536:
        return exception_response(unit_id, function, 0x02)
    payload = bytearray((unit_id, function, quantity * 2))
    for offset in range(quantity):
        value = (
            state.holding(unit_id, address + offset, dynamic_step)
            if function == 3
            else state.input_register(unit_id, address + offset, dynamic_step)
        )
        payload.extend((value >> 8, value & 0xFF))
    return append_crc(bytes(payload))


def handle_request(
    frame: bytes,
    slaves: Dict[int, SlaveState],
    dynamic_step: int | None = None,
) -> bytes | None:
    if len(frame) != 8 or modbus_crc(frame[:-2]) != int.from_bytes(frame[-2:], "little"):
        return None

    unit_id = frame[0]
    function = frame[1]
    if unit_id not in slaves:
        return None

    state = slaves[unit_id]
    state.requests += 1
    address = int.from_bytes(frame[2:4], "big")
    value = int.from_bytes(frame[4:6], "big")

    if function in (1, 2):
        response = read_bits(unit_id, function, address, value, state, dynamic_step)
    elif function in (3, 4):
        response = read_registers(
            unit_id, function, address, value, state, dynamic_step
        )
    elif function == 5:
        if value not in (0x0000, 0xFF00):
            response = exception_response(unit_id, function, 0x03)
        else:
            state.coil_overrides[address] = value == 0xFF00
            response = frame
    elif function == 6:
        state.holding_overrides[address] = value
        response = frame
    else:
        response = exception_response(unit_id, function, 0x01)

    state.replies += 1
    return response


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3", help="USB-RS485 serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--units", type=int, default=10, choices=range(1, 11))
    parser.add_argument(
        "--control",
        type=Path,
        default=Path(__file__).with_name("modbus_sim_control.json"),
    )
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="validate dynamic values and write overrides without opening a port",
    )
    return parser.parse_args()


def register_value(response: bytes, index: int = 0) -> int:
    offset = 3 + index * 2
    return int.from_bytes(response[offset : offset + 2], "big")


def run_self_test() -> int:
    slaves = {1: SlaveState()}
    read_input = append_crc(bytes((1, 4, 0, 200, 0, 1)))
    static_response = handle_request(read_input, slaves)
    first_response = handle_request(read_input, slaves, dynamic_step=0)
    second_response = handle_request(read_input, slaves, dynamic_step=7)
    assert static_response is not None
    assert first_response is not None
    assert second_response is not None
    assert register_value(static_response) == 1200
    assert register_value(first_response) != register_value(second_response)

    write_holding = append_crc(bytes((1, 6, 0, 100, 0x12, 0x34)))
    read_holding = append_crc(bytes((1, 3, 0, 100, 0, 1)))
    assert handle_request(write_holding, slaves, dynamic_step=10) == write_holding
    holding_response = handle_request(read_holding, slaves, dynamic_step=30)
    assert holding_response is not None
    assert register_value(holding_response) == 0x1234
    print(
        "PASS: dynamic input values change and FC06 holding overrides remain stable.",
        flush=True,
    )
    return 0


def main() -> int:
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()

    slaves = {unit_id: SlaveState() for unit_id in range(1, arguments.units + 1)}
    control_file = ControlFile(arguments.control)
    running = True
    rx_buffer = bytearray()
    simulation_started = time.monotonic()

    def stop(_signum: int, _frame: object) -> None:
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    with serial.Serial(
        port=arguments.port,
        baudrate=arguments.baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.02,
        write_timeout=0.2,
    ) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        print(
            f"Modbus RTU simulator: {arguments.port} {arguments.baud} 8N1, "
            f"units=1..{arguments.units}, control={arguments.control}",
            flush=True,
        )

        while running:
            incoming = port.read(max(1, port.in_waiting))
            if incoming:
                rx_buffer.extend(incoming)

            while len(rx_buffer) >= 8:
                frame = bytes(rx_buffer[:8])
                if modbus_crc(frame[:-2]) != int.from_bytes(frame[-2:], "little"):
                    del rx_buffer[0]
                    continue
                del rx_buffer[:8]

                control = control_file.refresh()
                unit_id = frame[0]
                function = frame[1]
                if unit_id in control.offline_units:
                    if not arguments.quiet:
                        print(f"RX unit={unit_id:3d} fc={function:02X} -> OFFLINE", flush=True)
                    continue

                dynamic_step = None
                if control.dynamic_values:
                    dynamic_step = int(
                        (time.monotonic() - simulation_started)
                        * 1000
                        / control.value_period_ms
                    )
                response = handle_request(frame, slaves, dynamic_step)
                if response is None:
                    continue
                if control.response_delay_ms:
                    time.sleep(control.response_delay_ms / 1000.0)
                if unit_id in control.corrupt_crc_units:
                    response = response[:-1] + bytes((response[-1] ^ 0x01,))
                port.write(response)
                port.flush()
                if not arguments.quiet:
                    print(
                        f"RX unit={unit_id:3d} fc={function:02X} "
                        f"addr={int.from_bytes(frame[2:4], 'big'):5d} "
                        f"value={int.from_bytes(frame[4:6], 'big'):5d} "
                        f"TX={response.hex(' ')}",
                        flush=True,
                    )

    total_requests = sum(state.requests for state in slaves.values())
    total_replies = sum(state.replies for state in slaves.values())
    print(f"stopped: requests={total_requests}, replies={total_replies}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

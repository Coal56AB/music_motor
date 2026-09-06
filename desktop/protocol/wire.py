import struct
import time
from dataclasses import dataclass
from enum import IntEnum

MAGIC = b"\xa5\x5a"
MAX_PAYLOAD = 240
QUEUE_CAPACITY = 256


class Command(IntEnum):
    PING = 1
    GET_INFO = 2
    GET_STATUS = 3
    EMERGENCY_STOP = 4
    SET_MASK = 5
    ENABLE = 6
    START = 7
    STOP = 8
    FREQUENCY = 9
    NOTE = 10
    DIR = 11
    MICROSTEP = 12
    SLEEP = 13
    RESET = 14
    RESET_PULSE = 15
    STREAM_START = 16
    STREAM_STOP = 17
    EVENTS = 18
    CLEAR = 19
    QUEUE = 20
    ACK = 128
    NAK = 129


class Error(IntEnum):
    OK = 0
    LENGTH = 1
    COMMAND = 2
    VALUE = 3
    STATE = 4
    FULL = 5
    ORDER = 6
    CRC = 7
    TIMEOUT = 8
    UART = 9
    UNDERRUN = 10
    OVERRUN = 11


@dataclass
class Frame:
    seq: int
    command: int
    payload: bytes = b""


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def encode(frame):
    if len(frame.payload) > MAX_PAYLOAD:
        raise ValueError("Payload exceeds 240 bytes")
    body = struct.pack("<BBB", len(frame.payload), frame.seq, frame.command) + frame.payload
    return MAGIC + body + struct.pack("<H", crc16(body))


class Parser:
    def __init__(self):
        self.buffer = bytearray()
        self.last = 0.0
        self.errors = 0

    def feed(self, data, now=None):
        now = time.monotonic() if now is None else now
        if self.buffer and now - self.last > 0.100:
            self.buffer.clear()
            self.errors += 1
        if data:
            self.last = now
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 2:
            if self.buffer[:2] != MAGIC:
                del self.buffer[0]
                continue
            if len(self.buffer) < 5:
                break
            length = self.buffer[2]
            if length > MAX_PAYLOAD:
                del self.buffer[0]
                self.errors += 1
                continue
            total = length + 7
            if len(self.buffer) < total:
                break
            packet = self.buffer[:total]
            if crc16(packet[2:-2]) != struct.unpack("<H", packet[-2:])[0]:
                del self.buffer[0]
                self.errors += 1
                continue
            frames.append(Frame(packet[3], packet[4], bytes(packet[5:-2])))
            del self.buffer[:total]
        return frames


def event_bytes(ms, motor, op, value=0):
    return struct.pack("<IBBI", ms, motor, op, value)


def decode_status(data):
    if len(data) != 52:
        raise ValueError("Invalid status size")
    mask, sleep, reset, raw, running, error, used, pos, faults = struct.unpack("<6BHII", data[:16])
    motors = []
    for i in range(6):
        flags, note, mhz = struct.unpack_from("<BBI", data, 16 + 6 * i)
        motors.append(
            dict(
                enabled=bool(flags & 1),
                active=bool(flags & 2),
                direction=bool(flags & 4),
                note=note,
                frequency=mhz / 1000,
            )
        )
    return dict(
        mask=mask,
        sleep=bool(sleep),
        reset=bool(reset),
        raw=raw,
        running=bool(running),
        error=error,
        used=used,
        position=pos,
        faults=faults,
        motors=motors,
    )

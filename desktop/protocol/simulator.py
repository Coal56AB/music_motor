"""Deterministic device model. Speaks frames, including ACK/NAK, like the MCU."""
import struct
import time
import math
from collections import deque
from protocol.wire import Command as C, Error as E, Frame, Parser, encode
from app.music_math import actual_frequency, note_frequency


class SimulatedSTM32:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.parser = Parser()
        self.mask, self.sleep, self.reset, self.raw = 15, True, True, 0
        self.running, self.error, self.faults, self.position = False, 0, 0, 0
        self.motors = [
            dict(enabled=False, active=False, direction=False, note=69, frequency=440.0)
            for _ in range(6)
        ]
        self.queue = deque()
        self.last_at = 0
        self.end_queued = False
        self.ready_at = self.clock()
        self.last_command = self.clock()
        self.origin = 0
        self.pulse_at = None
        self.previous = None
        self.previous_reply = b""
        self.previous_at = 0

    def clear(self):
        self.queue.clear()
        self.last_at, self.end_queued = 0, False

    def stop(self, disable=True):
        self.running = False
        self.clear()
        for m in self.motors:
            m["active"] = False
            if disable:
                m["enabled"] = False

    def estop(self):
        self.stop(True)
        self.sleep = self.reset = True
        self.pulse_at = None

    def fault(self, error):
        self.estop()
        self.error = int(error)
        self.faults += 1

    def tick(self):
        now = self.clock()
        if self.pulse_at is not None and now >= self.pulse_at:
            self.reset, self.pulse_at = False, None
            self.ready_at = now + 0.002
        if (self.running or any(m["enabled"] for m in self.motors)) and now - self.last_command > 2:
            self.fault(E.TIMEOUT)
        if not self.running or now < self.origin:
            return
        self.position = int((now - self.origin) * 1000)
        while self.queue and self.queue[0][0] <= self.position:
            at, m, op, value = self.queue.popleft()
            if op == 2:
                self.stop(False)
                return
            if op == 0:
                self.motors[m]["active"] = False
            else:
                if not self.motors[m]["enabled"] or self.sleep or self.reset:
                    self.fault(E.STATE)
                    return
                self.motors[m].update(
                    active=True,
                    frequency=actual_frequency(value / 1000),
                    note=round(69 + 12 * math.log2(value / 440000)),
                )
        if not self.queue:
            self.fault(E.UNDERRUN)

    def status_bytes(self):
        out = struct.pack(
            "<6BHII",
            self.mask,
            self.sleep,
            self.reset,
            self.raw,
            self.running,
            self.error,
            len(self.queue),
            self.position,
            self.faults,
        )
        for m in self.motors:
            flags = int(m["enabled"]) | int(m["active"]) << 1 | int(m["direction"]) << 2
            out += struct.pack("<BBI", flags, m["note"], round(m["frequency"] * 1000))
        return out

    def command(self, cmd, p):
        lengths = [0, 0, 0, 0, 0, 1, 2, 1, 1, 5, 2, 2, 1, 1, 1, 0, 0, 1, 255, 0, 0]
        if cmd < 1 or cmd > 20:
            return E.COMMAND, b""
        if cmd != C.EVENTS and len(p) != lengths[cmd]:
            return E.LENGTH, b""
        self.last_command = self.clock()
        motor = p[0] if p else 0
        if C.ENABLE <= cmd <= C.DIR:
            if motor >= 6:
                return E.VALUE, b""
            if not self.mask & (1 << motor) or self.running:
                return E.STATE, b""
        if self.running and cmd in (C.SET_MASK, C.MICROSTEP, C.STREAM_START):
            return E.STATE, b""
        if cmd == C.PING:
            return E.OK, b"OK"
        if cmd == C.GET_INFO:
            return E.OK, b"MusicMotor 1.0 SIM; protocol=1; motors=6; timer=1000000; queue=256"
        if cmd == C.GET_STATUS:
            return E.OK, self.status_bytes()
        if cmd == C.EMERGENCY_STOP:
            self.estop()
            self.error = 0
        elif cmd == C.SET_MASK:
            if motor > 63:
                return E.VALUE, b""
            self.mask = motor
            self.clear()
            for i, m in enumerate(self.motors):
                if not motor & (1 << i):
                    m.update(enabled=False, active=False)
        elif cmd == C.ENABLE:
            if p[1] > 1:
                return E.VALUE, b""
            self.motors[motor]["enabled"] = bool(p[1])
            if not p[1]:
                self.motors[motor]["active"] = False
        elif cmd == C.START:
            if (
                self.sleep
                or self.reset
                or not self.motors[motor]["enabled"]
                or self.clock() < self.ready_at
            ):
                return E.STATE, b""
            self.motors[motor]["active"] = True
            self.motors[motor]["frequency"] = actual_frequency(self.motors[motor]["frequency"])
        elif cmd == C.STOP:
            self.motors[motor]["active"] = False
        elif cmd in (C.FREQUENCY, C.NOTE):
            if cmd == C.NOTE and p[1] > 127:
                return E.VALUE, b""
            f = note_frequency(p[1]) if cmd == C.NOTE else struct.unpack("<I", p[1:])[0] / 1000
            try:
                f = actual_frequency(f)
            except ValueError:
                return E.VALUE, b""
            self.motors[motor].update(frequency=f, note=p[1] if cmd == C.NOTE else 255)
        elif cmd == C.DIR:
            if p[1] > 1:
                return E.VALUE, b""
            if self.motors[motor]["active"]:
                return E.STATE, b""
            self.motors[motor]["direction"] = bool(p[1])
            self.ready_at = self.clock() + 0.002
        elif cmd == C.MICROSTEP:
            if motor > 7:
                return E.VALUE, b""
            if any(m["active"] for m in self.motors):
                return E.STATE, b""
            self.raw = motor
        elif cmd in (C.SLEEP, C.RESET):
            if motor > 1:
                return E.VALUE, b""
            if motor:
                self.stop(False)
            if cmd == C.SLEEP:
                self.sleep = bool(motor)
            else:
                self.reset, self.pulse_at = bool(motor), None
            self.ready_at = self.clock() + 0.002
        elif cmd == C.RESET_PULSE:
            self.stop(False)
            self.reset = True
            self.pulse_at = self.clock() + 0.002
        elif cmd == C.STREAM_START:
            if self.sleep or self.reset or not self.queue or self.clock() < self.ready_at:
                return E.STATE, b""
            for m in self.motors:
                m["active"] = False
            self.running, self.position, self.error = True, 0, 0
            self.origin = self.clock() + 0.100
        elif cmd == C.STREAM_STOP:
            if motor > 1:
                return E.VALUE, b""
            self.stop(bool(motor))
        elif cmd == C.CLEAR:
            if self.running:
                return E.STATE, b""
            self.clear()
        elif cmd == C.QUEUE:
            return E.OK, struct.pack("<H", 256 - len(self.queue))
        elif cmd == C.EVENTS:
            if not p or len(p) % 10:
                return E.LENGTH, b""
            batch = [struct.unpack_from("<IBBI", p, i) for i in range(0, len(p), 10)]
            if len(batch) > 256 - len(self.queue):
                return E.FULL, b""
            previous, end = self.last_at, self.end_queued
            for at, m, op, value in batch:
                if end or at < previous or at > 86400000 or self.running and at < self.position:
                    return E.ORDER, b""
                if op > 2:
                    return E.VALUE, b""
                if op == 2:
                    if m != 255 or value:
                        return E.VALUE, b""
                    end = True
                elif m >= 6 or not self.mask & (1 << m):
                    return E.STATE, b""
                elif op == 1 and not 20000 <= value <= 4000000 or op == 0 and value:
                    return E.VALUE, b""
                previous = at
            self.queue.extend(batch)
            self.last_at, self.end_queued = previous, end
            return E.OK, struct.pack("<H", 256 - len(self.queue))
        return E.OK, b""

    def exchange(self, data):
        self.tick()
        output = b""
        old_errors = self.parser.errors
        for frame in self.parser.feed(data, self.clock()):
            key = (frame.seq, frame.command, frame.payload)
            if key == self.previous and self.clock() - self.previous_at < 1:
                output += self.previous_reply
                continue
            error, body = self.command(frame.command, frame.payload)
            if error:
                self.error = int(error)
                self.faults += 1
            reply = encode(
                Frame(frame.seq, C.NAK if error else C.ACK, bytes([frame.command, error]) + body)
            )
            self.previous, self.previous_reply, self.previous_at = key, reply, self.clock()
            output += reply
        if self.parser.errors != old_errors:
            self.error = E.CRC
            self.faults += self.parser.errors - old_errors
        return output

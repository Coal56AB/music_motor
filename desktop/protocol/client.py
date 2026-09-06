"""Single outstanding transaction; exact retries make mutating commands idempotent."""
import time
from collections import deque
import serial
from PySide2.QtCore import QObject, QTimer, Signal
from protocol.wire import Frame, Parser, Command as C, Error, encode, decode_status
from protocol.simulator import SimulatedSTM32

ERROR_TEXT = {
    1: "неверная длина команды",
    2: "неизвестная команда",
    3: "значение вне допустимого диапазона",
    4: "команда недоступна: проверьте ENABLE, SLEEP, RESET и состояние потока",
    5: "очередь STM32 заполнена",
    6: "нарушен порядок событий или событие уже в прошлом",
    7: "повреждён пакет CRC",
    8: "превышено время ожидания",
    9: "аппаратная ошибка UART",
    10: "закончились события до конца композиции",
    11: "пропущен срок обновления таймера STEP",
}


class Client(QObject):
    status = Signal(object)
    info = Signal(str)
    log = Signal(str)
    fault = Signal(str)
    connection = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.serial = self.sim = None
        self.connected = False
        self.parser = Parser()
        self.commands = deque()
        self.pending = None
        self.seq = 0
        self.last_ping = self.last_status = 0
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll)
        self.timer.start(5)

    def connect_device(self, port="", baud=115200, simulation=True):
        self.close()
        try:
            if simulation:
                self.sim = SimulatedSTM32()
            else:
                self.serial = serial.Serial(port, baud, timeout=0, write_timeout=0.1)
        except (serial.SerialException, OSError) as exc:
            self.fault.emit(str(exc))
            return
        self.parser = Parser()
        self.connected = True
        self.connection.emit(True)
        # Connecting always disarms any previous session.
        self.send(C.EMERGENCY_STOP)
        self.send(C.GET_INFO, callback=lambda b: self.info.emit(b.decode("ascii", "replace")))
        self.last_ping = self.last_status = 0

    def close(self):
        if self.connected:
            # Best-effort E-stop before closing; independent hardware watchdog is the fallback.
            try:
                self._write(encode(Frame((self.seq + 1) & 255, C.EMERGENCY_STOP)))
            except (serial.SerialException, OSError):
                pass
        if self.serial:
            self.serial.close()
        self.serial = self.sim = None
        self.commands.clear()
        self.pending = None
        self.connected = False
        self.connection.emit(False)

    def send(self, command, payload=b"", callback=None, urgent=False):
        if not self.connected:
            self.fault.emit("Нет соединения с устройством")
            return
        item = (int(command), bytes(payload), callback)
        if urgent:
            self.commands.clear()
            self.pending = None
            self.commands.appendleft(item)
        else:
            self.commands.append(item)

    def emergency(self):
        self.send(C.EMERGENCY_STOP, urgent=True)

    def _write(self, packet):
        self.log.emit(
            "TX #%d %s | " % (packet[3], C(packet[4]).name) + " ".join("%02x" % b for b in packet)
        )
        if self.sim:
            return self.sim.exchange(packet)
        written = self.serial.write(packet)
        if written != len(packet):
            raise OSError("Неполная запись UART")
        return b""

    def _receive(self, data):
        if data:
            self.log.emit("RX " + " ".join("%02x" % b for b in data))
        for f in self.parser.feed(data):
            if not self.pending or f.seq != self.pending["seq"] or len(f.payload) < 2:
                continue
            p = self.pending
            if f.payload[0] != p["command"] or f.command not in (C.ACK, C.NAK):
                continue
            self.pending = None
            self.log.emit("%s #%d %s" % (C(f.command).name, f.seq, C(p["command"]).name))
            if f.command == C.NAK or f.payload[1]:
                try:
                    reason = Error(f.payload[1]).name + ": " + ERROR_TEXT.get(f.payload[1], "")
                except ValueError:
                    reason = str(f.payload[1])
                self.commands.clear()
                self.fault.emit("%s: %s" % (C(p["command"]).name, reason))
                # Cancel the remaining transaction chain; never run START after a rejected setup.
                self.send(C.EMERGENCY_STOP, urgent=True)
            elif p["callback"]:
                p["callback"](f.payload[2:])

    def poll(self):
        if not self.connected:
            return
        now = time.monotonic()
        try:
            if self.sim:
                self.sim.tick()
            elif self.serial.in_waiting:
                self._receive(self.serial.read(self.serial.in_waiting))
            if self.pending and now - self.pending["sent"] > 0.25:
                if self.pending["tries"] >= 3:
                    self.fault.emit("UART: нет ACK после трёх попыток")
                    self.close()
                    return
                self.pending["tries"] += 1
                self.pending["sent"] = now
                self._receive(self._write(self.pending["packet"]))
            if now - self.last_ping > 0.5 and len(self.commands) < 8:
                self.send(C.PING)
                self.last_ping = now
            if now - self.last_status > 0.1 and len(self.commands) < 8:
                self.send(C.GET_STATUS, callback=lambda b: self.status.emit(decode_status(b)))
                self.last_status = now
            if not self.pending and self.commands:
                cmd, payload, callback = self.commands.popleft()
                self.seq = (self.seq + 1) & 255
                packet = encode(Frame(self.seq, cmd, payload))
                self.pending = dict(
                    seq=self.seq, command=cmd, packet=packet, callback=callback, sent=now, tries=1
                )
                self._receive(self._write(packet))
        except (serial.SerialException, OSError, ValueError) as exc:
            self.fault.emit(str(exc))
            self.close()

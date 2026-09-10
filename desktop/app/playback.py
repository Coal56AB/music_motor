import struct
from PySide2.QtCore import QObject, QTimer, Signal
from protocol.wire import Command as C, event_bytes
from app.music_math import note_frequency


def short_gap_note(segments, motor, position, threshold):
    previous = upcoming = None
    for start, end, voice, uid, pitch in segments:
        if voice != motor:
            continue
        if start <= position < end:
            return None
        if end <= position and (previous is None or end > previous[0]):
            previous = (end, pitch)
        if start > position and (upcoming is None or start < upcoming):
            upcoming = start
    if previous is not None and upcoming is not None and 0 < upcoming - previous[0] <= threshold:
        return previous[1]
    return None


class StreamPlayer(QObject):
    changed = Signal(str)
    progress = Signal(int, int)
    failed = Signal(str)

    def __init__(self, client, parent=None):
        super().__init__(parent)
        self.client = client
        self.state = "stopped"
        self.allocation = None
        self.index = self.offset = self.position = 0
        self.events = []
        self.inflight = False
        self.free = 256
        self.disable_after_stop = True
        self.lookahead = 1500
        self.epoch = 0
        self.before_start = None
        client.status.connect(self.on_status)
        client.fault.connect(self.on_fault)

    def set_state(self, value):
        self.state = value
        self.changed.emit(value)

    def play(self, allocation, config, start_ms=0):
        if not self.client.connected:
            self.failed.emit("Сначала подключите UART или симулятор")
            return
        self.allocation = allocation
        self.disable_after_stop = config["disable_after_stop"]
        self.lookahead = config["lookahead_ms"]
        self.config = config.copy()
        self.offset = max(0, min(int(start_ms), max(0, allocation.duration_ms - 1)))
        self.begin()

    def seek(self, position):
        if not self.allocation or self.state == 'stopped':
            return
        self.offset = self.position = max(0, min(int(position), max(0, self.allocation.duration_ms - 1)))
        if self.state in ('playing', 'preparing'):
            self.begin()
        self.progress.emit(self.position, 0)

    def begin(self):
        self.epoch += 1
        epoch = self.epoch
        self.events = [
            (t - self.offset, m, op, v)
            for t, m, op, v in self.allocation.events
            if t >= self.offset
        ]
        for start, end, motor, uid, pitch in self.allocation.segments:
            if start < self.offset < end:
                self.events.append((0, motor, 1, round(note_frequency(pitch) * 1000)))
        self.events.sort(key=lambda e: (e[0], e[2], e[1]))
        self.index = 0
        self.free = 256
        self.inflight = False
        self.position = self.offset
        self.set_state("preparing")
        self.client.send(C.STREAM_STOP, b"\x01", urgent=True)
        self.client.send(C.SET_MASK, bytes([self.config["installed_mask"]]))
        self.client.send(C.MICROSTEP, bytes([self.config["microstep_raw"]]))
        self.client.send(C.RESET, b"\x00")
        self.client.send(C.SLEEP, b"\x00")
        participating = self.config["installed_mask"] & self.config["music_mask"]
        for i in range(6):
            if self.config["installed_mask"] & (1 << i):
                self.client.send(C.DIR, bytes([i, self.config["directions"][i]]))
                self.client.send(C.ENABLE, bytes([i, int(bool(participating & (1 << i)))]))
        self.client.send(C.CLEAR, callback=lambda _: self.prefill(epoch))

    def prefill(self, epoch):
        if epoch != self.epoch:
            return
        self.fill(True)

    def fill(self, initial=False):
        if self.inflight or self.state not in ("preparing", "playing"):
            return
        if self.index >= len(self.events) or self.free < 24:
            if initial:
                self.start_stream()
            return
        # Fill until either the time horizon is covered or ~192 slots are used.
        local_position = self.position - self.offset
        buffered_until = self.events[self.index - 1][0] if self.index else -1
        if self.index and (buffered_until >= local_position + self.lookahead or self.free <= 64):
            if initial:
                self.start_stream()
            return
        batch = self.events[self.index : self.index + min(24, self.free)]
        epoch = self.epoch
        self.inflight = True

        def accepted(data):
            if epoch != self.epoch:
                return
            self.inflight = False
            self.index += len(batch)
            self.free = struct.unpack("<H", data)[0]
            self.fill(initial)

        self.client.send(
            C.EVENTS, b"".join(event_bytes(*event) for event in batch), callback=accepted
        )

    def start_stream(self):
        epoch = self.epoch

        def start():
            if epoch != self.epoch or self.state != "preparing":
                return
            def begin_stream():
                if epoch == self.epoch and self.state == 'preparing':
                    self.client.send(C.STREAM_START,
                        callback=lambda _: self.set_state("playing") if epoch == self.epoch else None)
            if self.before_start:
                self.before_start(begin_stream)
            else:
                begin_stream()

        QTimer.singleShot(5, start)

    def on_status(self, status):
        if self.state == "playing":
            self.position = status["position"] + self.offset
            self.free = 256 - status["used"]
            self.progress.emit(self.position, status["used"])
            if not status["running"]:
                if status["error"]:
                    self.on_fault("Поток остановлен STM32, код %d" % status["error"])
                else:
                    self.stop()
                return
            self.fill()

    def pause(self):
        if self.state != "playing":
            return
        self.offset = self.position
        self.epoch += 1
        self.set_state("paused")
        self.client.send(C.STREAM_STOP, bytes([int(self.disable_after_stop)]), urgent=True)

    def resume(self):
        if self.state == "paused":
            self.begin()

    def stop(self, emergency=False, disable=None):
        self.epoch += 1
        self.inflight = False
        self.set_state("stopped")
        if self.client.connected:
            if emergency:
                self.client.emergency()
            else:
                self.client.send(C.STREAM_STOP, bytes([int(self.disable_after_stop if disable is None else disable)]), urgent=True)

    def on_fault(self, text):
        if self.state != "stopped":
            self.epoch += 1
            self.set_state("stopped")
            self.failed.emit(text)

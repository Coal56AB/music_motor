from PySide2.QtCore import Qt, QRectF, Signal
from PySide2.QtGui import QColor, QPainter, QPen
from PySide2.QtWidgets import QWidget, QSizePolicy
from midi.allocator import MOTOR_COLORS


class MidiOverview(QWidget):
    """Motor voices with a seekable playback cursor."""
    seek_requested = Signal(int)
    def __init__(self, parent=None):
        super().__init__(parent)
        self.segments = []
        self.duration_ms = 0
        self.position_ms = 0
        self.mask = 0
        self.dragging = False
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip('Щёлкните или перетащите курсор, чтобы выбрать позицию воспроизведения')
        self.setMinimumHeight(104)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

    def set_allocation(self, allocation, mask):
        self.segments = allocation.segments if allocation else []
        self.duration_ms = allocation.duration_ms if allocation else 0
        self.mask = mask
        self.position_ms = 0
        self.update()

    def set_position(self, ms):
        if self.dragging:
            return
        self.position_ms = max(0, ms)
        self.update()

    def time_window(self):
        span = min(30000, max(8000, self.duration_ms))
        start = max(0, min(self.position_ms - span * .25, self.duration_ms - span))
        return start, span

    def cursor_at(self, pos):
        start, span = self.drag_window
        self.position_ms = round(max(0, min(max(0, self.duration_ms - 1),
                               start + (pos.x() - 12) / max(1, self.width() - 24) * span)))
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton and self.segments and event.pos().y() >= 24:
            self.drag_window = self.time_window()
            self.dragging = True
            self.cursor_at(event.pos())
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self.dragging:
            self.cursor_at(event.pos())
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self.dragging and event.button() == Qt.LeftButton:
            self.cursor_at(event.pos())
            self.dragging = False
            self.seek_requested.emit(self.position_ms)
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(QPen(QColor('#34475e'), 1))
        p.setBrush(QColor('#101a28'))
        p.drawRoundedRect(self.rect().adjusted(1, 1, -1, -1), 8, 8)
        p.setPen(QColor('#b9c9dd'))
        p.drawText(12, 20, 'MIDI · партии моторов')
        seconds = self.position_ms // 1000
        total = self.duration_ms // 1000
        p.drawText(self.rect().adjusted(0, 5, -12, 0), Qt.AlignTop | Qt.AlignRight,
                   '%02d:%02d / %02d:%02d' % (seconds//60, seconds%60, total//60, total%60))
        if not self.segments:
            p.drawText(self.rect(), Qt.AlignCenter, 'Откройте MIDI в редакторе')
            return
        left, top, width = 12, 38, max(1, self.width() - 24)
        height = self.height() - top - 10
        pitches = [pitch for a, b, motor, uid, pitch in self.segments]
        low, high = min(pitches) - 2, max(pitches) + 2
        pitch_height = min(3, height / (high - low + 1))
        start, span = self.drag_window if self.dragging else self.time_window()
        def x(ms):
            return left + (ms - start) / span * width
        for i in range(5):
            at = start + i * span / 4
            p.setPen(QColor('#71849d'))
            p.drawText(int(x(at)), 33, '%d:%02d' % (at//60000, at//1000%60))
        p.fillRect(QRectF(left, top, width, height), QColor('#172234'))
        p.save()
        p.setClipRect(QRectF(left, top, width, self.height()-top-10))
        for a, b, motor, uid, pitch in self.segments:
            if not self.mask & (1 << motor) or b < start or a > start + span:
                continue
            box = QRectF(x(a), top + (high - pitch) / (high - low + 1) * height,
                         max(2, x(b)-x(a)), pitch_height)
            active = a <= self.position_ms < b
            color = QColor(MOTOR_COLORS[motor])
            color.setAlpha(255 if active else 150)
            p.setBrush(color)
            p.setPen(QColor('#ffffff') if active else Qt.NoPen)
            p.drawRect(box)
        p.setPen(QPen(QColor('#f3f7ff'), 2))
        p.drawLine(int(x(self.position_ms)), top, int(x(self.position_ms)), self.height()-10)
        p.restore()

import math
from PySide2.QtCore import Qt, QPointF, Signal
from PySide2.QtGui import QPainter, QColor, QPen, QPolygonF, QFont
from PySide2.QtWidgets import (
    QWidget,
    QFrame,
    QLabel,
    QCheckBox,
    QDoubleSpinBox,
    QComboBox,
    QPushButton,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QProgressBar,
    QLineEdit,
)
from app.music_math import note_name, rpm
from midi.allocator import MOTOR_COLORS


class MotorDrawing(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.color = None
        self.setMinimumSize(86, 86)
        self.setMaximumSize(90, 90)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        size = min(self.width(), self.height())
        p.translate((self.width() - size) / 2, (self.height() - size) / 2)
        p.scale(size / 100, size / 100)
        points = [(18, 5), (82, 5), (95, 18), (95, 82), (82, 95), (18, 95), (5, 82), (5, 18)]
        p.setPen(QPen(QColor("#aab7c8"), 1.6))
        p.setBrush(QColor(self.color) if self.color else Qt.NoBrush)
        p.drawPolygon(QPolygonF([QPointF(x, y) for x, y in points]))
        for x, y in [(18, 18), (82, 18), (18, 82), (82, 82)]:
            p.setBrush(QColor("#151c27"))
            p.drawEllipse(QPointF(x, y), 5.4, 5.4)
            p.setPen(QPen(QColor("#8090a4"), 0.8))
            p.drawEllipse(QPointF(x, y), 3.6, 3.6)
        p.setPen(QPen(QColor("#d0d8e2"), 1.5))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QPointF(50, 50), 29, 29)
        p.drawEllipse(QPointF(50, 50), 21, 21)
        p.setBrush(QColor("#d4dde9"))
        p.drawEllipse(QPointF(50, 50), 6, 6)
        p.setBrush(QColor("#768599"))
        p.drawEllipse(QPointF(50, 50), 3.6, 3.6)


class MotorCard(QFrame):
    command = Signal(int, str, object)
    config_changed = Signal()

    def __init__(self, index, config, parent=None):
        super().__init__(parent)
        self.index, self.config = index, config
        self.setObjectName("motorCard")
        self.setMinimumWidth(305)
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 8, 12, 8)
        root.setSpacing(4)
        header = QHBoxLayout()
        number = QLabel("M%d" % (index + 1))
        number.setStyleSheet("font-size:16px;font-weight:700;color:%s" % MOTOR_COLORS[index])
        self.name = QLabel(config["names"][index])
        header.addWidget(number)
        header.addWidget(self.name, 1)
        root.addLayout(header)
        display = QHBoxLayout()
        self.drawing = MotorDrawing()
        display.addWidget(self.drawing)
        info = QVBoxLayout()
        info.setSpacing(3)
        self.state_label = QLabel("○ НЕ ПОДКЛЮЧЁН")
        self.state_label.setWordWrap(True)
        self.state_label.setStyleSheet("font-size:10px;font-weight:600")
        self.pitch = QLabel("—")
        self.pitch.setStyleSheet("font-size:23px;font-weight:600")
        self.frequency = QLabel("440.000 Гц STEP")
        self.rpm_label = QLabel("132.00 об/мин · расчёт")
        self.indicators = QLabel("EN ○   STEP ○   DIR ↻")
        for w in [self.state_label, self.pitch, self.frequency, self.rpm_label, self.indicators]:
            info.addWidget(w)
        display.addLayout(info, 1)
        root.addLayout(display)
        self.speed_bar = QProgressBar()
        self.speed_bar.setRange(0, 1000)
        self.speed_bar.setTextVisible(False)
        self.speed_bar.setFixedHeight(5)
        self.speed_bar.setStyleSheet("QProgressBar::chunk {background:%s}" % MOTOR_COLORS[index])
        root.addWidget(self.speed_bar)
        controls = QGridLayout()
        controls.setSpacing(5)
        self.hz = QDoubleSpinBox()
        self.hz.setRange(20, 4000)
        self.hz.setDecimals(3)
        self.hz.setValue(440)
        self.hz.setSuffix(" Гц")
        apply = QPushButton("Задать")
        apply.clicked.connect(lambda: self.command.emit(index, "frequency", self.hz.value()))
        self.note = QComboBox()
        for n in range(16, 108):
            self.note.addItem(note_name(n), n)
        self.note.setCurrentIndex(self.note.findData(69))
        # Separate closures per card avoid Nuitka/PySide2 binding all instances
        # of a bound method to the first motor. Read values without signal args.
        self.note.activated[int].connect(
            lambda *_args: self.command.emit(index, "note", self.note.currentData())
        )
        self.direction = QComboBox()
        self.direction.addItems(["↻ DIR 0", "↺ DIR 1"])
        self.direction.setCurrentIndex(config["directions"][index])
        self.direction.activated[int].connect(
            lambda *_args: self.command.emit(index, "direction", self.direction.currentIndex())
        )
        self.enable = QCheckBox("ENABLE")
        self.enable.clicked.connect(
            lambda *_args: self.command.emit(index, "enable", self.enable.isChecked())
        )
        self.start = QPushButton("▶ STEP")
        self.stop = QPushButton("■ Стоп")
        self.start.clicked.connect(lambda: self.command.emit(index, "start", None))
        self.stop.clicked.connect(lambda: self.command.emit(index, "stop", None))
        controls.addWidget(self.hz, 0, 0)
        controls.addWidget(apply, 0, 1)
        controls.addWidget(self.note, 0, 2)
        controls.addWidget(self.direction, 1, 0)
        controls.addWidget(self.enable, 1, 1, 1, 2)
        controls.addWidget(self.start, 2, 0)
        controls.addWidget(self.stop, 2, 1, 1, 2)
        root.addLayout(controls)
        self.controls = [
            self.hz,
            apply,
            self.note,
            self.direction,
            self.enable,
            self.start,
            self.stop,
        ]

    def update_status(self, status=None, connected=False, link_error=False, playing=False):
        installed = bool(self.config["installed_mask"] & (1 << self.index))
        m = (
            status["motors"][self.index]
            if status
            else dict(enabled=False, active=False, direction=False, note=255, frequency=440.0)
        )
        error = link_error or (status and status["error"])
        if not installed:
            color, text = None, "⊘ НЕ ИСПОЛЬЗУЕТСЯ"
        elif not connected:
            color, text = ("#884556" if link_error else None), "! ОШИБКА СВЯЗИ" if link_error else "○ НЕТ СОЕДИНЕНИЯ"
        elif error:
            color, text = "#884556", "! ОШИБКА"
        elif status and status["reset"]:
            color, text = None, "○ ОБЩИЙ RESET"
        elif status and status["sleep"]:
            color, text = None, "☾ ОБЩИЙ SLEEP"
        elif m["active"]:
            color, text = "#357662", "● STEP ГЕНЕРИРУЕТСЯ"
        elif m["enabled"]:
            color, text = None, "● ENABLE · STEP СТОП"
        else:
            color, text = None, "○ ДРАЙВЕР ОТКЛЮЧЁН"
        self.drawing.color = color
        self.drawing.update()
        self.state_label.setText(text)
        self.pitch.setText(note_name(m["note"]))
        self.frequency.setText("%.3f Гц STEP" % m["frequency"])
        microstep = self.config["microstep"]
        if status and status["raw"] != self.config["microstep_raw"]:
            microstep = {0: 1, 1: 2, 2: 4, 3: 8, 7: 16}.get(status["raw"])
        self.rpm_label.setText(
            "%.2f об/мин · расчёт"
            % rpm(
                m["frequency"],
                self.config["steps_per_revolution"][self.index],
                microstep,
            )
            if microstep else "RPM — укажите множитель микрошагов"
        )
        self.indicators.setText(
            "EN %s   STEP %s   DIR %s"
            % (
                "●" if m["enabled"] else "○",
                "●" if m["active"] else "○",
                "↺" if m["direction"] else "↻",
            )
        )
        self.speed_bar.setValue(
            int(
                max(
                    0,
                    min(1000, 1000 * math.log(max(20, m["frequency"]) / 20) / math.log(4000 / 20)),
                )
            )
        )
        self.enable.setChecked(m["enabled"])
        self.direction.setCurrentIndex(int(m["direction"]))
        for w in self.controls:
            w.setEnabled(installed and connected and not playing)
        self.stop.setEnabled(installed and connected)

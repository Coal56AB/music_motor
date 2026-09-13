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
    QSizePolicy,
)
from app.music_math import note_name, note_frequency, rpm
from midi.allocator import MOTOR_COLORS


def frequency_text(value):
    return ("%.1f" if value > 1000 else "%.2f") % value


class FrequencyInput(QDoubleSpinBox):
    def textFromValue(self, value):
        return frequency_text(value).replace('.', self.locale().decimalPoint())


class MotorDrawing(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.color = None
        self.setFixedSize(108, 108)

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
        outer = self.outer = QHBoxLayout(self)
        outer.setContentsMargins(14, 10, 14, 10)
        outer.setSpacing(12)
        self.body_widget = QWidget()
        self.body_widget.setObjectName('motorBody')
        self.body_widget.setStyleSheet('QWidget#motorBody {background:transparent;}')
        root = self.body = QVBoxLayout(self.body_widget)
        root.setContentsMargins(0, 0, 0, 0)
        outer.addWidget(self.body_widget, 2)
        root.setSpacing(6)
        header = self.header = QHBoxLayout()
        number = QLabel("M%d" % (index + 1))
        number.setStyleSheet("font-size:24px;font-weight:700;color:%s" % MOTOR_COLORS[index])
        self.name = QLabel(config["names"][index])
        self.name.setStyleSheet("font-size:16px;font-weight:600")
        number.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        self.name.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        header.addWidget(number)
        header.addWidget(self.name, 1)
        root.addLayout(header)
        display = self.display = QHBoxLayout()
        display.setSpacing(16)
        self.drawing = MotorDrawing()
        motor_column = QVBoxLayout()
        motor_column.setSpacing(2)
        motor_column.addWidget(self.drawing, 0, Qt.AlignHCenter)
        display.addLayout(motor_column)
        info = QVBoxLayout()
        info.setSpacing(2)
        info.setAlignment(Qt.AlignVCenter)
        self.state_label = QLabel("○ НЕ ПОДКЛЮЧЁН")
        self.state_label.setStyleSheet("font-size:12px;font-weight:600")
        self.state_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.state_label.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        header.addWidget(self.state_label)
        self.pitch = QLabel("—")
        self.pitch.setStyleSheet("font-size:32px;font-weight:600")
        self.frequency = QLabel("440.00 Гц")
        self.frequency.setStyleSheet("font-size:17px;font-weight:600")
        self.rpm_label = QLabel("132.00 об/мин")
        self.rpm_label.setStyleSheet("font-size:13px;color:#a9bcd2")
        self.indicators = QLabel("EN ○  STEP ○  DIR ↻")
        self.indicators.setStyleSheet("font-size:13px")
        self.indicators.setAlignment(Qt.AlignHCenter)
        motor_column.addWidget(self.indicators)
        for w in [self.pitch, self.frequency, self.rpm_label]:
            w.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
            info.addWidget(w)
        display.addLayout(info)
        root.addLayout(display, 1)
        self.speed_bar = QProgressBar()
        self.speed_bar.setRange(0, 1000)
        self.speed_bar.setTextVisible(False)
        self.speed_bar.setMinimumWidth(70)
        self.speed_bar.setFixedHeight(92)
        self.speed_bar.setToolTip("Частота STEP: от 20 до 1200 Гц, логарифмическая шкала")
        self.speed_bar.setStyleSheet(
            "QProgressBar {border:1px solid #34475e;border-radius:8px;background:#101a28}"
            "QProgressBar::chunk {background:%s;border-radius:7px}" % MOTOR_COLORS[index]
        )
        display.addWidget(self.speed_bar, 1)
        controls = self.controls_layout = QGridLayout()
        controls.setSpacing(8)
        for column in range(3):
            controls.setColumnStretch(column, 1)
        self.hz = FrequencyInput()
        self.hz.setRange(20, 1200)
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
        self.enable.setToolTip("Отдельная проверка ENABLE. «Пуск» включает его автоматически, «Стоп» выключает.")
        self.enable.clicked.connect(
            lambda *_args: self.command.emit(index, "enable", self.enable.isChecked())
        )
        self.step_stops = False
        self.step_button = QPushButton("▶ Пуск")
        self.step_button.setObjectName("motorStep")
        self.step_button.setCheckable(True)
        self.step_button.clicked.connect(
            lambda *_args: self.command.emit(index, "stop" if self.step_stops else "start", None)
        )
        controls.addWidget(self.hz, 0, 0)
        controls.addWidget(apply, 0, 1)
        controls.addWidget(self.note, 0, 2)
        controls.addWidget(self.direction, 1, 0)
        controls.addWidget(self.enable, 1, 1, 1, 2)
        root.addLayout(controls)
        root.addWidget(self.step_button)
        self.controls = [
            self.hz,
            apply,
            self.note,
            self.direction,
            self.enable,
            self.step_button,
        ]

    def set_vertical(self, vertical):
        self.setMinimumWidth(285 if vertical else 305)
        self.setMaximumWidth(16777215)
        self.outer.setContentsMargins(8 if vertical else 14, 10, 8 if vertical else 14, 10)
        self.outer.setSpacing(6 if vertical else 12)
        self.setMaximumHeight(480 if vertical else 16777215)
        self.drawing.setFixedSize(140 if vertical else 108, 140 if vertical else 108)
        self.pitch.setStyleSheet("font-size:%dpx;font-weight:600" % (46 if vertical else 32))
        self.frequency.setStyleSheet("font-size:%dpx;font-weight:600" % (18 if vertical else 17))
        self.rpm_label.setStyleSheet("font-size:%dpx;color:#a9bcd2" % (14 if vertical else 13))
        self.indicators.setStyleSheet("font-size:12px")
        self.state_label.setStyleSheet("font-size:11px;font-weight:600")
        self.display.setSpacing(6 if vertical else 16)
        if vertical:
            for label in (self.pitch, self.frequency, self.rpm_label):
                label.ensurePolished()
            text_width = max(self.frequency.fontMetrics().horizontalAdvance('1000.00 Гц'),
                             self.pitch.fontMetrics().horizontalAdvance('C#7'),
                             self.rpm_label.fontMetrics().horizontalAdvance('1200.00 об/мин'))
            self.body_widget.setFixedWidth(self.drawing.width() + self.display.spacing() + text_width)
        else:
            self.body_widget.setMinimumWidth(0)
            self.body_widget.setMaximumWidth(16777215)
        root = self.body
        root.setSpacing(2 if vertical else 6)
        self.header.removeWidget(self.state_label)
        root.removeWidget(self.state_label)
        self.outer.removeWidget(self.speed_bar)
        self.display.removeWidget(self.speed_bar)
        self.state_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.header.addWidget(self.state_label)
        self.speed_bar.setOrientation(Qt.Vertical if vertical else Qt.Horizontal)
        self.speed_bar.setMinimumWidth(22 if vertical else 70)
        self.speed_bar.setMaximumWidth(16777215)
        self.speed_bar.setMinimumHeight(100 if vertical else 92)
        self.speed_bar.setMaximumHeight(16777215 if vertical else 92)
        self.speed_bar.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding if vertical else QSizePolicy.Fixed)
        if vertical:
            self.outer.addWidget(self.speed_bar, 1)
        else:
            self.display.addWidget(self.speed_bar, 1)
        root.setStretch(root.indexOf(self.display), 0 if vertical else 1)
        grid = self.controls_layout
        grid.setSpacing(5 if vertical else 8)
        root.setStretch(root.indexOf(grid), 0)
        grid.setAlignment(Qt.AlignTop if vertical else Qt.Alignment())
        for row in range(5):
            grid.setRowStretch(row, 0)
        for widget in self.controls[:-1]:
            grid.removeWidget(widget)
            widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        for column in range(3):
            grid.setColumnStretch(column, 0 if vertical else 1)
        hz, apply, note, direction, enable = self.controls[:-1]
        root.removeWidget(self.step_button)
        grid.removeWidget(self.step_button)
        if vertical:
            grid.setColumnStretch(0, 1)
            for row, widget in enumerate([hz, apply, note, direction, enable, self.step_button]):
                grid.addWidget(widget, row, 0)
        else:
            grid.addWidget(hz, 0, 0)
            grid.addWidget(apply, 0, 1)
            grid.addWidget(note, 0, 2)
            grid.addWidget(direction, 1, 0)
            grid.addWidget(enable, 1, 1, 1, 2)
            root.addWidget(self.step_button)

    def update_status(self, status=None, connected=False, link_error=False, playing=False, music_playing=False, gap_note=None, note_range=None):
        low, high = self.config.get('min_frequency', 20), self.config.get('max_frequency', 1200)
        if getattr(self, '_frequency_limits', None) != (low, high):
            self._frequency_limits = (low, high)
            self.hz.setRange(low, high)
            self.speed_bar.setToolTip('Частота: %g–%g Гц, логарифмическая шкала' % (low, high))
            pitch = self.note.currentData()
            self.note.clear()
            for n in range(128):
                if low <= note_frequency(n) <= high:
                    self.note.addItem(note_name(n), n)
            selected = self.note.findData(pitch)
            self.note.setCurrentIndex(selected if selected >= 0 else 0)
        self.speed_bar.setToolTip(
            'Диапазон мелодии: %s–%s, шкала 10–100%%' % tuple(note_name(n) for n in note_range)
            if note_range is not None else 'Частота: %g–%g Гц, логарифмическая шкала' % (low, high))
        installed = bool(self.config["installed_mask"] & (1 << self.index))
        m = (
            status["motors"][self.index]
            if status
            else dict(enabled=False, active=False, direction=False, note=255, frequency=440.0)
        )
        error = link_error or (status and status["error"])
        ready = bool(installed and connected and not error and status
                     and not status['reset'] and not status['sleep'])
        sounding = ready and m['active'] and m['enabled']
        held = ready and music_playing and not sounding and gap_note is not None
        visual = dict(m, note=gap_note, frequency=note_frequency(gap_note)) if held else m
        if not installed:
            color, text = None, "⊘ НЕ ИСП."
        elif not connected:
            color, text = ("#884556" if link_error else None), "! СВЯЗЬ" if link_error else "○ НЕТ СВЯЗИ"
        elif error:
            color, text = "#884556", "! ОШИБКА"
        elif status and status["reset"]:
            color, text = None, "○ RESET"
        elif status and status["sleep"]:
            color, text = None, "☾ SLEEP"
        elif held:
            color, text = "#357662", "● ИГРАЕТ"
        elif sounding:
            color, text = "#357662", "● ИГРАЕТ"
        elif m["enabled"]:
            color, text = None, "● УДЕРЖАНИЕ"
        else:
            color, text = None, "○ ВЫКЛ."
        self.drawing.color = color
        self.drawing.update()
        self.state_label.setText(text)
        self.pitch.setText(note_name(visual["note"]))
        self.frequency.setText(frequency_text(visual["frequency"]) + " Гц")
        microstep = self.config["microstep"]
        if status and status["raw"] != self.config["microstep_raw"]:
            microstep = {0: 1, 1: 2, 2: 4, 3: 8, 7: 16}.get(status["raw"])
        self.rpm_label.setText(
            "%.2f об/мин"
            % rpm(
                visual["frequency"],
                self.config["steps_per_revolution"][self.index],
                microstep,
            )
            if microstep else "RPM — укажите множитель микрошагов"
        )
        self.indicators.setText(
            "EN %s  STEP %s  DIR %s"
            % (
                "●" if m["enabled"] else "○",
                "●" if m["active"] or held else "○",
                "↺" if m["direction"] else "↻",
            )
        )
        # Set once: QProgressBar may repaint synchronously in setValue().
        # Writing a frequency first and then zero caused one-frame idle flashes.
        level = round(max(0, min(1000, 1000 * math.log(max(low, visual['frequency']) / low)
                              / math.log(high / low)))) if sounding or held else 0
        if note_range is not None and (sounding or held):
            bottom, top = note_range
            fraction = (visual['note'] - bottom) / (top - bottom) if top > bottom else 1
            level = round(100 + 900 * max(0, min(1, fraction)))
        self.speed_bar.setValue(level)
        self.enable.setChecked(m["enabled"])
        self.direction.setCurrentIndex(int(m["direction"]))
        for w in self.controls:
            w.setEnabled(installed and connected and not playing)
        self.note.setEnabled(installed and connected and not playing and self.note.count() > 0)
        self.step_stops = bool(m["active"] or playing)
        self.step_button.setChecked(self.step_stops)
        self.step_button.setText("■ Стоп потока" if playing else
                                 "■ Стоп" if self.step_stops else "▶ Пуск")
        self.step_button.setEnabled(installed and connected)
        self.direction.setEnabled(installed and connected)

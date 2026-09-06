import copy
import datetime
import struct
from pathlib import Path
from PySide2.QtCore import Qt, QTimer, QObject, QThread, Signal
from PySide2.QtWidgets import (
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QPushButton,
    QComboBox,
    QCheckBox,
    QTabWidget,
    QGroupBox,
    QScrollArea,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QHeaderView,
    QFileDialog,
    QMessageBox,
    QSpinBox,
    QDoubleSpinBox,
    QSlider,
    QPlainTextEdit,
    QProgressBar,
    QFormLayout,
    QLineEdit,
    QInputDialog,
)
from serial.tools import list_ports
from app.settings import load_settings, save_settings, ROOT
from app.music_math import MICROSTEPS, note_name
from app.playback import StreamPlayer
from app.theme import STYLE
from widgets.motor_card import MotorCard
from widgets.piano_roll import PianoRoll, NoteItem
from protocol.client import Client
from protocol.wire import Command as C, Error
from midi.model import load_midi, save_midi, demo_song, save_project, load_project, Part, Song, Note
from midi.allocator import allocate, STRATEGIES, MOTOR_COLORS
from audio.transcribe import transcribe


def button(text, slot, kind=None):
    b = QPushButton(text)
    b.clicked.connect(slot)
    if kind:
        b.setObjectName(kind)
    return b


def spin(low, high, value):
    w = QSpinBox()
    w.setRange(low, high)
    w.setValue(value)
    return w


class AudioWorker(QObject):
    done = Signal(object)
    failed = Signal(str)
    progress = Signal(int)
    finished = Signal()

    def __init__(self, path, options):
        super().__init__()
        self.path = path
        self.options = options

    def run(self):
        try:
            self.done.emit(transcribe(self.path, progress=self.progress.emit, **self.options))
        except Exception as exc:
            self.failed.emit(str(exc))
        finally:
            self.finished.emit()


class MainWindow(QMainWindow):
    def __init__(self, settings_path=None, auto_connect=False, simulation=False):
        super().__init__()
        self.settings_path = settings_path
        self.config = load_settings(settings_path)
        self.simulation = simulation  # Injected only by service tests, never from saved settings.
        self.setWindowTitle("Music Motor Studio • STM32 / 6 voices")
        self.resize(1280, 930)
        self.setMinimumSize(1000, 700)
        self.setStyleSheet(STYLE)
        self.latest_status = None
        self.link_error = False
        self.allocation = None
        self.loading = False
        self.audio_thread = None
        self.audio_song = None
        self.client = Client(self)
        self.player = StreamPlayer(self.client, self)
        self.song = demo_song()
        outer = QWidget()
        self.setCentralWidget(outer)
        root = QVBoxLayout(outer)
        root.setContentsMargins(18, 14, 18, 8)
        head = QHBoxLayout()
        heading = QVBoxLayout()
        title = QLabel("MUSIC MOTOR  /  STUDIO")
        title.setObjectName("title")
        subtitle = QLabel("Шесть независимых голосов · STM32F103 · STEP ≡ высота ноты")
        subtitle.setObjectName("subtitle")
        heading.addWidget(title)
        heading.addWidget(subtitle)
        head.addLayout(heading, 1)
        self.connection_label = QLabel("○ Отключено")
        head.addWidget(self.connection_label)
        head.addWidget(button("■  STOP ALL", lambda: self.player.stop(True), "danger"))
        root.addLayout(head)
        link = QHBoxLayout()
        self.ports = QComboBox()
        self.ports.setMinimumWidth(155)
        self.ports.setMaximumWidth(260)
        self.baud = QComboBox()
        self.baud.addItems(["115200", "57600", "230400"])
        self.baud.setCurrentText(str(self.config["baudrate"]))
        self.connect_button = button("Подключить", self.toggle_connection, "primary")
        for w in [
            self.ports,
            button("↻ Порты", self.refresh_ports),
            self.baud,
            self.connect_button,
            button(
                "Запрос состояния",
                lambda: self.client.send(C.GET_STATUS, callback=self.receive_status_bytes),
            ),
        ]:
            link.addWidget(w)
        link.addStretch()
        self.version = QLabel("Прошивка —")
        link.addWidget(self.version)
        root.addLayout(link)
        self.tabs = QTabWidget()
        root.addWidget(self.tabs, 1)
        self.build_motors()
        self.build_editor()
        self.build_audio()
        self.build_playback()
        self.build_log()
        self.build_settings()
        self.client.status.connect(self.on_status)
        self.client.info.connect(self.show_version)
        self.client.log.connect(self.append_log)
        self.client.fault.connect(self.show_error)
        self.client.connection.connect(self.on_connection)
        self.player.changed.connect(self.player_state)
        self.player.progress.connect(self.on_progress)
        self.player.failed.connect(self.show_error)
        self.set_song(self.song)
        self.refresh_ports()
        self.on_connection(False)
        self.statusBar().showMessage(
            "Выберите COM-порт платы и нажмите «Подключить». Состав двигателей задаётся в настройках."
        )
        if auto_connect:
            QTimer.singleShot(0, self.toggle_connection)

    def page(self, title):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(12, 12, 12, 12)
        self.tabs.addTab(widget, title)
        return widget, layout

    def build_motors(self):
        page, root = self.page("Двигатели / Отладка")
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        content = QWidget()
        grid = self.motor_grid = QGridLayout(content)
        grid.setSpacing(10)
        self.cards = []
        for i in range(6):
            card = MotorCard(i, self.config, content)
            card.command.connect(self.manual_command)
            self.cards.append(card)
        self.no_motors = QLabel("Двигатели не выбраны. Откройте «Настройки» и укажите используемые моторы.", content)
        self.no_motors.setWordWrap(True)
        self.layout_motors()
        scroll.setWidget(content)
        root.addWidget(scroll, 1)
        self.common = QGroupBox("Общие сигналы для ВСЕХ шести драйверов")
        row = QHBoxLayout(self.common)
        self.micro = QComboBox()
        for name, (raw, factor) in MICROSTEPS.items():
            self.micro.addItem(name, (raw, factor))
        self.micro.addItem("Сырая комбинация", None)
        self.raw = spin(0, 7, self.config["microstep_raw"])
        self.raw.setPrefix("MS = ")
        self.raw.setToolTip("Бит 0 = MS1, бит 1 = MS2, бит 2 = MS3")
        self.micro.activated.connect(self.micro_selected)
        current = next(
            (
                i
                for i in range(self.micro.count() - 1)
                if self.micro.itemData(i)[0] == self.config["microstep_raw"]
            ),
            self.micro.count() - 1,
        )
        self.micro.setCurrentIndex(current)
        self.sleep = QCheckBox("SLEEP активен")
        self.reset = QCheckBox("RESET активен")
        self.sleep.clicked.connect(self.sleep_clicked)
        self.reset.clicked.connect(self.reset_clicked)
        for w in [
            self.micro,
            self.raw,
            button("Применить MS", self.apply_micro),
            self.sleep,
            self.reset,
            button("RESET PULSE", lambda: self.common_command(C.RESET_PULSE, None)),
        ]:
            row.addWidget(w)
        root.addWidget(self.common)
        hint = QLabel(
            "A4988: полный / ½ / ¼ / ⅛ / ¹⁄₁₆. Таблицу конкретного HR4998 сверьте с документацией модуля. RPM — расчёт, датчика вращения нет."
        )
        hint.setWordWrap(True)
        hint.setObjectName("subtitle")
        root.addWidget(hint)

    def build_editor(self):
        page, root = self.page("MIDI-редактор")
        tools = QHBoxLayout()
        for text, slot in [
            ("Открыть MIDI", self.open_midi),
            ("Сохранить MIDI", self.save_midi),
            ("Открыть проект", self.open_project),
            ("Сохранить проект", self.save_project),
        ]:
            tools.addWidget(button(text, slot))
        tools.addWidget(button("Экспорт для моторов", self.export_arrangement))
        tools.addStretch()
        root.addLayout(tools)
        split = QSplitter(Qt.Horizontal)
        left = QWidget()
        ll = QVBoxLayout(left)
        ll.setContentsMargins(0, 0, 0, 0)
        self.parts = QTableWidget(0, 4)
        self.parts.setHorizontalHeaderLabels(["✓ Партия", "M", "± нот", "Канал / GM"])
        self.parts.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.parts.setColumnWidth(1, 75)
        self.parts.setColumnWidth(2, 65)
        self.parts.setColumnWidth(3, 80)
        self.parts.setMinimumWidth(320)
        self.parts.itemChanged.connect(self.part_changed)
        self.parts.itemSelectionChanged.connect(self.part_selected)
        ll.addWidget(self.parts)
        ll.addWidget(button("Выделенные ноты → новая партия", self.split_part))
        self.skip_list = QPlainTextEdit()
        self.skip_list.setReadOnly(True)
        self.skip_list.setMaximumHeight(145)
        ll.addWidget(QLabel("Пропуски и note stealing"))
        ll.addWidget(self.skip_list)
        split.addWidget(left)
        right = QWidget()
        rl = QVBoxLayout(right)
        rl.setContentsMargins(0, 0, 0, 0)
        edit = QHBoxLayout()
        self.roll = PianoRoll()
        self.roll.edited.connect(self.on_edited)
        edit.addWidget(button("↶ Undo", self.roll.undo_stack.undo))
        edit.addWidget(button("↷ Redo", self.roll.undo_stack.redo))
        self.snap = QComboBox()
        for name, value in [("1/4", 1), ("1/8", 0.5), ("1/16", 0.25), ("1/32", 0.125)]:
            self.snap.addItem(name, value)
        self.snap.setCurrentIndex(2)
        self.snap.currentIndexChanged.connect(self.snap_selected)
        edit.addWidget(QLabel("Сетка"))
        edit.addWidget(self.snap)
        self.velocity = spin(1, 127, 90)
        edit.addWidget(self.velocity)
        edit.addWidget(button("Velocity", self.set_velocity))
        zoom = QSlider(Qt.Horizontal)
        zoom.setRange(25, 180)
        zoom.setValue(76)
        zoom.setMaximumWidth(140)
        zoom.valueChanged.connect(self.roll.set_zoom)
        edit.addWidget(QLabel("Масштаб"))
        edit.addWidget(zoom)
        rl.addLayout(edit)
        rl.addWidget(self.roll, 1)
        hint = QLabel(
            "Двойной щелчок: добавить / velocity · перетащить: переместить · правый край: длительность · Delete: удалить"
        )
        hint.setWordWrap(True)
        hint.setObjectName("subtitle")
        rl.addWidget(hint)
        split.addWidget(right)
        split.setSizes([340, 820])
        root.addWidget(split, 1)
        self.editor_tool_layouts = [tools, edit]

    def build_audio(self):
        page, root = self.page("Импорт аудио")
        title = QLabel("Аудиозапись → ноты → редактирование")
        title.setStyleSheet("font-size:22px;font-weight:600")
        root.addWidget(title)
        warning = QLabel(
            "Распознавание приблизительное. Сложный микс, вокал, ударные и гармоники могут давать ошибки.\nСпектральный анализ не разделяет запись на реальные инструменты; исправляйте результат в piano roll."
        )
        warning.setWordWrap(True)
        root.addWidget(warning)
        form = QFormLayout()
        self.audio_path = QLineEdit()
        r = QHBoxLayout()
        r.addWidget(self.audio_path)
        r.addWidget(button("Выбрать файл", self.choose_audio))
        form.addRow("WAV / MP3 / FLAC / OGG", r)
        self.audio_engine = QComboBox()
        self.audio_engine.addItem("Спектральные пики · доступен сразу", "spectral")
        self.audio_engine.addItem("librosa pYIN · одна нота", "pyin")
        self.audio_engine.addItem("Basic Pitch · внешний движок", "basic_pitch")
        form.addRow("Алгоритм", self.audio_engine)
        self.audio_poly = spin(1, 6, 4)
        form.addRow("Максимум одновременных нот", self.audio_poly)
        self.minimum_duration = QDoubleSpinBox()
        self.minimum_duration.setRange(0.03, 2)
        self.minimum_duration.setSingleStep(0.01)
        self.minimum_duration.setValue(0.1)
        self.minimum_duration.setSuffix(" с")
        form.addRow("Минимальная длительность", self.minimum_duration)
        self.sensitivity = QDoubleSpinBox()
        self.sensitivity.setRange(0.05, 1)
        self.sensitivity.setSingleStep(0.05)
        self.sensitivity.setValue(0.25)
        form.addRow("Порог (меньше → больше нот)", self.sensitivity)
        self.audio_low = spin(0, 127, 36)
        self.audio_high = spin(0, 127, 96)
        limits = QHBoxLayout()
        limits.addWidget(self.audio_low)
        limits.addWidget(self.audio_high)
        form.addRow("Диапазон MIDI min / max", limits)
        root.addLayout(form)
        actions = QHBoxLayout()
        self.analyze_button = button("Распознать ноты", self.analyze_audio, "primary")
        actions.addWidget(self.analyze_button)
        self.audio_accept = button("Перенести в MIDI-редактор", self.accept_audio)
        self.audio_accept.setEnabled(False)
        actions.addWidget(self.audio_accept)
        actions.addWidget(button("Сохранить промежуточный MIDI", self.save_audio_midi))
        actions.addStretch()
        root.addLayout(actions)
        self.audio_progress = QProgressBar()
        root.addWidget(self.audio_progress)
        self.audio_preview = PianoRoll()
        self.audio_preview.read_only = True
        root.addWidget(self.audio_preview, 1)

    def build_playback(self):
        page, root = self.page("Воспроизведение")
        self.song_label = QLabel()
        self.song_label.setStyleSheet("font-size:24px;font-weight:600")
        root.addWidget(self.song_label)
        box = QGroupBox("Распределение нот")
        form = QFormLayout(box)
        self.strategy = QComboBox()
        for key, name in STRATEGIES.items():
            self.strategy.addItem(name, key)
        self.strategy.setCurrentIndex(max(0, self.strategy.findData(self.config["strategy"])))
        form.addRow("Стратегия", self.strategy)
        self.poly = spin(1, 6, self.config["max_polyphony"])
        form.addRow("Максимальная полифония", self.poly)
        self.tempo = spin(25, 300, 100)
        self.tempo.setSuffix(" %")
        form.addRow("Темп (сохраняет изменения темпа MIDI)", self.tempo)
        self.transpose = spin(-48, 48, 0)
        form.addRow("Транспонирование композиции", self.transpose)
        self.low_hz = spin(20, 4000, self.config["min_frequency"])
        self.high_hz = spin(20, 4000, self.config["max_frequency"])
        limits = QHBoxLayout()
        limits.addWidget(self.low_hz)
        limits.addWidget(self.high_hz)
        form.addRow("Диапазон STEP, Гц min / max", limits)
        self.octave = QCheckBox("Переносить недоступные ноты по октавам")
        self.octave.setChecked(True)
        form.addRow(self.octave)
        self.drums = QCheckBox("Разрешить ударный канал (также включите его партию)")
        form.addRow(self.drums)
        self.disable_stop = QCheckBox("Отключать ENABLE после остановки")
        self.disable_stop.setChecked(self.config["disable_after_stop"])
        form.addRow(self.disable_stop)
        root.addWidget(box)
        self.playback_options = box
        row = QHBoxLayout()
        row.addWidget(button("Пересчитать назначения", self.reallocate))
        self.play_button = button("▶ Воспроизвести", self.play, "primary")
        row.addWidget(self.play_button)
        self.pause_button = button("Ⅱ Пауза", self.pause_resume)
        row.addWidget(self.pause_button)
        row.addWidget(button("■ Стоп", lambda: self.player.stop()))
        row.addStretch()
        root.addLayout(row)
        self.position = QLabel("00:00.000")
        self.position.setStyleSheet("font-size:32px;font-weight:600")
        root.addWidget(self.position)
        self.timeline = QProgressBar()
        self.timeline.setRange(0, 1000)
        root.addWidget(self.timeline)
        queue_row = QHBoxLayout()
        queue_row.addWidget(QLabel("Очередь STM32"))
        self.queue_bar = QProgressBar()
        self.queue_bar.setRange(0, 256)
        queue_row.addWidget(self.queue_bar, 1)
        root.addLayout(queue_row)
        self.play_state = QLabel("Остановлено")
        root.addWidget(self.play_state)
        voices = QHBoxLayout()
        self.voice_labels = []
        for i in range(6):
            label = QLabel("M%d\n—" % (i + 1))
            label.setAlignment(Qt.AlignCenter)
            label.setStyleSheet(
                "background:#1a293d;border-radius:6px;padding:14px;color:%s;font-size:18px"
                % MOTOR_COLORS[i]
            )
            voices.addWidget(label)
            self.voice_labels.append(label)
        root.addLayout(voices)
        self.allocation_label = QLabel()
        self.allocation_label.setWordWrap(True)
        root.addWidget(self.allocation_label)
        root.addStretch()
        for control in (self.poly, self.tempo, self.transpose, self.low_hz, self.high_hz):
            control.valueChanged.connect(self.invalidate_allocation)
        self.strategy.currentIndexChanged.connect(self.invalidate_allocation)
        self.octave.toggled.connect(self.invalidate_allocation)
        self.drums.toggled.connect(self.invalidate_allocation)

    def build_log(self):
        page, root = self.page("UART-журнал")
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(2500)
        root.addWidget(self.log)
        row = QHBoxLayout()
        row.addWidget(button("Очистить", self.log.clear))
        row.addWidget(button("Сохранить журнал", self.save_log))
        row.addStretch()
        root.addLayout(row)

    def build_settings(self):
        page, page_layout = self.page("Настройки")
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        content = QWidget()
        content.setMaximumWidth(820)
        root = QVBoxLayout(content)
        root.setSpacing(12)
        scroll.setWidget(content)
        page_layout.addWidget(scroll)
        self.motor_settings = QGroupBox("Используемые двигатели")
        motor_form = QGridLayout(self.motor_settings)
        for col, title in enumerate(["Использовать", "Название", "Полных шагов / оборот", "Направление"]):
            motor_form.addWidget(QLabel(title), 0, col)
        self.used_settings, self.name_settings, self.direction_settings = [], [], []
        self.step_settings = []
        for i in range(6):
            used = QCheckBox("M%d" % (i + 1))
            used.setChecked(bool(self.config["installed_mask"] & (1 << i)))
            name = QLineEdit(self.config["names"][i])
            name.setMaximumWidth(240)
            w = spin(1, 10000, self.config["steps_per_revolution"][i])
            w.setFixedWidth(115)
            direction = QComboBox()
            direction.setFixedWidth(125)
            direction.addItems(["↻ DIR 0", "↺ DIR 1"])
            direction.setCurrentIndex(self.config["directions"][i])
            self.used_settings.append(used)
            self.name_settings.append(name)
            self.step_settings.append(w)
            self.direction_settings.append(direction)
            for col, widget in enumerate([used, name, w, direction]):
                motor_form.addWidget(widget, i + 1, col)
        root.addWidget(self.motor_settings)
        hint = QLabel("На экране и в распределении нот участвуют только выбранные моторы. Изменения применяются кнопкой ниже.")
        hint.setWordWrap(True)
        root.addWidget(hint)
        playback_group = QGroupBox("Воспроизведение")
        form = QFormLayout(playback_group)
        form.setFieldGrowthPolicy(QFormLayout.FieldsStayAtSizeHint)
        form.setSpacing(10)
        self.micro_factor = spin(1, 256, self.config["microstep"])
        self.micro_factor.setFixedWidth(115)
        form.addRow("Множитель микрошагов для расчёта RPM", self.micro_factor)
        self.lookahead = spin(500, 10000, self.config["lookahead_ms"])
        self.lookahead.setSuffix(" мс")
        self.lookahead.setFixedWidth(115)
        form.addRow("Упреждение музыкального потока", self.lookahead)
        root.addWidget(playback_group)
        audio_group = QGroupBox("Аудио · внешние программы")
        form = QFormLayout(audio_group)
        form.setSpacing(10)
        self.ffmpeg = QLineEdit(self.config.get("ffmpeg", "ffmpeg"))
        self.ffmpeg.setMaximumWidth(410)
        form.addRow("Путь к ffmpeg.exe", self.ffmpeg)
        self.basic_pitch = QLineEdit(self.config.get("basic_pitch_command", ""))
        self.basic_pitch.setMaximumWidth(410)
        form.addRow("Путь к basic-pitch.exe (опционально)", self.basic_pitch)
        root.addWidget(audio_group)
        self.save_settings_button = button("Применить и сохранить настройки", self.persist, "primary")
        root.addWidget(self.save_settings_button, 0, Qt.AlignLeft)
        note = QLabel(
            "Перед ручным запуском снимите общие SLEEP и RESET, затем включите ENABLE нужного двигателя.\nSTOP ALL останавливает все импульсы и отключает драйверы."
        )
        note.setWordWrap(True)
        root.addWidget(note)
        root.addStretch()

    def refresh_ports(self):
        current = self.ports.currentData() or self.config.get("port", "")
        self.ports.clear()
        for p in list_ports.comports():
            self.ports.addItem("%s · %s" % (p.device, p.description), p.device)
        i = self.ports.findData(current)
        if i >= 0:
            self.ports.setCurrentIndex(i)

    def show_version(self, text):
        self.version.setText(text.split(";")[0])
        self.version.setToolTip(text)

    def toggle_connection(self):
        if self.client.connected:
            self.player.stop()
            self.client.close()
            return
        self.link_error = False
        self.client.connect_device(
            self.ports.currentData() or "",
            int(self.baud.currentText()),
            self.simulation,
        )
        if self.client.connected:
            self.client.send(C.SET_MASK, bytes([self.config["installed_mask"]]))
            self.client.send(C.MICROSTEP, bytes([self.config["microstep_raw"]]))
            for i, direction in enumerate(self.config["directions"]):
                if self.config["installed_mask"] & (1 << i):
                    self.client.send(C.DIR, bytes([i, direction]))

    def on_connection(self, connected):
        self.connection_label.setText(
            "● СИМУЛЯТОР"
            if connected and self.client.sim
            else "● UART"
            if connected
            else "○ Отключено"
        )
        self.connect_button.setText("Отключить" if connected else "Подключить")
        self.ports.setEnabled(not connected)
        self.baud.setEnabled(not connected)
        if not connected:
            self.latest_status = None
            self.player.on_fault("Соединение закрыто")
        self.refresh_cards()

    def receive_status_bytes(self, data):
        from protocol.wire import decode_status

        self.on_status(decode_status(data))

    def on_status(self, status):
        self.latest_status = status
        self.link_error = bool(status["error"])
        self.sleep.setChecked(status["sleep"])
        self.reset.setChecked(status["reset"])
        self.queue_bar.setValue(status["used"])
        self.queue_bar.setFormat("%v / 256 событий")
        self.refresh_cards()
        for i, label in enumerate(self.voice_labels):
            m = status["motors"][i]
            label.setText(
                "M%d\n%s\n%s"
                % (
                    i + 1,
                    "%.1f Гц" % m["frequency"] if m["active"] else "—",
                    "STEP ●" if m["active"] else "STEP ○",
                )
            )

    def refresh_cards(self):
        playing = self.player.state in ("preparing", "playing", "paused")
        self.motor_settings.setEnabled(not playing)
        self.save_settings_button.setEnabled(not playing)
        for card in self.cards:
            card.update_status(self.latest_status, self.client.connected, self.link_error, playing)
        for i, label in enumerate(self.voice_labels):
            label.setVisible(bool(self.config["installed_mask"] & (1 << i)))
        self.common.setEnabled(self.client.connected and not playing)

    def manual_command(self, m, command, value):
        if not self.config["installed_mask"] & (1 << m):
            return
        if self.player.state in ("playing", "preparing", "paused"):
            if command == "stop":
                self.player.stop()
            return
        if command == "frequency":
            self.client.send(C.FREQUENCY, struct.pack("<BI", m, round(value * 1000)))
        elif command == "note":
            self.client.send(C.NOTE, bytes([m, value]))
        elif command == "enable":
            self.client.send(C.ENABLE, bytes([m, int(value)]))
        elif command == "start":
            self.client.send(C.START, bytes([m]))
        elif command == "stop":
            self.client.send(C.STOP, bytes([m]))
        elif command == "direction":
            self.config["directions"][m] = value
            self.direction_settings[m].setCurrentIndex(value)
            self.client.send(C.STOP, bytes([m]))
            self.client.send(C.DIR, bytes([m, value]))

    def common_command(self, command, value):
        self.client.send(command, b"" if value is None else bytes([int(value)]))

    # Read the widget directly: compiled PySide2 callbacks can lose signal arguments.
    def sleep_clicked(self, *_args):
        self.common_command(C.SLEEP, self.sleep.isChecked())

    def reset_clicked(self, *_args):
        self.common_command(C.RESET, self.reset.isChecked())

    def snap_selected(self, *_args):
        self.roll.snap = self.snap.currentData()

    def part_motor_callback(self, part_id, widget):
        def changed(*_args):
            self.assign_part(part_id, widget.currentData())
        return changed

    def part_transpose_callback(self, part_id, widget):
        def changed(*_args):
            self.transpose_part(part_id, widget.value())
        return changed

    def micro_selected(self):
        data = self.micro.currentData()
        if data:
            self.raw.setValue(data[0])
            self.micro_factor.setValue(data[1])

    def apply_micro(self):
        self.config["microstep_raw"] = self.raw.value()
        self.config["microstep"] = self.micro_factor.value()
        self.client.send(C.MICROSTEP, bytes([self.raw.value()]))
        self.refresh_cards()

    def motor_config_changed(self):
        old = self.config["installed_mask"]
        self.config["installed_mask"] = sum(
            1 << i for i, used in enumerate(self.used_settings) if used.isChecked()
        )
        self.config["music_mask"] = self.config["installed_mask"]
        self.config["names"] = [w.text() for w in self.name_settings]
        if old != self.config["installed_mask"] and self.client.connected:
            self.client.send(C.SET_MASK, bytes([self.config["installed_mask"]]))
        for i, w in enumerate(self.direction_settings):
            value = w.currentIndex()
            if value != self.config["directions"][i] and self.client.connected and self.config["installed_mask"] & (1 << i):
                self.client.send(C.STOP, bytes([i]))
                self.client.send(C.DIR, bytes([i, value]))
            self.config["directions"][i] = value
        self.layout_motors()
        self.invalidate_allocation()
        self.refresh_cards()

    def layout_motors(self):
        for card in self.cards:
            self.motor_grid.removeWidget(card)
            card.hide()
            card.name.setText(self.config["names"][card.index])
        self.motor_grid.removeWidget(self.no_motors)
        self.no_motors.hide()
        used = [c for c in self.cards if self.config["installed_mask"] & (1 << c.index)]
        columns = 3 if len(used) > 4 else min(2, max(1, len(used)))
        for i, card in enumerate(used):
            self.motor_grid.addWidget(card, i // columns, i % columns)
            card.show()
        if not used:
            self.motor_grid.addWidget(self.no_motors, 0, 0)
            self.no_motors.show()

    def set_song(self, song):
        if self.player.state != "stopped":
            self.player.stop()
        self.song = song
        self.roll.set_song(song)
        self.populate_parts()
        self.song_label.setText(song.title)
        self.reallocate()

    def populate_parts(self):
        self.loading = True
        self.parts.setRowCount(len(self.song.parts))
        for row, p in enumerate(self.song.parts.values()):
            item = QTableWidgetItem(p.name)
            item.setData(Qt.UserRole, p.id)
            item.setCheckState(Qt.Checked if p.enabled else Qt.Unchecked)
            self.parts.setItem(row, 0, item)
            combo = QComboBox()
            combo.addItem("Авто", -1)
            for i in range(6):
                combo.addItem("M%d" % (i + 1), i)
            combo.setCurrentIndex(p.motor + 1)
            combo.currentIndexChanged.connect(self.part_motor_callback(p.id, combo))
            self.parts.setCellWidget(row, 1, combo)
            trans = spin(-48, 48, p.transpose)
            trans.valueChanged.connect(self.part_transpose_callback(p.id, trans))
            self.parts.setCellWidget(row, 2, trans)
            info = QTableWidgetItem(
                "%d / %d%s" % (p.channel + 1, p.program + 1, " 🥁" if p.percussion else "")
            )
            info.setFlags(info.flags() & ~Qt.ItemIsEditable)
            self.parts.setItem(row, 3, info)
        self.loading = False
        if self.parts.rowCount():
            self.parts.selectRow(0)

    def part_changed(self, item):
        if self.loading or item.column() != 0:
            return
        p = self.song.parts[item.data(Qt.UserRole)]
        p.enabled = item.checkState() == Qt.Checked
        p.name = item.text()
        self.invalidate_allocation()

    def part_selected(self):
        row = self.parts.currentRow()
        if row >= 0:
            self.roll.current_part = self.parts.item(row, 0).data(Qt.UserRole)
            self.roll.viewport().update()

    def assign_part(self, pid, motor):
        self.song.parts[pid].motor = motor
        self.invalidate_allocation()

    def transpose_part(self, pid, value):
        self.song.parts[pid].transpose = value
        self.invalidate_allocation()

    def split_part(self):
        if self.player.state != "stopped":
            return
        selected = [i.note for i in self.roll.scene().selectedItems() if isinstance(i, NoteItem)]
        if not selected:
            return
        name, ok = QInputDialog.getText(self, "Новая партия", "Название")
        if not ok or not name:
            return
        pid = "group:%d" % len(self.song.parts)
        while pid in self.song.parts:
            pid += "x"
        source = self.song.parts[selected[0].part]
        self.song.parts[pid] = Part(pid, name, source.track, source.channel, source.program)
        before = copy.deepcopy(self.song.notes)
        for n in selected:
            n.part = pid
        self.roll.commit(before, "Новая группа нот")
        self.populate_parts()

    def set_velocity(self):
        if self.player.state != "stopped":
            return
        before = copy.deepcopy(self.song.notes)
        for item in self.roll.scene().selectedItems():
            if isinstance(item, NoteItem):
                item.note.velocity = self.velocity.value()
        if before != self.song.notes:
            self.roll.commit(before, "Velocity")

    def on_edited(self):
        self.allocation = None
        self.reallocate()

    def invalidate_allocation(self, *args):
        if self.player.state == "stopped":
            self.allocation = None

    def reallocate(self):
        try:
            self.allocation = allocate(
                self.song,
                self.config["installed_mask"],
                self.config["music_mask"],
                self.poly.value(),
                self.strategy.currentData(),
                self.low_hz.value(),
                self.high_hz.value(),
                self.transpose.value(),
                self.octave.isChecked(),
                self.tempo.value() / 100,
                self.drums.isChecked(),
            )
        except (ValueError, OverflowError) as exc:
            self.show_error(str(exc))
            return False
        self.roll.set_allocation(self.allocation)
        available = sum(
            bool(self.config["installed_mask"] & self.config["music_mask"] & (1 << i))
            for i in range(6)
        )
        self.allocation_label.setText(
            "Доступно двигателей: %d · реальная полифония ≤ %d · назначено нот: %d · пропусков / прерываний: %d"
            % (
                available,
                min(available, self.poly.value()),
                len(self.allocation.assignments),
                len(self.allocation.skipped),
            )
        )
        by_id = {n.id: n for n in self.song.notes}
        self.skip_list.setPlainText(
            "\n".join(
                "%s @ %.2f: %s" % (note_name(by_id[uid].pitch), by_id[uid].start, reason)
                for uid, reason in self.allocation.skipped.items()
            )
        )
        return True

    def play(self):
        if self.player.state == "paused":
            self.player.resume()
            return
        if self.player.state != "stopped":
            return
        if not self.reallocate():
            return
        if not self.allocation.assignments:
            self.show_error("Нет нот для доступных двигателей")
            return
        self.collect_settings()
        self.player.play(self.allocation, self.config)

    def pause_resume(self):
        if self.player.state == "paused":
            self.player.resume()
        else:
            self.player.pause()

    def player_state(self, state):
        self.play_state.setText(
            {
                "stopped": "Остановлено",
                "preparing": "Подготовка драйверов и заполнение очереди…",
                "playing": "● Поток воспроизводится",
                "paused": "Ⅱ Пауза, STEP остановлен",
            }[state]
        )
        self.pause_button.setText("▶ Продолжить" if state == "paused" else "Ⅱ Пауза")
        self.play_button.setEnabled(state in ("stopped", "paused"))
        self.playback_options.setEnabled(state == "stopped")
        self.parts.setEnabled(state == "stopped")
        for layout in self.editor_tool_layouts:
            for i in range(layout.count()):
                widget = layout.itemAt(i).widget()
                if widget:
                    widget.setEnabled(state == "stopped")
        self.roll.read_only = state != "stopped"
        if state in ("stopped", "paused"):
            self.roll.position_ms = -1
            self.roll.viewport().update()
        self.refresh_cards()

    def on_progress(self, ms, used):
        self.position.setText("%02d:%02d.%03d" % (ms // 60000, (ms // 1000) % 60, ms % 1000))
        self.timeline.setValue(int(1000 * ms / max(1, self.allocation.duration_ms)))
        self.roll.set_playhead(self.song.beat_at(ms / 1000, self.tempo.value() / 100), ms)

    def open_midi(self):
        path, _ = QFileDialog.getOpenFileName(self, "Открыть MIDI", "", "MIDI (*.mid *.midi)")
        if path:
            try:
                self.set_song(load_midi(path))
            except Exception as exc:
                self.show_error(str(exc))

    def save_midi(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Сохранить MIDI", self.song.title + ".mid", "MIDI (*.mid)"
        )
        if path:
            try:
                save_midi(self.song, path)
            except Exception as exc:
                self.show_error(str(exc))

    def export_arrangement(self):
        if not self.reallocate():
            return
        path, _ = QFileDialog.getSaveFileName(
            self,
            "MIDI с учётом распределения, темпа и транспонирования",
            "motors.mid",
            "MIDI (*.mid)",
        )
        if not path:
            return
        song = Song(title="Motor arrangement")
        for start, end, motor, uid, pitch in self.allocation.segments:
            pid = str(motor)
            if pid not in song.parts:
                song.parts[pid] = Part(pid, self.config["names"][motor], motor, motor, 0)
            if end > start:
                song.notes.append(
                    Note(len(song.notes), pid, start / 500, (end - start) / 500, pitch)
                )
        try:
            save_midi(song, path)
        except Exception as exc:
            self.show_error(str(exc))

    def open_project(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Открыть проект", "", "Music Motor (*.motor.json)"
        )
        if path:
            try:
                self.set_song(load_project(path))
            except Exception as exc:
                self.show_error(str(exc))

    def save_project(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Сохранить проект", self.song.title + ".motor.json", "Music Motor (*.motor.json)"
        )
        if path:
            save_project(self.song, path)

    def choose_audio(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Аудиофайл", "", "Audio (*.wav *.mp3 *.flac *.ogg *.m4a *.aac);;Все файлы (*)"
        )
        if path:
            self.audio_path.setText(path)

    def analyze_audio(self):
        if self.audio_thread:
            return
        path = self.audio_path.text()
        if not Path(path).is_file():
            self.show_error("Выберите существующий аудиофайл")
            return
        self.audio_thread = QThread(self)
        options = dict(
            engine=self.audio_engine.currentData(),
            ffmpeg=self.ffmpeg.text(),
            basic_pitch_command=self.basic_pitch.text(),
            polyphony=self.audio_poly.value(),
            sensitivity=self.sensitivity.value(),
            minimum_duration=self.minimum_duration.value(),
            low_note=self.audio_low.value(),
            high_note=self.audio_high.value(),
        )
        self.audio_worker = AudioWorker(path, options)
        self.audio_worker.moveToThread(self.audio_thread)
        self.audio_thread.started.connect(self.audio_worker.run)
        self.audio_worker.done.connect(self.audio_done)
        self.audio_worker.failed.connect(self.show_error)
        self.audio_worker.progress.connect(self.audio_progress.setValue)
        self.audio_worker.finished.connect(self.audio_thread.quit)
        self.audio_worker.finished.connect(self.audio_worker.deleteLater)
        self.audio_thread.finished.connect(self.audio_thread.deleteLater)
        self.audio_thread.finished.connect(self.audio_finished)
        self.analyze_button.setEnabled(False)
        self.audio_progress.setValue(0)
        self.audio_thread.start()

    def audio_done(self, song):
        self.audio_song = song
        self.audio_preview.set_song(song)
        self.audio_accept.setEnabled(True)
        self.audio_progress.setValue(100)
        self.statusBar().showMessage(
            "Найдено %d приблизительных нот. Проверьте piano roll." % len(song.notes)
        )

    def audio_finished(self):
        self.audio_thread = None
        self.analyze_button.setEnabled(True)

    def accept_audio(self):
        if self.audio_song:
            self.set_song(self.audio_song.clone())
            self.tabs.setCurrentIndex(1)

    def save_audio_midi(self):
        if self.audio_song:
            path, _ = QFileDialog.getSaveFileName(
                self, "Промежуточный MIDI", "transcription.mid", "MIDI (*.mid)"
            )
            if path:
                save_midi(self.audio_song, path)

    def append_log(self, text):
        self.log.appendPlainText(datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3] + " " + text)

    def show_error(self, text):
        self.link_error = True
        self.statusBar().showMessage("Ошибка: " + text, 20000)
        if hasattr(self, "log"):
            self.append_log("ERROR " + text)
        if hasattr(self, "cards"):
            self.refresh_cards()

    def save_log(self):
        path, _ = QFileDialog.getSaveFileName(self, "Сохранить журнал", "uart.log", "Log (*.log)")
        if path:
            Path(path).write_text(self.log.toPlainText(), encoding="utf8")

    def collect_settings(self):
        self.config.update(
            port=self.ports.currentData() or "",
            baudrate=int(self.baud.currentText()),
            steps_per_revolution=[w.value() for w in self.step_settings],
            microstep=self.micro_factor.value(),
            microstep_raw=self.raw.value(),
            lookahead_ms=self.lookahead.value(),
            ffmpeg=self.ffmpeg.text(),
            basic_pitch_command=self.basic_pitch.text(),
            min_frequency=self.low_hz.value(),
            max_frequency=self.high_hz.value(),
            max_polyphony=self.poly.value(),
            strategy=self.strategy.currentData(),
            disable_after_stop=self.disable_stop.isChecked(),
        )

    def persist(self):
        self.collect_settings()
        self.motor_config_changed()
        save_settings(self.config, self.settings_path)
        self.refresh_cards()
        self.statusBar().showMessage("Настройки сохранены", 3000)

    def closeEvent(self, event):
        if self.audio_thread:
            self.statusBar().showMessage("Дождитесь окончания аудиоанализа перед закрытием.")
            event.ignore()
            return
        self.player.stop(True)
        self.client.close()
        self.persist()
        event.accept()

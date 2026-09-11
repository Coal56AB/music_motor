"""Bounded PCM synthesis for listening to edited notes without a controller."""
import numpy as np
from midi.model import sounding_spans
from PySide2.QtCore import QObject, QTimer, Signal
from PySide2.QtMultimedia import QAudio, QAudioDeviceInfo, QAudioFormat, QAudioOutput


def preview_notes(song, speed=1.0, transpose=0, allocation=None):
    """Return (start seconds, end seconds, MIDI pitch, velocity) snapshots."""
    if allocation is not None:
        by_id = {n.id: n for n in song.notes}
        return [(a / 1000.0, b / 1000.0, pitch, by_id[uid].velocity)
                for a, b, motor, uid, pitch in allocation.segments if b > a]
    notes = []
    spans = sounding_spans(song)
    for n in song.notes:
        part = song.parts[n.part]
        pitch = n.pitch + part.transpose + transpose
        if part.enabled and n.velocity > 0 and 0 <= pitch <= 127 and n.duration > 0:
            notes.append((song.seconds(n.start, speed),
                          song.seconds(spans.get(n.id, (n.start, n.start, 0))[1], speed), pitch, n.velocity))
    return notes


class Synthesizer:
    """A neutral pitched tone; no GM sound bank or motor acoustics simulation."""
    def __init__(self, notes, rate=44100):
        self.rate = rate
        self.notes = np.asarray(notes, dtype=np.float64).reshape(-1, 4)
        self.total_frames = int(np.ceil(max((n[1] for n in notes), default=0) * rate))

    def render(self, start, count):
        t = (start + np.arange(count, dtype=np.float64)) / self.rate
        mixed = np.zeros(count, dtype=np.float64)
        if count:
            active = self.notes[(self.notes[:, 0] < (start + count) / self.rate)
                                & (self.notes[:, 1] > start / self.rate)]
            for begin, end, pitch, velocity in active:
                age = t - begin
                envelope = np.clip(age / 0.005, 0, 1) * np.clip((end - t) / 0.012, 0, 1)
                frequency = 440.0 * 2 ** ((pitch - 69) / 12)
                phase = 2 * np.pi * frequency * age
                tone = np.sin(phase)
                if frequency * 2 < self.rate / 2:
                    tone += 0.18 * np.sin(phase * 2)
                mixed += tone * envelope * (velocity / 127.0) * 0.18
        # Smooth limiter keeps dense chords in range without per-chunk gain jumps.
        return (np.tanh(mixed) * 30000).astype('<i2').tobytes()


class PreviewPlayer(QObject):
    changed = Signal(str)
    progress = Signal(int)
    failed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.state = 'stopped'
        self.output = None
        self.device = None
        self.synth = None
        self.position_ms = 0
        self.volume = 0.35
        self.pending = b''
        self.timer = QTimer(self)
        self.timer.setInterval(20)
        self.timer.timeout.connect(self.pump)

    @property
    def duration_ms(self):
        return round(self.synth.total_frames * 1000 / self.synth.rate) if self.synth else 0

    def set_state(self, state):
        self.state = state
        self.changed.emit(state)

    def release_output(self):
        self.timer.stop()
        if self.output is not None:
            self.output.reset()
            self.output.deleteLater()
        self.output = self.device = None
        self.pending = b''

    def play(self, notes, start_ms=0):
        self.stop()
        if not notes:
            self.failed.emit('Нет нот для прослушивания. Проверьте включённые партии и выбранный режим.')
            return
        device = QAudioDeviceInfo.defaultOutputDevice()
        if device.isNull():
            self.failed.emit('Не найдено устройство вывода звука Windows.')
            return
        fmt = QAudioFormat()
        fmt.setCodec('audio/pcm')
        fmt.setSampleSize(16)
        fmt.setSampleType(QAudioFormat.SignedInt)
        fmt.setByteOrder(QAudioFormat.LittleEndian)
        for rate, channels in ((44100, 2), (48000, 2), (44100, 1), (48000, 1)):
            fmt.setSampleRate(rate)
            fmt.setChannelCount(channels)
            if device.isFormatSupported(fmt):
                break
        else:
            self.failed.emit('Устройство звука не поддерживает PCM 16 бит, 44.1/48 кГц.')
            return
        self.audio_device, self.fmt = device, fmt
        self.synth = Synthesizer(notes, fmt.sampleRate())
        self.start_output(start_ms)

    def start_output(self, ms, paused=False):
        self.release_output()
        self.position_ms = min(max(0, ms), self.duration_ms)
        self.start_frame = int(self.position_ms * self.synth.rate / 1000)
        self.frame = self.start_frame
        self.output = QAudioOutput(self.audio_device, self.fmt, self)
        self.frame_bytes = 2 * self.fmt.channelCount()
        self.output.setBufferSize(int(self.synth.rate * self.frame_bytes * 0.12))
        self.output.setVolume(self.volume)
        self.device = self.output.start()
        if self.device is None or self.output.error() != QAudio.NoError:
            self.stop()
            self.failed.emit('Не удалось открыть устройство вывода звука Windows.')
            return
        if paused:
            self.output.suspend()
            self.set_state('paused')
            return
        self.set_state('playing')
        self.pump()
        if self.state == 'playing':
            self.timer.start()

    def pump(self):
        if self.state != 'playing' or self.output is None:
            return
        if self.output.error() not in (QAudio.NoError, QAudio.UnderrunError):
            self.stop()
            self.failed.emit('Ошибка вывода звука. Проверьте аудиоустройство Windows.')
            return
        free = self.output.bytesFree()
        if not self.pending and self.frame < self.synth.total_frames:
            count = min(free // self.frame_bytes, 4096, self.synth.total_frames - self.frame)
            if count:
                pcm = self.synth.render(self.frame, count)
                if self.fmt.channelCount() == 2:
                    pcm = np.repeat(np.frombuffer(pcm, dtype='<i2'), 2).tobytes()
                self.pending = pcm
                self.frame += count
        if self.pending and free:
            written = self.device.write(self.pending[:free])
            if written < 0:
                self.stop()
                self.failed.emit('Не удалось передать звук аудиоустройству.')
                return
            self.pending = self.pending[written:]
        # Qt's processed duration excludes pauses and output underruns.
        self.position_ms = min(self.duration_ms, round(self.start_frame * 1000 / self.synth.rate
                               + self.output.processedUSecs() / 1000))
        self.progress.emit(self.position_ms)
        if (self.frame >= self.synth.total_frames and not self.pending
                and self.output.state() == QAudio.IdleState):
            self.stop()

    def pause_resume(self):
        if self.state == 'playing':
            self.timer.stop()
            self.output.suspend()
            self.set_state('paused')
        elif self.state == 'paused':
            self.output.resume()
            self.set_state('playing')
            self.timer.start()

    def seek(self, ms):
        if self.state in ('playing', 'paused'):
            self.start_output(ms, self.state == 'paused')
            self.progress.emit(self.position_ms)

    def set_volume(self, value):
        self.volume = max(0, min(1, value / 100.0))
        if self.output is not None:
            self.output.setVolume(self.volume)

    def stop(self):
        self.release_output()
        self.position_ms = 0
        self.set_state('stopped')
        self.progress.emit(0)

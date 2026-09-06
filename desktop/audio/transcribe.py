"""Approximate transcription, never an instrument separator."""
import math
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import stft, find_peaks, resample_poly
from midi.model import Song, Note, Part, load_midi


def find_ffmpeg(command="ffmpeg"):
    binary = shutil.which(command) or (command if Path(command).is_file() else None)
    if not binary and command == "ffmpeg":
        try:
            import imageio_ffmpeg

            binary = imageio_ffmpeg.get_ffmpeg_exe()
        except (ImportError, RuntimeError):
            pass
    return binary


def read_audio(path, ffmpeg="ffmpeg", sample_rate=22050):
    try:
        if sf.info(str(path)).duration > 1200:
            raise ValueError("Ограничение импорта: 20 минут. Разделите запись на части.")
        samples, rate = sf.read(str(path), always_2d=True, dtype="float32")
        mono = samples.mean(axis=1)
        if rate != sample_rate:
            gcd = math.gcd(rate, sample_rate)
            mono = resample_poly(mono, sample_rate // gcd, rate // gcd)
    except (RuntimeError, sf.LibsndfileError):
        binary = find_ffmpeg(ffmpeg)
        if not binary:
            raise ValueError("Для этого формата нужен FFmpeg. Укажите ffmpeg.exe в настройках.")
        process = subprocess.run(
            [
                binary,
                "-v",
                "error",
                "-i",
                str(path),
                "-t",
                "1201",
                "-f",
                "f32le",
                "-ac",
                "1",
                "-ar",
                str(sample_rate),
                "-",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=600,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if process.returncode:
            raise ValueError(process.stderr.decode("utf8", "replace")[-2000:])
        mono = np.frombuffer(process.stdout, dtype="<f4")
    if len(mono) < 64:
        raise ValueError("Аудиофайл слишком короткий")
    if len(mono) > sample_rate * 1200:
        raise ValueError("Ограничение импорта: 20 минут. Разделите запись на части.")
    return mono, sample_rate


def frames_to_song(frames, hop_seconds, minimum_duration=0.1, title="Аудиотранскрипция"):
    song = Song(title=title, parts={"audio": Part("audio", "Аудио · приблизительные ноты")})
    active = {}
    # Frames contain pitch->normalized confidence; allow a one-frame gap.
    for index, frame in enumerate(frames + [{}, {}]):
        for pitch, confidence in frame.items():
            if pitch not in active:
                active[pitch] = [index, index, confidence]
            active[pitch][1] = index
            active[pitch][2] = max(active[pitch][2], confidence)
        for pitch, (start, end, conf) in list(active.items()):
            if index - end > 1:
                duration = (end - start + 1) * hop_seconds
                if duration >= minimum_duration:
                    song.notes.append(
                        Note(
                            len(song.notes),
                            "audio",
                            start * hop_seconds * 2,
                            duration * 2,
                            pitch,
                            max(1, min(127, round(127 * conf))),
                        )
                    )
                del active[pitch]
    song.notes.sort(key=lambda n: (n.start, n.pitch))
    return song


def spectral_transcribe(
    samples,
    rate,
    polyphony=4,
    sensitivity=0.25,
    minimum_duration=0.1,
    low_note=36,
    high_note=96,
    progress=None,
):
    if not 1 <= polyphony <= 6 or not 0 < sensitivity <= 1 or not 0 <= low_note <= high_note <= 127:
        raise ValueError("Некорректные параметры анализа")
    size, hop = 4096, 256
    if len(samples) < size:
        samples = np.pad(samples, (0, size - len(samples)))
    frequencies, times, spectrum = stft(
        samples, rate, nperseg=size, noverlap=size - hop, boundary="zeros"
    )
    magnitude = np.abs(spectrum)
    global_peak = max(float(magnitude.max()), 1e-9)
    frames = []
    for index in range(magnitude.shape[1]):
        values = magnitude[:, index]
        if float(values.max()) < global_peak * 0.025:
            frames.append({})
            continue
        peaks, _ = find_peaks(
            values, height=max(float(values.max()) * sensitivity, global_peak * 0.018), distance=2
        )
        candidates = []
        for peak in peaks:
            if peak < 1 or peak >= len(values) - 1:
                continue
            logs = np.log(np.maximum(values[peak - 1 : peak + 2], 1e-12))
            denom = logs[0] - 2 * logs[1] + logs[2]
            delta = 0.5 * (logs[0] - logs[2]) / denom if abs(denom) > 1e-12 else 0
            frequency = (peak + np.clip(delta, -0.5, 0.5)) * rate / size
            if frequency <= 0:
                continue
            pitch = round(69 + 12 * math.log2(frequency / 440))
            if low_note <= pitch <= high_note:
                candidates.append((float(values[peak]), pitch, frequency))
        selected = {}
        fundamentals = []
        for amplitude, pitch, frequency in sorted(candidates, reverse=True):
            # Suppress obvious weaker upper harmonics; this can also suppress real octave notes.
            harmonic = any(
                frequency > f * 1.8 and abs(frequency / f - round(frequency / f)) < 0.025
                for f in fundamentals
            )
            if harmonic or pitch in selected:
                continue
            selected[pitch] = min(1.0, amplitude / global_peak)
            fundamentals.append(frequency)
            if len(selected) >= polyphony:
                break
        frames.append(selected)
        if progress and index % 100 == 0:
            progress(int(index * 100 / magnitude.shape[1]))
    return frames_to_song(frames, hop / rate, minimum_duration)


def transcribe(
    path, engine="spectral", ffmpeg="ffmpeg", basic_pitch_command="", progress=None, **options
):
    if engine == "basic_pitch":
        if not basic_pitch_command:
            raise ValueError("Укажите путь к basic-pitch.exe в настройках")
        with tempfile.TemporaryDirectory(prefix="musicmotor_") as temp:
            args = [
                basic_pitch_command,
                temp,
                str(Path(path).resolve()),
                "--minimum-note-length",
                str(options.get("minimum_duration", 0.1) * 1000),
                "--minimum-frequency",
                str(440 * 2 ** ((options.get("low_note", 36) - 69) / 12)),
                "--maximum-frequency",
                str(440 * 2 ** ((options.get("high_note", 96) - 69) / 12)),
            ]
            result = subprocess.run(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=1200,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            if result.returncode:
                raise ValueError(result.stderr.decode("utf8", "replace")[-3000:])
            files = list(Path(temp).glob("*.mid"))
            if not files:
                raise ValueError("Basic Pitch не создал MIDI")
            song = load_midi(files[0])
            return limit_transcription(song, **options)
    samples, rate = read_audio(path, ffmpeg)
    if engine == "pyin":
        try:
            from audio.runtime_compat import prepare_resampling_filters
            prepare_resampling_filters()
            import librosa
        except ImportError:
            raise ValueError("Установите requirements-audio.txt для librosa pYIN")
        low, high = options.get("low_note", 36), options.get("high_note", 96)
        f0, voiced, probability = librosa.pyin(
            samples,
            sr=rate,
            fmin=440 * 2 ** ((low - 69) / 12),
            fmax=440 * 2 ** ((high - 69) / 12),
            frame_length=4096,
            hop_length=256,
        )
        frames = []
        for f, v, confidence in zip(f0, voiced, probability):
            frames.append(
                {round(69 + 12 * math.log2(f / 440)): float(confidence)}
                if v and np.isfinite(f) and confidence >= options.get("sensitivity", 0.25)
                else {}
            )
        return frames_to_song(
            frames, 256 / rate, options.get("minimum_duration", 0.1), "pYIN · монофония"
        )
    return spectral_transcribe(samples, rate, progress=progress, **options)


def limit_transcription(
    song, polyphony=4, minimum_duration=0.1, low_note=36, high_note=96, sensitivity=0.25
):
    active, kept = [], []
    for n in sorted(song.notes, key=lambda n: (n.start, -n.velocity)):
        active = [x for x in active if x.start + x.duration > n.start]
        if (
            not low_note <= n.pitch <= high_note
            or song.seconds(n.start + n.duration) - song.seconds(n.start) < minimum_duration
        ):
            continue
        if len(active) < polyphony and n.velocity >= sensitivity * 127:
            kept.append(n)
            active.append(n)
    song.notes = kept
    return song

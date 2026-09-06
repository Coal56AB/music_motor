from dataclasses import dataclass, field, asdict
from bisect import bisect_right
import copy
import json
import mido


@dataclass
class Part:
    id: str
    name: str
    track: int = 0
    channel: int = 0
    program: int = 0
    enabled: bool = True
    motor: int = -1
    transpose: int = 0
    percussion: bool = False


@dataclass
class Note:
    id: int
    part: str
    start: float
    duration: float
    pitch: int
    velocity: int = 90


@dataclass
class Song:
    notes: list = field(default_factory=list)
    parts: dict = field(default_factory=dict)
    tempos: list = field(default_factory=lambda: [(0.0, 500000)])
    ticks_per_beat: int = 480
    title: str = "Новая композиция"

    def seconds(self, beat, speed=1.0):
        total = 0.0
        previous, tempo = 0.0, 500000
        for at, value in sorted(self.tempos):
            if at > beat:
                break
            total += (at - previous) * tempo / 1e6
            previous, tempo = at, value
        return (total + (beat - previous) * tempo / 1e6) / speed

    def beat_at(self, seconds, speed=1.0):
        lo, hi = 0.0, max(1.0, self.end + 1)
        for _ in range(40):
            mid = (lo + hi) / 2
            if self.seconds(mid, speed) < seconds:
                lo = mid
            else:
                hi = mid
        return (lo + hi) / 2

    @property
    def end(self):
        return max((n.start + n.duration for n in self.notes), default=0)

    def clone(self):
        return copy.deepcopy(self)


def load_midi(path):
    mid = mido.MidiFile(str(path))
    if mid.type == 2:
        raise ValueError("MIDI type 2 has independent timelines; convert to type 0/1 first")
    if mid.ticks_per_beat <= 0:
        raise ValueError("SMPTE division is not supported; convert to PPQN MIDI")
    song = Song(
        ticks_per_beat=mid.ticks_per_beat, title=str(path).replace("\\", "/").split("/")[-1]
    )
    tempos, uid = {0: 500000}, 0
    for ti, track in enumerate(mid.tracks):
        tick, programs, active = 0, [0] * 16, {}
        name = next((m.name for m in track if m.type == "track_name"), "Track %d" % (ti + 1))
        try:
            name = name.encode("latin1").decode("utf8")
        except (UnicodeEncodeError, UnicodeDecodeError):
            pass
        for msg in track:
            tick += msg.time
            beat = tick / mid.ticks_per_beat
            if msg.type == "set_tempo":
                tempos[beat] = msg.tempo
            elif msg.type == "program_change":
                programs[msg.channel] = msg.program
            elif msg.type == "note_on" and msg.velocity:
                ch, pitch = msg.channel, msg.note
                program = programs[ch]
                pid = "%d:%d:%d" % (ti, ch, program)
                if pid not in song.parts:
                    song.parts[pid] = Part(
                        pid,
                        "%s · ch %d · GM %d" % (name, ch + 1, program + 1),
                        ti,
                        ch,
                        program,
                        ch != 9,
                        percussion=ch == 9,
                    )
                active.setdefault((ch, pitch), []).append((beat, msg.velocity, pid))
            elif msg.type in ("note_off", "note_on"):
                stack = active.get((msg.channel, msg.note), [])
                if stack:
                    start, velocity, pid = stack.pop(0)
                    song.notes.append(
                        Note(
                            uid,
                            pid,
                            start,
                            max(1 / mid.ticks_per_beat, beat - start),
                            msg.note,
                            velocity,
                        )
                    )
                    uid += 1
        for (ch, pitch), stack in active.items():
            for start, velocity, pid in stack:
                song.notes.append(
                    Note(
                        uid,
                        pid,
                        start,
                        max(0.25, tick / mid.ticks_per_beat - start),
                        pitch,
                        velocity,
                    )
                )
                uid += 1
    song.tempos = sorted(tempos.items())
    song.notes.sort(key=lambda n: (n.start, n.id))
    return song


def save_midi(song, path):
    mid = mido.MidiFile(type=1, ticks_per_beat=song.ticks_per_beat, charset="utf8")
    tempo_track = mido.MidiTrack()
    mid.tracks.append(tempo_track)
    previous = 0
    for beat, tempo in sorted(song.tempos):
        tick = round(beat * song.ticks_per_beat)
        tempo_track.append(mido.MetaMessage("set_tempo", tempo=tempo, time=tick - previous))
        previous = tick
    for part in song.parts.values():
        track = mido.MidiTrack()
        mid.tracks.append(track)
        track.append(mido.MetaMessage("track_name", name=part.name))
        events = []
        for n in song.notes:
            if n.part == part.id:
                start = round(n.start * song.ticks_per_beat)
                end = max(start + 1, round((n.start + n.duration) * song.ticks_per_beat))
                events.extend([(start, 1, n), (end, 0, n)])
        previous = 0
        for tick, on, n in sorted(events, key=lambda e: (e[0], e[1], e[2].id)):
            if on:
                # Parts can share a channel after Program Change. Restore the program at
                # each attack, instead of placing conflicting changes at time zero.
                track.append(
                    mido.Message(
                        "program_change",
                        channel=part.channel,
                        program=part.program,
                        time=tick - previous,
                    )
                )
                previous = tick
            track.append(
                mido.Message(
                    "note_on" if on else "note_off",
                    channel=part.channel,
                    note=n.pitch,
                    velocity=n.velocity if on else 0,
                    time=tick - previous,
                )
            )
            previous = tick
    mid.save(str(path))


def save_project(song, path):
    with open(path, "w", encoding="utf8") as f:
        json.dump(asdict(song), f, ensure_ascii=False, indent=2)


def load_project(path):
    with open(path, encoding="utf8") as f:
        data = json.load(f)
    data["notes"] = [Note(**n) for n in data["notes"]]
    data["parts"] = {k: Part(**p) for k, p in data["parts"].items()}
    data["tempos"] = [tuple(t) for t in data["tempos"]]
    return Song(**data)


def demo_song():
    song = Song(title="Clockwork • демонстрация")
    song.parts = {
        str(i): Part(str(i), name, i, i, program)
        for i, (name, program) in enumerate([("Мелодия", 0), ("Бас", 32), ("Арпеджио", 10)])
    }
    uid = 0
    for bar, root in enumerate([60, 57, 53, 55] * 2):
        for k, delta in enumerate([0, 4, 7, 12, 7, 4, 2, 7]):
            song.notes.append(Note(uid, "0", bar * 4 + k * 0.5, 0.42, root + delta, 95))
            uid += 1
        song.notes.append(Note(uid, "1", bar * 4, 3.8, root - 12, 110))
        uid += 1
        for k, delta in enumerate([0, 7, 4, 7]):
            song.notes.append(Note(uid, "2", bar * 4 + k, 0.85, root + delta, 65))
            uid += 1
    return song

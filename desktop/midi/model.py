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
    source: int = 0


@dataclass
class Note:
    id: int
    part: str
    start: float
    duration: float
    pitch: int
    velocity: int = 90
    motor: int = -1  # Individual pin; -1 leaves the choice to the allocator.
    on_order: int = -1
    off_order: int = -1


@dataclass
class Control:
    at: float
    channel: int
    control: int
    value: int
    order: int = -1
    source: int = 0


@dataclass
class Song:
    notes: list = field(default_factory=list)
    parts: dict = field(default_factory=dict)
    tempos: list = field(default_factory=lambda: [(0.0, 500000)])
    ticks_per_beat: int = 480
    title: str = "Новая композиция"
    controls: list = field(default_factory=list)
    length: float = 0.0  # Includes end-of-track silence and pedal tails.

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
        return max(self.length, max((c.at for c in self.controls), default=0),
                   max((n.start + n.duration for n in self.notes), default=0))

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
    tempos, events, names = {0: 500000}, [], {}
    for ti, track in enumerate(mid.tracks):
        tick, source = 0, 0
        name = next((m.name for m in track if m.type == "track_name"), "Track %d" % (ti + 1))
        try:
            name = name.encode("latin1").decode("utf8")
        except (UnicodeEncodeError, UnicodeDecodeError):
            pass
        names[ti] = name
        for index, msg in enumerate(track):
            tick += msg.time
            if msg.type == 'midi_port':
                source = msg.port
            events.append((tick, ti, index, source, msg))
        song.length = max(song.length, tick / mid.ticks_per_beat)
    active, programs = {}, {}

    def finish(note, beat, order):
        note.duration = max(0.0, beat - note.start)
        note.off_order = order

    # A track is editorial metadata, not an independent MIDI channel.
    for order, (tick, ti, index, source, msg) in enumerate(sorted(events, key=lambda e: e[:3])):
        beat = tick / mid.ticks_per_beat
        if msg.type == 'set_tempo':
            tempos[beat] = msg.tempo
        elif msg.type == 'program_change':
            programs[source, msg.channel] = msg.program
        elif msg.type == 'control_change':
            song.controls.append(Control(beat, msg.channel, msg.control, msg.value, order, source))
            if msg.control in (120, 123, 124, 125, 126, 127):
                for key, stack in active.items():
                    if key[:2] == (source, msg.channel):
                        for note in stack:
                            finish(note, beat, order)
                        stack.clear()
        elif msg.type == 'note_on' and msg.velocity:
            ch, pitch = msg.channel, msg.note
            program = programs.get((source, ch), 0)
            pid = '%d:%d:%d:%d' % (ti, source, ch, program)
            if pid not in song.parts:
                song.parts[pid] = Part(pid, '%s · ch %d · GM %d' % (names[ti], ch + 1, program + 1),
                                      ti, ch, program, ch != 9, percussion=ch == 9, source=source)
            note = Note(len(song.notes), pid, beat, 0, pitch, msg.velocity, on_order=order)
            song.notes.append(note)
            active.setdefault((source, ch, pitch), []).append(note)
        elif msg.type in ('note_on', 'note_off'):
            stack = active.get((source, msg.channel, msg.note), [])
            if stack:
                finish(stack.pop(0), beat, order)
    for stack in active.values():
        for note in stack:
            finish(note, song.length, len(events))
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
    # One ordered stream per MIDI port preserves same-tick CC/On/Off ordering
    # across parts. All source notes are exported, including unallocated ones.
    sources = sorted({p.source for p in song.parts.values()} | {c.source for c in song.controls})
    for source in sources:
        track = mido.MidiTrack()
        mid.tracks.append(track)
        track.append(mido.MetaMessage('track_name', name=song.title))
        track.append(mido.MetaMessage('midi_port', port=source))
        previous = 0
        for beat, order, kind, obj in midi_timeline(song):
            part = song.parts[obj.part] if kind != 'cc' else None
            if (part.source if part else obj.source) != source:
                continue
            tick = round(beat * song.ticks_per_beat)
            if kind == 'on':
                track.append(mido.Message('program_change', channel=part.channel,
                                          program=part.program, time=tick - previous))
                previous = tick
            if kind == 'cc':
                msg = mido.Message('control_change', channel=obj.channel, control=obj.control, value=obj.value)
            else:
                msg = mido.Message('note_on' if kind == 'on' else 'note_off', channel=part.channel,
                                   note=obj.pitch, velocity=obj.velocity if kind == 'on' else 0)
            track.append(msg.copy(time=tick - previous))
            previous = tick
        track.append(mido.MetaMessage('end_of_track', time=max(0, round(song.end * song.ticks_per_beat) - previous)))
    mid.save(str(path))


def midi_timeline(song):
    events = []
    for n in song.notes:
        events.append((n.start, n.on_order if n.on_order >= 0 else 1000000000 + n.id, 'on', n))
        events.append((n.start + n.duration, n.off_order if n.off_order >= 0 else -1, 'off', n))
    events.extend((c.at, c.order if c.order >= 0 else -2, 'cc', c) for c in song.controls)
    return sorted(events, key=lambda e: (e[0], e[1], {'off': 0, 'cc': 1, 'on': 2}[e[2]]))


def sounding_spans(song):
    """Interpret note lifetimes before allocating motors; never edit source notes.

    Returns id -> (start beat, sound end beat, physical release beat).
    Repeated attacks remain separate; output allocation can merge equal pitches.
    """
    sounding, down, sustain, result = {}, set(), {}, {}
    releases = {n.id: n.start + n.duration for n in song.notes}

    def close(uid, at):
        n = sounding.pop(uid)
        result[uid] = (n.start, at, releases[uid])
        down.discard(uid)

    for at, order, kind, obj in midi_timeline(song):
        if kind == 'on':
            if obj.velocity > 0 and (obj.duration > 0 or 0 <= obj.on_order < obj.off_order):
                sounding[obj.id] = obj
                down.add(obj.id)
        elif kind == 'off':
            down.discard(obj.id)
            p = song.parts[obj.part]
            if obj.id in sounding and not sustain.get((p.source, p.channel), False):
                close(obj.id, at)
        else:
            channel = (obj.source, obj.channel)
            if obj.control == 64:
                sustain[channel] = obj.value >= 64
            elif obj.control == 121:
                sustain[channel] = False
            for uid, n in list(sounding.items()):
                p = song.parts[n.part]
                if (p.source, p.channel) != channel:
                    continue
                if obj.control in (123, 124, 125, 126, 127) and uid in down:
                    down.remove(uid)
                    releases[uid] = at
                if obj.control == 120 or (uid not in down and not sustain.get(channel, False)):
                    close(uid, at)
    for uid in list(sounding):
        close(uid, song.end)
    return result


def save_project(song, path):
    with open(path, "w", encoding="utf8") as f:
        json.dump(asdict(song), f, ensure_ascii=False, indent=2)


def load_project(path):
    with open(path, encoding="utf8") as f:
        data = json.load(f)
    data["notes"] = [Note(**n) for n in data["notes"]]
    data["parts"] = {k: Part(**p) for k, p in data["parts"].items()}
    data["tempos"] = [tuple(t) for t in data["tempos"]]
    data['controls'] = [Control(**c) for c in data.get('controls', [])]
    return Song(**data)


def demo_song():
    song = Song(title="Clockwise • демонстрация")
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

from dataclasses import dataclass, field
from app.music_math import fit_note, note_frequency, frequency_note_range

STRATEGIES = {
    "stable": "Стабильное распределение",
    "melody_bass": "Мелодия + бас",
    "loudest": "Самые громкие",
    "highest": "Верхние ноты",
    "lowest": "Нижние ноты",
    "parts": "Выбранные партии",
    "manual": "Ручное назначение",
}
MOTOR_COLORS = ["#63dbc0", "#7fb4ff", "#e4a2ff", "#ffcc72", "#ff8da5", "#a8dc78"]


@dataclass
class Allocation:
    events: list = field(default_factory=list)
    assignments: dict = field(default_factory=dict)
    skipped: dict = field(default_factory=dict)
    segments: list = field(default_factory=list)
    duration_ms: int = 0


def allocate(
    song,
    installed_mask=15,
    music_mask=15,
    polyphony=6,
    strategy="stable",
    low_hz=20,
    high_hz=4000,
    transpose=0,
    octave=True,
    speed=1.0,
    include_drums=False,
):
    if speed <= 0 or polyphony < 1:
        raise ValueError("Tempo and polyphony must be positive")
    motors = [i for i in range(6) if installed_mask & music_mask & (1 << i)]
    limit = min(polyphony, len(motors))
    result, timeline, pitches = Allocation(), [], {}
    low, high = frequency_note_range(low_hz, high_hz)
    for n in song.notes:
        p = song.parts[n.part]
        reason = None
        if not p.enabled:
            reason = "Партия выключена"
        elif p.percussion and not include_drums:
            reason = "Ударный канал"
        elif p.motor >= 0 and p.motor not in motors:
            reason = "Назначенный двигатель недоступен"
        elif strategy == "manual" and p.motor < 0:
            reason = "Нет ручного назначения"
        pitch = fit_note(n.pitch + transpose + p.transpose, low, high, octave)
        if pitch is None:
            reason = "Нота вне диапазона"
        if not motors:
            reason = "Нет доступных двигателей"
        if reason:
            result.skipped[n.id] = reason
            continue
        pitches[n.id] = pitch
        start = round(song.seconds(n.start, speed) * 1000)
        end = max(start + 1, round(song.seconds(n.start + n.duration, speed) * 1000))
        timeline.extend([(start, 1, n), (end, 0, n)])
    active, segments, last_pitch = {}, {}, {}

    def rank(n):
        pitch = pitches[n.id]
        if strategy == "loudest":
            return (n.velocity, pitch)
        if strategy == "highest":
            return (pitch, n.velocity)
        if strategy == "lowest":
            return (-pitch, n.velocity)
        if strategy == "parts":
            return (-list(song.parts).index(n.part), n.velocity)
        if strategy == "melody_bass":
            candidates = list(active.values()) + [n]
            extremes = (
                min(pitches[x.id] for x in candidates),
                max(pitches[x.id] for x in candidates),
            )
            return (int(pitch in extremes), n.velocity)
        return (n.velocity, -n.start)

    def release(motor, at, stolen=False):
        old = active.pop(motor)
        result.events.append((at, motor, 0, 0))
        start, pitch = segments.pop(motor)
        result.segments.append((start, at, motor, old.id, pitch))
        if stolen:
            result.skipped[old.id] = "Прервана: note stealing в %.3f с" % (at / 1000)

    # Batch starts with the same timestamp by priority, so ties are deterministic.
    grouped = {}
    for at, on, n in timeline:
        grouped.setdefault(at, [[], []])[on].append(n)
    for at, (offs, ons) in sorted(grouped.items()):
        for n in offs:
            for motor, current in list(active.items()):
                if current.id == n.id:
                    release(motor, at)
        if strategy == "melody_bass" and ons:
            ordered = sorted(ons, key=lambda n: pitches[n.id])
            priority = [ordered[-1]] + ([ordered[0]] if len(ordered) > 1 else [])
            priority += sorted(ordered[1:-1], key=lambda n: n.velocity, reverse=True)
        else:
            priority = sorted(ons, key=rank, reverse=True)
        for n in priority:
            p, pitch = song.parts[n.part], pitches[n.id]
            pool = [p.motor] if p.motor >= 0 else motors
            free = [m for m in pool if m not in active] if len(active) < limit else []
            if free:
                motor = min(free, key=lambda m: (abs(last_pitch.get(m, pitch) - pitch), m))
            else:
                victims = [m for m in pool if m in active]
                if not victims:
                    result.skipped[n.id] = "Лимит полифонии"
                    continue
                motor = min(victims, key=lambda m: rank(active[m]))
                if rank(n) <= rank(active[motor]):
                    result.skipped[n.id] = "Лимит полифонии / ниже приоритет"
                    continue
                release(motor, at, True)
            active[motor], last_pitch[motor] = n, pitch
            segments[motor] = (at, pitch)
            result.assignments[n.id] = motor
            result.events.append((at, motor, 1, round(note_frequency(pitch) * 1000)))
    result.events.sort(key=lambda e: (e[0], e[2], e[1]))
    result.duration_ms = max(
        round(song.seconds(song.end, speed) * 1000), max((e[0] for e in result.events), default=0)
    )
    result.events.append(
        (result.duration_ms + 2, 255, 2, 0)
    )  # Explicit end marker; empty queue is underrun.
    return result

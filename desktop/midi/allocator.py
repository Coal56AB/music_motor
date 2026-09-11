from dataclasses import dataclass, field
from app.music_math import fit_note, note_frequency, frequency_note_range
from midi.model import sounding_spans
from midi.harmony import Lines

STRATEGIES = {
    'intelligent': 'Аккорды, мелодия и Sustain',
    'stable': 'Стабильное распределение',
    'melody_bass': 'Мелодия + бас',
    'loudest': 'Самые громкие',
    'highest': 'Верхние ноты',
    'lowest': 'Нижние ноты',
    'parts': 'Выбранные партии',
    'manual': 'Ручное назначение',
}
MOTOR_COLORS = ['#63dbc0', '#7fb4ff', '#e4a2ff', '#ffcc72', '#ff8da5', '#a8dc78']


def pin_conflict_spans(song, installed_mask=63, music_mask=63, polyphony=6):
    """Read-only validation: mark exact overlapping sounding intervals of pins.

    Does not invoke the allocator or alter any existing motor assignments.
    """
    spans = sounding_spans(song)
    events = {}
    for n in song.notes:
        span = spans.get(n.id)
        if n.motor < 0 or not span or span[1] <= span[0]:
            continue
        events.setdefault(span[0], [[], []])[1].append(n)
        events.setdefault(span[1], [[], []])[0].append(n)
    limit = min(polyphony, sum(bool(installed_mask & music_mask & (1 << m)) for m in range(6)))
    active, conflicts = {}, {}
    previous = None
    for at, (offs, ons) in sorted(events.items()):
        if previous is not None and at > previous:
            motors = {}
            for n in active.values():
                motors.setdefault(n.motor, []).append(n)
            for n in active.values():
                reason = None
                if len(motors[n.motor]) > 1:
                    reason = 'Пересечение закреплённых нот на M%d' % (n.motor + 1)
                elif len(active) > limit:
                    reason = 'Превышен лимит полифонии закреплённых нот'
                if reason:
                    ranges = conflicts.setdefault(n.id, [])
                    if ranges and ranges[-1][1] == previous and ranges[-1][2] == reason:
                        ranges[-1] = (ranges[-1][0], at, reason)
                    else:
                        ranges.append((previous, at, reason))
        for n in offs:
            active.pop(n.id, None)
        for n in ons:
            active[n.id] = n
        previous = at
    return conflicts


@dataclass
class Allocation:
    events: list = field(default_factory=list)
    assignments: dict = field(default_factory=dict)
    skipped: dict = field(default_factory=dict)
    segments: list = field(default_factory=list)
    duration_ms: int = 0
    visual_segments: dict = field(default_factory=dict)
    sound_ends: dict = field(default_factory=dict)
    note_range: tuple = None


def allocate(song, installed_mask=15, music_mask=15, polyphony=6,
             strategy='intelligent', low_hz=20, high_hz=4000, transpose=0,
             octave=True, speed=1.0, include_drums=False):
    if speed <= 0 or polyphony < 1:
        raise ValueError('Tempo and polyphony must be positive')
    motors = [i for i in range(6) if installed_mask & music_mask & (1 << i)]
    limit = min(polyphony, len(motors))
    result, grouped, pitches, pools = Allocation(), {}, {}, {}
    spans = sounding_spans(song)
    result.sound_ends = {uid: span[1] for uid, span in spans.items()}
    low, high = frequency_note_range(low_hz, high_hz)
    part_order = {pid: i for i, pid in enumerate(song.parts)}
    for n in song.notes:
        p = song.parts[n.part]
        motor = n.motor if n.motor >= 0 else p.motor
        reason = None
        if not p.enabled:
            reason = 'Партия выключена'
        elif p.percussion and not include_drums:
            reason = 'Ударный канал'
        elif motor >= 0 and motor not in motors:
            reason = 'Назначенный двигатель недоступен'
        elif strategy == 'manual' and motor < 0:
            reason = 'Нет ручного назначения'
        pitch = fit_note(n.pitch + transpose + p.transpose, low, high, octave)
        if pitch is None:
            reason = 'Нота вне диапазона'
        if not motors:
            reason = 'Нет доступных двигателей'
        span = spans.get(n.id)
        if not span or span[1] <= span[0]:
            reason = 'Нет звучащего интервала'
        if reason:
            if n.motor >= 0:
                raise ValueError('Закреплённая нота #%d: %s' % (n.id, reason))
            result.skipped[n.id] = reason
            continue
        pitches[n.id] = pitch
        pools[n.id] = [motor] if motor >= 0 else motors
        start, end, release = span
        grouped.setdefault(start, [[], []])[1].append(n)
        grouped.setdefault(end, [[], []])[0].append(n)
        if start < release < end:
            grouped.setdefault(release, [[], []])

    sounding, active, starts, last_pitch = {}, {}, {}, {}
    lines = Lines()
    release_times = {uid: song.seconds(span[2], speed) for uid, span in spans.items()}

    def finish(motor, beat):
        old = active[motor]
        begin = starts.pop(motor)
        if beat > begin:
            a = round(song.seconds(begin, speed) * 1000)
            b = round(song.seconds(beat, speed) * 1000)
            result.segments.append((a, b, motor, old.id, pitches[old.id]))
            result.visual_segments.setdefault(old.id, []).append((begin, beat, motor))

    for beat, (offs, ons) in sorted(grouped.items()):
        now = song.seconds(beat, speed)
        at = round(now * 1000)
        for n in offs:
            sounding.pop(n.id, None)
        for n in sorted(ons, key=lambda n: (n.on_order if n.on_order >= 0 else n.id, n.id)):
            p = song.parts[n.part]
            lines.attack(n, now, (p.source, p.channel), list(sounding.values()), release_times)
            sounding[n.id] = n
        musical = lines.scores(sounding.values())
        old_ids = {n.id for n in active.values()}
        old_pitches = {n.pitch for n in active.values()}
        extremes = {min((pitches[n.id] for n in sounding.values()), default=0),
                    max((pitches[n.id] for n in sounding.values()), default=0)}

        def rank(n):
            pitch = pitches[n.id]
            down = now < release_times[n.id]
            if strategy == 'intelligent':
                debt = 0 if down else 20 + min(240, int(max(0, now - release_times[n.id]) * 80))
                score = musical[n.pitch] + (12 if n.pitch in old_pitches else 0) - debt
                return (score, down, release_times[n.id] if not down else 0,
                        n.on_order if not down else 0, n.id in old_ids, -pitch, -n.id)
            if strategy == 'loudest':
                return (n.velocity, pitch, -n.id)
            if strategy == 'highest':
                return (pitch, n.velocity, -n.id)
            if strategy == 'lowest':
                return (-pitch, n.velocity, -n.id)
            if strategy == 'parts':
                return (-part_order[n.part], n.velocity, -n.id)
            if strategy == 'melody_bass':
                return (pitch in extremes, n.velocity, -n.id)
            return (n.velocity, -n.start, -n.id)

        chosen = {}
        # Pins are hard constraints. Conflicting pins never silently steal.
        for n in sorted(sounding.values(), key=lambda n: n.id):
            if n.motor < 0:
                continue
            if n.motor in chosen:
                raise ValueError('Конфликт M%d: закреплённые ноты #%d и #%d пересекаются в %.3f с' %
                                 (n.motor + 1, chosen[n.motor].id, n.id, now))
            chosen[n.motor] = n
        if len(chosen) > limit:
            raise ValueError('Закреплённых нот больше лимита полифонии в %.3f с' % now)
        selected_pitches = {n.pitch for n in chosen.values()}

        def place(n, trial, visited):
            # Augment the six-slot matching without dropping higher-priority
            # choices. An unconstrained voice must not block a part's only motor.
            ordered = sorted(pools[n.id], key=lambda m: (
                not (m in active and active[m].id == n.id),
                m in trial,
                not (m in active and pitches[active[m].id] == pitches[n.id]),
                abs(last_pitch.get(m, pitches[n.id]) - pitches[n.id]), m))
            for motor in ordered:
                if motor in visited:
                    continue
                visited.add(motor)
                previous = trial.get(motor)
                if previous is None or (previous.motor < 0 and place(previous, trial, visited)):
                    trial[motor] = n
                    return True
            return False

        for n in sorted(sounding.values(), key=rank, reverse=True):
            if n.motor >= 0 or len(chosen) >= limit:
                continue
            if strategy == 'intelligent' and n.pitch in selected_pitches:
                continue
            trial = chosen.copy()
            if place(n, trial, set()):
                chosen = trial
                selected_pitches.add(n.pitch)
        for motor, old in active.items():
            new = chosen.get(motor)
            if new is None or new.id != old.id:
                finish(motor, beat)
            if new is None or pitches[new.id] != pitches[old.id]:
                result.events.append((at, motor, 0, 0))
        for motor, n in chosen.items():
            old = active.get(motor)
            if old is None or old.id != n.id:
                starts[motor] = beat
            if old is None or pitches[old.id] != pitches[n.id]:
                result.events.append((at, motor, 1, round(note_frequency(pitches[n.id]) * 1000)))
            last_pitch[motor] = pitches[n.id]
            result.assignments[n.id] = motor
        active = chosen
    for uid in pitches:
        start, end, release = spans[uid]
        played = sum(b - a for a, b, m in result.visual_segments.get(uid, []))
        if played + 1e-9 < end - start:
            result.skipped[uid] = ('Частично сыграна: ограничение голосов / приоритет' if played else
                                   'Не выбрана: ограничение голосов / приоритет')
    # Keep source chronology even when distinct beats round to one millisecond.
    result.duration_ms = max(round(song.seconds(song.end, speed) * 1000),
                             max((e[0] for e in result.events), default=0))
    result.events.append((result.duration_ms + 2, 255, 2, 0))
    if result.segments:
        pitches_used = [segment[4] for segment in result.segments]
        result.note_range = (min(pitches_used), max(pitches_used))
    return result

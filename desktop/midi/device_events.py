"""Transport preparation only; STM32 chooses voices and assigns motors."""
from midi.model import midi_timeline
from app.music_math import fit_note, frequency_note_range

STRATEGIES = ('intelligent','stable','melody_bass','loudest','highest','lowest','parts','manual')

def device_events(song, mask, polyphony=6, strategy='intelligent', low_hz=20,
                  high_hz=1200, transpose=0, octave=True, speed=1.0, include_drums=False):
    low, high = frequency_note_range(low_hz, min(1200, high_hz))
    # kind 5 sets the musical mask, polyphony and selection strategy.
    rows = [(0, mask, 0x85, min(6, polyphony) | (STRATEGIES.index(strategy) << 8))]
    admitted = {}
    part_order = {pid: i for i,pid in enumerate(song.parts)}
    if len(part_order)>128:
        raise ValueError('Для STM32 допускается не больше 128 редакторских партий')
    for n in song.notes:
        p = song.parts[n.part]
        pin = n.motor if n.motor >= 0 else p.motor
        pitch = fit_note(n.pitch + transpose + p.transpose, low, high, octave)
        if (not p.enabled or (p.percussion and not include_drums) or pitch is None or
                (pin >= 0 and not mask & (1 << pin)) or (strategy == 'manual' and pin < 0) or
                (n.duration <= 0 and not 0 <= n.on_order < n.off_order)):
            continue
        if not 0 <= p.source < 16 or not 0 <= p.channel < 16:
            raise ValueError('STM32 поддерживает MIDI-порты и каналы от 0 до 15')
        admitted[n.id] = (pitch, pin + 1, (p.source << 4) | p.channel,part_order[n.part])
    timeline = []
    for beat, order, kind, obj in midi_timeline(song):
        at = round(song.seconds(beat) * 1000 / speed)
        if kind == 'cc':
            if not 0 <= obj.source < 16 or not 0 <= obj.channel < 16:
                raise ValueError('STM32 поддерживает MIDI-порты и каналы от 0 до 15')
            timeline.append((at, (obj.source << 4) | obj.channel, 2, obj.control | (obj.value << 8)))
        elif obj.id in admitted:
            pitch, pin, address, priority = admitted[obj.id]
            timeline.append((at, address, 0 if kind == 'on' else 1,
                             pitch | ((obj.velocity if kind == 'on' else 0) << 8) | (pin << 16) | (priority << 24)))
    for i, (at, address, kind, value) in enumerate(timeline):
        last = i + 1 == len(timeline) or timeline[i + 1][0] != at
        rows.append((at, address, kind | (0x80 if last else 0), value))
    end = max(round(song.seconds(song.end) * 1000 / speed), rows[-1][0])
    rows.append((end, 0, 0x83, 0))
    return rows

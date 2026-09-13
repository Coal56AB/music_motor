import math

TIMER_HZ = 1_000_000
MIN_HZ = 20
MAX_HZ = 1200
MICROSTEPS = {"Полный шаг": (0, 1), "1/2": (1, 2), "1/4": (2, 4), "1/8": (3, 8), "1/16": (7, 16)}


def note_frequency(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def half_period(frequency):
    if not MIN_HZ <= frequency <= MAX_HZ:
        raise ValueError("STEP frequency must be 20–1200 Hz")
    return int(TIMER_HZ / (2 * frequency) + 0.5)


def actual_frequency(frequency):
    return TIMER_HZ / (2 * half_period(frequency))


def rpm(frequency, steps=200, microstep=1):
    if steps <= 0 or microstep <= 0:
        raise ValueError("Steps and microstep must be positive")
    return frequency * 60 / (steps * microstep)


def note_name(note):
    if note is None or note == 255:
        return "—"
    return ("C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B")[note % 12] + str(
        note // 12 - 1
    )


def fit_note(note, low, high, octave=True):
    if low > high:
        raise ValueError("Invalid note range")
    if low <= note <= high:
        return note
    if not octave:
        return None
    candidates = [n for n in range(low, high + 1) if (n - note) % 12 == 0]
    return min(candidates, key=lambda n: abs(n - note)) if candidates else None


def frequency_note_range(low, high):
    return (
        max(0, math.ceil(69 + 12 * math.log2(low / 440))),
        min(127, math.floor(69 + 12 * math.log2(high / 440))),
    )

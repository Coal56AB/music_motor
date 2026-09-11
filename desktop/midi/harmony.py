"""Instance-aware desktop counterpart of ESP32's musical scoring.

Weights/templates are deliberately identical; regression fixtures cover parity.
"""
CHORDS = ((0, 4, 7), (0, 3, 7), (0, 3, 6), (0, 4, 8), (0, 2, 7),
          (0, 5, 7), (0, 4, 7, 10), (0, 4, 7, 11), (0, 3, 7, 10))


def recognize(mask):
    for chord in CHORDS:
        for root in range(12):
            if {(root + i) % 12 for i in chord} == mask:
                return root, chord, 100
    for chord in CHORDS[:2]:
        for root in range(12):
            if {root, (root + chord[1]) % 12} == mask:
                return root, chord, 55
    return None


class Lines:
    def __init__(self):
        self.tracks = [None] * 32
        self.assignments = {}

    def attack(self, note, at, channel, sounding, release_times):
        best, cost = None, 100000
        occupied = {self.assignments.get(n.id) for n in sounding}
        held = {self.assignments.get(n.id) for n in sounding if release_times[n.id] > at}
        for i, t in enumerate(self.tracks):
            if t is None or t['channel'] != channel or not 0 <= at - t['at'] <= .6:
                continue
            if i in held and at - t['at'] <= .04:
                continue
            d = note.pitch - t['pitch']
            if abs(d) > 7:
                continue
            direction = (d > 0) - (d < 0)
            value = abs(d) * 10 + (8 if direction and t['direction'] and direction != t['direction'] else 0) - t['confidence'] * 3
            if value < cost:
                best, cost = i, value
        continued = best is not None
        if best is None:
            free = [i for i in range(32) if i not in occupied]
            if not free:
                self.assignments[note.id] = None
                return
            best = min(free, key=lambda i: self.tracks[i]['at'] if self.tracks[i] else 0)
        old = self.tracks[best]
        d = note.pitch - old['pitch'] if continued else 0
        self.tracks[best] = dict(at=at, pitch=note.pitch, channel=channel,
                                 direction=(d > 0) - (d < 0),
                                 confidence=min(4, old['confidence'] + 1) if continued else 0)
        self.assignments[note.id] = best

    def scores(self, notes):
        pitches, confidence, velocity = set(), {}, {}
        for n in notes:
            pitches.add(n.pitch)
            velocity[n.pitch] = max(velocity.get(n.pitch, 0), n.velocity)
            track = self.assignments.get(n.id)
            t = self.tracks[track] if track is not None else None
            c = t['confidence'] if t and t['pitch'] == n.pitch else 0
            confidence[n.pitch] = max(confidence.get(n.pitch, 0), c)
        groups = []
        for pitch in sorted(pitches):
            if confidence[pitch] >= 2:
                continue
            if not groups or pitch - groups[-1][-1] >= 13 or pitch - groups[-1][0] > 24:
                groups.append([])
            groups[-1].append(pitch)
        scores = {p: velocity[p] // 16 for p in pitches}
        for p in pitches:
            if confidence[p] >= 2:
                scores[p] += 400 + confidence[p] * 20 + (160 if p < 48 else 0)
        for group in groups:
            harmony = recognize({p % 12 for p in group})
            seen = set()
            for p in group:
                if len(group) == 1:
                    scores[p] += 260
                if harmony:
                    root, chord, certainty = harmony
                    interval = (p - root) % 12
                    scores[p] += (180 if interval == 0 else
                                  (110 if certainty == 100 else 20) if interval == chord[1] else
                                  90 if len(chord) == 4 and interval == chord[3] else 40)
                elif p == group[0]:
                    scores[p] += 90
                if p % 12 in seen:
                    scores[p] -= 220
                seen.add(p % 12)
        return scores

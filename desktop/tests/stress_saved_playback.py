"""Run after compiling native_saved_playback_test; virtual time, no hardware I/O."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'desktop'))
from midi.device_events import device_events
from midi.examples import imperial_march, stored_example

exe = ROOT / 'build/midi-tests/native_saved_playback_test.exe'
songs = [('imperial', imperial_march())] + [
    (name, stored_example(name)) for name in
    ('rachmaninoff_op23_5', 'rachmaninoff_op3_2', 'playing_god', 'od')]
runs = 0
for name, song in songs:
    data = ''.join(' '.join(map(str, e)) + '\n' for e in
                   device_events(song, 63, polyphony=6, strategy='stable'))
    for loss, delay in ((0, 1), (0, 10), (10, 10), (25, 10)):
        for seed in range(1, 11):
            result = subprocess.run([str(exe), str(loss), str(delay), str(seed)],
                                    input=data, text=True, capture_output=True)
            assert result.returncode == 0, (name, seed, result.stdout, result.stderr)
            assert 'tx_overflow=0' in result.stdout, result.stdout
            runs += 1
        print(f'{name}: loss={loss}%, added turn delay={delay} ms, 10 seeds PASS')
print(f'{runs} complete song runs passed; UI consumer never drained. '
      'This models the protocol, not ESP task scheduling or SPI timing.')

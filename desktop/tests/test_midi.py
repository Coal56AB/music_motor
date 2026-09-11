import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mido
from midi.model import Song, Note, Part, Control, load_midi, save_midi, save_project, load_project, sounding_spans
from midi.allocator import allocate, pin_conflict_spans


def song(notes, controls=(), length=0):
    return Song(notes=notes, parts={'p': Part('p', 'P')}, controls=list(controls), length=length)


class MidiTests(unittest.TestCase):
    def test_conflict_spans_include_pedal_tails_and_exclude_touching_notes(self):
        s = song([Note(0,'p',0,1,60,motor=0), Note(1,'p',2,2,72,motor=0)],
                 [Control(0,0,64,127),Control(3,0,64,0)],4)
        conflicts = pin_conflict_spans(s)
        self.assertEqual([(a,b) for a,b,reason in conflicts[0]], [(2,3)])
        self.assertEqual([(a,b) for a,b,reason in conflicts[1]], [(2,3)])
        s.controls = []
        s.notes[1].start = 1
        self.assertEqual(pin_conflict_spans(s), {})

    def test_cross_track_sustain_and_tempo(self):
        mid = mido.MidiFile(ticks_per_beat=480)
        mid.tracks.append(mido.MidiTrack([
            mido.MetaMessage('set_tempo', tempo=500000),
            mido.Message('control_change', control=64, value=127),
            mido.MetaMessage('set_tempo', tempo=1000000, time=480),
            mido.Message('control_change', control=64, value=0, time=480)]))
        mid.tracks.append(mido.MidiTrack([
            mido.Message('note_on', note=60, velocity=100),
            mido.Message('note_on', note=60, velocity=0, time=240)]))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'source.mid'
            mid.save(str(path))
            s = load_midi(path)
            self.assertEqual(s.notes[0].duration, .5)
            self.assertEqual(sounding_spans(s)[0], (0, 2, .5))
            self.assertEqual(s.seconds(2), 1.5)
            self.assertEqual(allocate(s).segments[0][:2], (0, 1500))
            save_midi(s, Path(tmp) / 'roundtrip.mid')
            again = load_midi(Path(tmp) / 'roundtrip.mid')
            self.assertEqual(sounding_spans(again), sounding_spans(s))

    def test_repeat_pedal_and_channel_controls(self):
        s = song([Note(0,'p',0,1,60,on_order=1,off_order=2),
                  Note(1,'p',2,2,60,on_order=3,off_order=5)],
                 [Control(0,0,64,127,0), Control(3,0,64,0,4)], 4)
        self.assertEqual(sounding_spans(s), {0:(0,3,1),1:(2,4,4)})
        s.controls = [Control(0,0,64,127),Control(3,0,123,0)]
        self.assertEqual(sounding_spans(s)[1], (2,4,3))
        s.controls.append(Control(3.5,0,121,0))
        self.assertEqual(sounding_spans(s)[1], (2,3.5,3))
        s.controls.append(Control(2.5,0,120,0))
        self.assertEqual(sounding_spans(s)[1][1], 2.5)

    def test_same_tick_order(self):
        n = Note(0,'p',0,1,60,on_order=0,off_order=2)
        before = song([n], [Control(1,0,64,127,1)], 3)
        after = song([n], [Control(1,0,64,127,3)], 3)
        self.assertEqual(sounding_spans(before)[0][1], 3)
        self.assertEqual(sounding_spans(after)[0][1], 1)

    def test_overlap_fifo_import(self):
        mid = mido.MidiFile(ticks_per_beat=100)
        mid.tracks.append(mido.MidiTrack([
            mido.Message('note_on',note=60,velocity=80),
            mido.Message('note_on',note=60,velocity=100,time=100),
            mido.Message('note_off',note=60,time=100),
            mido.Message('note_off',note=60,time=100)]))
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/'repeat.mid';mid.save(str(p));s=load_midi(p)
        self.assertEqual([(n.start,n.duration) for n in s.notes], [(0,2),(1,2)])

    def test_source_notes_and_pins_survive_save(self):
        s = song([Note(i,'p',0,4,48+i*2, motor=3 if i==7 else -1) for i in range(8)])
        before = copy.deepcopy(s)
        a = allocate(s,63,63)
        self.assertEqual(s, before)
        self.assertEqual(a.assignments[7],3)
        self.assertTrue(a.skipped)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'project.json';save_project(s,p);self.assertEqual(load_project(p),s)
            p=Path(tmp)/'all.mid';save_midi(s,p);self.assertEqual(len(load_midi(p).notes),8)
            p=Path(tmp)/'legacy.json'
            p.write_text(json.dumps(dict(notes=[dict(id=0,part='p',start=0,duration=1,pitch=60)],
                                         parts={'p':dict(id='p',name='P')},tempos=[[0,500000]])))
            self.assertEqual(load_project(p).notes[0].motor,-1)

    def test_conflicting_pins_and_unavailable_motor(self):
        s=song([Note(0,'p',0,2,60,motor=0),Note(1,'p',1,2,64,motor=0)])
        with self.assertRaisesRegex(ValueError,'Конфликт M1'):
            allocate(s)
        s.notes[1].start=2
        self.assertEqual(allocate(s).assignments,{0:0,1:0})
        s.notes[1].motor=5
        with self.assertRaisesRegex(ValueError,'недоступен'):
            allocate(s)

    def test_partial_grey_intervals_and_return(self):
        s=song([Note(0,'p',0,4,60,80),Note(1,'p',1,1,72,120)])
        a=allocate(s,1,1,1,strategy='loudest')
        self.assertEqual(a.visual_segments[0],[(0,1,0),(2,4,0)])
        self.assertIn(0,a.skipped)
        self.assertEqual(a.visual_segments[1],[(1,2,0)])

    def test_chronology_and_melody(self):
        s=song([Note(0,'p',0,.1,60),Note(1,'p',8,1,84)], [Control(0,0,64,127)],10)
        a=allocate(s,1,1,1)
        self.assertTrue(any(uid==1 and start==4000 for start,end,m,uid,p in a.segments))
        s=song([Note(i,'p',0,4,p) for i,p in enumerate((24,28,31,36,40,43))]+[Note(6,'p',1,1,79)])
        self.assertIn(6,allocate(s,63,63).assignments)

    def test_same_millisecond_keeps_chronological_commands(self):
        s=song([Note(0,'p',0,.0002,60),Note(1,'p',.0002,.0002,64)])
        a=allocate(s,1,1,1)
        state={}
        for at,m,op,v in a.events:
            if op==1:state[m]=v
            elif op==0:state.pop(m,None)
        self.assertFalse(state)

    def test_constrained_part_does_not_lose_to_greedy_motor_assignment(self):
        s=Song(notes=[Note(0,'p',0,2,60,120),Note(1,'q',0,2,64,80)],
               parts={'p':Part('p','P'),'q':Part('q','Q',motor=0)})
        a=allocate(s,3,3,2,strategy='loudest')
        self.assertEqual(a.assignments,{0:1,1:0})

    def test_new_voice_does_not_shuffle_survivors(self):
        s=song([Note(0,'p',0,4,60,80),Note(1,'p',0,4,64,90),Note(2,'p',1,1,79,120)])
        a=allocate(s,7,7,3,strategy='loudest')
        self.assertEqual(len(a.visual_segments[0]),1)
        self.assertEqual(len(a.visual_segments[1]),1)


if __name__ == '__main__':
    unittest.main()

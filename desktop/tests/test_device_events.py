import os
import subprocess
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from midi.model import Song, Note, Part, Control, demo_song
from midi.allocator import allocate
from midi.device_events import device_events
from midi.examples import happy_birthday, imperial_march, stored_example

class DeviceEventsTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_imperial_buffer_survives_transport_pause(self):
        rows=device_events(imperial_march(),63,polyphony=6,strategy='stable')
        data=''.join(' '.join(map(str,e))+'\n' for e in rows)
        for micro in (0,1):
            result=subprocess.run([os.environ['STM32_MUSIC_TEST_EXE'],'paced','10',str(micro),'4000'],
                                  input=data,universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertEqual(result.returncode,0,result.stderr)

    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_op23_5_paced_esp_refills(self):
        rows=device_events(stored_example('rachmaninoff_op23_5'),63,polyphony=6,strategy='stable')
        data=''.join(' '.join(map(str,e))+'\n' for e in rows)
        for micro in (0,1):
            for cadence in (10,80):
                with self.subTest(micro=micro,cadence=cadence):
                    result=subprocess.run([os.environ['STM32_MUSIC_TEST_EXE'],'paced',str(cadence),str(micro)],
                                          input=data,universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                    self.assertEqual(result.returncode,0,result.stderr)

    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_bundled_polyphia_and_rachmaninoff(self):
        for name in ('playing_god','od','rachmaninoff_op3_2','rachmaninoff_op23_5'):
            with self.subTest(name=name):
                song=stored_example(name)
                for motor in range(6):
                    notes=sorted((n for n in song.notes if n.part==str(motor)),key=lambda n:n.start)
                    self.assertTrue(notes)
                    self.assertTrue(all(a.start+a.duration<=b.start+1e-8 for a,b in zip(notes,notes[1:])))
                    self.assertTrue(all(20<=440*2**((n.pitch-69)/12)<=1200 for n in notes))
                rows=device_events(song,63,polyphony=6,strategy='stable')
                self.assertLess(len(rows)*10,192512)
                data='\n'.join(' '.join(map(str,e)) for e in rows)+'\n'
                output=subprocess.check_output([os.environ['STM32_MUSIC_TEST_EXE'],'device'],input=data,universal_newlines=True)
                states=[list(map(int,line.split())) for line in output.splitlines()]
                self.assertEqual(max(sum(p!=255 for p in row[1:]) for row in states),6)
                self.assertEqual(states[-1][1:],[255]*6)
                if name=='rachmaninoff_op3_2':
                    self.assertEqual(song.length,246)
                    self.assertEqual([at for at,_ in song.tempos],[0,6,54,170,178,218])

    def test_op3_2_section_timing(self):
        song=stored_example('rachmaninoff_op3_2')
        span=lambda a,b:song.seconds(b)-song.seconds(a)
        self.assertAlmostEqual(span(0,6),6.0)
        self.assertAlmostEqual(span(6,54),57.6)
        self.assertAlmostEqual(span(54,170),36.25)
        self.assertAlmostEqual(span(170,178),4.8)
        self.assertAlmostEqual(span(178,218),48.0)
        self.assertAlmostEqual(span(218,246),28*.967741)

    def test_op3_2_opening_durations(self):
        song=stored_example('rachmaninoff_op3_2')
        opening=[n for n in song.notes if n.start<6]
        self.assertEqual(len(opening),9)
        for onset,pitches in ((0,{33,45,57}),(1.5,{32,44,56}),(3,{25,37,49})):
            voices=[n for n in opening if abs(n.start-onset)<1e-8]
            self.assertEqual({n.pitch for n in voices},pitches)
            for note in voices:
                if onset<3:
                    self.assertAlmostEqual(song.seconds(note.start+note.duration)-song.seconds(note.start),1.5)
                else:
                    self.assertAlmostEqual(note.start+note.duration,8)
                    self.assertAlmostEqual(song.seconds(note.start+note.duration),8.4)

    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_imperial_march_six_independent_parts(self):
        song=imperial_march()
        lead=[n for n in song.notes if n.part=='0']
        high=[n for n in lead if 16<=n.start<48]
        self.assertEqual(next(n.pitch for n in high if n.start==16),79)
        self.assertGreater(sum(n.duration<.25 for n in high),12)
        self.assertEqual(next(n.pitch for n in lead if n.start==48),67)
        for motor in range(6):
            notes=sorted((n for n in song.notes if n.part==str(motor)),key=lambda n:n.start)
            self.assertTrue(notes)
            self.assertTrue(all(a.start+a.duration<=b.start for a,b in zip(notes,notes[1:])))
            self.assertTrue(all(20<=440*2**((n.pitch-69)/12)<=1200 for n in notes))
        rows=device_events(song,63,polyphony=6,strategy='stable')
        data='\n'.join(' '.join(map(str,e)) for e in rows)+'\n'
        output=subprocess.check_output([os.environ['STM32_MUSIC_TEST_EXE'],'device'],input=data,universal_newlines=True)
        states=[list(map(int,line.split())) for line in output.splitlines()]
        self.assertEqual(states[0][1:],[67,43,58,255,255,255])
        final=next(row for row in states if row[0]==round(song.seconds(64)*1000))
        self.assertEqual(final[1:],[67,43,50,58,62,74])
        self.assertEqual(states[-1][1:],[255]*6)

    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_birthday_starts_with_accompaniment_and_builds_to_six(self):
        song=happy_birthday()
        rows=device_events(song,63,polyphony=6,strategy='stable')
        data='\n'.join(' '.join(map(str,e)) for e in rows)+'\n'
        output=subprocess.check_output([os.environ['STM32_MUSIC_TEST_EXE'],'device'],input=data,universal_newlines=True)
        states=[list(map(int,line.split())) for line in output.splitlines()]
        starts=[0,7,19,31,50]
        for i in range(4):
            begin,end=[round(song.seconds(beat)*1000) for beat in starts[i:i+2]]
            peak=max(sum(note!=255 for note in row[1:]) for row in states if begin<=row[0]<end)
            self.assertEqual(peak,i+3)
        self.assertEqual(states[0][1:4],[67,36,48])
        final=next(row for row in states if row[0]==round(song.seconds(46)*1000))
        self.assertEqual(final[1:],[72,36,48,64,67,55])
        self.assertEqual(states[-1][1:],[255]*6)
    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_original_demo_uses_all_six_motors(self):
        song=demo_song()
        historical=allocate(song,63,63,polyphony=4,strategy='stable')
        self.assertEqual({e[1] for e in historical.events if e[2]==1},set(range(6)))
        rows=device_events(song,63,polyphony=4,strategy='stable')
        data='\n'.join(' '.join(map(str,e)) for e in rows)+'\n'
        output=subprocess.check_output([os.environ['STM32_MUSIC_TEST_EXE'],'device'],input=data,universal_newlines=True)
        states=[list(map(int,line.split())) for line in output.splitlines()]
        used={m for row in states for m,note in enumerate(row[1:]) if note!=255}
        self.assertEqual(used,set(range(6)))
        for row in states:
            expected=[255]*6
            for start,end,motor,uid,pitch in historical.segments:
                if start<=row[0]<end:expected[motor]=pitch
            self.assertEqual(row[1:],expected,'Clockwork motor assignment at %d ms'%row[0])
        transitions={t for start,end,*rest in historical.segments for t in (start,end)}
        self.assertTrue(transitions.issubset({row[0] for row in states}))

    def test_all_candidates_reach_stm(self):
        song=Song(notes=[Note(i,'p',0,1,48+i*3) for i in range(8)],parts={'p':Part('p','P')},length=2)
        result=allocate(song,63,63)
        self.assertEqual(sum((e[2]&127)==0 for e in result.raw_events),8)
        self.assertEqual(result.raw_events[0],(0,63,0x85,6))
        ons=[e for e in result.raw_events if (e[2]&127)==0]
        self.assertTrue(ons[-1][2]&128)
        self.assertFalse(any(e[2]&128 for e in ons[:-1]))

    def test_controls_ports_and_pin_are_preserved(self):
        song=Song(notes=[Note(0,'p',0,1,60,motor=2)],parts={'p':Part('p','P',source=3,channel=2)},
                  controls=[Control(0,2,64,127,source=3),Control(2,2,64,0,source=3)],length=3)
        rows=device_events(song,63)
        self.assertTrue(all(e[1]==0x32 for e in rows[1:-1]))
        self.assertEqual(next(e[3]>>16 for e in rows if e[2]&127==0),3)
        self.assertEqual(sum((e[2]&127)==2 for e in rows),2)

    @unittest.skipUnless(os.environ.get('STM32_MUSIC_TEST_EXE'),'Build the STM32 native engine test')
    def test_real_engine_streams_beyond_ring_capacity_and_seeks(self):
        notes=[Note(i,'p',i*.1,.05,48+i%24) for i in range(600)]
        song=Song(notes=notes,parts={'p':Part('p','P')},length=61)
        rows=device_events(song,63)
        data='\n'.join(' '.join(map(str,e)) for e in rows)+'\n'
        for seek in (0,10013):
            output=subprocess.check_output([os.environ['STM32_MUSIC_TEST_EXE'],'device',str(seek)],input=data,universal_newlines=True)
            states=[list(map(int,line.split())) for line in output.splitlines()]
            self.assertGreater(len(states),100)
            self.assertEqual(states[-1][1:],[255]*6)
            self.assertTrue(all(a[0]<=b[0] for a,b in zip(states,states[1:])))
            if seek:self.assertEqual(states[0][0],0)

if __name__=='__main__':unittest.main()

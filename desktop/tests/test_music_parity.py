import os
from pathlib import Path
import subprocess
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from midi.model import Song,Note,Part,Control,midi_timeline
from midi.allocator import allocate

EXE=os.environ.get('MUSIC_ENGINE_TEST_EXE')


@unittest.skipUnless(EXE,'Set MUSIC_ENGINE_TEST_EXE to the compiled native_music_test executable')
class ParityTests(unittest.TestCase):
    def check_song(self,s):
        groups={}
        for at,order,kind,obj in midi_timeline(s):
            if kind=='cc':
                row=(2,obj.channel,obj.control,obj.value,round(s.seconds(at)*1e6),obj.source)
            else:
                p=s.parts[obj.part]
                row=(0 if kind=='on' else 1,p.channel,obj.pitch,obj.velocity if kind=='on' else 0,
                     round(s.seconds(at)*1e6),p.source)
            groups.setdefault(at,[]).append(row)
        # End of file is a transport reset, not an indefinitely held pedal.
        groups.setdefault(s.end,[]).append((3,0,0,0,round(s.seconds(s.end)*1e6),0))
        rows=[]
        for at,events in sorted(groups.items()):
            rows.extend(' '.join(map(str,row+(int(i==len(events)-1),))) for i,row in enumerate(events))
        output=subprocess.check_output([EXE,'stream'],input='\n'.join(rows)+'\n',universal_newlines=True)
        a=allocate(s,63,63)
        for at,line in zip(sorted(groups),output.splitlines()):
            actual={int(n) for n in line.split()}
            expected={n.pitch for n in s.notes for start,end,m in a.visual_segments.get(n.id,[]) if start<=at<end}
            self.assertEqual(actual,expected,'beat %s'%at)
        self.assertEqual(len(output.splitlines()),len(groups))

    def test_chords_pedal_repeated_attacks(self):
        notes=[Note(i,'p',0,1,p) for i,p in enumerate((24,28,31,36,40,43))]
        notes += [Note(6,'p',2,1,79),Note(7,'p',3,1,81),Note(8,'p',4,1,83),
                  Note(9,'p',5,1,79),Note(10,'p',6,2,79)]
        self.check_song(Song(notes=notes,parts={'p':Part('p','P')},length=9,
                             controls=[Control(0,0,64,127),Control(7,0,64,0)]))

    def test_separate_channels_and_tempo(self):
        notes=[Note(i,'p',0,2,p) for i,p in enumerate((48,52,55,60,64,67))]
        notes += [Note(6,'q',1,4,79),Note(7,'q',2,4,81)]
        self.check_song(Song(notes=notes,parts={'p':Part('p','P'),'q':Part('q','Q',channel=1)},
                             tempos=[(0,500000),(3,750000)],length=8,
                             controls=[Control(0,0,64,127),Control(4,0,123,0),Control(5,0,121,0)]))


if __name__=='__main__':unittest.main()

"""Rebuild bundled motor projects from attributed MIDI and user-provided notes."""
import math
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from midi.model import Song, Part, Note, load_midi, save_midi, save_project
from midi.allocator import allocate
from midi.examples import imperial_march

ROOT = Path(__file__).resolve().parents[2] / 'Songs'


def export(name, song):
    save_project(song, ROOT / (name + '.motor.json'))
    save_midi(song, ROOT / (name + '.mid'))
    print(name, len(song.notes), 'notes,', round(song.seconds(song.end), 2), 'seconds')


def classical(name, title, end=None):
    source = load_midi(ROOT / 'sources' / (name + '.mid'))
    if end is not None:
        source.notes = [n for n in source.notes if n.start < end]
        for n in source.notes:n.duration = min(n.duration, end-n.start)
        source.controls = [c for c in source.controls if c.at < end]
        source.tempos = [(at,t) for at,t in source.tempos if at < end]
        source.length = end
    result = allocate(source, 63, 63, 6, 'melody_bass')
    song = Song(title=title, tempos=source.tempos, length=source.end)
    song.parts = {str(i): Part(str(i), 'Голос %d'%(i+1), i, i, motor=i) for i in range(6)}
    by_id = {n.id:n for n in source.notes}
    for begin,finish,motor,uid,pitch in result.segments:
        start = source.beat_at(begin/1000)
        stop = source.beat_at(finish/1000)
        song.notes.append(Note(len(song.notes),str(motor),start,stop-start,pitch,by_id[uid].velocity))
    if end is not None:
        # Close the excerpt on a G-minor chord instead of cutting into the slow section.
        for motor,pitch in enumerate((43,50,58,62,67,74)):
            song.notes.append(Note(len(song.notes),str(motor),end,1.8,pitch,90))
        song.length=end+2
    if name == 'rachmaninoff_op3_2':
        # Keep pitches and motor parts; redistribute only the opening durations.
        # A and G# last 1.5 beats each; C# starts one beat later, ends unchanged.
        for note in song.notes:
            onset=round(note.start)
            if abs(note.start-onset)<1e-8 and onset in (0,1):
                note.start=onset*1.5
                note.duration*=1.5
            elif abs(note.start-2)<1e-8:
                note.start+=1
                note.duration-=1
        # Pickup + bar 2, first chords, Agitato, two transition chords,
        # reprise at bar 46, then bar 56 coda.
        song.tempos = [(0,1000000),(6,1200000),(54,312500),
                       (170,600000),(178,1200000),(218,967741)]
    return song


DURATIONS={'QUARTER':1,'EIGHTH':.5,'SIXTEENTH':.25,'THIRTYSECOND':.125,
           'EIGHTH_DOT':.75,'EIGHTH_TRIPLET':1/3,'QUARTER_TRIPLET':2/3}
PITCHES={'DO':0,'RE':2,'MI':4,'FA':5,'SOL':7,'LA':9,'SI':11}


def pitch(symbol):
    if symbol=='NOTE_REST':return None
    m=re.fullmatch(r'(DO|RE|MI|FA|SOL|LA|SI)(00|[0-4])(s?)',symbol)
    if not m:raise ValueError(symbol)
    octave=-1 if m[2]=='00' else int(m[2])
    return 48+12*octave+PITCHES[m[1]]+bool(m[3])


def polyphia(name, symbol, title, bpm, roots):
    text=(ROOT/'sources'/'polyphia_user_notes.txt').read_text(encoding='utf8')
    body=text.split(symbol+'[] = {',1)[1].split('};',1)[0]
    rows=re.findall(r'\{\{(\w+),\s*NOTE_(\w+)\},\s*\{(\w+),\s*NOTE_(\w+)\}\}',body)
    voices=[[],[]];lengths=[]
    for voice in range(2):
        at=0
        for row in rows:
            note,duration=pitch(row[voice*2]),DURATIONS[row[voice*2+1]]
            if note is not None:voices[voice].append((at,duration,note))
            at+=duration
        lengths.append(at)
        # One transposition for the complete voice keeps every interval intact.
        shift=0
        while max(n[2] for n in voices[voice])+shift>86:shift-=12
        while min(n[2] for n in voices[voice])+shift<16:shift+=12
        voices[voice]=[(at,d,n+shift) for at,d,n in voices[voice]]
    period=math.ceil(max(lengths)/4)*4
    song=Song(title=title,tempos=[(0,round(60000000/bpm))],length=4*period+2)
    names=['Исходная мелодия','Исходный бас','Арпеджио','Синкопы','Ответы','Верхний голос']
    song.parts={str(i):Part(str(i),n,i,i,motor=i) for i,n in enumerate(names)}
    def add(m,at,d,n,v=75):song.notes.append(Note(len(song.notes),str(m),at,d,n,v))
    scale={4,6,7,9,11,0,2,3} if name=='playing_god' else {6,8,9,11,1,2,5}
    def neighbour(n):
        for step in range(1,4):
            if (n+step)%12 in scale and n+step<=86:return n+step
        return n-2
    for verse in range(4):
        offset=verse*period
        for voice in range(2):
            if voice==0 and verse==2:
                # Newly arranged middle section, built from the supplied opening motif.
                motif=[n for _,_,n in voices[0]][:4]
                rhythm=(.5,.25,.25,.5,.5,.25,.25,.5,1)
                contour=(0,1,2,3,2,1,2,1,0)
                for bar in range(period//4):
                    at=offset+bar*4
                    for k,d in enumerate(rhythm):
                        n=motif[contour[k]]
                        if bar%2:n=neighbour(n)
                        add(0,at,d*.9,n,100);at+=d
            else:
                for at,d,n in voices[voice]:
                    if voice==0 and verse in (1,3) and d>=.5:
                        # Retain the phrase's onset/end while ornamenting longer notes.
                        lengths=(d/2,d/4,d/4)
                        for length,tone in zip(lengths,(n,neighbour(n),n)):
                            add(0,offset+at,length*.9,tone,100);at+=length
                    else:add(voice,offset+at,d*.9,n,100 if voice==0 else 85)
        for beat in range(period):
            root=roots[(beat//4)%len(roots)];at=offset+beat
            add(2,at,.34,root+19+(0,3,5,0)[beat%4],70)
            if verse>=1:
                add(3,at+.5,.28,root+24+(3,7)[beat%2],72)
            if verse>=2 and beat%4==3:
                for k,interval in enumerate((31,27,26)):
                    add(4,at+k*.25,.20,root+interval,76)
            if verse>=3:
                for k,interval in enumerate((24,26,27) if beat%2==0 else (31,29,27)):
                    add(5,at+k*.25,.20,root+interval,78)
    root=roots[0]
    for m,interval in enumerate((24,0,7,15,19,31)):add(m,4*period,1.8,root+interval,90)
    return song


if __name__=='__main__':
    export('imperial_march',imperial_march())
    export('rachmaninoff_op3_2',classical('rachmaninoff_op3_2','Рахманинов op.3-2'))
    export('rachmaninoff_op23_5',classical('rachmaninoff_op23_5','Рахманинов op.23-5',132))
    export('playing_god',polyphia('playing_god','Polyphia_PlayingGod_Notes','Polyphia Playing God',112,[40,48,47,45]))
    export('od',polyphia('od','Polyphia_OD_Notes','Polyphia O.D.',128,[42,41,42,47,47]))

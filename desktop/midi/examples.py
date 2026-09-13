"""Built-in arrangements; no device settings are embedded in the songs."""
from pathlib import Path
from midi.model import Song, Note, Part, load_project


def stored_example(name):
    return load_project(Path(__file__).resolve().parents[2] / 'Songs' / (name + '.motor.json'))


def happy_birthday():
    song = Song(title='Happy Birthday — 6 моторов', tempos=[(0, 625000)], length=50)
    names = ['Мелодия', 'Бас', 'Арпеджио', 'Синкопы', 'Ответы', 'Контрмелодия']
    song.parts = {str(i): Part(str(i), name, i, i, 0, motor=i)
                  for i, name in enumerate(names)}

    def add(motor, at, duration, pitch, velocity=80):
        song.notes.append(Note(len(song.notes), str(motor), at, duration, pitch, velocity))

    # Four phrases, with the familiar two-note pickup before each phrase.
    phrases = [
        [(67,.5),(67,.5),(69,1),(67,1),(72,1),(71,2)],
        [(67,.5),(67,.5),(69,1),(67,1),(74,1),(72,2)],
        [(67,.5),(67,.5),(79,1),(76,1),(72,1),(71,1),(69,1)],
        [(77,.5),(77,.5),(76,1),(72,1),(74,1),(72,2)],
    ]
    for verse in range(2):
        at=verse*24
        for phrase in phrases:
            for pitch, duration in phrase:
                length=3.8 if at==46 else duration*.92
                add(0,at,length,pitch,100)
                at+=duration

    # New voices have different rhythms and contours, not sustained doublings.
    harmony=[(0,4,36),(4,7,31),(7,10,31),(10,13,36),(13,16,36),
             (16,19,29),(19,21,36),(21,22,31),(22,24,36)]
    entry={1:0,2:0,3:7,4:19,5:31}
    intervals={1:0,2:12,3:28,4:31,5:36}
    for verse in range(2):
        for start,end,root in harmony:
            start+=verse*24;end+=verse*24
            if start==46:end=50
            for motor in range(1,6):
                if start<entry[motor]:continue
                pitch=root+intervals[motor]
                if start==46 and motor==5:pitch=55
                if motor==2 and start<46:
                    for k in range(int(end-start)):
                        add(motor,start+k,.75,pitch+(0,4,7)[k%3],70)
                elif motor==3 and start<46:
                    # Short offbeat chord tones between the bass/arpeggio attacks.
                    for k in range(int(end-start)):
                        add(motor,start+k+.5,.32,root+(28,31,28)[k%3],72)
                elif motor==4 and start<46:
                    # Descending answers near each harmonic turn. In verse two
                    # a rising pickup gives the repeated theme a different shape.
                    offsets=(31,28,24) if verse==0 else (24,26,28)
                    count=min(3,int((end-start)*2))
                    for k in range(count):
                        add(motor,end-count*.5+k*.5,.42,root+offsets[k],76)
                elif motor==5 and start<46:
                    for k in range(int(end-start)):
                        add(motor,start+k,.8,pitch+(0,2,4)[k%3],65)
                else:
                    add(motor,start,(end-start)*.95,pitch,85 if motor==1 else 70)
    return song


def imperial_march():
    """Six monophonic motor parts: march pulse, offbeats and answering figures."""
    song = Song(title='Имперский марш', tempos=[(0, 600000)], length=66)
    names = ['Тема', 'Маршевый бас', 'Пульс', 'Синкопы', 'Ответы', 'Верхний голос']
    song.parts = {str(i): Part(str(i), name, i, i, 0, motor=i)
                  for i, name in enumerate(names)}

    def add(motor, at, duration, pitch, velocity=80):
        song.notes.append(Note(len(song.notes), str(motor), at, duration, pitch, velocity))

    theme = [
        (67,1),(67,1),(67,1),(63,.75),(70,.25),(67,1),(63,.75),(70,.25),(67,2),
        (74,1),(74,1),(74,1),(75,.75),(70,.25),(66,1),(63,.75),(70,.25),(67,2),
    ]
    # High-register continuation: octave leaps followed by chromatic short runs.
    bridge = [
        (79,1),(67,.75),(67,.25),(79,1),(78,.75),(77,.25),
        (76,.25),(75,.25),(76,.5),(None,.5),(68,.5),(73,1),(72,.75),(71,.25),
        (70,.25),(69,.25),(70,.5),(None,.5),(63,.5),(66,1),(63,.75),(66,.25),
        (70,1),(67,.75),(70,.25),(74,2),
    ]
    bridge_return = bridge[:-1] + [(74,.5),(75,.25),(74,.25),(72,.5),(70,.5)]
    sections = [theme, bridge, bridge_return, theme]
    for verse,melody in enumerate(sections):
        at=verse*16
        for pitch,duration in melody:
            if pitch is not None:add(0,at,duration*.9,pitch,105)
            at+=duration

    chords=[(0,3,43),(3,4,39),(4,5,43),(5,6,39),(6,8,43),
            (8,11,38),(11,12,39),(12,14,38),(14,16,43)]
    bridge_chords=[(0,2,43),(2,4,39),(4,5,37),(5,6,44),(6,8,37),
                   (8,10,46),(10,12,39),(12,14,43),(14,16,38)]
    for verse in range(4):
        for begin,end,root in (bridge_chords if verse in (1,2) else chords):
            begin+=verse*16;end+=verse*16
            minor=root!=38
            third=3 if minor else 4
            for beat in range(begin,end):
                # Root/fifth bass and a short midrange pulse sound immediately.
                add(1,beat,.72,root if beat%2==0 else root-5,90)
                add(2,beat,.30,root+12+(third if beat%2==0 else 7),72)
                if beat>=8:
                    add(3,beat+.5,.28,root+24+(7 if beat%2==0 else third),70)
                if beat>=24 and beat==end-1:
                    # Descending replies contrast with the repeated march rhythm.
                    for k,interval in enumerate((7,third,0)):
                        add(4,beat+k*.25,.20,root+24+interval,78)
                if beat>=40:
                    # A rising upper figure, then a longer response on alternate bars.
                    if beat%2==0:
                        for k,interval in enumerate((0,2,third)):
                            add(5,beat+k*.25,.20,root+24+interval,75)
                    else:
                        add(5,beat+.25,.60,root+31,75)
    # One full six-motor final chord, within the 1200 Hz limit.
    for motor,pitch in enumerate((67,43,50,58,62,74)):
        add(motor,64,1.8,pitch,95)
    return song

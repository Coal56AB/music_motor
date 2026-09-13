import copy
import os
import sys
import unittest
from pathlib import Path
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PySide2.QtCore import Qt, QPointF, QEvent
from PySide2.QtGui import QMouseEvent
from PySide2.QtTest import QTest
from PySide2.QtWidgets import QApplication
from widgets.piano_roll import PianoRoll
from midi.model import Song, Note, Part

class CursorClickTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.roll = PianoRoll()
        self.roll.resize(800, 400)
        self.roll.set_song(Song(notes=[Note(0,'p',1,2,60,80)], parts={'p':Part('p','P')}))
        self.roll.show()
        self.app.processEvents()
        self.events=[]
        self.roll.cursor_changed.connect(lambda beat: self.events.append(beat))

    def tearDown(self):
        self.roll.close()
        self.roll.deleteLater()
        self.app.processEvents()

    def pos(self, beat, pitch):
        r=self.roll
        return r.mapFromScene(QPointF(r.key_width+beat*r.beat_width,(127-pitch)*r.row_height+8))

    def test_empty_and_note_click_seek_without_edit(self):
        before=copy.deepcopy(self.roll.song.notes)
        QTest.mouseClick(self.roll.viewport(),Qt.LeftButton,Qt.NoModifier,self.pos(2,64))
        QTest.mouseClick(self.roll.viewport(),Qt.LeftButton,Qt.NoModifier,self.pos(1.5,60))
        self.assertEqual(self.events,[2,1.5])
        self.assertEqual(self.roll.song.notes,before)
        QTest.mouseClick(self.roll.viewport(),Qt.LeftButton,Qt.ControlModifier,self.pos(2,64))
        self.assertEqual(len(self.events),2)

    def test_drag_note_or_selection_does_not_seek(self):
        for pitch in (64,60):
            start,end=self.pos(1.5,pitch),self.pos(2.5,pitch)
            QTest.mousePress(self.roll.viewport(),Qt.LeftButton,Qt.NoModifier,start)
            move=QMouseEvent(QEvent.MouseMove,QPointF(end),Qt.NoButton,Qt.LeftButton,Qt.NoModifier)
            QApplication.sendEvent(self.roll.viewport(),move)
            QTest.mouseRelease(self.roll.viewport(),Qt.LeftButton,Qt.NoModifier,end)
        self.assertEqual(self.events,[])
        self.assertEqual(self.roll.song.notes[0].start,2)

import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PySide2.QtWidgets import QApplication, QPushButton
from app.main_window import MainWindow
from midi.model import Song, Note, Part, Control


class EditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.window = MainWindow(settings_path=Path(self.tmp.name)/'settings.json')
        self.window.poly.setValue(1)
        self.window.strategy.setCurrentIndex(self.window.strategy.findData('loudest'))
        self.window.set_song(Song(notes=[Note(0,'p',0,4,60,80),Note(1,'p',1,1,72,120)],
                                  parts={'p':Part('p','P')}))

    def tearDown(self):
        self.window.close()
        self.window.deleteLater()
        self.app.processEvents()
        self.tmp.cleanup()

    def test_pin_edit_does_not_compile_until_button(self):
        w=self.window
        original=w.allocation
        events=copy.deepcopy(original.events)
        w.roll.note_items[0].setSelected(True)
        w.roll.assign_selected(2)
        self.assertTrue(w.allocation_dirty)
        self.assertIs(w.allocation,original)
        self.assertEqual(w.allocation.events,events)
        self.assertEqual(w.song.notes[0].motor,2)
        self.assertFalse(w.require_allocation())
        self.assertIs(w.allocation,original)
        buttons=[b for b in w.findChildren(QPushButton) if b.text()=='Пересчитать распределение']
        buttons[0].click()
        self.assertFalse(w.allocation_dirty)
        self.assertEqual(w.allocation.assignments[0],2)
        w.roll.undo_stack.undo()
        self.assertEqual(w.song.notes[0].motor,-1)
        self.assertTrue(w.allocation_dirty)

    def test_conflict_keeps_last_allocation(self):
        w=self.window
        original=w.allocation
        for item in w.roll.note_items:item.setSelected(True)
        w.roll.assign_selected(0)
        self.assertFalse(w.reallocate())
        self.assertIs(w.allocation,original)
        self.assertIn('Конфликт',w.editor_allocation_status.text())
        self.assertEqual([n.motor for n in w.song.notes],[0,0])

    def test_conflict_highlights_only_overlap_without_reallocation(self):
        w = self.window
        original = w.allocation
        for item in w.roll.note_items:
            item.setSelected(True)
        w.roll.assign_selected(0)
        self.assertIs(w.allocation, original)
        self.assertEqual(w.roll.conflict_ranges, [(1, 2)])
        for item in w.roll.note_items:
            self.assertEqual(len(item.conflict_items), 1)
            rect = item.conflict_items[0].rect()
            self.assertEqual(rect.x(), (1 - item.note.start) * w.roll.beat_width)
            self.assertEqual(rect.width(), w.roll.beat_width)
            self.assertIn('Пересечение', item.toolTip())
        w.roll.undo_stack.undo()
        self.assertEqual(w.roll.conflict_ranges, [])
        self.assertTrue(all(not i.conflict_items for i in w.roll.note_items))
        self.assertIs(w.allocation, original)

    def test_partial_note_has_grey_base_and_colored_played_spans(self):
        item=self.window.roll.note_items[0]
        self.assertEqual(item.brush().color().name(),'#68717e')
        self.assertEqual(len(item.segment_items),3)
        self.assertEqual(item.segment_items[0].brush().color().name(),'#68717e')
        self.assertEqual(self.window.allocation.visual_segments[0],[(0,1,0),(2,4,0)])
        self.assertEqual(len(self.window.song.notes),2)

    def test_clean_listen_does_not_reallocate(self):
        w=self.window
        original=w.allocation
        w.listen_mode.setCurrentIndex(1)
        w.preview.play=lambda *args:None
        w.reallocate=lambda: self.fail('Playback must not run the allocator')
        w.listen()
        self.assertIs(w.allocation,original)


if __name__=='__main__':unittest.main()

import copy
from PySide2.QtCore import Qt, QRectF, Signal
from PySide2.QtGui import QPainter, QColor, QPen, QBrush, QKeySequence
from PySide2.QtWidgets import (
    QGraphicsView,
    QGraphicsScene,
    QGraphicsRectItem,
    QGraphicsItem,
    QUndoStack,
    QUndoCommand,
    QInputDialog,
)
from midi.model import Note
from midi.allocator import MOTOR_COLORS
from app.music_math import note_name


class EditCommand(QUndoCommand):
    def __init__(self, roll, before, after, name):
        super().__init__(name)
        self.roll = roll
        self.before = before
        self.after = after

    def apply(self, value):
        self.roll.song.notes = copy.deepcopy(value)
        self.roll.rebuild()
        self.roll.edited.emit()

    def undo(self):
        self.apply(self.before)

    def redo(self):
        self.apply(self.after)


class NoteItem(QGraphicsRectItem):
    def __init__(self, roll, note):
        self.roll, self.note = roll, note
        super().__init__(0, 0, note.duration * roll.beat_width, roll.row_height - 2)
        self.setPos(
            roll.key_width + note.start * roll.beat_width, (127 - note.pitch) * roll.row_height + 1
        )
        self.setFlag(QGraphicsItem.ItemIsSelectable)
        self.setAcceptHoverEvents(True)
        self.setToolTip("%s · velocity %d" % (note_name(note.pitch), note.velocity))
        self.before = None

    def paint(self, painter, option, widget=None):
        r = self.roll
        note = self.note
        index = list(r.song.parts).index(note.part) % 6
        motor = r.allocation.assignments.get(note.id) if r.allocation else None
        skipped = r.allocation.skipped.get(note.id) if r.allocation else None
        color = QColor(MOTOR_COLORS[motor if motor is not None else index])
        if skipped:
            color = QColor("#9a5964")
        if r.current_part and note.part != r.current_part:
            color.setAlpha(65)
        sounding = False
        if r.allocation:
            sounding = any(
                uid == note.id and start <= r.position_ms < end
                for start, end, m, uid, p in r.allocation.segments
            )
        painter.setPen(
            QPen(
                QColor("#ffffff") if self.isSelected() or sounding else color.lighter(120),
                2 if sounding else 1,
            )
        )
        painter.setBrush(color.lighter(130) if sounding else color)
        painter.drawRoundedRect(self.rect(), 2, 2)
        if self.rect().width() > 32:
            painter.setPen(QColor("#141b27"))
            label = note_name(note.pitch) + (" M%d" % (motor + 1) if motor is not None else "")
            painter.drawText(self.rect().adjusted(3, 0, -1, 0), Qt.AlignVCenter, label)
        if skipped:
            painter.setPen(QPen(QColor("#ffb7b7"), 1))
            painter.drawLine(self.rect().topLeft(), self.rect().bottomRight())

    def mousePressEvent(self, event):
        if self.roll.read_only:
            event.ignore()
            return
        super().mousePressEvent(event)
        self.before = copy.deepcopy(self.roll.song.notes)
        self.anchor = event.scenePos()
        self.resize = event.pos().x() > self.rect().width() - 9
        self.orig = (self.note.start, self.note.pitch, self.note.duration)
        self.drag_notes = [
            (item.note, item.note.start, item.note.pitch)
            for item in self.roll.scene().selectedItems()
            if isinstance(item, NoteItem)
        ]

    def mouseMoveEvent(self, event):
        if self.before is None:
            return
        delta = event.scenePos() - self.anchor
        r = self.roll
        if self.resize:
            self.note.duration = max(
                r.snap, round((self.orig[2] + delta.x() / r.beat_width) / r.snap) * r.snap
            )
            self.setRect(0, 0, self.note.duration * r.beat_width, r.row_height - 2)
        else:
            beats = round(delta.x() / r.beat_width / r.snap) * r.snap
            pitches = -round(delta.y() / r.row_height)
            beats = max(beats, -min(start for n, start, pitch in self.drag_notes))
            pitches = max(
                -min(p for n, s, p in self.drag_notes),
                min(127 - max(p for n, s, p in self.drag_notes), pitches),
            )
            for n, start, pitch in self.drag_notes:
                n.start = start + beats
                n.pitch = pitch + pitches
            for item in r.note_items:
                item.setPos(
                    r.key_width + item.note.start * r.beat_width,
                    (127 - item.note.pitch) * r.row_height + 1,
                )

    def mouseReleaseEvent(self, event):
        super().mouseReleaseEvent(event)
        if self.before is not None:
            before = self.before
            self.before = None
            if before != self.roll.song.notes:
                self.roll.commit(before, "Длительность" if self.resize else "Перемещение нот")


class PianoRoll(QGraphicsView):
    edited = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setScene(QGraphicsScene(self))
        self.setRenderHint(QPainter.Antialiasing)
        self.setDragMode(QGraphicsView.RubberBandDrag)
        self.setViewportUpdateMode(QGraphicsView.BoundingRectViewportUpdate)
        self.beat_width, self.row_height, self.key_width = 76, 18, 58
        self.snap = 0.25
        self.song = None
        self.current_part = None
        self.allocation = None
        self.position_ms = -1
        self.playhead_beat = 0
        self.read_only = False
        self.undo_stack = QUndoStack(self)
        self.note_items = []

    def set_song(self, song):
        self.song = song
        self.current_part = next(iter(song.parts), None)
        self.allocation = None
        self.undo_stack.clear()
        self.rebuild()
        pitches = [n.pitch for n in song.notes]
        self.centerOn(
            250, (127 - (sum(pitches) / len(pitches) if pitches else 64)) * self.row_height
        )

    def rebuild(self):
        self.scene().clear()
        self.note_items = []
        if not self.song:
            return
        self.scene().setSceneRect(
            0,
            0,
            self.key_width + max(16, self.song.end + 4) * self.beat_width,
            128 * self.row_height,
        )
        for n in self.song.notes:
            item = NoteItem(self, n)
            self.scene().addItem(item)
            self.note_items.append(item)
        self.viewport().update()

    def commit(self, before, name):
        self.undo_stack.push(EditCommand(self, before, copy.deepcopy(self.song.notes), name))

    def drawBackground(self, p, rect):
        p.fillRect(rect, QColor("#121b29"))
        first = max(0, int(rect.top() / self.row_height))
        last = min(127, int(rect.bottom() / self.row_height))
        for row in range(first, last + 1):
            pitch = 127 - row
            y = row * self.row_height
            if pitch % 12 in (1, 3, 6, 8, 10):
                p.fillRect(
                    QRectF(self.key_width, y, rect.right(), self.row_height), QColor("#172234")
                )
            p.setPen(QColor("#324057") if pitch % 12 == 0 else QColor("#202c3e"))
            p.drawLine(self.key_width, y, int(rect.right()), y)
        firstbeat = max(0, int((rect.left() - self.key_width) / self.beat_width))
        lastbeat = int((rect.right() - self.key_width) / self.beat_width) + 1
        for k in range(firstbeat * 4, (lastbeat + 1) * 4):
            x = self.key_width + k * self.beat_width / 4
            p.setPen(
                QColor("#46536b")
                if k % 16 == 0
                else QColor("#2c384c")
                if k % 4 == 0
                else QColor("#1d293b")
            )
            p.drawLine(int(x), int(rect.top()), int(x), int(rect.bottom()))

    def drawForeground(self, p, rect):
        # Keyboard and bar ruler stay pinned to the viewport during scrolling.
        left = self.mapToScene(0, 0).x()
        top = self.mapToScene(0, 0).y()
        p.fillRect(QRectF(left, top, self.key_width, self.viewport().height()), QColor("#27364a"))
        for row in range(
            max(0, int(top / self.row_height)), min(128, int(rect.bottom() / self.row_height) + 1)
        ):
            pitch = 127 - row
            y = row * self.row_height
            black = pitch % 12 in (1, 3, 6, 8, 10)
            p.fillRect(
                QRectF(left, y, self.key_width - 2, self.row_height - 1),
                QColor("#182233") if black else QColor("#bdc8d6"),
            )
            p.setPen(QColor("#a8b9cc") if black else QColor("#23334a"))
            if pitch % 12 == 0 or self.row_height >= 18:
                p.drawText(
                    QRectF(left + 4, y, self.key_width - 6, self.row_height),
                    Qt.AlignVCenter,
                    note_name(pitch),
                )
        p.fillRect(
            QRectF(left + self.key_width, top, self.viewport().width(), 20), QColor("#26354a")
        )
        start = max(0, int((rect.left() - self.key_width) / self.beat_width))
        for beat in range(start, int((rect.right() - self.key_width) / self.beat_width) + 1):
            p.setPen(QColor("#8da2ba"))
            p.drawText(
                int(self.key_width + beat * self.beat_width + 3),
                int(top + 14),
                "%d.%d" % (beat // 4 + 1, beat % 4 + 1),
            )
        x = self.key_width + self.playhead_beat * self.beat_width
        p.setPen(QPen(QColor("#eff6ff"), 1.6))
        p.drawLine(int(x), int(top + 20), int(x), int(rect.bottom()))

    def mouseDoubleClickEvent(self, event):
        if self.read_only or not self.song:
            return
        item = self.itemAt(event.pos())
        if isinstance(item, NoteItem):
            value, ok = QInputDialog.getInt(
                self, "Velocity", "Громкость MIDI (не ток двигателя)", item.note.velocity, 1, 127
            )
            if ok:
                before = copy.deepcopy(self.song.notes)
                item.note.velocity = value
                self.commit(before, "Velocity")
            return
        if not self.current_part:
            return
        pos = self.mapToScene(event.pos())
        if event.pos().x() < self.key_width:
            return
        start = max(0, round((pos.x() - self.key_width) / self.beat_width / self.snap) * self.snap)
        pitch = max(0, min(127, 127 - int(pos.y() / self.row_height)))
        before = copy.deepcopy(self.song.notes)
        self.song.notes.append(
            Note(
                max((n.id for n in self.song.notes), default=-1) + 1,
                self.current_part,
                start,
                self.snap * 2,
                pitch,
            )
        )
        self.commit(before, "Добавить ноту")

    def keyPressEvent(self, event):
        if self.read_only:
            return
        if event.matches(QKeySequence.Undo):
            self.undo_stack.undo()
            return
        if event.matches(QKeySequence.Redo):
            self.undo_stack.redo()
            return
        if event.key() in (Qt.Key_Delete, Qt.Key_Backspace):
            ids = {i.note.id for i in self.scene().selectedItems() if isinstance(i, NoteItem)}
            if ids:
                before = copy.deepcopy(self.song.notes)
                self.song.notes = [n for n in self.song.notes if n.id not in ids]
                self.commit(before, "Удалить ноты")
            return
        super().keyPressEvent(event)

    def set_zoom(self, value):
        self.beat_width = value
        self.rebuild()

    def set_playhead(self, beat, ms):
        self.playhead_beat = beat
        self.position_ms = ms
        self.viewport().update()

    def set_allocation(self, allocation):
        self.allocation = allocation
        for item in self.note_items:
            reason = allocation.skipped.get(item.note.id, "")
            motor = allocation.assignments.get(item.note.id)
            item.setToolTip(
                "%s · velocity %d\n%s\n%s"
                % (
                    note_name(item.note.pitch),
                    item.note.velocity,
                    "M%d" % (motor + 1) if motor is not None else "Без назначения",
                    reason,
                )
            )
        self.viewport().update()

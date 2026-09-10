import copy
from PySide2.QtCore import Qt, QRectF, Signal
from PySide2.QtGui import QPainter, QColor, QPen, QBrush, QKeySequence
from PySide2.QtWidgets import (
    QGraphicsView,
    QGraphicsScene,
    QGraphicsRectItem,
    QGraphicsSimpleTextItem,
    QGraphicsItem,
    QInputDialog,
)
from midi.model import Note
from midi.allocator import MOTOR_COLORS
from app.music_math import note_name


class EditCommand:
    def __init__(self, roll, before, after, name):
        self.name = name
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


class EditHistory:
    """Call edits directly; PySide2/Nuitka can skip QUndoCommand virtual methods."""
    def __init__(self):
        self.clear()

    def clear(self):
        self.commands, self.index = [], 0

    def push(self, command):
        del self.commands[self.index:]
        self.commands.append(command)
        self.index += 1
        command.redo()

    def undo(self, *_args):
        if self.index:
            self.index -= 1
            self.commands[self.index].undo()

    def redo(self, *_args):
        if self.index < len(self.commands):
            command = self.commands[self.index]
            self.index += 1
            command.redo()


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
        self.label = QGraphicsSimpleTextItem(self)
        self.label.setBrush(QBrush(QColor("#141b27")))
        self.label.setPos(3, 0)
        self.label.setAcceptedMouseButtons(Qt.NoButton)
        self.update_visual()

    def update_visual(self, sounding=False):
        r, note = self.roll, self.note
        index = list(r.song.parts).index(note.part) % 6
        motor = r.allocation.assignments.get(note.id) if r.allocation else None
        skipped = r.allocation.skipped.get(note.id) if r.allocation else None
        color = QColor("#9a5964" if skipped else MOTOR_COLORS[motor if motor is not None else index])
        if r.current_part and note.part != r.current_part:
            color.setAlpha(110)
        # Native Qt brushes also render in Nuitka/PySide2, where Python paint
        # overrides on QGraphicsRectItem may be skipped.
        self.setPen(QPen(QColor("#ffffff") if self.isSelected() or sounding else color.lighter(120),
                         2 if sounding else 1))
        self.setBrush(QBrush(color.lighter(130) if sounding else color))
        self.label.setText(note_name(note.pitch) + (" M%d" % (motor + 1) if motor is not None else ""))
        self.label.setVisible(self.rect().width() >= self.label.boundingRect().width() + 6)



class PianoRoll(QGraphicsView):
    edited = Signal()
    cursor_changed = Signal(float)
    zoom_changed = Signal(int)

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
        self.undo_stack = EditHistory()
        self.note_items = []
        self.seeking = False
        self.note_drag = None
        self.scene().selectionChanged.connect(self.refresh_notes)

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
        self.note_drag = None
        self.note_items = []
        self.scene().clear()
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

    def seek_cursor(self, pos):
        beat = max(0, (self.mapToScene(pos).x() - self.key_width) / self.beat_width)
        beat = min(self.song.end, round(beat / self.snap) * self.snap)
        self.set_playhead(beat, -1)
        self.cursor_changed.emit(beat)

    def mousePressEvent(self, event):
        if (event.button() == Qt.LeftButton and self.song and not self.read_only
                and event.pos().y() < 20 and event.pos().x() >= self.key_width):
            self.seeking = True
            self.seek_cursor(event.pos())
            event.accept()
            return
        if event.button() == Qt.LeftButton and self.song and not self.read_only and event.pos().x() >= self.key_width:
            item = self.note_at(event.pos())
            if item is not None:
                if event.modifiers() & Qt.ControlModifier:
                    item.setSelected(not item.isSelected())
                elif not item.isSelected():
                    self.scene().clearSelection()
                    item.setSelected(True)
                if item.isSelected():
                    self.note_drag = dict(item=item, before=copy.deepcopy(self.song.notes),
                        anchor=self.mapToScene(event.pos()), duration=item.note.duration,
                        resize=item.mapFromScene(self.mapToScene(event.pos())).x() > item.rect().width() - 9,
                        notes=[(i.note, i.note.start, i.note.pitch) for i in self.note_items if i.isSelected()])
                self.setFocus()
                event.accept()
                return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self.seeking:
            self.seek_cursor(event.pos())
            event.accept()
            return
        drag = self.note_drag
        if drag is not None:
            delta = self.mapToScene(event.pos()) - drag['anchor']
            item = drag['item']
            if drag['resize']:
                item.note.duration = max(self.snap, round((drag['duration'] + delta.x() / self.beat_width) / self.snap) * self.snap)
                item.setRect(0, 0, item.note.duration * self.beat_width, self.row_height - 2)
            else:
                beats = max(round(delta.x() / self.beat_width / self.snap) * self.snap,
                            -min(start for n, start, pitch in drag['notes']))
                pitches = max(-min(p for n, s, p in drag['notes']),
                              min(127 - max(p for n, s, p in drag['notes']), -round(delta.y() / self.row_height)))
                for note, start, pitch in drag['notes']:
                    note.start, note.pitch = start + beats, pitch + pitches
                for i in self.note_items:
                    i.setPos(self.key_width + i.note.start * self.beat_width, (127 - i.note.pitch) * self.row_height + 1)
            self.refresh_notes()
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self.seeking:
            self.seeking = False
            event.accept()
            return
        if self.note_drag is not None:
            drag, self.note_drag = self.note_drag, None
            if drag['before'] != self.song.notes:
                selected = {i.note.id for i in self.note_items if i.isSelected()}
                self.commit(drag['before'], "Длительность" if drag['resize'] else "Перемещение нот")
                for item in self.note_items:
                    item.setSelected(item.note.id in selected)
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def note_at(self, pos):
        scene_pos = self.mapToScene(pos)
        # Keep the Python instances: compiled Qt item lookup may return base wrappers.
        return next((item for item in reversed(self.note_items)
                     if item.contains(item.mapFromScene(scene_pos))), None)

    def mouseDoubleClickEvent(self, event):
        if self.read_only or not self.song:
            return
        if event.pos().y() < 20:
            if event.pos().x() >= self.key_width:
                self.seek_cursor(event.pos())
            return
        self.note_drag = None
        item = self.note_at(event.pos())
        if isinstance(item, NoteItem):
            value, ok = QInputDialog.getInt(
                self, "Velocity", "Громкость MIDI (не ток двигателя)", item.note.velocity, 1, 127
            )
            if ok:
                before = copy.deepcopy(self.song.notes)
                item.note.velocity = value
                self.commit(before, "Velocity")
            return
        if self.current_part is None:
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
            ids = {i.note.id for i in self.note_items if i.isSelected()}
            if ids:
                before = copy.deepcopy(self.song.notes)
                self.song.notes = [n for n in self.song.notes if n.id not in ids]
                self.commit(before, "Удалить ноты")
            return
        super().keyPressEvent(event)

    def set_zoom(self, value, anchor=None):
        value = max(25, min(180, int(value)))
        if value == self.beat_width:
            return
        anchor = anchor if anchor is not None else self.viewport().rect().center()
        before = self.mapToScene(anchor)
        beat = (before.x() - self.key_width) / self.beat_width
        selected = {i.note.id for i in self.note_items if i.isSelected()}
        self.beat_width = value
        self.rebuild()
        for item in self.note_items:
            item.setSelected(item.note.id in selected)
        after = self.mapToScene(anchor)
        bar = self.horizontalScrollBar()
        bar.setValue(bar.value() + round(self.key_width + beat * value - after.x()))
        self.zoom_changed.emit(value)

    def wheelEvent(self, event):
        if event.modifiers() & Qt.ControlModifier:
            delta = event.angleDelta().y()
            if delta:
                self.set_zoom(round(self.beat_width * 1.15 ** (delta / 120)), event.pos())
            event.accept()
            return
        super().wheelEvent(event)

    def refresh_notes(self):
        sounding = {uid for start, end, motor, uid, pitch in self.allocation.segments
                    if start <= self.position_ms < end} if self.allocation else set()
        for item in self.note_items:
            item.update_visual(item.note.id in sounding)
        self.viewport().update()

    def set_playhead(self, beat, ms):
        self.playhead_beat = beat
        self.position_ms = ms
        self.refresh_notes()

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
        self.refresh_notes()

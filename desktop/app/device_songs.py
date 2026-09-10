"""Reliable stop-and-wait storage upload through the motor controller."""
import struct
import time
import zlib
from PySide2.QtCore import QObject, QTimer, Signal
from protocol.wire import Command as C, event_bytes

class DeviceSongs(QObject):
    progress = Signal(str, int)
    finished = Signal()
    failed = Signal(str)
    def __init__(self, client, parent=None):
        super().__init__(parent)
        self.client=client; self.active=False; self.epoch=0
        client.fault.connect(self.fail)
        client.connection.connect(lambda connected: None if connected else self.fail('Соединение прервано'))
    def fail(self, text):
        if self.active:
            self.active=False; self.epoch+=1; self.failed.emit(text)
    def start(self, allocation, title, slot, config):
        if self.active:return
        if not self.client.connected or self.client.sim:
            self.failed.emit('Подключите STM32 к компьютеру');return
        self.data=b''.join(event_bytes(*event) for event in allocation.events)
        if not self.data or len(self.data)>192512:
            self.failed.emit('Мелодия слишком велика: максимум 19 251 событие');return
        name=title.encode('utf-8')[:31].decode('utf-8','ignore').encode('utf-8').ljust(32,b'\0')
        mask=config['installed_mask'] & config['music_mask']
        if not mask:self.failed.emit('Не выбраны моторы');return
        dirs=sum(int(bool(d))<<i for i,d in enumerate(config['directions']))
        metadata=struct.pack('<BIIIBBB',slot,len(self.data)//10,allocation.duration_ms,zlib.crc32(self.data),mask,config['microstep_raw'],dirs)+name
        self.active=True;self.epoch+=1;self.offset=0
        self.progress.emit('Начало сохранения',0)
        self.submit(C.SONG_BEGIN,metadata)
    def submit(self,command,payload):
        if not self.active:return
        self.command=command;self.deadline=time.monotonic()+30;epoch=self.epoch
        self.client.send(command,payload,callback=lambda _: self.poll(epoch))
    def poll(self,epoch):
        if not self.active or epoch!=self.epoch:return
        if time.monotonic()>self.deadline:self.fail('Устройство не завершило сохранение');return
        self.client.send(C.SONG_STATUS,callback=lambda data:self.status(epoch,data))
    def status(self,epoch,data):
        if not self.active or epoch!=self.epoch:return
        if len(data)!=8:self.fail('Некорректный ответ устройства');return
        command,result,stage,percent,offset=struct.unpack('<BBBBI',data)
        if command!=self.command:self.fail('Нарушена последовательность сохранения');return
        stages={1:'Подготовка памяти',2:'Приём мелодии',3:'Проверка и сохранение',4:'Мелодия сохранена',6:'Ошибка сохранения'}
        self.progress.emit(stages.get(stage,'Сохранение'),min(percent,100))
        if result==255:
            QTimer.singleShot(50,lambda:self.poll(epoch));return
        if result:
            reasons={3:'Неверные данные мелодии',4:'Память недоступна или устройство занято',5:'Нет свободного места',6:'Нарушен порядок передачи',7:'Не совпала контрольная сумма',8:'Нет ответа от экрана',9:'Ошибка Flash'}
            self.fail(reasons.get(result,'Ошибка устройства: %d'%result));return
        if self.command==C.SONG_COMMIT:
            if stage!=4 or percent!=100:self.fail('Нет подтверждения сохранения');return
            self.active=False;self.finished.emit();return
        self.offset=offset
        if self.offset<len(self.data):
            chunk=self.data[self.offset:self.offset+180]
            self.submit(C.SONG_DATA,struct.pack('<I',self.offset)+chunk)
        else:self.submit(C.SONG_COMMIT,b'')

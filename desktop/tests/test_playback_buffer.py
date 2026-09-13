import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from app.playback import StreamPlayer
from protocol.wire import Command as C


class SignalStub:
    def connect(self, callback):
        pass


class Client:
    status=SignalStub()
    fault=SignalStub()
    def __init__(self,capacity):
        self.capacity=capacity
        self.commands=[]
    def send(self,command,payload=b'',callback=None):
        self.commands.append(command)
        if callback:
            callback(struct.pack('<H',self.capacity) if command==C.QUEUE else b'')


class BufferCapacityTests(unittest.TestCase):
    def test_old_and_new_controller_capacity(self):
        for capacity in (256,512):
            for raw in (False,True):
                with self.subTest(capacity=capacity,raw=raw):
                    client=Client(capacity)
                    player=StreamPlayer(client)
                    player.raw_mode=raw
                    ready=[]
                    player.prefill=lambda epoch:ready.append(player.free)
                    player.prepare_seek(player.epoch)
                    expected=max(0,capacity//13-1) if raw else capacity
                    self.assertEqual(ready,[expected])
                    self.assertEqual(client.commands,[C.QUEUE,C.RAW_SEEK] if raw else [C.QUEUE])
                    player.state='playing'
                    player.fill=lambda:None
                    player.on_status(dict(position=100,used=200,running=True))
                    self.assertEqual(player.free,max(0,(capacity-200)//13-1) if raw else capacity-200)

#pragma once
#include "music.h"
using MidiSink = void (*)(const music::Event &);
void usb_midi_start(MidiSink sink);

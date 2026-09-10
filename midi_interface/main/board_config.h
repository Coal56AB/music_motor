#pragma once
// GPIO19/20 belong to native USB. Adjust these three pins for your board.
#define MIDI_UART_TX 17
#define MIDI_UART_RX 18
#define MIDI_DEBUG_PIN 4
#define MIDI_DEBUG_ENABLED 0
// External protected 5 V supply on host VBUS; -1 means no software power switch.
#define USB_VBUS_ENABLE_PIN -1
#define USB_VBUS_ENABLE_LEVEL 1
#define MIDI_EVENT_QUEUE_SIZE 32

#pragma once
#include <stdint.h>
#include <stddef.h>
namespace music {
enum class Type : uint8_t { On, Off, Control, Reset };
struct Event { Type type; uint8_t channel, note, velocity; uint64_t timestamp; uint8_t source = 0; };
struct Config {
    unsigned voices = 6;
    int group_gap = 13, group_span = 24, track_distance = 7;
    uint64_t gesture_us = 40000, track_us = 600000;
    int independent = 260, melody = 400, bass = 160;
    int root = 180, third = 110, seventh = 90, fifth = 40, dyad_third = 20;
    int duplicate = 220, retained = 12, age = 8, released = 20;
    int distance_weight = 10, direction_penalty = 8, track_confidence_weight = 3;
    unsigned line_threshold = 2, max_confidence = 4;
    int confidence_bonus = 20, bass_boundary = 48, velocity_divisor = 16;
    uint64_t age_unit_us = 50000;
};
struct Chord { const char *name; uint8_t intervals[4], count; };
extern const Chord chords[9];
struct Harmony { int root = -1, kind = -1, confidence = 0; };
Harmony recognize(uint16_t pitch_mask);
struct NoteSet {
    uint8_t count = 0, notes[6] = {};
    bool operator==(const NoteSet &b) const;
};
class Engine {
public:
    explicit Engine(Config config = {}): cfg(config) { if (cfg.voices > 6) cfg.voices = 6; }
    NoteSet process(Event e);
    void reset();
    const NoteSet &output() const { return selected; }
    int group_of(uint8_t note) const { return groups[note]; }
    unsigned continuity(uint8_t note) const { return confidence[note]; }
    unsigned overflow_count = 0;
private:
    struct Key { uint64_t at = 0; uint8_t note = 0, channel = 0, source = 0, velocity = 0, track = 0; bool used = false, down = false; };
    struct Track { uint64_t at = 0; int note = 0, direction = 0; uint8_t channel = 0, source = 0, confidence = 0; bool used = false; };
    Config cfg;
    Key keys[256] = {};
    Track tracks[32] = {};
    bool sustain[16][16] = {};
    Event history[128] = {};
    unsigned history_head = 0;
    NoteSet selected;
    int groups[128] = {};
    unsigned confidence[128] = {};
    uint64_t selected_at[128] = {};
    uint8_t assign_track(const Event &e);
    NoteSet choose(uint64_t now);
};
// USB-MIDI 1.0 event packet decoder. Other transports call Engine::process directly.
bool decode_usb(const uint8_t packet[4], uint64_t timestamp, Event &event);
// Descriptor scanner: first MIDI 1.0 streaming bulk IN, no identity/endpoint assumptions.
struct Endpoint { uint8_t interface_number, alternate, address; uint16_t packet_size; };
bool find_endpoint(const uint8_t *descriptor, size_t length, Endpoint &out);
}

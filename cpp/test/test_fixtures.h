#pragma once
#include <vector>
#include <cstdint>
#include <fstream>
#include <string>

namespace midi_fixtures {

inline void appendVarLen(std::vector<uint8_t>& out, uint32_t value) {
    std::vector<uint8_t> bytes;
    bytes.push_back(value & 0x7F);
    value >>= 7;
    while (value > 0) {
        bytes.push_back(0x80 | (value & 0x7F));
        value >>= 7;
    }
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
        out.push_back(*it);
}

inline void appendUint16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

inline void appendUint32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

struct TrackBuilder {
    std::vector<uint8_t> events;

    void delta(uint32_t d) { appendVarLen(events, d); }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel, uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0x90 | (ch & 0x0F));
        events.push_back(note);
        events.push_back(vel);
    }

    void noteOff(uint8_t ch, uint8_t note, uint32_t deltaTicks = 480) {
        delta(deltaTicks);
        events.push_back(0x80 | (ch & 0x0F));
        events.push_back(note);
        events.push_back(0);
    }

    void cc(uint8_t ch, uint8_t ccNum, uint8_t val, uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0xB0 | (ch & 0x0F));
        events.push_back(ccNum);
        events.push_back(val);
    }

    void programChange(uint8_t ch, uint8_t prog, uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0xC0 | (ch & 0x0F));
        events.push_back(prog);
    }

    void pitchBend(uint8_t ch, int value, uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0xE0 | (ch & 0x0F));
        events.push_back(value & 0x7F);
        events.push_back((value >> 7) & 0x7F);
    }

    void tempo(uint32_t mpq, uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0xFF);
        events.push_back(0x51);
        events.push_back(3);
        events.push_back((mpq >> 16) & 0xFF);
        events.push_back((mpq >> 8) & 0xFF);
        events.push_back(mpq & 0xFF);
    }

    void endOfTrack(uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0xFF);
        events.push_back(0x2F);
        events.push_back(0);
    }

    void sysex(const std::vector<uint8_t>& body, uint32_t deltaTicks = 0) {
        delta(deltaTicks);
        events.push_back(0xF0);
        appendVarLen(events, (uint32_t)body.size());
        events.insert(events.end(), body.begin(), body.end());
    }
};

inline std::vector<uint8_t> buildSmf(const std::vector<uint8_t>& trackData,
                                     uint16_t format = 0,
                                     uint16_t tpq = 480) {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'M', 'T', 'h', 'd'});
    appendUint32BE(out, 6);
    appendUint16BE(out, format);
    appendUint16BE(out, 1);
    appendUint16BE(out, tpq);
    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    appendUint32BE(out, (uint32_t)trackData.size());
    out.insert(out.end(), trackData.begin(), trackData.end());
    return out;
}

inline bool writeTempFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    return f.good();
}

// Roland GS DT1 SysEx body (without F0/F7)
inline std::vector<uint8_t> rolandGsDt1(uint8_t addr1, uint8_t addr2, uint8_t addr3,
                                        uint8_t value, uint8_t device = 0x10) {
    std::vector<uint8_t> body = {0x41, device, 0x42, 0x12, addr1, addr2, addr3, value};
    int sum = addr1 + addr2 + addr3 + value;
    body.push_back((uint8_t)((128 - (sum % 128)) % 128));
    return body;
}

inline std::vector<uint8_t> makeSimpleNoteMidi(uint8_t channel, uint8_t note, uint8_t velocity) {
    TrackBuilder tb;
    tb.noteOn(channel, note, velocity);
    tb.noteOff(channel, note, 480);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

inline std::vector<uint8_t> makeTempoMidi(double bpm) {
    uint32_t mpq = (uint32_t)(60000000.0 / bpm);
    TrackBuilder tb;
    tb.tempo(mpq);
    tb.noteOn(0, 60, 100);
    tb.noteOff(0, 60, 480);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

inline std::vector<uint8_t> makeBankSelectMidi(uint8_t ch, uint8_t msb, uint8_t lsb) {
    TrackBuilder tb;
    tb.cc(ch, 0, msb);
    tb.cc(ch, 32, lsb);
    tb.programChange(ch, 0);
    tb.noteOn(ch, 60, 100);
    tb.noteOff(ch, 60, 480);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

inline std::vector<uint8_t> makeGsReverbLevelMidi(uint8_t level) {
    TrackBuilder tb;
    tb.sysex(rolandGsDt1(0x40, 0x01, 0x03, level));
    tb.noteOn(0, 60, 100);
    tb.noteOff(0, 60, 480);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

inline std::vector<uint8_t> makeCh10DrumMidi() {
    TrackBuilder tb;
    tb.noteOn(9, 36, 110);
    tb.noteOff(9, 36, 480);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

inline std::vector<uint8_t> makeCh10MelodyModeMidi() {
    TrackBuilder tb;
    // GS part 9 (MIDI ch10) -> Melody mode (0x00)
    tb.sysex(rolandGsDt1(0x40, 0x19, 0x15, 0x00));
    tb.programChange(9, 0);
    tb.noteOn(9, 60, 100);
    tb.noteOff(9, 60, 480);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

// Sustained note for amplitude-modulation / undulation tests (CC1=0, CC93=0).
inline std::vector<uint8_t> makeSustainedNoteMidi(uint8_t channel = 0, uint8_t note = 60,
                                                  uint8_t velocity = 100,
                                                  uint32_t holdTicks = 480 * 4) {
    TrackBuilder tb;
    tb.cc(channel, 1, 0);
    tb.cc(channel, 93, 0);
    tb.programChange(channel, 0);
    tb.noteOn(channel, note, velocity);
    tb.noteOff(channel, note, holdTicks);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

inline std::vector<uint8_t> makeChorusMidi(uint8_t chorusCC, uint8_t channel = 0) {
    TrackBuilder tb;
    tb.cc(channel, 1, 0);
    tb.cc(channel, 93, chorusCC);
    tb.programChange(channel, 0);
    tb.noteOn(channel, 60, 100);
    tb.noteOff(channel, 60, 480 * 2);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

// Ch1 melody + Ch10 drum for full-mix regression.
inline std::vector<uint8_t> makeDrumAndMelodyMidi() {
    TrackBuilder tb;
    tb.programChange(0, 0);
    tb.noteOn(0, 60, 100);
    tb.noteOn(9, 36, 110);
    tb.noteOff(0, 60, 480 * 2);
    tb.noteOff(9, 36, 480 * 2);
    tb.endOfTrack();
    return buildSmf(tb.events);
}

} // namespace midi_fixtures

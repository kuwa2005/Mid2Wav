#include "midi_file.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <numeric>

bool MidiFile::load(const std::string& path) {
    m_tempoMap.clear();
    m_tempoMap.push_back({0, 120.0}); // デフォルトテンポ

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    size_t pos = 0;
    if (!parseHeader(data.data(), pos)) return false;

    while (pos < data.size()) {
        if (!parseTrack(data.data(), data.size(), pos)) break;
    }

    // テンポマップと演算データをソート
    std::sort(m_tempoMap.begin(), m_tempoMap.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    m_expression.sort();

    extractNotes();
    return true;
}

double MidiFile::tickToSeconds(int64_t tick) const {
    return tickToSeconds(tick, -1);
}

double MidiFile::tickToSeconds(int64_t tick, int track) const {
    const auto& tmap = (track >= 0 && track < (int)m_trackTempoMaps.size() && m_formatType == 2)
                        ? m_trackTempoMaps[track] : m_tempoMap;
    if (tmap.empty()) return (double)tick / m_ticksPerQuarter * (60.0 / 120.0);

    double seconds = 0;
    int64_t prevTick = 0;
    double prevBPM = tmap[0].second;

    for (const auto& [tempoTick, bpm] : tmap) {
        if (tempoTick >= tick) break;
        seconds += (double)(tempoTick - prevTick) / m_ticksPerQuarter * (60.0 / prevBPM);
        prevTick = tempoTick;
        prevBPM = bpm;
    }
    seconds += (double)(tick - prevTick) / m_ticksPerQuarter * (60.0 / prevBPM);
    return seconds;
}

bool MidiFile::parseHeader(const uint8_t* data, size_t& pos) {
    if (memcmp(data, "MThd", 4) != 0) return false;
    uint32_t headerLen = readUint32(data + 4);
    m_formatType = readUint16(data + 8);
    // uint16_t numTracks = readUint16(data + 10);
    uint16_t timeDivision = readUint16(data + 12);
    if (timeDivision & 0x8000) {
        // SMPTE format — treat as 120 BPM fallback
        m_ticksPerQuarter = 480;
    } else {
        m_ticksPerQuarter = timeDivision;
    }
    pos = 8 + headerLen;
    return true;
}

bool MidiFile::parseTrack(const uint8_t* data, size_t dataSize, size_t& pos) {
    if (pos + 8 > dataSize) return false;
    if (memcmp(data + pos, "MTrk", 4) != 0) return false;
    pos += 4;
    uint32_t trackSize = readUint32(data + pos);
    pos += 4;
    if (pos + trackSize > dataSize) return false;

    m_trackData.emplace_back(data + pos, data + pos + trackSize);
    pos += trackSize;
    return true;
}

void MidiFile::extractNotes() {
    int trackIdx = 0;

    for (auto& trackData : m_trackData) {
        size_t pos = 0;
        uint8_t runningStatus = 0;
        int64_t currentTick = 0;

        // チャンネル×ノートの状態管理
        std::array<std::array<int64_t, 128>, 16> noteOnTick{};
        std::array<std::array<bool, 128>, 16> noteOn{};
        std::array<std::array<uint8_t, 128>, 16> noteOnVel{};
        std::vector<MidiEvent> trackEvts;
        std::vector<std::pair<int64_t, double>> trackTempo;

        while (pos < trackData.size()) {
            uint32_t delta = readVarLen(trackData.data(), pos);
            currentTick += delta;

            uint8_t status = trackData[pos];
            if (status & 0x80) { pos++; runningStatus = status; }
            else status = runningStatus;

            if (status == 0xFF) {
                uint8_t metaType = trackData[pos++];
                uint32_t metaLen = readVarLen(trackData.data(), pos);
                if (metaType == 0x51 && metaLen == 3) {
                    uint32_t mpq = (trackData[pos] << 16) | (trackData[pos + 1] << 8) | trackData[pos + 2];
                    double bpm = 60000000.0 / mpq;
                    m_tempoMap.push_back({currentTick, bpm});
                    trackTempo.push_back({currentTick, bpm});
                }
                if (metaType == 0x03) {
                    std::string name((const char*)&trackData[pos], metaLen);
                    MidiEvent e; e.tick = currentTick; e.type = 0xFF; e.data1 = 0x03;
                    trackEvts.push_back(e);
                }
                pos += metaLen;
                continue;
            }

            if (status == 0xFC) { pos++; continue; }
            if (status == 0x00) { pos++; continue; }

            uint8_t channel = status & 0x0F;
            uint8_t msgType = status & 0xF0;

            MidiEvent evt;
            evt.tick = currentTick;
            evt.type = status;
            evt.channel = channel;

            if (msgType == 0x90) {
                uint8_t note = trackData[pos++];
                uint8_t vel = trackData[pos++];
                evt.data1 = note; evt.data2 = vel;
                trackEvts.push_back(evt);
                if (vel > 0) {
                    noteOnTick[channel][note] = currentTick;
                    noteOn[channel][note] = true;
                    noteOnVel[channel][note] = vel;
                } else if (noteOn[channel][note]) {
                    MidiNote n; n.channel = channel; n.note = note;
                    n.velocity = noteOnVel[channel][note];
                    n.startTime = noteOnTick[channel][note]; n.endTime = currentTick;
                    n.track = trackIdx;
                    m_notes.push_back(n); noteOn[channel][note] = false;
                }
            } else if (msgType == 0x80) {
                uint8_t note = trackData[pos++];
                uint8_t vel = trackData[pos++];
                evt.data1 = note; evt.data2 = vel;
                trackEvts.push_back(evt);
                if (noteOn[channel][note]) {
                    MidiNote n; n.channel = channel; n.note = note;
                    n.velocity = noteOnVel[channel][note];
                    n.startTime = noteOnTick[channel][note]; n.endTime = currentTick;
                    n.track = trackIdx;
                    m_notes.push_back(n); noteOn[channel][note] = false;
                }
            } else if (msgType == 0xB0) {
                uint8_t cc = trackData[pos++];
                uint8_t val = trackData[pos++];
                evt.data1 = cc; evt.data2 = val;
                trackEvts.push_back(evt);
                switch (cc) {
                    case 0: m_expression.addBankSelectMSB(channel, currentTick, val); break;
                    case 1: m_expression.addModulation(channel, currentTick, val); break;
                    case 5: m_expression.addPortamentoTime(channel, currentTick, val); break;
                    case 7: m_expression.addVolume(channel, currentTick, val); break;
                    case 10: m_expression.addPan(channel, currentTick, val); break;
                    case 11: m_expression.addExpression(channel, currentTick, val); break;
                    case 32: break;
                    case 64: m_expression.addSustain(channel, currentTick, val); break;
                    case 65: m_expression.addPortamentoOn(channel, currentTick, val); break;
                    default: break;
                }
            } else if (msgType == 0xE0) {
                uint8_t lsb = trackData[pos++];
                uint8_t msb = trackData[pos++];
                int value = (msb << 7) | lsb;
                evt.data1 = lsb; evt.data2 = msb;
                trackEvts.push_back(evt);
                m_expression.addPitchBend(channel, currentTick, value);
            } else if (msgType == 0xC0) {
                uint8_t d1 = trackData[pos++];
                evt.data1 = d1;
                trackEvts.push_back(evt);
                m_expression.addProgramChange(channel, currentTick, d1);
            } else if (msgType == 0xD0) {
                uint8_t d1 = trackData[pos++];
                evt.data1 = d1;
                trackEvts.push_back(evt);
            }

            // SysExメッセージ (F0 ... F7)
            if (status == 0xF0) {
                // F0の次のバイトから読み取り
                size_t sysexStart = pos;
                while (pos < trackData.size() && trackData[pos] != 0xF7) pos++;
                if (pos < trackData.size()) pos++; // F7をスキップ

                size_t sysexLen = pos - sysexStart;
                if (sysexLen >= 2) {
                    uint8_t mfr = trackData[sysexStart]; // メーカーID
                    if (mfr == 0x41) {
                        // Roland
                        m_hasRolandGS = true;
                        // Device ID (通常 10=0x10), Unit (0x42=GS)
                        if (sysexLen >= 4 && trackData[sysexStart + 2] == 0x42) {
                            // GS DT1 SysEx: F0 41 10 42 12 <addr> <data...> F7
                            if (sysexLen >= 8 && trackData[sysexStart + 3] == 0x12) {
                                uint8_t addr1 = trackData[sysexStart + 4]; // アドレス上位
                                uint8_t addr2 = trackData[sysexStart + 5]; // アドレス中
                                uint8_t addr3 = sysexLen > 6 ? trackData[sysexStart + 6] : 0; // アドレス下位

                                // SC-8850 model ID detection
                                if (addr1 == 0x00 && addr2 == 0x00 && sysexLen >= 10) {
                                    if (trackData[sysexStart + 6] == 0x54 &&
                                        trackData[sysexStart + 7] == 0x00 &&
                                        trackData[sysexStart + 8] == 0x14) {
                                        m_hasSC8850 = true;
                                    }
                                }

                                // SC-88VL/SC-88: Model ID varies
                                if (sysexLen >= 8 && trackData[sysexStart + 3] == 0x12) {
                                    if (sysexLen >= 10) {
                                        if (trackData[sysexStart + 6] == 0x54 &&
                                            trackData[sysexStart + 7] == 0x00) {
                                            uint8_t modelVer = trackData[sysexStart + 8];
                                            if (modelVer == 0x08) m_hasSC88VL = true;
                                            else if (modelVer == 0x04) m_hasSC88 = true;
                                            else if (modelVer == 0x02) m_hasSC55 = true;
                                        }
                                    }
                                }

                                // GS エフェクトパラメータ解析
                                // リバーブ: addr1=0x40, addr2=0x01
                                if (addr1 == 0x40 && addr2 == 0x01 && sysexLen > 7) {
                                    uint8_t param = addr3;
                                    uint8_t val = trackData[sysexStart + 7];
                                    switch (param) {
                                        case 0x00: m_expression.sysReverbType.push_back({currentTick, val}); break;
                                        case 0x01: m_expression.sysReverbChar.push_back({currentTick, val}); break;
                                        case 0x02: m_expression.sysReverbPreLPF.push_back({currentTick, val}); break;
                                        case 0x03: m_expression.sysReverbLevel.push_back({currentTick, val}); break;
                                        case 0x04: m_expression.sysReverbDelay.push_back({currentTick, val}); break;
                                    }
                                }
                                // コーラス: addr1=0x40, addr2=0x02
                                if (addr1 == 0x40 && addr2 == 0x02 && sysexLen > 7) {
                                    uint8_t param = addr3;
                                    uint8_t val = trackData[sysexStart + 7];
                                    switch (param) {
                                        case 0x00: m_expression.sysChorusType.push_back({currentTick, val}); break;
                                        case 0x02: m_expression.sysChorusLevel.push_back({currentTick, val}); break;
                                        case 0x03: m_expression.sysChorusDelay.push_back({currentTick, val}); break;
                                        case 0x04: m_expression.sysChorusFeed.push_back({currentTick, val}); break;
                                    }
                                }
                                // ディレイ: addr1=0x40, addr2=0x03
                                if (addr1 == 0x40 && addr2 == 0x03 && sysexLen > 7) {
                                    uint8_t param = addr3;
                                    uint8_t val = trackData[sysexStart + 7];
                                    switch (param) {
                                        case 0x00: m_expression.sysDelayType.push_back({currentTick, val}); break;
                                        case 0x02: m_expression.sysDelayLevel.push_back({currentTick, val}); break;
                                        case 0x03: m_expression.sysDelayTime.push_back({currentTick, val}); break;
                                        case 0x04: m_expression.sysDelayFeed.push_back({currentTick, val}); break;
                                    }
                                }
                                // パート別エフェクト送り量: addr1=0x40, addr2=0x10-0x1F
                                if (addr1 == 0x40 && addr2 >= 0x10 && addr2 <= 0x1F && sysexLen > 7) {
                                    int part = addr2 - 0x10;
                                    uint8_t param = addr3;
                                    uint8_t val = trackData[sysexStart + 7];
                                    if (part >= 0 && part < 16) {
                                        switch (param) {
                                            case 0x03: m_expression.sysPartReverbSend[part].push_back({currentTick, val}); break;
                                            case 0x05: m_expression.sysPartChorusSend[part].push_back({currentTick, val}); break;
                                            case 0x06: m_expression.sysPartDelaySend[part].push_back({currentTick, val}); break;
                                        }
                                    }
                                }
                            }
                        }
                    } else if (mfr == 0x43) {
                        // Yamaha
                        m_hasYamahaXG = true;
                        // XG System: F0 43 10 4C ...
                        if (sysexLen >= 3 && trackData[sysexStart + 2] == 0x4C) {
                            // MU model detection from device ID or model-specific addresses
                            // MU-128, MU-100, MU-80, MU-50 are detected by XG system
                        }
                    }
                }
            }
        }

        m_trackEvents.push_back(std::move(trackEvts));
        m_trackTempoMaps.push_back(trackTempo);
        trackIdx++;
    }

    std::sort(m_notes.begin(), m_notes.end(),
        [](const MidiNote& a, const MidiNote& b) { return a.startTime < b.startTime; });
}

bool MidiFile::hasDamperPedal(int channel) const {
    for (auto& trackEvts : m_trackEvents) {
        for (auto& e : trackEvts) {
            if ((e.type & 0xF0) == 0xB0 && e.channel == channel && e.data1 == 64)
                return true;
        }
    }
    return false;
}

std::vector<TrackInfo> MidiFile::analyzeTracks() {
    std::vector<TrackInfo> tracks;

    std::array<std::vector<const MidiNote*>, 16> byChannel;
    for (auto& n : m_notes) byChannel[n.channel].push_back(&n);

    for (int ch = 0; ch < 16; ch++) {
        auto& chNotes = byChannel[ch];
        if (chNotes.empty()) continue;

        TrackInfo info{};
        info.index = ch;
        info.name = "track_" + std::to_string(ch);
        info.noteCount = (int)chNotes.size();
        info.isDrum = (ch == 9);
        info.hasDamperPedal = hasDamperPedal(ch);

        double sumPitch = 0, sumVel = 0;
        for (auto* n : chNotes) { sumPitch += n->note; sumVel += n->velocity; }
        info.avgPitch = sumPitch / chNotes.size();
        info.avgVelocity = sumVel / chNotes.size();

        int64_t minTick = chNotes.front()->startTime;
        int64_t maxTick = chNotes.back()->endTime;
        info.durationSeconds = tickToSeconds(maxTick - minTick);
        if (info.durationSeconds > 0) info.notesPerSecond = info.noteCount / info.durationSeconds;

        // Polyphony estimate
        int sampleCount = std::min(info.noteCount, 1000);
        double totalPoly = 0;
        for (int i = 0; i < sampleCount; i++) {
            int concurrent = 0;
            for (int j = std::max(0, i - 10); j < std::min(info.noteCount, i + 10); j++) {
                if (i != j && chNotes[j]->startTime < chNotes[i]->endTime && chNotes[j]->endTime > chNotes[i]->startTime)
                    concurrent++;
            }
            totalPoly += (1 + concurrent);
        }
        info.polyphonyEstimate = sampleCount > 0 ? totalPoly / sampleCount : 0;

        // Score calculation
        if (info.isDrum) {
            info.melodyScore = -100; info.bassScore = -100; info.harmonyScore = -100; info.ornamentScore = 5;
        } else {
            info.melodyScore = (info.avgPitch / 127.0) * 3.0
                + std::max(0.0, 2.0 - std::abs(info.polyphonyEstimate - 1.2))
                + std::max(0.0, 2.0 - std::abs(info.notesPerSecond - 3.0));
            info.bassScore = ((127.0 - info.avgPitch) / 127.0) * 4.0
                + std::max(0.0, 2.0 - std::abs(info.polyphonyEstimate - 1.1))
                + std::max(0.0, 2.0 - std::abs(info.notesPerSecond - 2.5));
            info.harmonyScore = std::max(0.0, 2.5 - std::abs(info.avgPitch - 60.0) / 20.0)
                + std::min(info.polyphonyEstimate, 4.0)
                + std::max(0.0, 1.5 - std::abs(info.notesPerSecond - 4.0));
            info.ornamentScore = std::min(info.notesPerSecond, 8.0) + (info.avgPitch / 127.0);
            if (info.notesPerSecond > 30) info.keep = false;
        }

        info.keep = info.noteCount >= 4;
        info.role = "unknown";
        tracks.push_back(info);
    }

    // Role assignment
    std::vector<TrackInfo*> drums, nonDrums;
    for (auto& t : tracks) { if (t.keep) { if (t.isDrum) drums.push_back(&t); else nonDrums.push_back(&t); } }
    for (auto* d : drums) d->role = "drums";

    if (!nonDrums.empty()) {
        auto* melody = *std::max_element(nonDrums.begin(), nonDrums.end(),
            [](TrackInfo* a, TrackInfo* b) { return a->melodyScore < b->melodyScore; });
        melody->role = "melody";

        TrackInfo* bass = nullptr;
        double bestBass = -1e9;
        for (auto* t : nonDrums) { if (t != melody && t->bassScore > bestBass) { bestBass = t->bassScore; bass = t; } }
        if (bass) bass->role = "bass";

        for (auto* t : nonDrums) {
            if (t == melody || t == bass) continue;
            if (t->melodyScore >= 3.0) { t->role = "sub_melody"; continue; }
            int harmonyCount = 0;
            for (auto* h : nonDrums) { if (h->role == "harmony") harmonyCount++; }
            if (harmonyCount < 2 && t->harmonyScore > 3.0) { t->role = "harmony"; continue; }
            if (t->ornamentScore > 5.0 && t->notesPerSecond <= 12.0) { t->role = "ornament"; continue; }
            t->role = "harmony";
        }
    }

    return tracks;
}

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <cmath>
#include <algorithm>

struct MidiNote {
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    int64_t startTime;
    int64_t endTime;
    int track = 0;
};

struct MidiEvent {
    int64_t tick;
    uint8_t type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
};

struct MidiExpression {
    // ピッチベンド: (tick, value 0-16383, center=8192)
    std::vector<std::pair<int64_t, int>> pitchBend[16];
    // モジュレーション（ビブラート）: (tick, value 0-127)
    std::vector<std::pair<int64_t, int>> modulation[16];
    // ポルタメントタイム: (tick, value 0-127)
    std::vector<std::pair<int64_t, int>> portamentoTime[16];
    // ポルタメントオン/オフ: (tick, 0=off, >0=on)
    std::vector<std::pair<int64_t, int>> portamentoOn[16];
    // ピッチベンドレンジ（半音）
    std::vector<std::pair<int64_t, int>> pitchBendRange[16];
    // チャンネルボリューム: (tick, value 0-127)
    std::vector<std::pair<int64_t, int>> volume[16];
    // パン: (tick, value 0-127, center=64)
    std::vector<std::pair<int64_t, int>> pan[16];
    // エクスプレッション: (tick, value 0-127)
    std::vector<std::pair<int64_t, int>> expression[16];
    // サステインペダル: (tick, 0=off, >=64=on)
    std::vector<std::pair<int64_t, int>> sustain[16];
    // プログラムチェンジ: (tick, program 0-127)
    std::vector<std::pair<int64_t, int>> programChange[16];
    // バンクセレクトMSB (CC0): (tick, value)
    std::vector<std::pair<int64_t, int>> bankSelectMSB[16];
    // GS SysEx: リバーブパラメータ
    std::vector<std::pair<int64_t, int>> sysReverbType;      // (tick, type 0-5)
    std::vector<std::pair<int64_t, int>> sysReverbLevel;     // (tick, level 0-127)
    std::vector<std::pair<int64_t, int>> sysReverbChar;      // (tick, character)
    std::vector<std::pair<int64_t, int>> sysReverbPreLPF;    // (tick, pre-LPF)
    std::vector<std::pair<int64_t, int>> sysReverbDelay;     // (tick, delay)
    // GS SysEx: コーラスパラメータ
    std::vector<std::pair<int64_t, int>> sysChorusType;      // (tick, type)
    std::vector<std::pair<int64_t, int>> sysChorusLevel;     // (tick, level)
    std::vector<std::pair<int64_t, int>> sysChorusDelay;     // (tick, delay)
    std::vector<std::pair<int64_t, int>> sysChorusFeed;      // (tick, feedback)
    // GS SysEx: ディレイパラメータ
    std::vector<std::pair<int64_t, int>> sysDelayType;       // (tick, type)
    std::vector<std::pair<int64_t, int>> sysDelayLevel;      // (tick, level)
    std::vector<std::pair<int64_t, int>> sysDelayTime;       // (tick, time)
    std::vector<std::pair<int64_t, int>> sysDelayFeed;       // (tick, feedback)
    // GS SysEx: パート別エフェクト送り量
    std::vector<std::pair<int64_t, int>> sysPartReverbSend[16];  // (tick, level)
    std::vector<std::pair<int64_t, int>> sysPartChorusSend[16];  // (tick, level)
    std::vector<std::pair<int64_t, int>> sysPartDelaySend[16];   // (tick, level)

    void addPitchBend(int ch, int64_t tick, int value) { pitchBend[ch].push_back({tick, value}); }
    void addModulation(int ch, int64_t tick, int value) { modulation[ch].push_back({tick, value}); }
    void addPortamentoTime(int ch, int64_t tick, int value) { portamentoTime[ch].push_back({tick, value}); }
    void addPortamentoOn(int ch, int64_t tick, int value) { portamentoOn[ch].push_back({tick, value}); }
    void addPitchBendRange(int ch, int64_t tick, int semi) { pitchBendRange[ch].push_back({tick, semi}); }
    void addVolume(int ch, int64_t tick, int value) { volume[ch].push_back({tick, value}); }
    void addPan(int ch, int64_t tick, int value) { pan[ch].push_back({tick, value}); }
    void addExpression(int ch, int64_t tick, int value) { expression[ch].push_back({tick, value}); }
    void addSustain(int ch, int64_t tick, int value) { sustain[ch].push_back({tick, value}); }
    void addProgramChange(int ch, int64_t tick, int value) { programChange[ch].push_back({tick, value}); }
    void addBankSelectMSB(int ch, int64_t tick, int value) { bankSelectMSB[ch].push_back({tick, value}); }

    void sort() {
        for (int ch = 0; ch < 16; ch++) {
            auto cmp = [](auto& a, auto& b) { return a.first < b.first; };
            std::sort(pitchBend[ch].begin(), pitchBend[ch].end(), cmp);
            std::sort(modulation[ch].begin(), modulation[ch].end(), cmp);
            std::sort(portamentoTime[ch].begin(), portamentoTime[ch].end(), cmp);
            std::sort(portamentoOn[ch].begin(), portamentoOn[ch].end(), cmp);
            std::sort(pitchBendRange[ch].begin(), pitchBendRange[ch].end(), cmp);
            std::sort(volume[ch].begin(), volume[ch].end(), cmp);
            std::sort(pan[ch].begin(), pan[ch].end(), cmp);
            std::sort(expression[ch].begin(), expression[ch].end(), cmp);
            std::sort(sustain[ch].begin(), sustain[ch].end(), cmp);
            std::sort(programChange[ch].begin(), programChange[ch].end(), cmp);
            std::sort(bankSelectMSB[ch].begin(), bankSelectMSB[ch].end(), cmp);
        }
        // GS SysEx sort
        auto cmp2 = [](auto& a, auto& b) { return a.first < b.first; };
        std::sort(sysReverbType.begin(), sysReverbType.end(), cmp2);
        std::sort(sysReverbLevel.begin(), sysReverbLevel.end(), cmp2);
        std::sort(sysReverbChar.begin(), sysReverbChar.end(), cmp2);
        std::sort(sysReverbPreLPF.begin(), sysReverbPreLPF.end(), cmp2);
        std::sort(sysReverbDelay.begin(), sysReverbDelay.end(), cmp2);
        std::sort(sysChorusType.begin(), sysChorusType.end(), cmp2);
        std::sort(sysChorusLevel.begin(), sysChorusLevel.end(), cmp2);
        std::sort(sysChorusDelay.begin(), sysChorusDelay.end(), cmp2);
        std::sort(sysChorusFeed.begin(), sysChorusFeed.end(), cmp2);
        std::sort(sysDelayType.begin(), sysDelayType.end(), cmp2);
        std::sort(sysDelayLevel.begin(), sysDelayLevel.end(), cmp2);
        std::sort(sysDelayTime.begin(), sysDelayTime.end(), cmp2);
        std::sort(sysDelayFeed.begin(), sysDelayFeed.end(), cmp2);
        for (int ch = 0; ch < 16; ch++) {
            std::sort(sysPartReverbSend[ch].begin(), sysPartReverbSend[ch].end(), cmp2);
            std::sort(sysPartChorusSend[ch].begin(), sysPartChorusSend[ch].end(), cmp2);
            std::sort(sysPartDelaySend[ch].begin(), sysPartDelaySend[ch].end(), cmp2);
        }
    }

    int getPitchBendRange(int ch, int64_t tick) const {
        int semi = 2;
        for (auto& [t, s] : pitchBendRange[ch]) { if (t > tick) break; semi = s; }
        return semi;
    }

    int getValueAtTick(const std::vector<std::pair<int64_t, int>>& data, int64_t tick, int defaultVal = 0) const {
        int val = defaultVal;
        for (auto& [t, v] : data) { if (t > tick) break; val = v; }
        return val;
    }
};

struct TrackInfo {
    int index;
    std::string name;
    int noteCount;
    double avgPitch;
    double avgVelocity;
    double durationSeconds;
    double notesPerSecond;
    double polyphonyEstimate;
    std::string role;
    bool isDrum;
    bool hasDamperPedal;
    bool keep;
    double melodyScore;
    double bassScore;
    double harmonyScore;
    double ornamentScore;
};

class MidiFile {
public:
    bool load(const std::string& path);

    int ticksPerQuarterNote() const { return m_ticksPerQuarter; }
    double initialTempo() const { return m_tempoMap.empty() ? 120.0 : m_tempoMap[0].second; }
    const std::vector<std::pair<int64_t, double>>& tempoMap() const { return m_tempoMap; }

    const std::vector<MidiNote>& notes() const { return m_notes; }
    const std::vector<MidiEvent>& events() const { return m_events; }
    const MidiExpression& expression() const { return m_expression; }

    int formatType() const { return m_formatType; }
    int numTracks() const { return (int)m_trackData.size(); }

    std::vector<TrackInfo> analyzeTracks();
    bool hasDamperPedal(int channel) const;

    double tickToSeconds(int64_t tick) const;
    double tickToSeconds(int64_t tick, int track) const;

    bool hasRolandGS() const { return m_hasRolandGS; }
    bool hasYamahaXG() const { return m_hasYamahaXG; }
    bool hasRolandSC8850() const { return m_hasSC8850; }
    bool hasRolandSC88VL() const { return m_hasSC88VL; }
    bool hasRolandSC88() const { return m_hasSC88; }
    bool hasRolandSC55() const { return m_hasSC55; }
    bool hasYamahaMU128() const { return m_hasMU128; }
    bool hasYamahaMU100() const { return m_hasMU100; }
    bool hasYamahaMU80() const { return m_hasMU80; }
    bool hasYamahaMU50() const { return m_hasMU50; }

private:
    int m_formatType = 0;
    int m_ticksPerQuarter = 480;
    std::vector<MidiNote> m_notes;
    std::vector<MidiEvent> m_events;
    MidiExpression m_expression;
    std::vector<std::vector<uint8_t>> m_trackData;
    std::vector<std::vector<MidiEvent>> m_trackEvents;
    std::vector<std::pair<int64_t, double>> m_tempoMap;
    std::vector<std::vector<std::pair<int64_t, double>>> m_trackTempoMaps;

    // SysEx detection flags
    bool m_hasRolandGS = false;
    bool m_hasYamahaXG = false;
    bool m_hasSC8850 = false;
    bool m_hasSC88VL = false;
    bool m_hasSC88 = false;
    bool m_hasSC55 = false;
    bool m_hasMU128 = false;
    bool m_hasMU100 = false;
    bool m_hasMU80 = false;
    bool m_hasMU50 = false;

    bool parseHeader(const uint8_t* data, size_t& pos);
    bool parseTrack(const uint8_t* data, size_t dataSize, size_t& pos);
    void extractNotes();

    static uint16_t readUint16(const uint8_t* d) { return (d[0] << 8) | d[1]; }
    static uint32_t readUint32(const uint8_t* d) { return ((uint32_t)d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]; }
    static uint32_t readVarLen(const uint8_t* d, size_t& pos) {
        uint32_t v = 0; uint8_t b;
        do { b = d[pos++]; v = (v << 7) | (b & 0x7F); } while (b & 0x80);
        return v;
    }
};

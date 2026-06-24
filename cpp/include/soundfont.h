#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <array>

struct SF2Sample {
    std::string name;
    uint32_t start;
    uint32_t end;
    uint32_t startLoop;
    uint32_t endLoop;
    uint32_t sampleRate;
    uint8_t originalKey;
    int8_t correction;
    uint16_t sampleLink;
    uint16_t sampleType;
};

struct SF2Generator {
    uint16_t oper;
    int16_t amount;
};

enum class SF2GenOper : uint16_t {
    startAddrsOffset = 0,
    endAddrsOffset = 1,
    startloopAddrsOffset = 2,
    endloopAddrsOffset = 3,
    startAddrsCoarseOffset = 4,
    modLfoToPitch = 5,
    vibLfoToPitch = 6,
    modEnvToPitch = 7,
    initialFilterFc = 8,
    initialFilterQ = 9,
    modLfoToFilterFc = 10,
    modEnvToFilterFc = 11,
    endAddrsCoarseOffset = 12,
    modLfoToVolume = 13,
    chorusEffectsSend = 15,
    reverbEffectsSend = 16,
    pan = 17,
    delayModLFO = 21,
    freqModLFO = 22,
    delayVibLFO = 23,
    freqVibLFO = 24,
    delayModEnv = 25,
    attackModEnv = 26,
    holdModEnv = 27,
    decayModEnv = 28,
    sustainModEnv = 29,
    releaseModEnv = 30,
    keynumToModEnvHold = 31,
    keynumToModEnvDecay = 32,
    delayVolEnv = 33,
    attackVolEnv = 34,
    holdVolEnv = 35,
    decayVolEnv = 36,
    sustainVolEnv = 37,
    releaseVolEnv = 38,
    keynumToVolEnvHold = 39,
    keynumToVolEnvDecay = 40,
    instrument = 41,
    keyRange = 43,
    velRange = 44,
    startloopAddrsCoarseOffset = 45,
    keynum = 46,
    velocity = 47,
    initialAttenuation = 48,
    endloopAddrsCoarseOffset = 50,
    coarseTune = 51,
    fineTune = 52,
    sampleID = 53,
    sampleModes = 54,
    scaleTuning = 56,
    exclusiveClass = 57,
    overridingRootKey = 58,
};

struct SF2Zone {
    std::vector<SF2Generator> generators;
};

struct SF2Preset {
    std::string name;
    uint16_t preset;
    uint16_t bank;
    uint32_t presetBagNdx;
};

struct SF2PresetBag {
    uint32_t genNdx;
    uint32_t modNdx;
};

struct SF2Instrument {
    std::string name;
    uint32_t instBagNdx;
};

struct SF2InstBag {
    uint32_t genNdx;
    uint32_t modNdx;
};

class SoundFont {
public:
    bool load(const std::string& path);

    const std::vector<SF2Sample>& samples() const { return m_samples; }
    const std::vector<SF2Preset>& presets() const { return m_presets; }
    const std::vector<SF2Instrument>& instruments() const { return m_instruments; }
    const std::vector<SF2PresetBag>& presetBags() const { return m_presetBags; }
    const std::vector<SF2Generator>& presetGenerators() const { return m_presetGenerators; }
    const std::vector<SF2InstBag>& instBags() const { return m_instBags; }
    const std::vector<SF2Generator>& instGenerators() const { return m_instGenerators; }
    const int16_t* sampleData() const { return m_sampleData.data(); }
    size_t sampleDataSize() const { return m_sampleData.size(); }

    int findPreset(uint16_t bank, uint16_t preset) const;
    int findInstrument(const std::string& name) const;

private:
    bool parseINFO(const uint8_t* data, size_t size);
    bool parseSDTA(const uint8_t* data, size_t size);
    bool parsePDTA(const uint8_t* data, size_t size);

    std::vector<int16_t> m_sampleData;
    std::vector<SF2Sample> m_samples;
    std::vector<SF2Preset> m_presets;
    std::vector<SF2PresetBag> m_presetBags;
    std::vector<SF2Generator> m_presetGenerators;
    std::vector<SF2Instrument> m_instruments;
    std::vector<SF2InstBag> m_instBags;
    std::vector<SF2Generator> m_instGenerators;

    std::string m_name;
    std::string m_engine;
    std::string m_author;
    std::string m_copyright;

    static uint16_t readUint16(const uint8_t* d) { return (uint16_t)(d[0] | (d[1] << 8)); }
    static uint32_t readUint32(const uint8_t* d) { return (uint32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24)); }
    static int16_t readInt16(const uint8_t* d) { return (int16_t)(d[0] | (d[1] << 8)); }
};

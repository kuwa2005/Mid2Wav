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
    startAddressOffset = 0,
    endAddressOffset = 1,
    startloopAddressOffset = 2,
    endloopAddressOffset = 3,
    startAddressCoarseOffset = 4,
    keyRange = 5,
    velRange = 6,
    startloopAddressCoarseOffset = 7,
    keynums = 8,
    velocity = 9,
    initialAttenuation = 10,
    endAddressCoarseOffset = 12,
    pan = 17,
    endloopAddressCoarseOffset = 18,
    lightModalTuning = 21,
    scaleTuning = 24,
    exclusiveClass = 25,
    overridingRootKey = 26,
    initialPitch = 33,
    attackVolEnv = 34,
    decayVolEnv = 36,
    sustainVolEnv = 37,
    releaseVolEnv = 38,
    attackModEnv = 39,
    decayModEnv = 40,
    sustainModEnv = 41,
    releaseModEnv = 42,
    initialFilterFc = 43,
    initialFilterQ = 44,
    attackModLfo = 31,
    delayModLfo = 32,
    frequencyModLfo = 22,
    delayVibLfo = 23,
    frequencyVibLfo = 24,
    delayModEnv = 25,
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
    const int32_t* sampleData() const { return m_sampleData.data(); }
    size_t sampleDataSize() const { return m_sampleData.size(); }

    int findPreset(uint16_t bank, uint16_t preset) const;
    int findInstrument(const std::string& name) const;

private:
    bool parseINFO(const uint8_t* data, size_t size);
    bool parseSDTA(const uint8_t* data, size_t size);
    bool parsePDTA(const uint8_t* data, size_t size);

    std::vector<int32_t> m_sampleData;
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

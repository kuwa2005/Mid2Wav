#include "soundfont.h"
#include <fstream>
#include <iostream>
#include <cstring>

bool SoundFont::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[SF2] Cannot open: " << path << std::endl;
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    size_t pos = 0;

    // RIFF ヘッダチェック
    if (fileSize < 12 || memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "sfbk", 4) != 0) {
        std::cerr << "[SF2] Not a valid SF2 file" << std::endl;
        return false;
    }
    pos = 12;

    // チャンク読み込み
    while (pos + 8 <= fileSize) {
        char chunkId[5] = {};
        memcpy(chunkId, data.data() + pos, 4);
        uint32_t chunkSize = readUint32(data.data() + pos + 4);
        pos += 8;

        if (pos + chunkSize > fileSize) {
            std::cerr << "[SF2] Truncated chunk: " << chunkId << std::endl;
            break;
        }

        if (memcmp(chunkId, "LIST", 4) == 0) {
            char listType[5] = {};
            memcpy(listType, data.data() + pos, 4);

            if (memcmp(listType, "INFO", 4) == 0) {
                parseINFO(data.data() + pos + 4, chunkSize - 4);
            } else if (memcmp(listType, "sdta", 4) == 0) {
                parseSDTA(data.data() + pos + 4, chunkSize - 4);
            } else if (memcmp(listType, "pdta", 4) == 0) {
                parsePDTA(data.data() + pos + 4, chunkSize - 4);
            }
        }

        pos += chunkSize;
        if (chunkSize % 2 != 0) pos++; // パディング
    }

    std::cout << "[SF2] Loaded: " << m_name << std::endl;
    std::cout << "[SF2] Presets: " << m_presets.size() << ", Instruments: " << m_instruments.size()
              << ", Samples: " << m_samples.size() << std::endl;
    std::cout << "[SF2] Sample data: " << m_sampleData.size() * 2 << " bytes" << std::endl;

    return true;
}

bool SoundFont::parseINFO(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        char chunkId[5] = {};
        memcpy(chunkId, data + pos, 4);
        uint32_t chunkSize = readUint32(data + pos + 4);
        pos += 8;

        if (pos + chunkSize > size) break;

        std::string value((const char*)(data + pos), chunkSize);
        // NULL文字を除去
        while (!value.empty() && value.back() == '\0') value.pop_back();

        if (memcmp(chunkId, "name", 4) == 0) m_name = value;
        else if (memcmp(chunkId, "isng", 4) == 0) m_engine = value;
        else if (memcmp(chunkId, "IART", 4) == 0 || memcmp(chunkId, "iart", 4) == 0) m_author = value;
        else if (memcmp(chunkId, "ICOP", 4) == 0 || memcmp(chunkId, "icop", 4) == 0) m_copyright = value;

        pos += chunkSize;
        if (chunkSize % 2 != 0) pos++;
    }
    return true;
}

bool SoundFont::parseSDTA(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        char chunkId[5] = {};
        memcpy(chunkId, data + pos, 4);
        uint32_t chunkSize = readUint32(data + pos + 4);
        pos += 8;

        if (pos + chunkSize > size) break;

        if (memcmp(chunkId, "smpl", 4) == 0) {
            size_t sampleCount = chunkSize / sizeof(int16_t);
            m_sampleData.resize(sampleCount);
            memcpy(m_sampleData.data(), data + pos, chunkSize);
        }

        pos += chunkSize;
        if (chunkSize % 2 != 0) pos++;
    }
    return true;
}

bool SoundFont::parsePDTA(const uint8_t* data, size_t size) {
    size_t pos = 0;

    while (pos + 8 <= size) {
        char chunkId[5] = {};
        memcpy(chunkId, data + pos, 4);
        uint32_t chunkSize = readUint32(data + pos + 4);
        pos += 8;

        if (pos + chunkSize > size) break;

        if (memcmp(chunkId, "phdr", 4) == 0) {
            size_t count = chunkSize / 38;
            m_presets.resize(count > 0 ? count - 1 : 0);
            for (size_t i = 0; i < count - 1; i++) {
                const uint8_t* d = data + pos + i * 38;
                m_presets[i].name = std::string((const char*)d, 20);
                while (!m_presets[i].name.empty() && m_presets[i].name.back() == '\0') m_presets[i].name.pop_back();
                m_presets[i].preset = readUint16(d + 20);
                m_presets[i].bank = readUint16(d + 22);
                // SF2 spec: dwPresetBagNdx is at offset 24 (4 bytes)
                // But some SF2 files use 38-byte records where bagNdx is at offset 24
                m_presets[i].presetBagNdx = readUint16(d + 24);
            }
        } else if (memcmp(chunkId, "pbag", 4) == 0) {
            size_t count = chunkSize / 4;
            m_presetBags.resize(count);
            for (size_t i = 0; i < count; i++) {
                const uint8_t* d = data + pos + i * 4;
                m_presetBags[i].genNdx = readUint16(d);
                m_presetBags[i].modNdx = readUint16(d + 2);
            }
        } else if (memcmp(chunkId, "pgen", 4) == 0) {
            size_t count = chunkSize / 4;
            m_presetGenerators.resize(count);
            for (size_t i = 0; i < count; i++) {
                const uint8_t* d = data + pos + i * 4;
                m_presetGenerators[i].oper = readUint16(d);
                m_presetGenerators[i].amount = readInt16(d + 2);
            }
        } else if (memcmp(chunkId, "inst", 4) == 0) {
            size_t count = chunkSize / 22; // 22 bytes per entry
            m_instruments.resize(count > 0 ? count - 1 : 0); // 最後は終端マーカー
            for (size_t i = 0; i < count - 1; i++) {
                const uint8_t* d = data + pos + i * 22;
                m_instruments[i].name = std::string((const char*)d, 20);
                while (!m_instruments[i].name.empty() && m_instruments[i].name.back() == '\0') m_instruments[i].name.pop_back();
                m_instruments[i].instBagNdx = readUint16(d + 20); // uint16, not uint32
            }
        } else if (memcmp(chunkId, "ibag", 4) == 0) {
            size_t count = chunkSize / 4;
            m_instBags.resize(count);
            for (size_t i = 0; i < count; i++) {
                const uint8_t* d = data + pos + i * 4;
                m_instBags[i].genNdx = readUint16(d);
                m_instBags[i].modNdx = readUint16(d + 2);
            }
        } else if (memcmp(chunkId, "igen", 4) == 0) {
            size_t count = chunkSize / 4;
            m_instGenerators.resize(count);
            for (size_t i = 0; i < count; i++) {
                const uint8_t* d = data + pos + i * 4;
                m_instGenerators[i].oper = readUint16(d);
                m_instGenerators[i].amount = readInt16(d + 2);
            }
        } else if (memcmp(chunkId, "shdr", 4) == 0) {
            size_t count = chunkSize / 46;
            m_samples.resize(count > 0 ? count - 1 : 0); // 最後は終端マーカー
            for (size_t i = 0; i < count - 1; i++) {
                const uint8_t* d = data + pos + i * 46;
                m_samples[i].name = std::string((const char*)d, 20);
                while (!m_samples[i].name.empty() && m_samples[i].name.back() == '\0') m_samples[i].name.pop_back();
                m_samples[i].start = readUint32(d + 20);
                m_samples[i].end = readUint32(d + 24);
                m_samples[i].startLoop = readUint32(d + 28);
                m_samples[i].endLoop = readUint32(d + 32);
                m_samples[i].sampleRate = readUint32(d + 36);
                m_samples[i].originalKey = d[40];
                m_samples[i].correction = (int8_t)d[41];
                m_samples[i].sampleLink = readUint16(d + 42);
                m_samples[i].sampleType = readUint16(d + 44);
            }
        }

        pos += chunkSize;
        if (chunkSize % 2 != 0) pos++;
    }
    return true;
}

int SoundFont::findPreset(uint16_t bank, uint16_t preset) const {
    for (size_t i = 0; i < m_presets.size(); i++) {
        if (m_presets[i].bank == bank && m_presets[i].preset == preset) return (int)i;
    }
    return -1;
}

int SoundFont::findInstrument(const std::string& name) const {
    for (size_t i = 0; i < m_instruments.size(); i++) {
        if (m_instruments[i].name == name) return (int)i;
    }
    return -1;
}

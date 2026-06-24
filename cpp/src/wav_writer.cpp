#include "wav_writer.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

bool WavWriter::write(const std::string& path,
                      const std::vector<float>& left,
                      const std::vector<float>& right,
                      int sampleRate) {
    if (left.size() != right.size() || left.empty()) return false;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    size_t sampleCount = left.size();
    uint32_t dataSize = (uint32_t)(sampleCount * 2 * sizeof(int16_t));

    // RIFF ヘッダ
    fwrite("RIFF", 1, 4, f);
    uint32_t fileSize = 4 + (8 + 16) + (8 + dataSize);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt チャンク
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFmt = 1; // PCM
    fwrite(&audioFmt, 2, 1, f);
    uint16_t numChannels = 2;
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    uint32_t byteRate = sampleRate * numChannels * sizeof(int16_t);
    fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = numChannels * sizeof(int16_t);
    fwrite(&blockAlign, 2, 1, f);
    uint16_t bitsPerSample = 16;
    fwrite(&bitsPerSample, 2, 1, f);

    // data チャンク
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

    // インターリーブされたPCMデータを書き込み
    std::vector<int16_t> interleaved(sampleCount * 2);
    for (size_t i = 0; i < sampleCount; i++) {
        interleaved[i * 2] = (int16_t)std::clamp(left[i] * 32767.0f, -32768.0f, 32767.0f);
        interleaved[i * 2 + 1] = (int16_t)std::clamp(right[i] * 32767.0f, -32768.0f, 32767.0f);
    }

    fwrite(interleaved.data(), dataSize, 1, f);
    fclose(f);

    return true;
}

bool WavReader::read(const std::string& path,
                     std::vector<float>& left, std::vector<float>& right,
                     int& sampleRate) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // RIFF header
    char riff[4];
    if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) { fclose(f); return false; }
    uint32_t fileSize;
    if (fread(&fileSize, 4, 1, f) != 1) { fclose(f); return false; }
    char wave[4];
    if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) { fclose(f); return false; }

    uint16_t numChannels = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataSize = 0;
    bool foundFmt = false, foundData = false;

    while (!feof(f) && !(foundFmt && foundData)) {
        char chunkId[4];
        if (fread(chunkId, 1, 4, f) != 4) break;
        uint32_t chunkSize;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t audioFmt;
            fread(&audioFmt, 2, 1, f);
            fread(&numChannels, 2, 1, f);
            fread(&sampleRate, 4, 1, f);
            uint32_t byteRate;
            fread(&byteRate, 4, 1, f);
            uint16_t blockAlign;
            fread(&blockAlign, 2, 1, f);
            fread(&bitsPerSample, 2, 1, f);
            if (chunkSize > 16) fseek(f, chunkSize - 16, SEEK_CUR);
            foundFmt = true;
        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            foundData = true;
        } else {
            fseek(f, chunkSize, SEEK_CUR);
        }
    }

    if (!foundFmt || !foundData || (numChannels != 2) || (bitsPerSample != 16)) {
        fclose(f);
        return false;
    }

    uint32_t sampleCount = dataSize / (numChannels * sizeof(int16_t));
    std::vector<int16_t> interleaved(sampleCount * 2);
    if (fread(interleaved.data(), sizeof(int16_t), sampleCount * 2, f) != sampleCount * 2) {
        fclose(f);
        return false;
    }
    fclose(f);

    left.resize(sampleCount);
    right.resize(sampleCount);
    for (size_t i = 0; i < sampleCount; i++) {
        left[i] = interleaved[i * 2] / 32768.0f;
        right[i] = interleaved[i * 2 + 1] / 32768.0f;
    }
    return true;
}

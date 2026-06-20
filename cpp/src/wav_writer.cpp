#include "wav_writer.h"
#include <cstdio>
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

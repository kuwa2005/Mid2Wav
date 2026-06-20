#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct WavWriter {
    static bool write(const std::string& path,
                      const std::vector<float>& left,
                      const std::vector<float>& right,
                      int sampleRate);
};

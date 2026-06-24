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

struct WavReader {
    static bool read(const std::string& path,
                     std::vector<float>& left, std::vector<float>& right,
                     int& sampleRate);
};

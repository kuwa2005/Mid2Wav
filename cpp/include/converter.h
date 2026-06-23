#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class DeviceModel { GM, GS, SC55, SC88, SC88VL, SC8850, XG, MU50, MU80, MU100, MU128, MOTIF };

struct ConvertOptions {
    std::string inputPath;
    std::string outputPath;
    std::string sf2Path = "soundfonts/GM.sf2";
    bool sf2Auto = false;
    bool analyzeOnly = false;
    bool channelSplit = false;
    DeviceModel device = DeviceModel::GM;
    bool deviceAuto = true;
    int pitchShift = 0;
    bool csvLog = false;
    bool noNormalize = false;
    double gainDb = 0.0; // Master gain in dB
};

struct BatchLogEntry {
    std::string inputFile;
    std::string outputFile;
    std::string status;
    std::string failReason;
    int notes = 0;
    double duration = 0;
    double rms = 0;
    double peak = 0;
    long processingTimeMs = 0;
};

class BatchLogger {
public:
    BatchLogger(const std::string& outputDir);
    void addEntry(const BatchLogEntry& entry);
    void saveLog();
    void printSummary();
private:
    std::string m_outputDir;
    std::string m_logFile;
    std::vector<BatchLogEntry> m_entries;
};

std::string formatAnalysisText(const std::vector<struct TrackInfo>& tracks, const struct MidiFile& midi);

int runConverter(const ConvertOptions& opts);

#pragma once
#include "log.h"
#include <string>
#include <iomanip>
#include <sstream>

// Per-file pipeline milestones (fixed weights, approximate overall %)
namespace ProgressStage {
    constexpr int Start       = 0;
    constexpr int MidiLoaded  = 5;
    constexpr int SynthReady  = 10;
    constexpr int RenderStart = 15;
    constexpr int RenderEnd   = 85;
    constexpr int Mixing      = 90;
    constexpr int Done        = 100;
}

class BatchProgress {
public:
    static BatchProgress* active() { return s_active; }

    void beginBatch(int totalFiles) {
        m_totalFiles = totalFiles > 0 ? totalFiles : 1;
    }

    void beginFile(int fileIndex, const std::string& fileName) {
        m_fileIndex = fileIndex;
        m_fileName = fileName;
        m_filePercent = ProgressStage::Start;
        s_active = this;
        refresh();
    }

    void endFile() {
        refresh();
        Log::finishProgressLine();
        s_active = nullptr;
    }

    void setFilePercent(int percent) {
        m_filePercent = percent;
        refresh();
    }

    void setRenderProgress(int channelIndex, int channelCount, int samplePercent) {
        if (channelCount <= 0) {
            m_filePercent = ProgressStage::RenderEnd;
        } else {
            const int renderRange = ProgressStage::RenderEnd - ProgressStage::RenderStart;
            const double channelFrac =
                (static_cast<double>(channelIndex) + samplePercent / 100.0) / channelCount;
            m_filePercent = ProgressStage::RenderStart
                + static_cast<int>(channelFrac * renderRange);
        }
        refresh();
    }

    int overallPercent() const {
        return (m_fileIndex * 100 + m_filePercent) / m_totalFiles;
    }

    std::string formatLine() const {
        std::ostringstream ss;
        ss << '[' << (m_fileIndex + 1) << '/' << m_totalFiles << "] "
           << std::setw(3) << overallPercent() << "%  " << m_fileName;
        return ss.str();
    }

private:
    void refresh() { Log::updateProgressLine(formatLine()); }

    static BatchProgress* s_active;

    int m_totalFiles = 1;
    int m_fileIndex = 0;
    std::string m_fileName;
    int m_filePercent = 0;
};

inline BatchProgress* BatchProgress::s_active = nullptr;

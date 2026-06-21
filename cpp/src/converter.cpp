#include "converter.h"
#include "midi_file.h"
#include "wav_writer.h"
#include "soundfont.h"
#include "sf_synth.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

static DeviceModel detectDevice(const MidiFile& midi) {
    if (midi.hasRolandSC8850()) return DeviceModel::SC8850;
    if (midi.hasRolandSC88VL()) return DeviceModel::SC88VL;
    if (midi.hasRolandSC88()) return DeviceModel::SC88;
    if (midi.hasRolandSC55()) return DeviceModel::SC55;
    if (midi.hasRolandGS()) return DeviceModel::GS;
    if (midi.hasYamahaMU128()) return DeviceModel::MU128;
    if (midi.hasYamahaMU100()) return DeviceModel::MU100;
    if (midi.hasYamahaMU80()) return DeviceModel::MU80;
    if (midi.hasYamahaMU50()) return DeviceModel::MU50;
    if (midi.hasYamahaXG()) return DeviceModel::XG;
    return DeviceModel::GM;
}

static const char* deviceName(DeviceModel d) {
    switch (d) {
        case DeviceModel::GM: return "GM";
        case DeviceModel::GS: return "GS";
        case DeviceModel::SC55: return "SC-55";
        case DeviceModel::SC88: return "SC-88";
        case DeviceModel::SC88VL: return "SC-88VL";
        case DeviceModel::SC8850: return "SC-8850";
        case DeviceModel::XG: return "XG";
        case DeviceModel::MU50: return "MU-50";
        case DeviceModel::MU80: return "MU-80";
        case DeviceModel::MU100: return "MU-100";
        case DeviceModel::MU128: return "MU-128";
        case DeviceModel::MOTIF: return "MOTIF";
    }
    return "Unknown";
}

static std::string findBestSoundFont() {
    // 検索ディレクトリ
    std::vector<std::string> searchDirs = {"soundfonts", "../soundfonts", "../../soundfonts"};

    // 品質優先順位（名前に含まれるキーワードで判定）
    // 高品質: Tyroland > FluidR3 > GeneralUser > Arachno > Timbres > SGM > FatBoy
    auto scoreSF2 = [](const std::string& name) -> int {
        std::string lower = name;
        for (auto& c : lower) c = std::tolower(c);
        if (lower.find("tyroland") != std::string::npos) return 100;
        if (lower.find("fluidr3") != std::string::npos || lower.find("fluid") != std::string::npos) return 90;
        if (lower.find("generaluser") != std::string::npos || lower.find("general_user") != std::string::npos) return 80;
        if (lower.find("arachno") != std::string::npos) return 70;
        if (lower.find("timbre") != std::string::npos) return 60;
        if (lower.find("sgm") != std::string::npos) return 50;
        if (lower.find("fatboy") != std::string::npos) return 40;
        if (lower.find("gm") != std::string::npos || lower.find("general midi") != std::string::npos) return 30;
        return 10;
    };

    std::string bestPath;
    int bestScore = -1;

    for (auto& dir : searchDirs) {
        if (!fs::is_directory(dir)) continue;
        for (auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = std::tolower(c);
            if (ext != ".sf2" && ext != ".sf3") continue;

            int score = scoreSF2(entry.path().filename().string());
            if (score > bestScore) {
                bestScore = score;
                bestPath = entry.path().string();
            }
        }
    }

    return bestPath;
}

BatchLogger::BatchLogger(const std::string& outputDir) : m_outputDir(outputDir) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "batch_log_%Y%m%d_%H%M%S.csv", std::localtime(&t));
    m_logFile = (fs::path(outputDir) / buf).string();
}

void BatchLogger::addEntry(const BatchLogEntry& entry) { m_entries.push_back(entry); }

void BatchLogger::saveLog() {
    if (m_entries.empty()) return;
    std::ofstream f(m_logFile);
    f << "InputFile,OutputFile,Status,FailReason,Notes,Duration,RMS,Peak,ProcessingTimeMs\n";
    for (auto& e : m_entries) {
        auto esc = [](const std::string& s) { return s.find(',') != std::string::npos ? "\"" + s + "\"" : s; };
        f << esc(e.inputFile) << "," << esc(e.outputFile) << "," << e.status << "," << esc(e.failReason)
          << "," << e.notes << "," << std::fixed << std::setprecision(2) << e.duration
          << "," << std::setprecision(4) << e.rms << "," << e.peak
          << "," << e.processingTimeMs << "\n";
    }
    f.close();
    std::cout << "[INFO] Log saved: " << m_logFile << std::endl;
}

void BatchLogger::printSummary() {
    if (m_entries.empty()) return;
    int total = m_entries.size(), success = 0, failed = 0;
    for (auto& e : m_entries) {
        if (e.status == "success") success++;
        else failed++;
    }
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "  Total: " << total << ", Success: " << success << ", Failed: " << failed << std::endl;
    std::cout << "  Success rate: " << (total > 0 ? (double)success / total * 100 : 0) << "%" << std::endl;
    if (failed > 0) {
        std::cout << "\n  Failed files:" << std::endl;
        for (auto& e : m_entries) if (e.status == "fail") std::cout << "    " << e.inputFile << ": " << e.failReason << std::endl;
    }
}

std::string formatAnalysisText(const std::vector<TrackInfo>& tracks, const MidiFile& midi) {
    std::ostringstream ss;
    ss << "=== MIDI Analysis ===\nTracks: " << tracks.size() << "\n\n";
    for (auto& t : tracks) {
        ss << "[" << t.index << "] " << t.name << "\n"
           << "  Notes: " << t.noteCount << ", AvgPitch: " << std::fixed << std::setprecision(1) << t.avgPitch
           << ", NPS: " << std::setprecision(2) << t.notesPerSecond << ", Role: " << t.role
           << ", Drum: " << (t.isDrum ? "Yes" : "No") << "\n";
    }
    return ss.str();
}

int runConverter(const ConvertOptions& opts) {
    auto startTime = std::chrono::steady_clock::now();

    std::cout << "[INFO] Input: " << opts.inputPath << std::endl;
    std::cout << "[INFO] Output: " << opts.outputPath << std::endl;

    fs::create_directories(opts.outputPath);

    // SF2 auto-selection
    std::string sf2Path = opts.sf2Path;
    if (opts.sf2Auto) {
        std::string found = findBestSoundFont();
        if (!found.empty()) {
            sf2Path = found;
            std::cout << "[INFO] Auto-selected SoundFont: " << fs::path(found).filename().string() << std::endl;
        } else {
            std::cerr << "[WARN] No SoundFont files found in soundfonts/ directory" << std::endl;
        }
    } else {
        if (!fs::exists(sf2Path)) {
            std::string found = findBestSoundFont();
            if (!found.empty()) {
                sf2Path = found;
                std::cout << "[INFO] Default not found, auto-selected: " << fs::path(found).filename().string() << std::endl;
            }
        }
    }
    std::cout << "[INFO] SoundFont: " << sf2Path << std::endl;

    std::vector<std::string> midiFiles;
    if (fs::is_regular_file(opts.inputPath)) {
        midiFiles.push_back(opts.inputPath);
    } else if (fs::is_directory(opts.inputPath)) {
        for (auto& entry : fs::directory_iterator(opts.inputPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                for (auto& c : ext) c = std::tolower(c);
                if (ext == ".mid" || ext == ".midi") midiFiles.push_back(entry.path().string());
            }
        }
    }

    if (midiFiles.empty()) { std::cerr << "[ERROR] No MIDI files found" << std::endl; return 1; }
    std::cout << "[INFO] Found " << midiFiles.size() << " MIDI files" << std::endl;

    // SoundFont読み込み
    SoundFont sf2;
    bool sf2Loaded = false;
    if (!opts.analyzeOnly) {
        if (fs::exists(sf2Path)) {
            if (sf2.load(sf2Path)) {
                sf2Loaded = true;
                std::cout << "[INFO] SoundFont loaded successfully" << std::endl;
            } else {
                std::cerr << "[WARN] Failed to load SoundFont, using fallback mode" << std::endl;
            }
        } else {
            std::cerr << "[WARN] SoundFont not found: " << sf2Path << std::endl;
            std::cerr << "[INFO] Using fallback mode (sine waves)" << std::endl;
            std::cerr << "[INFO] For better quality, place a GM SoundFont in soundfonts/" << std::endl;
        }
    }

    BatchLogger* logger = opts.csvLog ? new BatchLogger(opts.outputPath) : nullptr;
    int success = 0, failed = 0;

    for (size_t idx = 0; idx < midiFiles.size(); idx++) {
        auto& midiPath = midiFiles[idx];
        std::string fileName = fs::path(midiPath).stem().string();
        std::cout << "\n[" << (idx + 1) << "/" << midiFiles.size() << "] " << fileName << std::endl;

        BatchLogEntry log;
        log.inputFile = midiPath;
        auto procStart = std::chrono::steady_clock::now();

        try {
            MidiFile midi;
            std::cout << "  [Debug] Loading MIDI: " << midiPath << std::endl;
            if (!midi.load(midiPath)) {
                std::cerr << "  [ERROR] Failed to load MIDI" << std::endl;
                log.status = "fail"; log.failReason = "Failed to load MIDI";
                if (logger) logger->addEntry(log); failed++;
                continue;
            }

            auto tracks = midi.analyzeTracks();
            std::cout << "  [Debug] Tracks analyzed, notes=" << midi.notes().size() << std::endl;
            log.notes = midi.notes().size();
            log.duration = midi.tickToSeconds(midi.notes().empty() ? 0 : midi.notes().back().endTime);

            // デバイス自動判定
            ConvertOptions runOpts = opts;
            if (runOpts.deviceAuto) {
                runOpts.device = detectDevice(midi);
            }

            std::cout << "  [Analysis] " << tracks.size() << " tracks, " << log.notes << " notes, "
                      << std::fixed << std::setprecision(1) << log.duration << " sec"
                      << "  Device: " << deviceName(runOpts.device) << std::endl;

            if (opts.analyzeOnly) {
                std::cout << formatAnalysisText(tracks, midi);
                log.status = "success";
                if (logger) logger->addEntry(log);
                success++;
                continue;
            }

            std::string outPath = (fs::path(opts.outputPath) / (fileName + ".wav")).string();

            SFSynthesizer synth;
            if (sf2Loaded) {
                synth.init(sf2, 44100);
            } else {
                synth.initFallback(44100);
            }

            if (runOpts.channelSplit) {
                synth.renderToWavPerChannel(midi.notes(), fileName, runOpts.outputPath, midi, runOpts.pitchShift);
            }
            synth.renderToWav(midi.notes(), outPath, runOpts, midi);

            auto procEnd = std::chrono::steady_clock::now();
            log.outputFile = outPath;
            log.status = "success";
            log.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(procEnd - procStart).count();

            std::cout << "  [Done] " << outPath << " (" << log.processingTimeMs << "ms)" << std::endl;
            if (logger) logger->addEntry(log);
            success++;

        } catch (const std::exception& e) {
            std::cerr << "  [ERROR] " << e.what() << std::endl;
            log.status = "fail"; log.failReason = e.what();
            if (logger) logger->addEntry(log);
            failed++;
        }
    }

    if (logger) {
        logger->saveLog();
        logger->printSummary();
        delete logger;
    }

    auto endTime = std::chrono::steady_clock::now();
    std::cout << "\n[INFO] Total time: " << std::chrono::duration<double>(endTime - startTime).count() << " sec" << std::endl;
    return 0;
}

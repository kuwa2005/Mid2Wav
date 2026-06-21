#include "converter.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

void printUsage() {
    std::cout << "Mid2Wav - MIDI to WAV converter (SoundFont)\n\n"
              << "Usage:\n"
              << "  Mid2Wav -i <input> -o <output> [options]\n\n"
              << "Options:\n"
              << "  -i, --input <path>        Input MIDI file or directory\n"
              << "  -o, --output <path>       Output WAV directory\n"
              << "  --sf2 <path|auto>         SoundFont file or 'auto' (default: auto)\n"
              << "  --device <model>          Device emulation (default: auto):\n"
              << "                              auto, GM, GS\n"
              << "                              Roland: SC-55, SC-88, SC-88VL, SC-8850\n"
              << "                              Yamaha: MU-50, MU-80, MU-100, MU-128, MOTIF\n"
              << "  --channels                Split output per MIDI channel\n"
              << "  --pitch <semitones>       Transpose all notes (+12=+1oct, -12=-1oct)\n"
              << "  --csv                     Output batch log CSV file\n"
              << "  --analyze                 Analyze only (no convert)\n"
              << "  --help, -h                Show this help\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    ConvertOptions opts;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };

        if (a == "-i" || a == "--input") opts.inputPath = next();
        else if (a == "-o" || a == "--output") opts.outputPath = next();
        else if (a == "--sf2") {
            std::string v = next();
            if (v.empty() || v == "auto") opts.sf2Auto = true;
            else opts.sf2Path = v;
        }
        else if (a == "--channels") opts.channelSplit = true;
        else if (a == "--csv") opts.csvLog = true;
        else if (a == "--pitch") opts.pitchShift = std::stoi(next());
        else if (a == "--device") {
            std::string v = next();
            opts.deviceAuto = false;
            if (v == "auto") { opts.deviceAuto = true; }
            else if (v == "gs") opts.device = DeviceModel::GS;
            else if (v == "sc55" || v == "SC-55") opts.device = DeviceModel::SC55;
            else if (v == "sc88" || v == "SC-88") opts.device = DeviceModel::SC88;
            else if (v == "sc88vl" || v == "SC-88VL") opts.device = DeviceModel::SC88VL;
            else if (v == "sc8850" || v == "SC-8850") opts.device = DeviceModel::SC8850;
            else if (v == "xg") opts.device = DeviceModel::XG;
            else if (v == "mu50" || v == "MU-50") opts.device = DeviceModel::MU50;
            else if (v == "mu80" || v == "MU-80") opts.device = DeviceModel::MU80;
            else if (v == "mu100" || v == "MU-100") opts.device = DeviceModel::MU100;
            else if (v == "mu128" || v == "MU-128") opts.device = DeviceModel::MU128;
            else if (v == "motif" || v == "MOTIF") opts.device = DeviceModel::MOTIF;
            else opts.deviceAuto = true;
        }
        else if (a == "--analyze" || a == "--analysis") opts.analyzeOnly = true;
        else if (a == "--help" || a == "-h") { printUsage(); return 0; }
    }

    // --sf2未指定時はauto
    if (!opts.sf2Auto && opts.sf2Path == "soundfonts/GM.sf2") {
        opts.sf2Auto = true;
    }

    if (opts.inputPath.empty() || opts.outputPath.empty()) { printUsage(); return 1; }
    return runConverter(opts);
}

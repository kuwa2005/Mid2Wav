#include "test_framework.h"
#include "test_fixtures.h"
#include "test_audio_analysis.h"
#include "sf_synth.h"
#include "soundfont.h"
#include "midi_file.h"
#include "converter.h"
#include "wav_writer.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace audio_analysis;

// ── Thresholds (documented in test/README.md) ─────────────────────
// Sustained piano with CC1=0: detrended ripple CV (warble on top of decay).
static constexpr float kMaxDetrendedCv = 0.18f;

// CC1=80 sustained: vibrato should alter waveform but block-stepping warble must stay bounded.
static constexpr float kMaxCC1HeavyDetrendedCv = 0.32f;
static constexpr float kMaxCC1HeavyCorrelation = 0.995f;

// renderToWav vs per-channel+mix: paths differ in FX but should correlate.
static constexpr float kPathMinCorrelation = 0.80f;
static constexpr float kPathRmsRatioMin = 0.45f;
static constexpr float kPathRmsRatioMax = 2.20f;

// Converter default must match direct renderToWav (not per-channel mix regression).
static constexpr float kConverterPeakRatioMin = 0.88f;
static constexpr float kConverterPeakRatioMax = 1.12f;
static constexpr float kConverterRmsRatioMin = 0.88f;
static constexpr float kConverterRmsRatioMax = 1.12f;
static constexpr float kConverterMinCorrelation = 0.95f;

// CC93=127 vs main path: x127 chorus bug would inflate per-channel RMS >> 2x.
static constexpr float kMaxChorusPathRmsRatio = 1.85f;

// Per-channel mix undulation should not greatly exceed direct path.
static constexpr float kMaxMixModDepthRatio = 2.5f;

static SoundFont* loadTestSf2(TestContext& ctx) {
    static SoundFont sf2;
    static bool loaded = false;
    if (!loaded) {
        std::string path = findSoundFont();
        if (path.empty()) return nullptr;
        if (!sf2.load(path)) return nullptr;
        loaded = true;
    }
    return &sf2;
}

static std::string qualityOutDir(const char* tag) {
    return std::string("/tmp/mid2wav_quality_") + tag;
}

static bool loadMidiFromData(const std::vector<uint8_t>& data, const std::string& path, MidiFile& midi) {
    if (!midi_fixtures::writeTempFile(path, data)) return false;
    return midi.load(path);
}

static bool readWavStereo(const std::string& path,
                          std::vector<float>& left, std::vector<float>& right, int& sr) {
    return WavReader::read(path, left, right, sr);
}

static bool renderDirect(SFSynthesizer& synth, MidiFile& midi, const std::string& wavPath) {
    fs::path p(wavPath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    ConvertOptions opts;
    opts.noNormalize = true;
    synth.renderToWav(midi.notes(), wavPath, opts, midi);
    return fs::exists(wavPath);
}

static bool renderPerChannelMix(SFSynthesizer& synth, MidiFile& midi,
                              const std::string& dir, const std::string& baseName,
                              const std::string& outPath) {
    fs::create_directories(dir);
    synth.renderToWavPerChannel(midi.notes(), baseName, dir, midi, 0, true, {});
    return SFSynthesizer::mixFromChannelWavs(dir, baseName, outPath, synth.sampleRate(), true);
}

static float sustainDetrendedCv(const std::vector<float>& left,
                                const std::vector<float>& right, int sr) {
    auto mono = toMono(left, right);
    size_t skip = (size_t)(sr * 0.10);
    size_t analyzeLen = (size_t)(sr * 1.2);
    if (skip + analyzeLen > mono.size())
        analyzeLen = mono.size() > skip ? mono.size() - skip : 0;
    return detrendedModulationCv(mono, sr, 40, skip, analyzeLen);
}

static void testQualitySustainedLowUndulation(TestContext& ctx) {
    TEST(ctx, "sustained note low amplitude modulation (CC1=0)");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    auto* sf2 = loadTestSf2(ctx);

    std::string midPath = "/tmp/mid2wav_quality_sustain.mid";
    std::string wavPath = "/tmp/mid2wav_quality_sustain.wav";
    auto data = midi_fixtures::makeSustainedNoteMidi();

    MidiFile midi;
    ASSERT_TRUE(ctx, loadMidiFromData(data, midPath, midi), "midi load failed");

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*sf2, 48000), "synth init failed");
    ASSERT_TRUE(ctx, renderDirect(synth, midi, wavPath), "render failed");

    std::vector<float> left, right;
    int sr = 0;
    ASSERT_TRUE(ctx, readWavStereo(wavPath, left, right, sr), "wav read failed");

    auto cv = sustainDetrendedCv(left, right, sr);
    ASSERT_GT(ctx, cv, 0.0f, "analysis region silent");

    if (cv > kMaxDetrendedCv) {
        FAIL(ctx, std::string("excessive envelope ripple (detrended CV=") +
            std::to_string(cv) + ", max=" + std::to_string(kMaxDetrendedCv) +
            ") — possible undulation/warble");
        return;
    }

    fs::remove(midPath);
    fs::remove(wavPath);
    OK(ctx);
}

static void testQualityRenderPathSimilarity(TestContext& ctx) {
    TEST(ctx, "renderToWav vs per-channel+mix similarity");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    auto* sf2 = loadTestSf2(ctx);

    std::string midPath = "/tmp/mid2wav_quality_paths.mid";
    auto data = midi_fixtures::makeSustainedNoteMidi();
    MidiFile midi;
    ASSERT_TRUE(ctx, loadMidiFromData(data, midPath, midi), "midi load failed");

    std::string dir = qualityOutDir("paths");
    std::string directPath = dir + "/direct.wav";
    std::string mixPath = dir + "/mixed.wav";
    fs::remove_all(dir);

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*sf2, 48000), "synth init failed");
    ASSERT_TRUE(ctx, renderDirect(synth, midi, directPath), "direct render failed");
    ASSERT_TRUE(ctx, renderPerChannelMix(synth, midi, dir, "paths", mixPath), "per-channel mix failed");

    std::vector<float> dL, dR, mL, mR;
    int sr = 0;
    ASSERT_TRUE(ctx, readWavStereo(directPath, dL, dR, sr), "read direct failed");
    ASSERT_TRUE(ctx, readWavStereo(mixPath, mL, mR, sr), "read mix failed");

    auto sim = compareWavSimilarity(dL, dR, mL, mR);
    if (!withinTolerance(sim, kPathRmsRatioMin, kPathRmsRatioMax,
                         kPathRmsRatioMin, kPathRmsRatioMax, kPathMinCorrelation)) {
        FAIL(ctx, std::string("paths diverge (rmsRatio=") + std::to_string(sim.rmsRatio) +
            " corr=" + std::to_string(sim.correlation) + ")");
        return;
    }

    auto modDirect = detectAmplitudeModulation(toMono(dL, dR), sr, 50,
        (size_t)(sr * 0.25), (size_t)(sr * 1.0));
    auto modMix = detectAmplitudeModulation(toMono(mL, mR), sr, 50,
        (size_t)(sr * 0.25), (size_t)(sr * 1.0));
    if (modDirect.meanRms > 1e-6f && modMix.modulationDepth > modDirect.modulationDepth * kMaxMixModDepthRatio) {
        FAIL(ctx, std::string("per-channel mix has much higher undulation (mixDepth=") +
            std::to_string(modMix.modulationDepth) + " directDepth=" +
            std::to_string(modDirect.modulationDepth) + ")");
        return;
    }

    fs::remove_all(dir);
    fs::remove(midPath);
    OK(ctx);
}

static void testQualityConverterUsesDirectRender(TestContext& ctx) {
    TEST(ctx, "converter default matches renderToWav (not per-channel mix)");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    std::string sf2Path = findSoundFont();

    std::string midPath = "/tmp/mid2wav_quality_converter.mid";
    auto data = midi_fixtures::makeSustainedNoteMidi();
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write midi failed");

    std::string convDir = qualityOutDir("conv");
    std::string synthDir = qualityOutDir("synth");
    std::string mixDir = qualityOutDir("mix");
    fs::remove_all(convDir);
    fs::remove_all(synthDir);
    fs::remove_all(mixDir);
    fs::create_directories(convDir);
    fs::create_directories(synthDir);

    ConvertOptions opts;
    opts.inputPath = midPath;
    opts.outputPath = convDir;
    opts.sf2Path = sf2Path;
    opts.noNormalize = true;
    ASSERT_EQ(ctx, runConverter(opts), 0, "runConverter failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(midPath), "midi load failed");
    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*loadTestSf2(ctx), 48000), "synth init failed");

    std::string synthPath = synthDir + "/mid2wav_quality_converter.wav";
    ASSERT_TRUE(ctx, renderDirect(synth, midi, synthPath), "direct render failed");
    std::string mixPath = mixDir + "/mid2wav_quality_converter_mix.wav";
    ASSERT_TRUE(ctx, renderPerChannelMix(synth, midi, mixDir, "mid2wav_quality_converter", mixPath),
                "per-channel mix failed");

    std::vector<float> cL, cR, dL, dR, mL, mR;
    int sr = 0;
    readWavStereo(convDir + "/mid2wav_quality_converter.wav", cL, cR, sr);
    readWavStereo(synthPath, dL, dR, sr);
    readWavStereo(mixPath, mL, mR, sr);

    auto simConvDirect = compareWavSimilarity(cL, cR, dL, dR);
    auto simConvMix = compareWavSimilarity(cL, cR, mL, mR);

    bool matchesDirect = withinTolerance(simConvDirect,
        kConverterPeakRatioMin, kConverterPeakRatioMax,
        kConverterRmsRatioMin, kConverterRmsRatioMax,
        kConverterMinCorrelation);

    if (!matchesDirect) {
        FAIL(ctx, std::string("converter output differs from renderToWav (rmsRatio=") +
            std::to_string(simConvDirect.rmsRatio) + " corr=" +
            std::to_string(simConvDirect.correlation) +
            ") — possible per-channel mix regression");
        return;
    }

    // Converter should be closer to direct than to per-channel mix.
    if (simConvMix.correlation > simConvDirect.correlation &&
        std::abs(simConvMix.rmsRatio - 1.0f) < std::abs(simConvDirect.rmsRatio - 1.0f)) {
        FAIL(ctx, "converter output closer to per-channel mix than renderToWav");
        return;
    }

    fs::remove_all(convDir);
    fs::remove_all(synthDir);
    fs::remove_all(mixDir);
    fs::remove(midPath);
    OK(ctx);
}

static void testQualityChorusNotScaled127(TestContext& ctx) {
    TEST(ctx, "per-channel chorus not at x127 scale (CC93=127)");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    auto* sf2 = loadTestSf2(ctx);

    std::string midPath = "/tmp/mid2wav_quality_chorus.mid";
    auto data = midi_fixtures::makeChorusMidi(127);
    MidiFile midi;
    ASSERT_TRUE(ctx, loadMidiFromData(data, midPath, midi), "midi load failed");

    std::string dir = qualityOutDir("chorus");
    std::string directPath = dir + "/direct.wav";
    std::string mixPath = dir + "/mixed.wav";
    fs::remove_all(dir);

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*sf2, 48000), "synth init failed");
    ASSERT_TRUE(ctx, renderDirect(synth, midi, directPath), "direct render failed");
    ASSERT_TRUE(ctx, renderPerChannelMix(synth, midi, dir, "chorus", mixPath), "per-channel mix failed");

    std::vector<float> dL, dR, mL, mR;
    int sr = 0;
    ASSERT_TRUE(ctx, readWavStereo(directPath, dL, dR, sr), "read direct failed");
    ASSERT_TRUE(ctx, readWavStereo(mixPath, mL, mR, sr), "read mix failed");

    float rmsDirect = rmsStereo(dL, dR);
    float rmsMix = rmsStereo(mL, mR);
    float ratio = (rmsDirect > 1e-8f) ? rmsMix / rmsDirect : 0.0f;

    if (ratio > kMaxChorusPathRmsRatio) {
        FAIL(ctx, std::string("per-channel path RMS too high vs direct (ratio=") +
            std::to_string(ratio) + ", max=" + std::to_string(kMaxChorusPathRmsRatio) +
            ") — possible chorus x127 regression");
        return;
    }

    fs::remove_all(dir);
    fs::remove(midPath);
    OK(ctx);
}

static void testQualityDrumAndMelodyInMix(TestContext& ctx) {
    TEST(ctx, "full mix contains drum and melody energy");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    auto* sf2 = loadTestSf2(ctx);

    std::string midPath = "/tmp/mid2wav_quality_drummelody.mid";
    auto fullData = midi_fixtures::makeDrumAndMelodyMidi();
    MidiFile fullMidi;
    ASSERT_TRUE(ctx, loadMidiFromData(fullData, midPath, fullMidi), "full midi load failed");

    std::string dir = qualityOutDir("drummelody");
    std::string fullPath = dir + "/full.wav";
    fs::remove_all(dir);
    fs::create_directories(dir);

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*sf2, 48000), "synth init failed");
    ASSERT_TRUE(ctx, renderDirect(synth, fullMidi, fullPath), "full render failed");

    std::vector<float> fL, fR;
    int sr = 0;
    ASSERT_TRUE(ctx, readWavStereo(fullPath, fL, fR, sr), "read full failed");
    float fullRms = rmsStereo(fL, fR);
    ASSERT_GT(ctx, fullRms, 0.001f, "full mix silent");

    // Melody-only
    auto melodyData = midi_fixtures::makeSimpleNoteMidi(0, 60, 100);
    std::string melPath = dir + "/melody.mid";
    std::string melWav = dir + "/melody.wav";
    MidiFile melMidi;
    ASSERT_TRUE(ctx, loadMidiFromData(melodyData, melPath, melMidi), "melody midi failed");
    SFSynthesizer synth2;
    ASSERT_TRUE(ctx, synth2.init(*sf2, 48000), "synth2 init failed");
    ASSERT_TRUE(ctx, renderDirect(synth2, melMidi, melWav), "melody render failed");

    std::vector<float> mL, mR;
    ASSERT_TRUE(ctx, readWavStereo(melWav, mL, mR, sr), "read melody failed");
    float melRms = rmsStereo(mL, mR);

    // Drum-only (Ch10)
    auto drumData = midi_fixtures::makeCh10DrumMidi();
    std::string drumPath = dir + "/drum.mid";
    std::string drumWav = dir + "/drum.wav";
    MidiFile drumMidi;
    ASSERT_TRUE(ctx, loadMidiFromData(drumData, drumPath, drumMidi), "drum midi failed");
    SFSynthesizer synth3;
    ASSERT_TRUE(ctx, synth3.init(*sf2, 48000), "synth3 init failed");
    ASSERT_TRUE(ctx, renderDirect(synth3, drumMidi, drumWav), "drum render failed");

    std::vector<float> dL, dR;
    ASSERT_TRUE(ctx, readWavStereo(drumWav, dL, dR, sr), "read drum failed");
    float drumRms = rmsStereo(dL, dR);

    ASSERT_GT(ctx, melRms, 0.0005f, "melody-only silent");
    ASSERT_GT(ctx, drumRms, 0.0005f, "drum-only silent");

    float combinedEstimate = std::sqrt(melRms * melRms + drumRms * drumRms);
    ASSERT_GT(ctx, fullRms, combinedEstimate * 0.35f,
              "full mix RMS too low vs melody+drum components — missing channel energy");

    fs::remove_all(dir);
    fs::remove(midPath);
    OK(ctx);
}

static void testQualityNoDoubleFxEnergy(TestContext& ctx) {
    TEST(ctx, "chorus CC93=64 does not double energy vs dry");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    auto* sf2 = loadTestSf2(ctx);

    auto renderChorus = [&](uint8_t cc, std::vector<float>& left, std::vector<float>& right) {
        auto data = midi_fixtures::makeChorusMidi(cc);
        std::string midPath = qualityOutDir("fx") + "_cc" + std::to_string(cc) + ".mid";
        std::string wavPath = qualityOutDir("fx") + "_cc" + std::to_string(cc) + ".wav";
        MidiFile midi;
        if (!loadMidiFromData(data, midPath, midi)) return false;
        SFSynthesizer synth;
        if (!synth.init(*sf2, 48000)) return false;
        if (!renderDirect(synth, midi, wavPath)) return false;
        int sr = 0;
        bool ok = readWavStereo(wavPath, left, right, sr);
        fs::remove(midPath);
        fs::remove(wavPath);
        return ok;
    };

    std::vector<float> dryL, dryR, wetL, wetR;
    ASSERT_TRUE(ctx, renderChorus(0, dryL, dryR), "dry render failed");
    ASSERT_TRUE(ctx, renderChorus(64, wetL, wetR), "wet render failed");

    float dryRms = rmsStereo(dryL, dryR);
    float wetRms = rmsStereo(wetL, wetR);
    float ratio = (dryRms > 1e-8f) ? wetRms / dryRms : 0.0f;

    // x127-scale double FX would push ratio well above 1.5; normal chorus is subtle.
    ASSERT_TRUE(ctx, ratio < 1.45f,
                std::string("chorus inflated energy (ratio=") + std::to_string(ratio) +
                ") — possible double FX application");

    OK(ctx);
}

static void testQualityCC1HeavyBounded(TestContext& ctx) {
    TEST(ctx, "CC1-heavy sustain vibrato without block-step warble");

    if (!loadTestSf2(ctx)) { SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)"); return; }
    auto* sf2 = loadTestSf2(ctx);

    auto renderSustain = [&](uint8_t cc1, const std::string& wavPath) -> bool {
        auto data = midi_fixtures::makeSustainedNoteMidi(0, 60, 100, 480 * 4, cc1);
        std::string midPath = wavPath + ".mid";
        MidiFile midi;
        if (!loadMidiFromData(data, midPath, midi)) return false;
        SFSynthesizer synth;
        if (!synth.init(*sf2, 48000)) return false;
        bool ok = renderDirect(synth, midi, wavPath);
        fs::remove(midPath);
        return ok;
    };

    std::string dryPath = "/tmp/mid2wav_quality_cc1_dry.wav";
    std::string wetPath = "/tmp/mid2wav_quality_cc1_heavy.wav";
    ASSERT_TRUE(ctx, renderSustain(0, dryPath), "CC1=0 render failed");
    ASSERT_TRUE(ctx, renderSustain(80, wetPath), "CC1=80 render failed");

    std::vector<float> dryL, dryR, wetL, wetR;
    int sr = 0;
    ASSERT_TRUE(ctx, readWavStereo(dryPath, dryL, dryR, sr), "read CC1=0 failed");
    ASSERT_TRUE(ctx, readWavStereo(wetPath, wetL, wetR, sr), "read CC1=80 failed");

    float wetCv = sustainDetrendedCv(wetL, wetR, sr);
    if (wetCv > kMaxCC1HeavyDetrendedCv) {
        FAIL(ctx, std::string("CC1=80 excessive warble (detrended CV=") +
            std::to_string(wetCv) + ", max=" + std::to_string(kMaxCC1HeavyDetrendedCv) +
            ") — possible block-level vibrato LFO");
        return;
    }

    auto sim = compareWavSimilarity(dryL, dryR, wetL, wetR);
    if (sim.correlation > kMaxCC1HeavyCorrelation) {
        FAIL(ctx, std::string("CC1=80 render too similar to CC1=0 (corr=") +
            std::to_string(sim.correlation) + ") — vibrato not applied");
        return;
    }

    fs::remove(dryPath);
    fs::remove(wetPath);
    OK(ctx);
}

static void runQualitySuite(TestContext& ctx) {
    testQualitySustainedLowUndulation(ctx);
    testQualityCC1HeavyBounded(ctx);
    testQualityRenderPathSimilarity(ctx);
    testQualityConverterUsesDirectRender(ctx);
    testQualityChorusNotScaled127(ctx);
    testQualityDrumAndMelodyInMix(ctx);
    testQualityNoDoubleFxEnergy(ctx);
}

void registerQualityTests() {
    registerSuite({"quality", runQualitySuite});
}

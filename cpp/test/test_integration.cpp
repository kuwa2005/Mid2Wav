#include "test_framework.h"
#include "test_fixtures.h"
#include "converter.h"
#include "wav_writer.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static constexpr int kExpectedSampleRate = 48000;
static constexpr float kMinPeak = 0.001f;
static constexpr float kMinRms = 0.0001f;
static constexpr float kGoldenPeakTol = 0.05f;
static constexpr float kGoldenRmsTol = 0.02f;

static std::string fixturePath(const char* name) {
    const std::vector<std::string> candidates = {
        std::string("test/fixtures/") + name,
        std::string("../test/fixtures/") + name,
        std::string("fixtures/") + name,
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) return p;
    }
    return candidates[0];
}

static std::string integrationOutDir(const char* tag) {
    return std::string("/tmp/mid2wav_integration_") + tag;
}

#define REQUIRE_SF2(ctx, var) \
    std::string var = findSoundFont(); \
    if (var.empty()) SKIP(ctx, "TyrolandGSV30fix.sf2 not found (place under soundfonts/)")

static int runRender(TestContext& ctx, const std::string& midPath,
                     const std::string& outDir, const ConvertOptions& baseOpts) {
    fs::create_directories(outDir);
    ConvertOptions opts = baseOpts;
    opts.inputPath = midPath;
    opts.outputPath = outDir;
    return runConverter(opts);
}

static void readWavOrFail(TestContext& ctx, const std::string& path,
                          std::vector<float>& left, std::vector<float>& right, int& sr) {
    ASSERT_TRUE(ctx, fs::exists(path), "output wav missing");
    ASSERT_TRUE(ctx, WavReader::read(path, left, right, sr), "wav read failed");
}

static void testIntegrationFullPipeline(TestContext& ctx) {
    TEST(ctx, "full pipeline MIDI to WAV");

    REQUIRE_SF2(ctx, sf2);
    std::string midPath = fixturePath("simple_note.mid");
    if (!fs::exists(midPath)) {
        auto data = midi_fixtures::makeSimpleNoteMidi(0, 60, 100);
        ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write fixture midi failed");
    }

    std::string outDir = integrationOutDir("pipeline");
    fs::remove_all(outDir);

    ConvertOptions opts;
    opts.sf2Path = sf2;
    int rc = runRender(ctx, midPath, outDir, opts);
    ASSERT_EQ(ctx, rc, 0, "runConverter failed");

    std::string wavPath = outDir + "/simple_note.wav";
    std::vector<float> left, right;
    int sr = 0;
    readWavOrFail(ctx, wavPath, left, right, sr);

    ASSERT_EQ(ctx, sr, kExpectedSampleRate, "sample rate");
    ASSERT_FALSE(ctx, left.empty(), "empty left channel");
    ASSERT_GT(ctx, peakStereo(left, right), kMinPeak, "silent output");
    ASSERT_GT(ctx, rmsStereo(left, right), kMinRms, "rms too low");

    fs::remove_all(outDir);
    OK(ctx);
}

static void testIntegrationOutputLevels(TestContext& ctx) {
    TEST(ctx, "output WAV sample rate and levels");

    REQUIRE_SF2(ctx, sf2);
    std::string midPath = fixturePath("simple_note.mid");
    std::string outDir = integrationOutDir("levels");
    fs::remove_all(outDir);

    ConvertOptions opts;
    opts.sf2Path = sf2;
    ASSERT_EQ(ctx, runRender(ctx, midPath, outDir, opts), 0, "render failed");

    std::vector<float> left, right;
    int sr = 0;
    readWavOrFail(ctx, outDir + "/simple_note.wav", left, right, sr);

    ASSERT_EQ(ctx, sr, kExpectedSampleRate, "sample rate mismatch");
    float peak = peakStereo(left, right);
    float rms = rmsStereo(left, right);
    ASSERT_GT(ctx, peak, kMinPeak, "peak too low");
    ASSERT_GT(ctx, rms, kMinRms, "rms too low");
    ASSERT_TRUE(ctx, peak <= 1.05f, "peak exceeds normalized range");

    fs::remove_all(outDir);
    OK(ctx);
}

static void testIntegrationChannelSplit(TestContext& ctx) {
    TEST(ctx, "channelSplit creates per-channel WAVs");

    REQUIRE_SF2(ctx, sf2);
    std::string midPath = fixturePath("simple_note.mid");
    std::string outDir = integrationOutDir("channels");
    fs::remove_all(outDir);

    ConvertOptions opts;
    opts.sf2Path = sf2;
    opts.channelSplit = true;
    ASSERT_EQ(ctx, runRender(ctx, midPath, outDir, opts), 0, "render failed");

    ASSERT_TRUE(ctx, fs::exists(outDir + "/simple_note.wav"), "mix wav missing");

    bool foundChannelWav = false;
    for (const auto& entry : fs::directory_iterator(outDir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.find("simple_note_") == 0 && entry.path().extension() == ".wav") {
            foundChannelWav = true;
            break;
        }
    }
    ASSERT_TRUE(ctx, foundChannelWav, "no per-channel wav found");

    fs::remove_all(outDir);
    OK(ctx);
}

static void testIntegrationCh10Drum(TestContext& ctx) {
    TEST(ctx, "channel 10 drum not silent");

    REQUIRE_SF2(ctx, sf2);
    std::string midPath = fixturePath("ch10_drum.mid");
    if (!fs::exists(midPath)) {
        auto data = midi_fixtures::makeCh10DrumMidi();
        ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write drum fixture failed");
    }

    std::string outDir = integrationOutDir("ch10");
    fs::remove_all(outDir);

    ConvertOptions opts;
    opts.sf2Path = sf2;
    ASSERT_EQ(ctx, runRender(ctx, midPath, outDir, opts), 0, "render failed");

    std::vector<float> left, right;
    int sr = 0;
    readWavOrFail(ctx, outDir + "/ch10_drum.wav", left, right, sr);
    ASSERT_GT(ctx, peakStereo(left, right), kMinPeak, "ch10 drum output silent");

    fs::remove_all(outDir);
    OK(ctx);
}

static void testIntegrationSelfConsistency(TestContext& ctx) {
    TEST(ctx, "render self-consistency (two passes)");

    REQUIRE_SF2(ctx, sf2);
    std::string midPath = fixturePath("simple_note.mid");
    std::string outA = integrationOutDir("consist_a");
    std::string outB = integrationOutDir("consist_b");
    fs::remove_all(outA);
    fs::remove_all(outB);

    ConvertOptions opts;
    opts.sf2Path = sf2;
    ASSERT_EQ(ctx, runRender(ctx, midPath, outA, opts), 0, "first render failed");
    ASSERT_EQ(ctx, runRender(ctx, midPath, outB, opts), 0, "second render failed");

    std::vector<float> l1, r1, l2, r2;
    int sr1 = 0, sr2 = 0;
    readWavOrFail(ctx, outA + "/simple_note.wav", l1, r1, sr1);
    readWavOrFail(ctx, outB + "/simple_note.wav", l2, r2, sr2);
    ASSERT_EQ(ctx, sr1, sr2, "sample rate mismatch between runs");
    ASSERT_TRUE(ctx, compareWavSimilar(l1, r1, l2, r2, kGoldenPeakTol, kGoldenRmsTol),
                "two renders differ beyond tolerance");

    fs::remove_all(outA);
    fs::remove_all(outB);
    OK(ctx);
}

static void testIntegrationGoldenReference(TestContext& ctx) {
    TEST(ctx, "golden WAV comparison (RMS/peak tolerance)");

    REQUIRE_SF2(ctx, sf2);
    std::string midPath = fixturePath("simple_note.mid");
    std::string goldenPath = fixturePath("expected/simple_note.wav");
    std::string outDir = integrationOutDir("golden");
    fs::remove_all(outDir);

    ConvertOptions opts;
    opts.sf2Path = sf2;
    ASSERT_EQ(ctx, runRender(ctx, midPath, outDir, opts), 0, "render failed");

    std::vector<float> actualL, actualR;
    int actualSr = 0;
    readWavOrFail(ctx, outDir + "/simple_note.wav", actualL, actualR, actualSr);

    if (!fs::exists(goldenPath)) {
        std::cout << " (no golden at " << goldenPath << ", self-check only) ";
        ASSERT_GT(ctx, peakStereo(actualL, actualR), kMinPeak, "rendered wav silent");
        fs::remove_all(outDir);
        OK(ctx);
        return;
    }

    std::vector<float> goldenL, goldenR;
    int goldenSr = 0;
    ASSERT_TRUE(ctx, WavReader::read(goldenPath, goldenL, goldenR, goldenSr), "golden read failed");
    ASSERT_EQ(ctx, actualSr, goldenSr, "golden sample rate mismatch");
    ASSERT_TRUE(ctx, compareWavSimilar(actualL, actualR, goldenL, goldenR,
                                      kGoldenPeakTol, kGoldenRmsTol),
                "golden comparison failed (peak/rms tolerance)");

    fs::remove_all(outDir);
    OK(ctx);
}

static void testIntegrationAnalyzeOnlyNoWav(TestContext& ctx) {
    TEST(ctx, "analyzeOnly produces no WAV");

    std::string midPath = fixturePath("simple_note.mid");
    std::string outDir = integrationOutDir("analyze");
    fs::remove_all(outDir);

    ConvertOptions opts;
    opts.analyzeOnly = true;
    opts.sf2Path = findSoundFont();
    ASSERT_EQ(ctx, runRender(ctx, midPath, outDir, opts), 0, "analyze failed");

    bool hasWav = false;
    if (fs::exists(outDir)) {
        for (const auto& entry : fs::directory_iterator(outDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                hasWav = true;
                break;
            }
        }
    }
    ASSERT_FALSE(ctx, hasWav, "analyzeOnly should not create wav");

    fs::remove_all(outDir);
    OK(ctx);
}

static void runIntegrationSuite(TestContext& ctx) {
    testIntegrationAnalyzeOnlyNoWav(ctx);
    testIntegrationFullPipeline(ctx);
    testIntegrationOutputLevels(ctx);
    testIntegrationChannelSplit(ctx);
    testIntegrationCh10Drum(ctx);
    testIntegrationSelfConsistency(ctx);
    testIntegrationGoldenReference(ctx);
}

void registerIntegrationTests() {
    registerSuite({"integration", runIntegrationSuite});
}

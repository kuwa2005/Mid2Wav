#include "test_framework.h"
#include "test_fixtures.h"
#include "converter.h"
#include "midi_file.h"
#include <filesystem>

namespace fs = std::filesystem;

static void testFormatAnalysisText(TestContext& ctx) {
    TEST(ctx, "formatAnalysisText output");

    auto data = midi_fixtures::makeSimpleNoteMidi(0, 60, 90);
    std::string path = "/tmp/mid2wav_test_analysis.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");

    auto tracks = midi.analyzeTracks();
    std::string text = formatAnalysisText(tracks, midi);

    ASSERT_TRUE(ctx, text.find("MIDI Analysis") != std::string::npos, "header missing");
    ASSERT_TRUE(ctx, text.find("Notes:") != std::string::npos, "notes field missing");

    fs::remove(path);
    OK(ctx);
}

static void testConverterAnalyzeOnly(TestContext& ctx) {
    TEST(ctx, "runConverter analyzeOnly");

    auto data = midi_fixtures::makeSimpleNoteMidi(0, 64, 100);
    std::string midPath = "/tmp/mid2wav_test_convert.mid";
    std::string outDir = "/tmp/mid2wav_test_convert_out";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write midi failed");

    fs::create_directories(outDir);

    ConvertOptions opts;
    opts.inputPath = midPath;
    opts.outputPath = outDir;
    opts.analyzeOnly = true;
    opts.sf2Path = findSoundFont();

    int rc = runConverter(opts);
    ASSERT_EQ(ctx, rc, 0, "analyzeOnly should succeed");

    fs::remove(midPath);
    fs::remove_all(outDir);
    OK(ctx);
}

static void testConverterRender(TestContext& ctx) {
    TEST(ctx, "runConverter full render");

    std::string sf2 = findSoundFont();
    if (sf2.empty()) { FAIL(ctx, "SF2 not found"); return; }

    auto data = midi_fixtures::makeSimpleNoteMidi(0, 60, 100);
    std::string midPath = "/tmp/mid2wav_test_render.mid";
    std::string outDir = "/tmp/mid2wav_test_render_out";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write midi failed");

    fs::create_directories(outDir);

    ConvertOptions opts;
    opts.inputPath = midPath;
    opts.outputPath = outDir;
    opts.sf2Path = sf2;
    opts.analyzeOnly = false;

    int rc = runConverter(opts);
    std::string expectedWav = outDir + "/mid2wav_test_render.wav";
    ASSERT_EQ(ctx, rc, 0, "render should succeed");
    ASSERT_TRUE(ctx, fs::exists(expectedWav), "output wav missing");

    fs::remove(midPath);
    fs::remove(expectedWav);
    fs::remove_all(outDir);
    OK(ctx);
}

static void runConverterSuite(TestContext& ctx) {
    testFormatAnalysisText(ctx);
    testConverterAnalyzeOnly(ctx);
    testConverterRender(ctx);
}

void registerConverterTests() {
    registerSuite({"converter", runConverterSuite});
}

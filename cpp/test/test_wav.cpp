#include "test_framework.h"
#include "wav_writer.h"
#include "sf_synth.h"
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

static void testWavRoundTrip(TestContext& ctx) {
    TEST(ctx, "WavReader/WavWriter round-trip");

    int sr = 48000;
    int n = 1000;
    std::vector<float> left(n), right(n);
    for (int i = 0; i < n; i++) {
        left[i] = (float)std::sin(2.0 * M_PI * 440.0 * i / sr);
        right[i] = (float)std::sin(2.0 * M_PI * 880.0 * i / sr);
    }

    std::string path = "/tmp/mid2wav_test_roundtrip.wav";
    ASSERT_TRUE(ctx, WavWriter::write(path, left, right, sr), "write failed");

    std::vector<float> readL, readR;
    int readSr = 0;
    ASSERT_TRUE(ctx, WavReader::read(path, readL, readR, readSr), "read failed");
    ASSERT_EQ(ctx, readSr, sr, "sample rate mismatch");
    ASSERT_EQ(ctx, (int)readL.size(), n, "sample count mismatch");

    float maxErr = 0;
    for (int i = 0; i < n; i++) {
        maxErr = std::max(maxErr, std::abs(left[i] - readL[i]));
        maxErr = std::max(maxErr, std::abs(right[i] - readR[i]));
    }
    ASSERT_NEAR(ctx, maxErr, 0.0f, 0.001f, "samples differ");

    fs::remove(path);
    OK(ctx);
}

static void testWavEmptyRejected(TestContext& ctx) {
    TEST(ctx, "WavWriter rejects empty buffers");

    std::vector<float> empty;
    std::string path = "/tmp/mid2wav_test_empty.wav";
    ASSERT_FALSE(ctx, WavWriter::write(path, empty, empty, 48000), "empty write should fail");

    OK(ctx);
}

static void testMixFromChannelWavs(TestContext& ctx) {
    TEST(ctx, "mixFromChannelWavs");

    int sr = 48000;
    int n = 1000;
    std::string dir = "/tmp/mid2wav_test_mix";
    fs::create_directories(dir);

    std::string base = "test";
    std::vector<float> ch1L(n, 0.5f), ch1R(n, 0.5f);
    std::vector<float> ch2L(n, 0.3f), ch2R(n, 0.3f);

    WavWriter::write(dir + "/test_01_A.wav", ch1L, ch1R, sr);
    WavWriter::write(dir + "/test_02_B.wav", ch2L, ch2R, sr);

    std::string outPath = "/tmp/mid2wav_test_mix_out.wav";
    ASSERT_TRUE(ctx, SFSynthesizer::mixFromChannelWavs(dir, base, outPath, sr, true),
                "mixFromChannelWavs returned false");

    std::vector<float> mixL, mixR;
    int readSr = 0;
    ASSERT_TRUE(ctx, WavReader::read(outPath, mixL, mixR, readSr), "read mix failed");

    float expected = 0.8f;
    float maxErr = 0;
    for (int i = 0; i < n; i++) {
        maxErr = std::max(maxErr, std::abs(mixL[i] - expected));
        maxErr = std::max(maxErr, std::abs(mixR[i] - expected));
    }

    fs::remove_all(dir);
    fs::remove(outPath);
    ASSERT_NEAR(ctx, maxErr, 0.0f, 0.01f, "mixed value wrong");
    OK(ctx);
}

static void runWavSuite(TestContext& ctx) {
    testWavRoundTrip(ctx);
    testWavEmptyRejected(ctx);
    testMixFromChannelWavs(ctx);
}

void registerWavTests() {
    registerSuite({"wav", runWavSuite});
}

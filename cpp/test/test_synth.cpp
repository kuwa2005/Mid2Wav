#include "test_framework.h"
#include "test_fixtures.h"
#include "sf_synth.h"
#include "soundfont.h"
#include "midi_file.h"
#include "converter.h"
#include "wav_writer.h"
#include <filesystem>

namespace fs = std::filesystem;

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

static void testSynthDrumRendering(TestContext& ctx) {
    TEST(ctx, "Drum rendering (bank=128)");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(*sf2, 48000)) { FAIL(ctx, "synth init failed"); return; }

    synth.controlChange(0, 0, 0);
    synth.programChange(0, 8, 128);
    synth.noteOn(0, 35, 127);

    std::vector<float> out = synth.render(0.5);
    ASSERT_GT(ctx, peakOf(out), 0.001f, "silent drum output");

    OK(ctx);
}

static void testSynthMelodyRendering(TestContext& ctx) {
    TEST(ctx, "Melody rendering (bank=0)");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(*sf2, 48000)) { FAIL(ctx, "synth init failed"); return; }

    synth.programChange(0, 0, 0);
    synth.noteOn(0, 60, 100);

    std::vector<float> out = synth.render(1.0);
    ASSERT_GT(ctx, peakOf(out), 0.001f, "silent melody");

    OK(ctx);
}

static void testSynthNoteOnOff(TestContext& ctx) {
    TEST(ctx, "Note on/off envelope");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(*sf2, 48000)) { FAIL(ctx, "synth init failed"); return; }

    synth.programChange(0, 0, 0);
    synth.noteOn(0, 60, 100);

    float peak1 = peakOf(synth.render(0.1));
    synth.noteOff(0, 60);
    float peak2 = peakOf(synth.render(0.5));

    ASSERT_GT(ctx, peak1, 0.001f, "noteOn silent");
    ASSERT_TRUE(ctx, peak2 < peak1, "noteOff did not reduce volume");

    OK(ctx);
}

static void testSynthMultipleVoices(TestContext& ctx) {
    TEST(ctx, "25 simultaneous voices");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(*sf2, 48000)) { FAIL(ctx, "synth init failed"); return; }

    synth.programChange(0, 0, 0);
    for (int note = 48; note <= 72; note++)
        synth.noteOn(0, note, 80);

    float peak = peakOf(synth.render(0.5));
    ASSERT_GT(ctx, peak, 0.01f, "peak too low");

    OK(ctx);
}

static void testSynthCh10Drum(TestContext& ctx) {
    TEST(ctx, "Channel 10 drum via MIDI (effectiveBankMSB=128)");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    auto data = midi_fixtures::makeCh10DrumMidi();
    std::string midPath = "/tmp/mid2wav_test_ch10drum.mid";
    std::string wavPath = "/tmp/mid2wav_test_ch10drum.wav";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(midPath), "load midi failed");

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*sf2, 48000), "synth init failed");

    ConvertOptions opts;
    synth.renderToWav(midi.notes(), wavPath, opts, midi);

    std::vector<float> left, right;
    int sr = 0;
    ASSERT_TRUE(ctx, WavReader::read(wavPath, left, right, sr), "read wav failed");
    ASSERT_GT(ctx, peakStereo(left, right), 0.001f, "Ch10 drum render silent");

    fs::remove(midPath);
    fs::remove(wavPath);
    OK(ctx);
}

static void testSynthCh10MelodyMode(TestContext& ctx) {
    TEST(ctx, "Channel 10 melody mode via MIDI render");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    auto data = midi_fixtures::makeCh10MelodyModeMidi();
    std::string midPath = "/tmp/mid2wav_test_ch10melody.mid";
    std::string wavPath = "/tmp/mid2wav_test_ch10melody.wav";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midPath, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(midPath), "load midi failed");

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.init(*sf2, 48000), "synth init failed");

    ConvertOptions opts;
    synth.renderToWav(midi.notes(), wavPath, opts, midi);

    std::vector<float> left, right;
    int sr = 0;
    ASSERT_TRUE(ctx, WavReader::read(wavPath, left, right, sr), "read wav failed");
    ASSERT_GT(ctx, peakStereo(left, right), 0.001f, "Ch10 melody render silent");

    fs::remove(midPath);
    fs::remove(wavPath);
    OK(ctx);
}

static void testSynthReverbCC(TestContext& ctx) {
    TEST(ctx, "Reverb CC91 via MIDI render");

    auto* sf2 = loadTestSf2(ctx);
    if (!sf2) { FAIL(ctx, "SF2 load failed"); return; }

    auto makeMidi = [](uint8_t reverbCC) {
        midi_fixtures::TrackBuilder tb;
        if (reverbCC > 0) tb.cc(0, 91, reverbCC);
        tb.noteOn(0, 72, 100);
        tb.noteOff(0, 72, 480);
        tb.endOfTrack();
        return midi_fixtures::buildSmf(tb.events);
    };

    std::string midDry = "/tmp/mid2wav_test_reverb_dry.mid";
    std::string midWet = "/tmp/mid2wav_test_reverb_wet.mid";
    std::string wavDry = "/tmp/mid2wav_test_reverb_dry.wav";
    std::string wavWet = "/tmp/mid2wav_test_reverb_wet.wav";

    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midDry, makeMidi(0)), "write dry midi");
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(midWet, makeMidi(127)), "write wet midi");

    auto renderMidi = [&](const std::string& midPath, const std::string& wavPath) -> std::vector<float> {
        MidiFile midi;
        if (!midi.load(midPath)) return {};
        SFSynthesizer synth;
        if (!synth.init(*sf2, 48000)) return {};
        ConvertOptions opts;
        opts.noNormalize = true;
        synth.renderToWav(midi.notes(), wavPath, opts, midi);
        std::vector<float> left, right;
        int sr = 0;
        if (!WavReader::read(wavPath, left, right, sr)) return {};
        std::vector<float> mono(left.size());
        for (size_t i = 0; i < left.size(); i++)
            mono[i] = (left[i] + right[i]) * 0.5f;
        return mono;
    };

    auto dry = renderMidi(midDry, wavDry);
    auto wet = renderMidi(midWet, wavWet);

    ASSERT_FALSE(ctx, dry.empty(), "dry silent");
    ASSERT_FALSE(ctx, wet.empty(), "wet silent");

    float diff = 0;
    size_t n = std::min(dry.size(), wet.size());
    for (size_t i = 0; i < n; i++) {
        float d = dry[i] - wet[i];
        diff += d * d;
    }
    diff = n > 0 ? std::sqrt(diff / n) : 0;

    ASSERT_GT(ctx, diff, 0.0001f, "reverb had no effect");

    fs::remove(midDry);
    fs::remove(midWet);
    fs::remove(wavDry);
    fs::remove(wavWet);
    OK(ctx);
}

static void testSynthFallback(TestContext& ctx) {
    TEST(ctx, "Fallback sine mode");

    SFSynthesizer synth;
    ASSERT_TRUE(ctx, synth.initFallback(48000), "initFallback failed");
    synth.noteOn(0, 60, 100);

    float peak = peakOf(synth.render(0.2));
    ASSERT_GT(ctx, peak, 0.001f, "fallback silent");

    OK(ctx);
}

static void runSynthSuite(TestContext& ctx) {
    testSynthDrumRendering(ctx);
    testSynthMelodyRendering(ctx);
    testSynthNoteOnOff(ctx);
    testSynthMultipleVoices(ctx);
    testSynthCh10Drum(ctx);
    testSynthCh10MelodyMode(ctx);
    testSynthReverbCC(ctx);
    testSynthFallback(ctx);
}

void registerSynthTests() {
    registerSuite({"synth", runSynthSuite});
}

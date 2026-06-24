#include "test_framework.h"
#include "test_fixtures.h"
#include "midi_file.h"
#include <cmath>

static void testMidiSimpleNote(TestContext& ctx) {
    TEST(ctx, "Parse simple note on/off");

    auto data = midi_fixtures::makeSimpleNoteMidi(0, 60, 100);
    std::string path = "/tmp/mid2wav_test_simple.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");
    ASSERT_EQ(ctx, midi.ticksPerQuarterNote(), 480, "tpq");
    ASSERT_EQ(ctx, (int)midi.notes().size(), 1, "note count");
    ASSERT_EQ(ctx, (int)midi.notes()[0].note, 60, "note number");
    ASSERT_EQ(ctx, (int)midi.notes()[0].velocity, 100, "velocity");
    ASSERT_EQ(ctx, (int)midi.notes()[0].channel, 0, "channel");

    std::filesystem::remove(path);
    OK(ctx);
}

static void testMidiTempo(TestContext& ctx) {
    TEST(ctx, "Tempo meta event (90 BPM)");

    auto data = midi_fixtures::makeTempoMidi(90.0);
    std::string path = "/tmp/mid2wav_test_tempo.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");
    ASSERT_NEAR(ctx, midi.initialTempo(), 90.0, 0.01, "initial tempo");

    // tick 480 = 1 quarter at 90 BPM => 60/90 = 0.667 sec
    double sec = midi.tickToSeconds(480);
    ASSERT_NEAR(ctx, sec, 60.0 / 90.0, 0.01, "tickToSeconds at 480");

    std::filesystem::remove(path);
    OK(ctx);
}

static void testMidiBankSelect(TestContext& ctx) {
    TEST(ctx, "CC bank select MSB/LSB");

    auto data = midi_fixtures::makeBankSelectMidi(1, 5, 3);
    std::string path = "/tmp/mid2wav_test_bank.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");

    const auto& expr = midi.expression();
    ASSERT_EQ(ctx, (int)expr.bankSelectMSB[1].size(), 1, "MSB events");
    ASSERT_EQ(ctx, expr.bankSelectMSB[1][0].second, 5, "MSB value");
    ASSERT_EQ(ctx, (int)expr.bankSelectLSB[1].size(), 1, "LSB events");
    ASSERT_EQ(ctx, expr.bankSelectLSB[1][0].second, 3, "LSB value");
    ASSERT_EQ(ctx, (int)expr.programChange[1].size(), 1, "program change");
    ASSERT_EQ(ctx, expr.programChange[1][0].second, 0, "program");

    std::filesystem::remove(path);
    OK(ctx);
}

static void testMidiGsSysEx(TestContext& ctx) {
    TEST(ctx, "GS SysEx reverb level");

    auto data = midi_fixtures::makeGsReverbLevelMidi(100);
    std::string path = "/tmp/mid2wav_test_gs.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");
    ASSERT_TRUE(ctx, midi.hasRolandGS(), "GS flag");
    ASSERT_EQ(ctx, (int)midi.expression().sysReverbLevel.size(), 1, "reverb level events");
    ASSERT_EQ(ctx, midi.expression().sysReverbLevel[0].second, 100, "reverb level value");

    std::filesystem::remove(path);
    OK(ctx);
}

static void testMidiGsPartMode(TestContext& ctx) {
    TEST(ctx, "GS SysEx part mode (Ch10 melody)");

    auto data = midi_fixtures::makeCh10MelodyModeMidi();
    std::string path = "/tmp/mid2wav_test_partmode.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");
    ASSERT_EQ(ctx, (int)midi.expression().sysPartMode[9].size(), 1, "part mode events");
    ASSERT_EQ(ctx, midi.expression().sysPartMode[9][0].second, 0, "melody mode");

    std::filesystem::remove(path);
    OK(ctx);
}

static void testMidiExpressionGetValue(TestContext& ctx) {
    TEST(ctx, "MidiExpression::getValueAtTick");

    MidiExpression expr;
    expr.addVolume(0, 0, 100);
    expr.addVolume(0, 480, 64);
    expr.addVolume(0, 960, 32);

    ASSERT_EQ(ctx, expr.getValueAtTick(expr.volume[0], 0, 127), 100, "tick 0");
    ASSERT_EQ(ctx, expr.getValueAtTick(expr.volume[0], 480, 127), 64, "tick 480");
    ASSERT_EQ(ctx, expr.getValueAtTick(expr.volume[0], 700, 127), 64, "tick 700");
    ASSERT_EQ(ctx, expr.getValueAtTick(expr.volume[0], 960, 127), 32, "tick 960");
    ASSERT_EQ(ctx, expr.getValueAtTick(expr.volume[0], 2000, 127), 32, "tick 2000");

    OK(ctx);
}

static void testMidiTimeRoundTrip(TestContext& ctx) {
    TEST(ctx, "timeToTick / tickToSeconds round-trip");

    auto data = midi_fixtures::makeSimpleNoteMidi(0, 60, 80);
    std::string path = "/tmp/mid2wav_test_time.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");

    double t = 0.5;
    int64_t tick = midi.timeToTick(t);
    double t2 = midi.tickToSeconds(tick);
    ASSERT_NEAR(ctx, t2, t, 0.05, "round-trip time");

    std::filesystem::remove(path);
    OK(ctx);
}

static void testMidiAnalyzeTracks(TestContext& ctx) {
    TEST(ctx, "analyzeTracks drum channel");

    auto data = midi_fixtures::makeCh10DrumMidi();
    std::string path = "/tmp/mid2wav_test_analyze.mid";
    ASSERT_TRUE(ctx, midi_fixtures::writeTempFile(path, data), "write midi failed");

    MidiFile midi;
    ASSERT_TRUE(ctx, midi.load(path), "load failed");

    auto tracks = midi.analyzeTracks();
    bool foundDrum = false;
    for (auto& t : tracks) {
        if (t.isDrum) {
            foundDrum = true;
            ASSERT_EQ(ctx, t.index, 9, "drum channel index");
        }
    }
    ASSERT_TRUE(ctx, foundDrum, "drum track not found");

    std::filesystem::remove(path);
    OK(ctx);
}

static void runMidiSuite(TestContext& ctx) {
    testMidiSimpleNote(ctx);
    testMidiTempo(ctx);
    testMidiBankSelect(ctx);
    testMidiGsSysEx(ctx);
    testMidiGsPartMode(ctx);
    testMidiExpressionGetValue(ctx);
    testMidiTimeRoundTrip(ctx);
    testMidiAnalyzeTracks(ctx);
}

void registerMidiTests() {
    registerSuite({"midi", runMidiSuite});
}

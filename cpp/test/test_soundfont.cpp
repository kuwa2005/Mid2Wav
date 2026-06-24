#include "test_framework.h"
#include "soundfont.h"
#include <fstream>
#include <filesystem>

static void testSf2Load(TestContext& ctx) {
    TEST(ctx, "Load TyrolandGSV30fix.sf2");

    std::string path = findSoundFont();
    ASSERT_FALSE(ctx, path.empty(), "SF2 not found");

    SoundFont sf2;
    ASSERT_TRUE(ctx, sf2.load(path), "load failed");
    ASSERT_GT(ctx, (int)sf2.presets().size(), 0, "presets");
    ASSERT_GT(ctx, (int)sf2.instruments().size(), 0, "instruments");
    ASSERT_GT(ctx, (int)sf2.samples().size(), 0, "samples");
    ASSERT_GT(ctx, sf2.sampleDataSize(), (size_t)0, "sample data");

    OK(ctx);
}

static void testSf2FindPreset(TestContext& ctx) {
    TEST(ctx, "findPreset GM piano and drum kit");

    std::string path = findSoundFont();
    if (path.empty()) { FAIL(ctx, "SF2 not found"); return; }

    SoundFont sf2;
    if (!sf2.load(path)) { FAIL(ctx, "load failed"); return; }

    int piano = sf2.findPreset(0, 0);
    ASSERT_GE(ctx, piano, 0, "GM piano preset");

    int drums = sf2.findPreset(128, 0);
    ASSERT_GE(ctx, drums, 0, "drum kit preset");

    ASSERT_TRUE(ctx, piano != drums, "piano and drum presets differ");

    OK(ctx);
}

static void testSf2Generators(TestContext& ctx) {
    TEST(ctx, "Preset/instrument generators present");

    std::string path = findSoundFont();
    if (path.empty()) { FAIL(ctx, "SF2 not found"); return; }

    SoundFont sf2;
    if (!sf2.load(path)) { FAIL(ctx, "load failed"); return; }

    ASSERT_GT(ctx, (int)sf2.presetGenerators().size(), 0, "preset generators");
    ASSERT_GT(ctx, (int)sf2.instGenerators().size(), 0, "inst generators");
    ASSERT_GT(ctx, (int)sf2.presetBags().size(), 0, "preset bags");
    ASSERT_GT(ctx, (int)sf2.instBags().size(), 0, "inst bags");

    bool hasSampleId = false;
    for (auto& g : sf2.instGenerators()) {
        if (g.oper == (uint16_t)SF2GenOper::sampleID) { hasSampleId = true; break; }
    }
    ASSERT_TRUE(ctx, hasSampleId, "sampleID generator exists");

    OK(ctx);
}

static void testSf2InvalidFile(TestContext& ctx) {
    TEST(ctx, "Reject invalid SF2");

    std::string path = "/tmp/mid2wav_test_invalid.sf2";
    std::vector<uint8_t> junk = {'N', 'O', 'T', 'S', 'F', '2'};
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(junk.data()), junk.size());
    f.close();

    SoundFont sf2;
    ASSERT_FALSE(ctx, sf2.load(path), "should reject invalid file");

    std::filesystem::remove(path);
    OK(ctx);
}

static void runSoundfontSuite(TestContext& ctx) {
    testSf2Load(ctx);
    testSf2FindPreset(ctx);
    testSf2Generators(ctx);
    testSf2InvalidFile(ctx);
}

void registerSoundfontTests() {
    registerSuite({"soundfont", runSoundfontSuite});
}

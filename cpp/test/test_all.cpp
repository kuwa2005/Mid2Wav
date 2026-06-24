#include "wav_writer.h"
#include "sf_synth.h"
#include "soundfont.h"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

static int passed = 0;
static int failed = 0;

#define TEST(name) std::cout << "TEST: " << name << " ... " << std::flush;
#define OK() do { std::cout << "OK" << std::endl; passed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << std::endl; failed++; } while(0)

static float peakOf(const std::vector<float>& v) {
    float p = 0;
    for (float s : v) p = std::max(p, std::abs(s));
    return p;
}

static void testWavRoundTrip() {
    TEST("WavReader/WavWriter round-trip");

    int sr = 48000;
    int n = 1000;
    std::vector<float> left(n), right(n);
    for (int i = 0; i < n; i++) {
        left[i] = std::sin(2.0 * M_PI * 440.0 * i / sr);
        right[i] = std::sin(2.0 * M_PI * 880.0 * i / sr);
    }

    std::string path = "/tmp/unit_test_roundtrip.wav";
    if (!WavWriter::write(path, left, right, sr)) { FAIL("write failed"); return; }

    std::vector<float> readL, readR;
    int readSr = 0;
    if (!WavReader::read(path, readL, readR, readSr)) { FAIL("read failed"); return; }
    if (readSr != sr) { FAIL("sample rate mismatch"); return; }
    if ((int)readL.size() != n) { FAIL("sample count mismatch"); return; }

    float maxErr = 0;
    for (int i = 0; i < n; i++) {
        maxErr = std::max(maxErr, std::abs(left[i] - readL[i]));
        maxErr = std::max(maxErr, std::abs(right[i] - readR[i]));
    }
    if (maxErr > 0.001f) { FAIL("samples differ: " + std::to_string(maxErr)); return; }

    fs::remove(path);
    OK();
}

static void testMixFromChannelWavs() {
    TEST("mixFromChannelWavs");

    int sr = 48000;
    int n = 1000;
    std::string dir = "/tmp/unit_test_mix";
    fs::create_directories(dir);

    std::string base = "test";
    std::vector<float> ch1L(n, 0.5f), ch1R(n, 0.5f);
    std::vector<float> ch2L(n, 0.3f), ch2R(n, 0.3f);

    WavWriter::write(dir + "/test_01_A.wav", ch1L, ch1R, sr);
    WavWriter::write(dir + "/test_02_B.wav", ch2L, ch2R, sr);

    std::string outPath = "/tmp/unit_test_mix_out.wav";
    bool ok = SFSynthesizer::mixFromChannelWavs(dir, base, outPath, sr, true);
    if (!ok) { FAIL("returned false"); fs::remove_all(dir); return; }

    std::vector<float> mixL, mixR;
    int readSr = 0;
    if (!WavReader::read(outPath, mixL, mixR, readSr)) { FAIL("read failed"); fs::remove_all(dir); return; }

    float expected = 0.8f;
    float maxErr = 0;
    for (int i = 0; i < n; i++) {
        maxErr = std::max(maxErr, std::abs(mixL[i] - expected));
        maxErr = std::max(maxErr, std::abs(mixR[i] - expected));
    }

    fs::remove_all(dir);
    fs::remove(outPath);
    if (maxErr > 0.01f) { FAIL("value wrong: expected " + std::to_string(expected) + " err=" + std::to_string(maxErr)); return; }
    OK();
}

static void testSynthDrumRendering() {
    TEST("Synth drum rendering (bank=128)");

    SoundFont sf2;
    if (!sf2.load("../soundfonts/TyrolandGSV30fix.sf2")) { FAIL("SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(sf2, 48000)) { FAIL("synth init failed"); return; }

    synth.controlChange(0, 0, 0);
    synth.programChange(0, 8, 128);
    synth.noteOn(0, 35, 127);
    synth.noteOn(0, 38, 127);
    synth.noteOn(0, 42, 127);

    std::vector<float> out = synth.render(0.5);
    float peak = peakOf(out);

    if (peak < 0.001f) { FAIL("silent, peak=" + std::to_string(peak)); return; }
    OK();
}

static void testSynthMelodyRendering() {
    TEST("Synth melody rendering (bank=0)");

    SoundFont sf2;
    if (!sf2.load("../soundfonts/TyrolandGSV30fix.sf2")) { FAIL("SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(sf2, 48000)) { FAIL("synth init failed"); return; }

    synth.programChange(0, 0, 0);
    synth.noteOn(0, 60, 100);

    std::vector<float> out = synth.render(1.0);
    float peak = peakOf(out);

    if (peak < 0.001f) { FAIL("silent"); return; }
    OK();
}

static void testSynthNoteOnOff() {
    TEST("Synth note on/off envelope");

    SoundFont sf2;
    if (!sf2.load("../soundfonts/TyrolandGSV30fix.sf2")) { FAIL("SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(sf2, 48000)) { FAIL("synth init failed"); return; }

    synth.programChange(0, 0, 0);
    synth.noteOn(0, 60, 100);

    std::vector<float> out1 = synth.render(0.1);
    float peak1 = peakOf(out1);

    synth.noteOff(0, 60);
    std::vector<float> out2 = synth.render(0.5);
    float peak2 = peakOf(out2);

    if (peak1 < 0.001f) { FAIL("noteOn produced no sound"); return; }
    if (peak2 >= peak1) { FAIL("noteOff did not reduce volume"); return; }
    OK();
}

static void testSynthMultipleVoices() {
    TEST("Synth 25 simultaneous voices");

    SoundFont sf2;
    if (!sf2.load("../soundfonts/TyrolandGSV30fix.sf2")) { FAIL("SF2 load failed"); return; }

    SFSynthesizer synth;
    if (!synth.init(sf2, 48000)) { FAIL("synth init failed"); return; }

    synth.programChange(0, 0, 0);
    for (int note = 48; note <= 72; note++) {
        synth.noteOn(0, note, 80);
    }

    std::vector<float> out = synth.render(0.5);
    float peak = peakOf(out);

    if (peak < 0.01f) { FAIL("peak too low: " + std::to_string(peak)); return; }
    OK();
}

int main(int argc, char** argv) {
    std::cout << "=== Mid2Wav Unit Tests ===" << std::endl;

    testWavRoundTrip();
    testMixFromChannelWavs();
    testSynthDrumRendering();
    testSynthMelodyRendering();
    testSynthNoteOnOff();
    testSynthMultipleVoices();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    return failed > 0 ? 1 : 0;
}

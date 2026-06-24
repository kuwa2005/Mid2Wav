#include "test_framework.h"
#include "reverb.h"
#include "chorus.h"
#include "delay.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void fillSine(std::vector<float>& left, std::vector<float>& right, int sr, float freq) {
    int n = (int)left.size();
    for (int i = 0; i < n; i++) {
        float s = (float)std::sin(2.0 * M_PI * freq * i / sr);
        left[i] = s;
        right[i] = s * 0.9f;
    }
}

static float rmsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float sum = 0;
    int n = std::min((int)a.size(), (int)b.size());
    for (int i = 0; i < n; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return n > 0 ? std::sqrt(sum / n) : 0;
}

static void testReverbProcess(TestContext& ctx) {
    TEST(ctx, "SimpleReverb changes signal");

    const int sr = 48000;
    const int n = sr / 10;
    std::vector<float> left(n), right(n), dryL(n), dryR(n);
    fillSine(left, right, sr, 440.0f);
    dryL = left;
    dryR = right;

    SimpleReverb rev;
    rev.init(sr);
    rev.process(left.data(), right.data(), n, 0.5f, false);

    float diff = rmsDiff(left, dryL);
    ASSERT_GT(ctx, diff, 0.001f, "reverb had no effect");

    OK(ctx);
}

static void testReverbDrumMode(TestContext& ctx) {
    TEST(ctx, "SimpleReverb drumMode");

    const int sr = 48000;
    const int n = sr / 20;
    std::vector<float> left(n), right(n), dryL(n), dryR(n);
    fillSine(left, right, sr, 200.0f);
    dryL = left;
    dryR = right;

    SimpleReverb rev;
    rev.init(sr);
    rev.process(left.data(), right.data(), n, 0.4f, true);

    float diff = rmsDiff(left, dryL);
    ASSERT_GT(ctx, diff, 0.0005f, "drum reverb had no effect");

    OK(ctx);
}

static void testChorusProcess(TestContext& ctx) {
    TEST(ctx, "Chorus changes signal");

    const int sr = 48000;
    const int n = sr / 5;
    std::vector<float> left(n), right(n), dryL(n);
    fillSine(left, right, sr, 330.0f);
    dryL = left;

    Chorus chorus;
    chorus.init(sr);
    chorus.process(left.data(), right.data(), n, 80);

    float diff = rmsDiff(left, dryL);
    ASSERT_GT(ctx, diff, 0.00001f, "chorus had no effect");

    OK(ctx);
}

static void testChorusBypass(TestContext& ctx) {
    TEST(ctx, "Chorus bypass at CC=0");

    const int sr = 48000;
    const int n = 256;
    std::vector<float> left(n), right(n), dryL(n);
    fillSine(left, right, sr, 440.0f);
    dryL = left;

    Chorus chorus;
    chorus.init(sr);
    chorus.process(left.data(), right.data(), n, 0);

    float diff = rmsDiff(left, dryL);
    ASSERT_NEAR(ctx, diff, 0.0f, 0.0001f, "chorus should bypass at 0");

    OK(ctx);
}

static void testDelayProcess(TestContext& ctx) {
    TEST(ctx, "Delay produces echo");

    const int sr = 48000;
    const int n = sr / 2;
    std::vector<float> left(n, 0), right(n, 0);
    left[100] = 1.0f;
    right[100] = 1.0f;

    Delay delay;
    delay.init(sr);
    delay.process(left.data(), right.data(), n, 0.1f, 0.3f, 0.5f);

    float tailEnergy = 0;
    for (int i = 5000; i < n; i++)
        tailEnergy += std::abs(left[i]) + std::abs(right[i]);

    ASSERT_GT(ctx, tailEnergy, 0.01f, "no delay tail");

    OK(ctx);
}

static void testDelayBypass(TestContext& ctx) {
    TEST(ctx, "Delay bypass at mix=0");

    const int sr = 48000;
    const int n = 1024;
    std::vector<float> left(n), right(n), dryL(n);
    fillSine(left, right, sr, 440.0f);
    dryL = left;

    Delay delay;
    delay.init(sr);
    delay.process(left.data(), right.data(), n, 0.25f, 0.3f, 0.0f);

    float diff = rmsDiff(left, dryL);
    ASSERT_NEAR(ctx, diff, 0.0f, 0.0001f, "delay should bypass at mix=0");

    OK(ctx);
}

static void runFxSuite(TestContext& ctx) {
    testReverbProcess(ctx);
    testReverbDrumMode(ctx);
    testChorusProcess(ctx);
    testChorusBypass(ctx);
    testDelayProcess(ctx);
    testDelayBypass(ctx);
}

void registerFxTests() {
    registerSuite({"fx", runFxSuite});
}

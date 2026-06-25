#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <filesystem>

struct TestContext {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    std::string currentSuite;
};

struct TestSuite {
    std::string name;
    std::function<void(TestContext&)> run;
};

inline std::vector<TestSuite>& allSuites() {
    static std::vector<TestSuite> suites;
    return suites;
}

inline void registerSuite(const TestSuite& suite) {
    allSuites().push_back(suite);
}

#define TEST(ctx, name) std::cout << "  TEST: " << name << " ... " << std::flush

#define OK(ctx) do { std::cout << "OK" << std::endl; (ctx).passed++; } while (0)

#define FAIL(ctx, msg) do { \
    std::cout << "FAIL: " << (msg) << std::endl; \
    (ctx).failed++; \
} while (0)

#define SKIP(ctx, msg) do { \
    std::cout << "SKIP: " << (msg) << std::endl; \
    (ctx).skipped++; \
    return; \
} while (0)

#define ASSERT_TRUE(ctx, cond, msg) do { \
    if (!(cond)) { FAIL(ctx, msg); return; } \
} while (0)

#define ASSERT_FALSE(ctx, cond, msg) ASSERT_TRUE(ctx, !(cond), msg)

#define ASSERT_EQ(ctx, a, b, msg) do { \
    if ((a) != (b)) { \
        FAIL(ctx, std::string(msg) + " (got " + std::to_string(a) + ", expected " + std::to_string(b) + ")"); \
        return; \
    } \
} while (0)

#define ASSERT_NEAR(ctx, a, b, eps, msg) do { \
    if (std::abs((a) - (b)) > (eps)) { \
        FAIL(ctx, std::string(msg) + " (got " + std::to_string(a) + ", expected ~" + std::to_string(b) + ")"); \
        return; \
    } \
} while (0)

#define ASSERT_GT(ctx, a, b, msg) do { \
    if (!((a) > (b))) { \
        FAIL(ctx, std::string(msg) + " (got " + std::to_string(a) + ", need > " + std::to_string(b) + ")"); \
        return; \
    } \
} while (0)

#define ASSERT_GE(ctx, a, b, msg) do { \
    if (!((a) >= (b))) { \
        FAIL(ctx, std::string(msg) + " (got " + std::to_string(a) + ", need >= " + std::to_string(b) + ")"); \
        return; \
    } \
} while (0)

inline void runSuite(TestContext& ctx, const TestSuite& suite) {
    ctx.currentSuite = suite.name;
    std::cout << "\n=== Suite: " << suite.name << " ===" << std::endl;
    suite.run(ctx);
}

inline float peakOf(const std::vector<float>& v) {
    float p = 0;
    for (float s : v) p = std::max(p, std::abs(s));
    return p;
}

inline float peakStereo(const std::vector<float>& left, const std::vector<float>& right) {
    return std::max(peakOf(left), peakOf(right));
}

inline float rmsOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0.0;
    for (float s : v) sum += (double)s * s;
    return (float)std::sqrt(sum / v.size());
}

inline float rmsStereo(const std::vector<float>& left, const std::vector<float>& right) {
    if (left.empty() && right.empty()) return 0.0f;
    double sum = 0.0;
    for (float s : left) sum += (double)s * s;
    for (float s : right) sum += (double)s * s;
    return (float)std::sqrt(sum / (left.size() + right.size()));
}

inline bool compareWavSimilar(const std::vector<float>& l1, const std::vector<float>& r1,
                              const std::vector<float>& l2, const std::vector<float>& r2,
                              float peakTol, float rmsTol) {
    if (l1.size() != l2.size() || r1.size() != r2.size()) return false;
    float p1 = peakStereo(l1, r1), p2 = peakStereo(l2, r2);
    float r1v = rmsStereo(l1, r1), r2v = rmsStereo(l2, r2);
    return std::abs(p1 - p2) <= peakTol && std::abs(r1v - r2v) <= rmsTol;
}

inline std::string findSoundFont() {
    namespace fs = std::filesystem;
    const std::vector<std::string> candidates = {
        "../soundfonts/TyrolandGSV30fix.sf2",
        "soundfonts/TyrolandGSV30fix.sf2",
        "../../soundfonts/TyrolandGSV30fix.sf2",
    };
    for (const auto& path : candidates) {
        if (fs::exists(path)) return path;
    }
    return "";
}

inline bool skipWithoutSf2(TestContext& ctx, const char* msg = "TyrolandGSV30fix.sf2 not found (place under soundfonts/)") {
    if (!findSoundFont().empty()) return false;
    std::cout << "SKIP: " << msg << std::endl;
    ctx.skipped++;
    return true;
}

inline bool suiteMatches(const std::string& name, const std::vector<std::string>& filters) {
    if (filters.empty()) return true;
    for (const auto& f : filters) {
        if (f == name || f == "all") return true;
    }
    return false;
}

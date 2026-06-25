#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace audio_analysis {

inline std::vector<float> toMono(const std::vector<float>& left,
                                 const std::vector<float>& right) {
    size_t n = std::min(left.size(), right.size());
    std::vector<float> mono(n);
    for (size_t i = 0; i < n; i++)
        mono[i] = (left[i] + right[i]) * 0.5f;
    return mono;
}

inline float computeRms(const std::vector<float>& samples,
                        size_t start = 0, size_t len = 0) {
    if (samples.empty()) return 0.0f;
    if (len == 0) len = samples.size() - start;
    if (start + len > samples.size()) len = samples.size() - start;
    if (len == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = start; i < start + len; i++)
        sum += (double)samples[i] * samples[i];
    return (float)std::sqrt(sum / len);
}

struct ModulationMetrics {
    float meanRms = 0.0f;
    float minRms = 0.0f;
    float maxRms = 0.0f;
    float coefficientOfVariation = 0.0f;
    float modulationDepth = 0.0f;
    int windowCount = 0;
};

// Split [start, start+len) into fixed windows; return RMS variance metrics.
// High CV / modulationDepth on a steady sustain suggests undulation / warble.
inline ModulationMetrics detectAmplitudeModulation(
    const std::vector<float>& samples, int sampleRate,
    int windowMs, size_t start = 0, size_t len = 0) {
    ModulationMetrics m;
    if (samples.empty() || sampleRate <= 0 || windowMs <= 0) return m;
    if (len == 0) len = samples.size() - start;
    if (start + len > samples.size()) len = samples.size() - start;

    size_t windowSamples = (size_t)((int64_t)sampleRate * windowMs / 1000);
    if (windowSamples < 1) windowSamples = 1;

    std::vector<float> windowRms;
    for (size_t pos = start; pos + windowSamples <= start + len; pos += windowSamples) {
        windowRms.push_back(computeRms(samples, pos, windowSamples));
    }
    if (windowRms.empty()) return m;

    m.windowCount = (int)windowRms.size();
    double sum = 0.0, sumSq = 0.0;
    m.minRms = m.maxRms = windowRms[0];
    for (float r : windowRms) {
        sum += r;
        sumSq += (double)r * r;
        m.minRms = std::min(m.minRms, r);
        m.maxRms = std::max(m.maxRms, r);
    }
    m.meanRms = (float)(sum / windowRms.size());
    if (m.meanRms > 1e-8f) {
        double mean = sum / windowRms.size();
        double variance = sumSq / windowRms.size() - mean * mean;
        float stddev = (float)std::sqrt(std::max(0.0, variance));
        m.coefficientOfVariation = stddev / m.meanRms;
        m.modulationDepth = (m.maxRms - m.minRms) / m.meanRms;
    }
    return m;
}

// RMS windows with linear trend removed; isolates ripple/warble from natural decay.
inline float detrendedModulationCv(const std::vector<float>& samples, int sampleRate,
                                  int windowMs, size_t start = 0, size_t len = 0) {
    if (samples.empty() || sampleRate <= 0 || windowMs <= 0) return 0.0f;
    if (len == 0) len = samples.size() - start;
    if (start + len > samples.size()) len = samples.size() - start;

    size_t windowSamples = (size_t)((int64_t)sampleRate * windowMs / 1000);
    if (windowSamples < 1) windowSamples = 1;

    std::vector<float> windowRms;
    for (size_t pos = start; pos + windowSamples <= start + len; pos += windowSamples)
        windowRms.push_back(computeRms(samples, pos, windowSamples));
    if (windowRms.size() < 3) return 0.0f;

    int n = (int)windowRms.size();
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    for (int i = 0; i < n; i++) {
        sumX += i;
        sumY += windowRms[i];
        sumXY += (double)i * windowRms[i];
        sumX2 += (double)i * i;
    }
    double denom = (double)n * sumX2 - sumX * sumX;
    double slope = (std::abs(denom) > 1e-12) ? ((double)n * sumXY - sumX * sumY) / denom : 0.0;
    double intercept = (sumY - slope * sumX) / n;

    double resSumSq = 0.0;
    for (int i = 0; i < n; i++) {
        double res = windowRms[i] - (intercept + slope * i);
        resSumSq += res * res;
    }
    double rmsRes = std::sqrt(resSumSq / n);
    double meanLevel = sumY / n;
    return (meanLevel > 1e-8f) ? (float)(rmsRes / meanLevel) : 0.0f;
}

struct SimilarityMetrics {
    float peakA = 0.0f;
    float peakB = 0.0f;
    float rmsA = 0.0f;
    float rmsB = 0.0f;
    float peakRatio = 0.0f;
    float rmsRatio = 0.0f;
    float correlation = 0.0f;
};

inline SimilarityMetrics compareWavSimilarity(
    const std::vector<float>& l1, const std::vector<float>& r1,
    const std::vector<float>& l2, const std::vector<float>& r2) {
    SimilarityMetrics s;
    auto m1 = toMono(l1, r1);
    auto m2 = toMono(l2, r2);
    size_t n = std::min(m1.size(), m2.size());
    if (n == 0) return s;

    double sum1 = 0.0, sum2 = 0.0, sum12 = 0.0, sum1sq = 0.0, sum2sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        s.peakA = std::max(s.peakA, std::abs(m1[i]));
        s.peakB = std::max(s.peakB, std::abs(m2[i]));
        sum1 += m1[i];
        sum2 += m2[i];
        sum12 += (double)m1[i] * m2[i];
        sum1sq += (double)m1[i] * m1[i];
        sum2sq += (double)m2[i] * m2[i];
    }
    s.rmsA = (float)std::sqrt(sum1sq / n);
    s.rmsB = (float)std::sqrt(sum2sq / n);
    s.peakRatio = (s.peakB > 1e-8f) ? s.peakA / s.peakB : 0.0f;
    s.rmsRatio = (s.rmsB > 1e-8f) ? s.rmsA / s.rmsB : 0.0f;

    double mean1 = sum1 / n, mean2 = sum2 / n;
    double cov = sum12 / n - mean1 * mean2;
    double std1 = std::sqrt(std::max(0.0, sum1sq / n - mean1 * mean1));
    double std2 = std::sqrt(std::max(0.0, sum2sq / n - mean2 * mean2));
    if (std1 > 1e-10 && std2 > 1e-10)
        s.correlation = (float)(cov / (std1 * std2));
    return s;
}

inline bool withinTolerance(const SimilarityMetrics& s,
                            float peakRatioMin, float peakRatioMax,
                            float rmsRatioMin, float rmsRatioMax,
                            float minCorrelation) {
    if (s.peakRatio < peakRatioMin || s.peakRatio > peakRatioMax) return false;
    if (s.rmsRatio < rmsRatioMin || s.rmsRatio > rmsRatioMax) return false;
    if (s.correlation < minCorrelation) return false;
    return true;
}

} // namespace audio_analysis

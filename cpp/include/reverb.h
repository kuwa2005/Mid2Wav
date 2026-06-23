#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

class SimpleReverb {
public:
    void init(int sampleRate) {
        m_sampleRate = sampleRate;
        // Comb filter delay lines (FDN - Feedback Delay Network)
        // Tuned for a medium room reverb at 44100 Hz
        float combTuning[] = { 0.0317f, 0.0371f, 0.0411f, 0.0437f };
        float allpassTuning[] = { 0.0053f, 0.0017f };

        for (int i = 0; i < NUM_COMBS; i++) {
            int delay = (int)(combTuning[i] * sampleRate);
            m_combBuffer[i].resize(std::max(delay, 1), 0.0f);
            m_combPos[i] = 0;
        }
        for (int i = 0; i < NUM_ALLPASS; i++) {
            int delay = (int)(allpassTuning[i] * sampleRate);
            m_allpassBuffer[i].resize(std::max(delay, 1), 0.0f);
            m_allpassPos[i] = 0;
        }
        m_dampLpf = 0.0f;
        m_init = true;
    }

    void reset() {
        for (int i = 0; i < NUM_COMBS; i++) {
            std::fill(m_combBuffer[i].begin(), m_combBuffer[i].end(), 0.0f);
            m_combPos[i] = 0;
        }
        for (int i = 0; i < NUM_ALLPASS; i++) {
            std::fill(m_allpassBuffer[i].begin(), m_allpassBuffer[i].end(), 0.0f);
            m_allpassPos[i] = 0;
        }
        m_dampLpf = 0.0f;
    }

    // mix: 0.0 = dry, 1.0 = full wet
    // drumMode: true = shorter reverb for percussion (less feedback/damp)
    void process(float* left, float* right, int count, float mix, bool drumMode = false) {
        if (!m_init || mix < 0.001f || count <= 0) return;

        // Clamp mix
        mix = std::min(mix, 1.0f);

        // Reverb parameters (Dattorro-inspired)
        // Reduced feedback range to prevent excessive resonance on percussion
        float combFeedback = drumMode ? 0.55f : (0.70f - mix * 0.10f);
        float dampAmount = drumMode ? 0.55f : (0.35f + mix * 0.35f);
        float allpassFeedback = drumMode ? 0.25f : 0.45f;

        for (int i = 0; i < count; i++) {
            float input = (left[i] + right[i]) * 0.25f;

            // 4 comb filters in parallel
            float combOut = 0.0f;
            for (int c = 0; c < NUM_COMBS; c++) {
                int len = (int)m_combBuffer[c].size();
                if (len <= 0) continue;

                int pos = m_combPos[c];
                float out = m_combBuffer[c][pos];

                // Damping: simple low-pass on feedback
                m_dampLpf = out * dampAmount + m_dampLpf * (1.0f - dampAmount);

                // Write to buffer: input + damped feedback
                m_combBuffer[c][pos] = input + m_dampLpf * combFeedback;

                // Advance position with wrap
                m_combPos[c] = (pos + 1) % len;

                combOut += out;
            }
            combOut *= 0.25f;

            // 2 allpass filters in series (stereo spread)
            for (int a = 0; a < NUM_ALLPASS; a++) {
                int len = (int)m_allpassBuffer[a].size();
                if (len <= 0) continue;

                int pos = m_allpassPos[a];
                float out = m_allpassBuffer[a][pos];

                m_allpassBuffer[a][pos] = combOut + out * allpassFeedback;
                combOut = out - m_allpassBuffer[a][pos] * allpassFeedback;

                m_allpassPos[a] = (pos + 1) % len;
            }

            // Wet/dry mix with stereo spread
            float wet = combOut * mix;
            float dry = 1.0f - mix * 0.5f;
            left[i] = left[i] * dry + wet;
            right[i] = right[i] * dry + wet * 0.93f;
        }
    }

private:
    static const int NUM_COMBS = 4;
    static const int NUM_ALLPASS = 2;
    std::vector<float> m_combBuffer[NUM_COMBS];
    int m_combPos[NUM_COMBS] = {};
    std::vector<float> m_allpassBuffer[NUM_ALLPASS];
    int m_allpassPos[NUM_ALLPASS] = {};
    float m_dampLpf = 0.0f;
    int m_sampleRate = 44100;
    bool m_init = false;
};

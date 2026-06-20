#pragma once
#include <vector>
#include <cmath>

class Delay {
public:
    void init(int sampleRate) {
        m_sampleRate = sampleRate;
        // Max delay buffer: 2 seconds
        int maxSamples = (int)(2.0f * sampleRate);
        m_bufferL.resize(maxSamples, 0.0f);
        m_bufferR.resize(maxSamples, 0.0f);
        m_pos = 0;
        m_init = true;
    }

    void reset() {
        std::fill(m_bufferL.begin(), m_bufferL.end(), 0.0f);
        std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
        m_pos = 0;
    }

    // delayTimeSec: delay time in seconds
    // feedback: 0.0-0.9 (how much delayed signal feeds back)
    // mix: 0.0-1.0 wet/dry ratio
    void process(float* left, float* right, int count,
                 float delayTimeSec, float feedback, float mix) {
        if (!m_init || count <= 0 || mix < 0.001f) return;

        feedback = std::clamp(feedback, 0.0f, 0.9f);
        mix = std::clamp(mix, 0.0f, 1.0f);

        int delaySamples = (int)(delayTimeSec * m_sampleRate);
        if (delaySamples < 1) delaySamples = 1;
        if (delaySamples >= (int)m_bufferL.size()) delaySamples = (int)m_bufferL.size() - 1;

        int bufLen = (int)m_bufferL.size();

        for (int i = 0; i < count; i++) {
            // Read positions for L and R (slight offset for stereo ping-pong)
            int readL = (m_pos - delaySamples + bufLen) % bufLen;
            int readR = (m_pos - delaySamples + bufLen / 2) % bufLen; // R is offset by half delay

            float delayedL = m_bufferL[readL];
            float delayedR = m_bufferR[readR];

            // Write to delay buffers with feedback
            m_bufferL[m_pos] = left[i] + delayedL * feedback;
            m_bufferR[m_pos] = right[i] + delayedR * feedback;

            // Advance
            m_pos = (m_pos + 1) % bufLen;

            // Mix dry and wet
            left[i] = left[i] * (1.0f - mix) + delayedL * mix;
            right[i] = right[i] * (1.0f - mix) + delayedR * mix;
        }
    }

private:
    std::vector<float> m_bufferL;
    std::vector<float> m_bufferR;
    int m_pos = 0;
    int m_sampleRate = 44100;
    bool m_init = false;
};

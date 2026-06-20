#pragma once
#include <vector>
#include <cmath>

class Chorus {
public:
    void init(int sampleRate) {
        m_sampleRate = sampleRate;
        // Chorus delay line: ~20ms buffer (enough for modulation depth)
        int delaySamples = (int)(0.025f * sampleRate);
        m_buffer.resize(std::max(delaySamples, 1), 0.0f);
        m_pos = 0;
        m_lfoPhase = 0.0f;
        m_init = true;
    }

    void reset() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_pos = 0;
        m_lfoPhase = 0.0f;
    }

    // rate: CC93 value (0-127), maps to LFO rate and depth
    void process(float* left, float* right, int count, int rateCC) {
        if (!m_init || rateCC <= 0 || count <= 0) return;

        float mix = rateCC / 127.0f;
        float lfoRate = 0.5f + mix * 2.5f;   // 0.5 - 3.0 Hz
        float depth = 0.002f + mix * 0.008f;  // modulation depth in seconds
        float wetMix = 0.2f + mix * 0.3f;     // 20-50% wet

        int bufLen = (int)m_buffer.size();
        float phaseInc = 2.0f * 3.14159265f * lfoRate / (float)m_sampleRate;

        for (int i = 0; i < count; i++) {
            // Write input to delay buffer (mono sum)
            float input = (left[i] + right[i]) * 0.5f;
            m_buffer[m_pos] = input;

            // LFO modulation
            float lfo = std::sin(m_lfoPhase);
            m_lfoPhase += phaseInc;
            if (m_lfoPhase > 2.0f * 3.14159265f) m_lfoPhase -= 2.0f * 3.14159265f;

            // Read from delay buffer with LFO-modulated offset
            float delaySec = 0.012f + lfo * depth; // base 12ms + modulation
            float readPos = (float)m_pos - delaySec * (float)m_sampleRate;
            if (readPos < 0) readPos += (float)bufLen;
            int readIdx = (int)readPos % bufLen;
            if (readIdx < 0) readIdx += bufLen;
            float delayed = m_buffer[readIdx];

            // Mix dry and wet
            float out = input + delayed * wetMix;

            // Advance buffer position
            m_pos = (m_pos + 1) % bufLen;

            // Apply to stereo with slight offset for width
            left[i] = left[i] * (1.0f - wetMix) + out * wetMix;
            right[i] = right[i] * (1.0f - wetMix) + out * wetMix * 0.95f;
        }
    }

private:
    std::vector<float> m_buffer;
    int m_pos = 0;
    float m_lfoPhase = 0.0f;
    int m_sampleRate = 44100;
    bool m_init = false;
};

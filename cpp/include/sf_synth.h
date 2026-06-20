#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "soundfont.h"
#include "midi_file.h"
#include "reverb.h"
#include "chorus.h"
#include "delay.h"

struct SF2Voice {
    bool active = false;
    bool releasing = false;
    int channel = 0;
    int note = -1;
    int velocity = 127;
    int sampleIndex = -1;
    uint32_t sampleStart = 0;
    uint32_t sampleEnd = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    bool loop = false;
    double sampleRate = 44100.0;
    double position = 0.0;
    double pitchRatio = 1.0;
    double amplitude = 0.0;
    double pan = 0.0;
    double attackRate = 0.0;
    double decayRate = 0.0;
    double sustainLevel = 1.0;
    double releaseRate = 0.0;
    double envLevel = 0.0;
    double releaseLevel = 0.0;
    double filterFc = 13500.0;
    double filterQ = 0.7;
    double filterState = 0.0;
};

struct ChannelState {
    int program = 0;
    int bank = 0;
    int volume = 100;
    int expression = 127;
    int pan = 64;
    int pitchBend = 8192;
    int pitchBendRange = 2;
    int modulation = 0;
    int sustain = 0;
    int reverb = 0;
    int chorus = 0;
    int delay = 0;
};

struct ConvertOptions;

class SFSynthesizer {
public:
    bool init(const SoundFont& sf2, int sampleRate = 44100);
    bool initFallback(int sampleRate = 44100);

    void renderToWav(const std::vector<MidiNote>& notes,
                     const std::string& wavPath,
                     const ConvertOptions& opts,
                     const MidiFile& midi);

    void renderToWavPerChannel(const std::vector<MidiNote>& notes,
                               const std::string& baseName,
                               const std::string& outputDir,
                               const MidiFile& midi,
                               int pitchShift = 0);

    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void programChange(int channel, int program, int bank = 0);
    void controlChange(int channel, int cc, int value);
    void pitchBend(int channel, int value);

    std::vector<float> render(double seconds);

    const SoundFont* sf2() const { return m_sf2; }
    int sampleRate() const { return m_sampleRate; }
    const std::vector<SF2Voice>& voices() const { return m_voices; }

private:
    struct ResolvedZone {
        int sampleIndex = -1;
        int keyLow = 0, keyHigh = 127;
        int velLow = 0, velHigh = 127;
        double attenuation = 0.0;
        double pan = 0.0;
        double rootKey = -1.0;
        double fineTune = 0.0;
        double attack = 0.0;
        double decay = 0.0;
        double sustain = 1.0;
        double release = 0.0;
        double filterFc = 13500.0;
        double filterQ = 0.7;
        double sampleRate = 44100.0;
        uint32_t sampleStart = 0;
        uint32_t sampleEnd = 0;
        uint32_t loopStart = 0;
        uint32_t loopEnd = 0;
        bool loop = false;
    };

    void buildPresetZones(int channel);
    bool resolveNote(int channel, int note, int velocity, ResolvedZone& out);
    void startVoice(const ResolvedZone& zone, int channel, int note, int velocity);
    void processBlock(std::vector<float>& left, std::vector<float>& right, int count);
    void processVoice(SF2Voice& v, float* left, float* right, int count);

    double getEffectivePitchBend(int channel);
    double timecentsToSeconds(int tc);
    double attenuateDb(double db);

    const SoundFont* m_sf2 = nullptr;
    bool m_fallbackMode = false;
    int m_sampleRate = 44100;
    std::vector<SF2Voice> m_voices;
    std::vector<ChannelState> m_channels;
    static const int MAX_VOICES = 256;

    std::vector<ResolvedZone> m_channelZones[16];
    SimpleReverb m_reverb;
    Chorus m_chorus;
    Delay m_delay;
};

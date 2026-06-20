#include "sf_synth.h"
#include "converter.h"
#include "wav_writer.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────── utilities ───────────────────────────

// ─────────────────────────── init ───────────────────────────────

bool SFSynthesizer::init(const SoundFont& sf2, int sampleRate) {
    m_sf2 = &sf2;
    m_fallbackMode = false;
    m_sampleRate = sampleRate;
    m_channels.resize(16);
    m_voices.resize(MAX_VOICES);
    m_reverb.init(sampleRate);
    m_chorus.init(sampleRate);
    m_delay.init(sampleRate);
    std::cout << "[Synth] Initialized (" << MAX_VOICES << " voices, " << sampleRate << " Hz)" << std::endl;
    return true;
}

bool SFSynthesizer::initFallback(int sampleRate) {
    m_sf2 = nullptr;
    m_fallbackMode = true;
    m_sampleRate = sampleRate;
    m_channels.resize(16);
    m_voices.resize(MAX_VOICES);
    std::cout << "[Synth] Fallback mode (sine waves, " << sampleRate << " Hz)" << std::endl;
    return true;
}

// ─────────────────────────── SF2 helpers ─────────────────────────

double SFSynthesizer::timecentsToSeconds(int tc) {
    if (tc == -12000) return 0.0;
    return std::pow(2.0, (double)tc / 1200.0);
}

double SFSynthesizer::attenuateDb(double db) {
    return std::pow(10.0, -db / 20.0);
}

// ─────────────────────────── preset zone builder ─────────────────
// Build ResolvedZone list for a preset.  Each zone is the product
// of preset generators applied on top of instrument generators.

void SFSynthesizer::buildPresetZones(int channel) {
    if (channel < 0 || channel >= 16) return;
    m_channelZones[channel].clear();
    if (!m_sf2) return;

    const auto& ch = m_channels[channel];
    int pIdx = m_sf2->findPreset(ch.bank, ch.program);
    if (pIdx < 0) pIdx = m_sf2->findPreset(0, ch.program);
    if (pIdx < 0) return;

    const auto& presets = m_sf2->presets();
    const auto& pBags = m_sf2->presetBags();
    const auto& pGens = m_sf2->presetGenerators();
    const auto& instruments = m_sf2->instruments();
    const auto& iBags = m_sf2->instBags();
    const auto& iGens = m_sf2->instGenerators();
    const auto& samples = m_sf2->samples();

    size_t bagStart = presets[pIdx].presetBagNdx;
    size_t bagEnd = (pIdx + 1 < (int)presets.size()) ?
                    presets[pIdx + 1].presetBagNdx : pBags.size();
    int pBank = presets[pIdx].bank;

    for (size_t b = bagStart; b < bagEnd; b++) {
        size_t genEnd = (b + 1 < pBags.size()) ? pBags[b + 1].genNdx : pGens.size();

        // ── preset bag generators ──
        int instIdx = -1;
        int pkLow = -1, pkHigh = -1, pvLow = -1, pvHigh = -1;
        double pAtten = 0, pPan = 0, pRoot = -1, pTune = 0;
        double pAtk = 0, pDec = 0, pSus = 1.0, pRel = 0, pFc = -1, pQ = -1;

        for (size_t g = pBags[b].genNdx; g < genEnd; g++) {
            const auto& gen = pGens[g];
            int16_t a = gen.amount;
            switch (gen.oper) {
                case 5: pkLow = a & 0x7F; pkHigh = (a >> 8) & 0x7F; break;
                case 6: pvLow = a & 0x7F; pvHigh = (a >> 8) & 0x7F; break;
                case 10: pAtten = a / 10.0; break;
                case 17: pPan = a / 1000.0; break;
                case 26: pRoot = a; break;
                case 24: pTune = a / 100.0; break;
                case 34: pAtk = timecentsToSeconds(a); break;
                case 36: pDec = timecentsToSeconds(a); break;
                case 37: pSus = 1.0 - a / 1000.0; break;
                case 38: pRel = timecentsToSeconds(a); break;
                case 41: instIdx = a; break;
                case 43: pFc = std::pow(2.0, a / 1200.0) * 8.176; break;
                case 44: pQ = a / 10.0; break;
                default: break;
            }
        }
        if (instIdx < 0 || instIdx >= (int)instruments.size()) continue;

        // ── instrument generators (resolve zone) ──
        size_t ibStart = instruments[instIdx].instBagNdx;
        size_t ibEnd = (instIdx + 1 < (int)instruments.size()) ?
                       instruments[instIdx + 1].instBagNdx : iBags.size();

        // First bag is default generators; inherit from it.
        ResolvedZone def{};
        for (size_t ig = iBags[ibStart].genNdx;
             ig < ((ibStart + 1 < iBags.size()) ? iBags[ibStart + 1].genNdx : iGens.size()); ig++) {
            const auto& gen = iGens[ig];
            int16_t a = gen.amount;
            switch (gen.oper) {
                case 5: def.keyLow = a & 0x7F; def.keyHigh = (a >> 8) & 0x7F; break;
                case 6: def.velLow = a & 0x7F; def.velHigh = (a >> 8) & 0x7F; break;
                case 10: def.attenuation = a / 10.0; break;
                case 17: def.pan = a / 1000.0; break;
                case 24: def.fineTune = a / 100.0; break;
                case 26: def.rootKey = a; break;
                case 34: def.attack = timecentsToSeconds(a); break;
                case 36: def.decay = timecentsToSeconds(a); break;
                case 37: def.sustain = 1.0 - a / 1000.0; break;
                case 38: def.release = timecentsToSeconds(a); break;
                case 43: def.filterFc = std::pow(2.0, a / 1200.0) * 8.176; break;
                case 44: def.filterQ = a / 10.0; break;
                case 53: def.sampleIndex = a; break;
                default: break;
            }
        }

        for (size_t ib = ibStart + 1; ib < ibEnd; ib++) {
            size_t igEnd = (ib + 1 < iBags.size()) ? iBags[ib + 1].genNdx : iGens.size();

            ResolvedZone z = def;

            for (size_t ig = iBags[ib].genNdx; ig < igEnd; ig++) {
                const auto& gen = iGens[ig];
                int16_t a = gen.amount;
                switch (gen.oper) {
                    case 5: z.keyLow = a & 0x7F; z.keyHigh = (a >> 8) & 0x7F; break;
                    case 6: z.velLow = a & 0x7F; z.velHigh = (a >> 8) & 0x7F; break;
                    case 10: z.attenuation = a / 10.0; break;
                    case 17: z.pan = a / 1000.0; break;
                    case 24: z.fineTune = a / 100.0; break;
                    case 26: z.rootKey = a; break;
                    case 34: z.attack = timecentsToSeconds(a); break;
                    case 36: z.decay = timecentsToSeconds(a); break;
                    case 37: z.sustain = 1.0 - a / 1000.0; break;
                    case 38: z.release = timecentsToSeconds(a); break;
                    case 43: z.filterFc = std::pow(2.0, a / 1200.0) * 8.176; break;
                    case 44: z.filterQ = a / 10.0; break;
                    case 53: z.sampleIndex = a; break;
                    default: break;
                }
            }

            // Apply preset overrides
            if (pkLow >= 0) { z.keyLow = pkLow; z.keyHigh = pkHigh; }
            if (pvLow >= 0) { z.velLow = pvLow; z.velHigh = pvHigh; }
            z.attenuation += pAtten;
            z.pan += pPan;
            if (pRoot >= 0) z.rootKey = pRoot;
            z.fineTune += pTune;
            if (pAtk > 0) z.attack = pAtk;
            if (pDec > 0) z.decay = pDec;
            if (pSus < 1.0) z.sustain = pSus;
            if (pRel > 0) z.release = pRel;
            if (pFc >= 0) z.filterFc = pFc;
            if (pQ >= 0) z.filterQ = pQ;

            if (z.sampleIndex < 0 || z.sampleIndex >= (int)samples.size()) continue;

            const auto& s = samples[z.sampleIndex];

            // ドラムバンク: keyRange未指定の場合、サンプル名からGMドラム番号を推定
            bool zoneIsDrum = (pBank == 128);
            if (zoneIsDrum && z.keyLow == 0 && z.keyHigh == 127) {
                std::string sname = s.name;
                // 末尾の数字を抽出（例: "D_Hybrid Kick1_36" → 36）
                int drumNote = -1;
                for (int i = (int)sname.size() - 1; i >= 0; i--) {
                    if (sname[i] >= '0' && sname[i] <= '9') {
                        int num = 0;
                        int mult = 1;
                        for (int j = i; j >= 0 && sname[j] >= '0' && sname[j] <= '9'; j--) {
                            num += (sname[j] - '0') * mult;
                            mult *= 10;
                        }
                        drumNote = num;
                        break;
                    }
                }
                if (drumNote >= 35 && drumNote <= 81) {
                    z.keyLow = drumNote;
                    z.keyHigh = drumNote;
                }
            }

            z.sampleRate = s.sampleRate;
            if (z.rootKey < 0) z.rootKey = (s.originalKey > 0) ? s.originalKey : 60.0;
            z.rootKey += z.fineTune / 100.0;

            z.sampleStart = s.start;
            z.sampleEnd = s.end;
            z.loopStart = s.startLoop;
            z.loopEnd = s.endLoop;
            z.loop = (s.sampleType & 0x1000) != 0;

            m_channelZones[channel].push_back(z);
        }
    }
}

// ─────────────────────────── note resolution ─────────────────────

bool SFSynthesizer::resolveNote(int channel, int note, int velocity, ResolvedZone& out) {
    bool isDrum = (m_channels[channel].bank == 128);
    int bestRange = isDrum ? 999 : 0;
    int bestVelDist = 999;
    bool found = false;

    for (const auto& z : m_channelZones[channel]) {
        if (note < z.keyLow || note > z.keyHigh) continue;
        if (velocity < z.velLow || velocity > z.velHigh) continue;
        if (isDrum) {
            // ドラム: 最も狭いkeyRangeを優先
            int range = z.keyHigh - z.keyLow;
            if (range < bestRange) {
                bestRange = range;
                out = z;
                found = true;
            }
        } else {
            // メロディ: ベロシティ範囲の中心に最も近いゾーンを選択
            int velCenter = (z.velLow + z.velHigh) / 2;
            int velDist = std::abs(velocity - velCenter);
            if (velDist < bestVelDist) {
                bestVelDist = velDist;
                out = z;
                found = true;
            }
        }
    }
    return found;
}

void SFSynthesizer::startVoice(const ResolvedZone& zone, int channel, int note, int velocity) {
    // Find a free voice
    for (auto& v : m_voices) {
        if (!v.active) {
            v.active = true;
            v.releasing = false;
            v.channel = channel;
            v.note = note;
            v.velocity = velocity;
            v.sampleIndex = zone.sampleIndex;
            v.sampleStart = zone.sampleStart;
            v.sampleEnd = zone.sampleEnd;
            v.loopStart = zone.loopStart;
            v.loopEnd = zone.loopEnd;
            v.loop = zone.loop;
            v.sampleRate = zone.sampleRate;
            v.position = 0.0;

            // Pitch ratio
            double noteFreq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            double rootFreq = 440.0 * std::pow(2.0, (zone.rootKey - 69) / 12.0);
            double pitchBend = getEffectivePitchBend(channel);
            double tuning = getEffectiveTuning(channel);
            bool isDrumChannel = (m_channels[channel].bank == 128);
            if (isDrumChannel) {
                // ドラム: サンプルを自然ピッチで再生（ノート番号は楽器選択のみ）
                v.pitchRatio = zone.sampleRate / (double)m_sampleRate;
            } else {
                v.pitchRatio = (noteFreq / rootFreq) * std::pow(2.0, (pitchBend + tuning) / 12.0)
                               * (zone.sampleRate / (double)m_sampleRate);
            }

            // Amplitude
            double velAmp = (double)velocity / 127.0;
            double attenLin = attenuateDb(zone.attenuation);
            v.amplitude = velAmp * attenLin;

            // Pan
            double chPan = (m_channels[channel].pan - 64) / 64.0;
            v.pan = std::clamp(zone.pan + chPan, -1.0, 1.0);

            // Envelope: timecents → rate per sample
            double aTime = std::max(zone.attack, 0.001);
            double dTime = std::max(zone.decay, 0.001);
            double rTime = std::max(zone.release, 0.001);
            v.attackRate = 1.0 / (aTime * m_sampleRate);
            v.decayRate = (1.0 - zone.sustain) / (dTime * m_sampleRate);
            v.sustainLevel = std::clamp(zone.sustain, 0.0, 1.0);
            v.releaseRate = 1.0 / (rTime * m_sampleRate);
            v.envLevel = 0.0;
            v.releaseLevel = 1.0;

            // Filter
            v.filterFc = std::clamp(zone.filterFc, 20.0, m_sampleRate * 0.49);
            v.filterQ = std::max(0.5, zone.filterQ);
            v.filterState = 0.0;

            // Vibrato depth from modulation (CC1)
            v.vibratoDepth = m_channels[channel].modulation / 127.0 * 2.0; // max 2 semitones

            return;
        }
    }
    // No free voice — steal oldest (first active)
    // (In practice MAX_VOICES=256 is plenty)
}

// ─────────────────────────── MIDI events ─────────────────────────

void SFSynthesizer::noteOn(int channel, int note, int velocity) {
    if (m_fallbackMode) {
        for (auto& v : m_voices) {
            if (!v.active) {
                v.active = true;
                v.releasing = false;
                v.channel = channel;
                v.note = note;
                v.velocity = velocity;
                v.position = 0.0;
                v.pitchRatio = 440.0 * std::pow(2.0, (note - 69) / 12.0) / (double)m_sampleRate;
                v.amplitude = (double)velocity / 127.0 * 0.3;
                double chPan = (m_channels[channel].pan - 64) / 64.0;
                v.pan = std::clamp(chPan, -1.0, 1.0);
                v.attackRate = 1.0 / (0.002 * m_sampleRate);
                v.decayRate = 0.3 / (0.1 * m_sampleRate);
                v.sustainLevel = 0.7;
                v.releaseRate = 1.0 / (0.05 * m_sampleRate);
                v.envLevel = 0.0;
                v.releaseLevel = 1.0;
                v.sampleIndex = -1;
                return;
            }
        }
        return;
    }

    ResolvedZone zone;
    if (resolveNote(channel, note, velocity, zone)) {
        startVoice(zone, channel, note, velocity);
    }
}

void SFSynthesizer::noteOff(int channel, int note) {
    for (auto& v : m_voices) {
        if (v.active && v.channel == channel && v.note == note && !v.releasing) {
            if (m_channels[channel].sustain >= 64) {
                // サステインペダルON: ボイスを保持
                v.held = true;
            } else {
                // サステインOFF: リリースへ
                v.releasing = true;
                v.releaseLevel = v.envLevel;
            }
        }
    }
}

void SFSynthesizer::programChange(int channel, int program, int bank) {
    if (channel >= 16) return;
    m_channels[channel].program = program;
    m_channels[channel].bank = bank;
    if (!m_fallbackMode && m_sf2) {
        buildPresetZones(channel);
    }
}

void SFSynthesizer::controlChange(int channel, int cc, int value) {
    if (channel >= 16) return;
    auto& ch = m_channels[channel];
    switch (cc) {
        case 0: ch.bank = value; break;
        case 1: ch.modulation = value; break;
        case 2: ch.breath = value; break;
        case 4: ch.foot = value; break;
        case 7: ch.volume = value; break;
        case 10: ch.pan = value; break;
        case 11: ch.expression = value; break;
        case 32: break; // Bank Select LSB
        case 64: {
            int prevSustain = ch.sustain;
            ch.sustain = value;
            // サステインOFF: 保持中のボイスをリリース
            if (prevSustain >= 64 && value < 64) {
                for (auto& v : m_voices) {
                    if (v.active && v.channel == channel && v.held) {
                        v.held = false;
                        v.releasing = true;
                        v.releaseLevel = v.envLevel;
                    }
                }
            }
            break;
        }
        case 65: break; // Portamento On/Off (未実装)
        case 91: ch.reverb = value; break;
        case 93: ch.chorus = value; break;
        case 94: ch.delay = value; break;
        case 98: ch.rpnLSB = value; break;  // NRPN LSB
        case 99: ch.rpnMSB = value; break;  // NRPN MSB
        case 100: ch.rpnLSB = value; break; // RPN LSB
        case 101: ch.rpnMSB = value; break; // RPN MSB
        case 106: {
            ch.rpnValue = value; // Data Entry MSB
            // RPN処理
            if (ch.rpnMSB == 0 && ch.rpnLSB == 0) {
                // RPN 0: Pitch Bend Sensitivity (0-24半音)
                ch.pitchBendRange = std::clamp(value, 0, 24);
            } else if (ch.rpnMSB == 0 && ch.rpnLSB == 1) {
                // RPN 1: Channel Fine Tuning (0-16383, 中心=8192=0 cents)
                // ピッチベンドに反映: fineTuning = (value - 8192) / 8192 * 100 cents
                ch.fineTune = (value - 64) * 100.0 / 64.0; // ±100 cents
            } else if (ch.rpnMSB == 0 && ch.rpnLSB == 2) {
                // RPN 2: Channel Coarse Tuning (0-127, 中心=64=0半音)
                ch.coarseTune = value - 64; // ±64半音
            }
            break;
        }
        case 38: break; // Data Entry LSB
        case 96: break; // Data Increment
        case 97: break; // Data Decrement
        case 120: // All Sound Off
            for (auto& v : m_voices) {
                if (v.active && v.channel == channel) {
                    v.active = false;
                }
            }
            break;
        case 121: // Reset All Controllers
            ch.volume = 100; ch.expression = 127; ch.pan = 64;
            ch.pitchBend = 8192; ch.pitchBendRange = 2;
            ch.modulation = 0; ch.sustain = 0;
            ch.reverb = 0; ch.chorus = 0; ch.delay = 0;
            ch.rpnLSB = 127; ch.rpnMSB = 127; ch.rpnValue = 0;
            break;
        case 123: // All Notes Off
            for (auto& v : m_voices) {
                if (v.active && v.channel == channel) {
                    v.held = false;
                    v.releasing = true;
                    v.releaseLevel = v.envLevel;
                }
            }
            break;
        default: break;
    }
}

void SFSynthesizer::pitchBend(int channel, int value) {
    if (channel < 16) m_channels[channel].pitchBend = value;
}

double SFSynthesizer::getEffectivePitchBend(int channel) {
    if (channel >= 16) return 0;
    const auto& ch = m_channels[channel];
    double bend = (ch.pitchBend - 8192) / 8192.0;
    return bend * ch.pitchBendRange;
}

double SFSynthesizer::getEffectiveTuning(int channel) {
    if (channel >= 16) return 0;
    const auto& ch = m_channels[channel];
    return ch.fineTune / 100.0 + ch.coarseTune; // semitones
}

// ─────────────────────────── per-sample processing ───────────────

void SFSynthesizer::processVoice(SF2Voice& v, float* left, float* right, int count) {
    if (!v.active) return;

    bool isDrumChannel = (m_channels[v.channel].bank == 128);
    double vibratoPhase = v.position; // for vibrato LFO

    // ── Envelope ──
    for (int i = 0; i < count; i++) {
        if (!v.active) break;

        // Update envelope
        switch (1) { // unified: check releasing flag
        default:
            if (v.releasing) {
                v.envLevel -= v.releaseRate;
                if (v.envLevel <= 0.0) { v.envLevel = 0.0; v.active = false; break; }
            } else {
                if (v.envLevel < 1.0) {
                    v.envLevel += v.attackRate;
                    if (v.envLevel >= 1.0) { v.envLevel = 1.0; }
                } else if (v.envLevel > v.sustainLevel) {
                    v.envLevel -= v.decayRate;
                    if (v.envLevel < v.sustainLevel) v.envLevel = v.sustainLevel;
                }
            }
        }

        if (!v.active) break;

        // Sample position
        double absPos = v.position + v.sampleStart;
        int idx = (int)absPos;

        // Loop / bounds check
        if (v.loop && v.loopEnd > v.loopStart) {
            uint32_t loopLen = v.loopEnd - v.loopStart;
            if (idx >= (int)v.loopEnd) {
                double offsetInLoop = std::fmod(absPos - v.loopStart, (double)loopLen);
                if (offsetInLoop < 0) offsetInLoop += loopLen;
                absPos = v.loopStart + offsetInLoop;
                idx = (int)absPos;
            }
        } else if (idx >= (int)v.sampleEnd) {
            v.active = false;
            break;
        }
        if (idx < (int)v.sampleStart) { idx = (int)v.sampleStart; v.position = 0; }

        // Linear interpolation
        double frac = absPos - std::floor(absPos);
        int idx1 = idx + 1;
        if (idx1 >= (int)v.sampleEnd) idx1 = idx;

        const int16_t* sd = m_sf2->sampleData();
        size_t sdSize = m_sf2->sampleDataSize();
        double s0 = (idx >= 0 && idx < (int)sdSize) ? sd[idx] / 32768.0 : 0.0;
        double s1 = (idx1 >= 0 && idx1 < (int)sdSize) ? sd[idx1] / 32768.0 : 0.0;
        double sample = s0 + frac * (s1 - s0);

        // Low-pass filter (simple 1-pole)
        if (v.filterFc < m_sampleRate * 0.45) {
            double f = 2.0 * M_PI * v.filterFc / m_sampleRate;
            double a = std::exp(-f);
            double b = 1.0 - a;
            v.filterState = a * v.filterState + b * sample;
            sample = v.filterState;
        }

        // Amplitude
        double envAmp = v.envLevel * v.amplitude;
        double chVol = m_channels[v.channel].volume / 127.0;
        double chExpr = m_channels[v.channel].expression / 127.0;
        double breathAmp = 0.5 + 0.5 * m_channels[v.channel].breath / 127.0; // 50-100%
        double footAmp = 0.5 + 0.5 * m_channels[v.channel].foot / 127.0;     // 50-100%
        double vol = envAmp * chVol * chExpr * breathAmp * footAmp;

        // Pan
        double panL = std::sqrt(0.5 * (1.0 - v.pan));
        double panR = std::sqrt(0.5 * (1.0 + v.pan));

        double outL = sample * vol * panL;
        double outR = sample * vol * panR;

        if (std::isfinite(outL)) left[i] += (float)outL;
        if (std::isfinite(outR)) right[i] += (float)outR;

        // Advance
        double vibratoShift = 0.0;
        if (v.vibratoDepth > 0.0 && !isDrumChannel) {
            vibratoShift = std::sin(v.position * 0.02) * v.vibratoDepth; // ~5Hz LFO
        }
        v.position += v.pitchRatio * std::pow(2.0, vibratoShift / 12.0);
    }
}

// ─────────────────────────── fallback (sine) ─────────────────────

static void processVoiceFallback(SF2Voice& v, float* left, float* right, int count, int sampleRate) {
    for (int i = 0; i < count; i++) {
        if (!v.active) break;

        if (v.releasing) {
            v.envLevel -= v.releaseRate;
            if (v.envLevel <= 0) { v.active = false; break; }
        } else {
            if (v.envLevel < 1.0) {
                v.envLevel += v.attackRate;
                if (v.envLevel > 1.0) v.envLevel = 1.0;
            } else if (v.envLevel > v.sustainLevel) {
                v.envLevel -= v.decayRate;
                if (v.envLevel < v.sustainLevel) v.envLevel = v.sustainLevel;
            }
        }

        double t = v.position;
        double sample = std::sin(2.0 * M_PI * v.pitchRatio * sampleRate * t);
        v.position += v.pitchRatio;

        double vol = v.envLevel * v.amplitude;
        double chVol = 1.0;
        double panL = std::sqrt(0.5 * (1.0 - v.pan));
        double panR = std::sqrt(0.5 * (1.0 + v.pan));

        left[i] += (float)(sample * vol * chVol * panL);
        right[i] += (float)(sample * vol * chVol * panR);
    }
}

// ─────────────────────────── block processing ────────────────────

void SFSynthesizer::processBlock(std::vector<float>& left, std::vector<float>& right, int count) {
    for (auto& v : m_voices) {
        if (!v.active) continue;
        if (m_fallbackMode) {
            processVoiceFallback(v, left.data(), right.data(), count, m_sampleRate);
        } else {
            processVoice(v, left.data(), right.data(), count);
        }
    }
}

// ─────────────────────────── render (standalone) ─────────────────

std::vector<float> SFSynthesizer::render(double seconds) {
    int total = (int)(seconds * m_sampleRate);
    std::vector<float> left(total, 0.0f);
    std::vector<float> right(total, 0.0f);
    const int BS = 1024;
    for (int off = 0; off < total; off += BS) {
        int n = std::min(BS, total - off);
        std::vector<float> bl(n, 0.0f), br(n, 0.0f);
        processBlock(bl, br, n);
        for (int i = 0; i < n; i++) { left[off + i] = bl[i]; right[off + i] = br[i]; }
    }
    return left;
}

// ─────────────────────────── renderToWav ─────────────────────────

void SFSynthesizer::renderToWav(const std::vector<MidiNote>& notes,
                                 const std::string& wavPath,
                                 const ConvertOptions& opts,
                                 const MidiFile& midi) {
    if (notes.empty()) return;

    auto t0 = std::chrono::steady_clock::now();

    int64_t maxTick = 0;
    for (auto& n : notes) maxTick = std::max(maxTick, n.endTime);
    double totalSec = 0;
    for (auto& n : notes) {
        double t = midi.tickToSeconds(n.endTime, n.track) + 1.0;
        totalSec = std::max(totalSec, t);
    }
    int totalSamples = (int)(totalSec * m_sampleRate);

    std::vector<float> left(totalSamples, 0.0f);
    std::vector<float> right(totalSamples, 0.0f);

    std::cout << "  [Render] " << notes.size() << " notes, " << (int)totalSec << " sec..." << std::flush;

    // Reset
    for (auto& v : m_voices) v.active = false;

    // 各チャンネルの初期program/bankを決定
    const auto& expr = midi.expression();
    for (int ch = 0; ch < 16; ch++) {
        int64_t firstNoteTick = INT64_MAX;
        for (auto& n : notes) {
            if (n.channel == ch && n.startTime < firstNoteTick) firstNoteTick = n.startTime;
        }

        int initProgram = 0;
        int initBank = 0;

        // firstNoteTick以前の最新program changeを探す
        for (auto& [t, v] : expr.programChange[ch]) {
            if (t <= firstNoteTick) initProgram = v;
        }
        for (auto& [t, v] : expr.bankSelectMSB[ch]) {
            if (t <= firstNoteTick) initBank = v;
        }

        // GM規約: ch10(0-indexed 9)は常にドラムバンク128
        if (ch == 9) {
            initBank = 128;
        }

        m_channels[ch].program = initProgram;
        m_channels[ch].bank = initBank;
        programChange(ch, m_channels[ch].program, m_channels[ch].bank);
    }

    // チャンネル情報表示
    std::cout << "  [Channels] " << std::endl;
    for (int ch = 0; ch < 16; ch++) {
        int noteCount = 0;
        for (auto& n : notes) if (n.channel == ch) noteCount++;
        if (noteCount == 0) continue;
        const char* drumTag = (ch == 9) ? " [PERC]" : "";
        std::cout << "    Ch " << (ch + 1) << ": program=" << m_channels[ch].program
                  << " bank=" << m_channels[ch].bank
                  << " notes=" << noteCount << drumTag << std::endl;
    }

    // Sort notes by start time
    std::vector<MidiNote> sorted = notes;
    std::sort(sorted.begin(), sorted.end(),
              [](const MidiNote& a, const MidiNote& b) { return a.startTime < b.startTime; });

    struct ActiveNote { const MidiNote* note; bool offDone; };
    std::vector<ActiveNote> active;

    // Track per-channel preset for lazy rebuild
    int channelPresetKey[16] = {};
    for (int c = 0; c < 16; c++) channelPresetKey[c] = -1;

    const int BS = 1024;

    for (int pos = 0; pos < totalSamples; pos += BS) {
        int blockEnd = std::min(pos + BS, totalSamples);
        int blockLen = blockEnd - pos;

        // Apply MIDI expression data (CC, pitch bend, program change) for this block's tick range
        int64_t blockTickStart = 0, blockTickEnd = 0;
        {
            double blockSecStart = (double)pos / m_sampleRate;
            double blockSecEnd = (double)blockEnd / m_sampleRate;
            const auto& tmap = midi.tempoMap();
            auto tickForTime = [&](double sec) -> int64_t {
                int64_t tick = 0;
                double prevTime = 0;
                double prevTick = 0;
                for (auto& [t, bpm] : tmap) {
                    double thisTime = midi.tickToSeconds(t);
                    if (thisTime >= sec) break;
                    tick = t;
                    prevTime = thisTime;
                }
                return tick;
            };
            blockTickStart = tickForTime(blockSecStart);
            blockTickEnd = tickForTime(blockSecEnd);
        }

        const auto& expr = midi.expression();
        for (int ch = 0; ch < 16; ch++) {
            // Bank Select MSB → Program Change の順に適用（MIDI仕様）
            bool needProgramChange = false;
            bool hasBankSelectInBlock = false;
            int newProgram = m_channels[ch].program;
            for (auto& [t, v] : expr.bankSelectMSB[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) { m_channels[ch].bank = v; hasBankSelectInBlock = true; }
            }
            for (auto& [t, v] : expr.programChange[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) {
                    newProgram = v;
                    needProgramChange = true;
                }
            }
            if (needProgramChange) {
                // GM規約: ch10(0-indexed 9)は常にドラムバンク128
                int useBank = (ch == 9) ? 128 : m_channels[ch].bank;
                programChange(ch, newProgram, useBank);
                channelPresetKey[ch] = newProgram * 256 + useBank;
            }
            // Volume
            for (auto& [t, v] : expr.volume[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) controlChange(ch, 7, v);
            }
            // Pan
            for (auto& [t, v] : expr.pan[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) controlChange(ch, 10, v);
            }
            // Expression
            for (auto& [t, v] : expr.expression[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) controlChange(ch, 11, v);
            }
            // Pitch bend
            for (auto& [t, v] : expr.pitchBend[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) pitchBend(ch, v);
            }
            // Pitch bend range
            for (auto& [t, v] : expr.pitchBendRange[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) m_channels[ch].pitchBendRange = v;
            }
            // Sustain
            for (auto& [t, v] : expr.sustain[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) controlChange(ch, 64, v);
            }
        }

        // Note On: start new notes in this block
        for (auto& n : sorted) {
            int noteStart = (int)(midi.tickToSeconds(n.startTime, n.track) * m_sampleRate);
            if (noteStart >= pos && noteStart < blockEnd) {
                // Ensure correct preset loaded for this channel
                int needKey = m_channels[n.channel].program * 256 + m_channels[n.channel].bank;
                if (channelPresetKey[n.channel] != needKey) {
                    programChange(n.channel, m_channels[n.channel].program, m_channels[n.channel].bank);
                    channelPresetKey[n.channel] = needKey;
                }
                int shiftedNote = std::clamp(n.note + opts.pitchShift, 0, 127);
                noteOn(n.channel, shiftedNote, n.velocity);
                active.push_back({&n, false});
            }
        }

        // Note Off: end notes that finished
        for (auto& a : active) {
            if (a.offDone) continue;
            int noteEnd = (int)(midi.tickToSeconds(a.note->endTime, a.note->track) * m_sampleRate);
            if (noteEnd < pos) {
                noteOff(a.note->channel, a.note->note);
                a.offDone = true;
            }
        }
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [](const ActiveNote& a) { return a.offDone; }), active.end());

        // Process audio block
        std::vector<float> bl(blockLen, 0.0f), br(blockLen, 0.0f);
        processBlock(bl, br, blockLen);

        // リバーブ適用
        float maxReverb = 0.0f;
        for (int ch = 0; ch < 16; ch++) {
            maxReverb = std::max(maxReverb, (float)m_channels[ch].reverb / 127.0f);
        }
        if (maxReverb > 0.001f) {
            m_reverb.process(bl.data(), br.data(), blockLen, maxReverb * 0.6f);
        }

        // コーラス適用
        int maxChorus = 0;
        for (int ch = 0; ch < 16; ch++) {
            maxChorus = std::max(maxChorus, m_channels[ch].chorus);
        }
        if (maxChorus > 0) {
            m_chorus.process(bl.data(), br.data(), blockLen, maxChorus);
        }

        // ディレイ適用 (CC94)
        int maxDelay = 0;
        for (int ch = 0; ch < 16; ch++) {
            maxDelay = std::max(maxDelay, m_channels[ch].delay);
        }
        if (maxDelay > 0) {
            float delayMix = maxDelay / 127.0f * 0.4f;  // max 40% wet
            float delayTime = 0.25f + (maxDelay / 127.0f) * 0.5f;  // 250ms - 750ms
            float feedback = 0.2f + (maxDelay / 127.0f) * 0.3f;    // 20-50%
            m_delay.process(bl.data(), br.data(), blockLen, delayTime, feedback, delayMix);
        }

        for (int i = 0; i < blockLen; i++) {
            left[pos + i] = bl[i];
            right[pos + i] = br[i];
        }

        if ((pos / BS) % 50 == 0) {
            std::cout << "\r  [Render] " << (int)((double)pos / totalSamples * 100) << "%   " << std::flush;
        }
    }
    std::cout << std::endl;

    auto t1 = std::chrono::steady_clock::now();
    std::cout << "  [Render] Done (" << std::chrono::duration<double>(t1 - t0).count() << " sec)" << std::endl;

    // Normalize to peak
    float peak = 1e-6f;
    for (size_t i = 0; i < left.size(); i++) {
        float lv = std::isfinite(left[i]) ? std::abs(left[i]) : 0.0f;
        float rv = std::isfinite(right[i]) ? std::abs(right[i]) : 0.0f;
        peak = std::max(peak, std::max(lv, rv));
    }
    float gain = (peak > 1e-6f) ? (0.95f / peak) : 1.0f;
    for (size_t i = 0; i < left.size(); i++) {
        left[i] = std::isfinite(left[i]) ? left[i] * gain : 0.0f;
        right[i] = std::isfinite(right[i]) ? right[i] * gain : 0.0f;
    }

    WavWriter::write(wavPath, left, right, m_sampleRate);
}

void SFSynthesizer::renderToWavPerChannel(const std::vector<MidiNote>& notes,
                                           const std::string& baseName,
                                           const std::string& outputDir,
                                           const MidiFile& midi,
                                           int pitchShift) {
    if (notes.empty()) return;

    auto t0 = std::chrono::steady_clock::now();

    // baseNameをサニタイズ（ファイル名に問題のある文字を_に変換）
    std::string safeBaseName;
    for (auto& c : baseName) {
        if (c == '(' || c == ')') continue;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            safeBaseName += '_';
        else
            safeBaseName += c;
    }

    // 使用されているチャンネル一覧を取得
    std::vector<int> usedChannels;
    for (int ch = 0; ch < 16; ch++) {
        for (auto& n : notes) {
            if (n.channel == ch) { usedChannels.push_back(ch); break; }
        }
    }

    // 最大テンポリ長を計算
    double totalSec = 0;
    for (auto& n : notes) {
        double t = midi.tickToSeconds(n.endTime, n.track) + 1.0;
        totalSec = std::max(totalSec, t);
    }
    int totalSamples = (int)(totalSec * m_sampleRate);
    const char* gmNames[] = {
        "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano",
        "Electric Piano 1","Electric Piano 2","Harpsichord","Clavi",
        "Celesta","Glockenspiel","Music Box","Vibraphone",
        "Marimba","Xylophone","Tubular Bells","Dulcimer",
        "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ",
        "Reed Organ","Accordion","Harmonica","Tango Accordion",
        "Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)",
        "Electric Guitar (muted)","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
        "Acoustic Bass","Electric Bass (finger)","Electric Bass (pick)","Fretless Bass",
        "Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
        "Violin","Viola","Cello","Contrabass",
        "Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
        "String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2",
        "Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
        "Trumpet","Trombone","Tuba","Muted Trumpet",
        "French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
        "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax",
        "Oboe","English Horn","Bassoon","Clarinet",
        "Piccolo","Flute","Recorder","Pan Flute",
        "Blown Bottle","Shakuhachi","Whistle","Ocarina",
        "Lead 1 (square)","Lead 2 (sawtooth)","Lead 3 (calliope lead)","Lead 4 (chiff lead)",
        "Lead 5 (charang)","Lead 6 (voice)","Lead 7 (fifths)","Lead 8 (bass + lead)",
        "Pad 1 (new age)","Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)",
        "Pad 5 (bowed)","Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)",
        "FX 1 (rain)","FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)",
        "FX 5 (brightness)","FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)",
        "Sitar","Banjo","Shamisen","Koto",
        "Kalimba","Bagpipe","Fiddle","Shanai",
        "Tinkle Bell","Agogo","Steel Drums","Woodblock",
        "Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
        "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet",
        "Telephone Ring","Helicopter","Applause","Gunshot"
    };

    std::cout << "  [Channel Split] " << usedChannels.size() << " channels, "
              << (int)totalSec << " sec..." << std::endl;

    const auto& expr = midi.expression();

    for (int ch : usedChannels) {
        int noteCount = 0;
        for (auto& n : notes) if (n.channel == ch) noteCount++;

        // このチャンネルのprogram/bankをMIDI expressionから取得
        int chProgram = 0;
        int chBank = 0;
        int64_t firstNoteTick = INT64_MAX;
        for (auto& n : notes) if (n.channel == ch && n.startTime < firstNoteTick) firstNoteTick = n.startTime;
        for (auto& [t, v] : expr.programChange[ch]) if (t <= firstNoteTick) chProgram = v;
        for (auto& [t, v] : expr.bankSelectMSB[ch]) if (t <= firstNoteTick) chBank = v;
        // GM規約: ch10(0-indexed 9)は常にドラムバンク128
        if (ch == 9) chBank = 128;

        // このチャンネルのノートのみを抽出
        std::vector<MidiNote> chNotes;
        for (auto& n : notes) if (n.channel == ch) chNotes.push_back(n);

        // チャンネルごとにシンセを再初期化
        SFSynthesizer chSynth;
        chSynth.init(*m_sf2, m_sampleRate);

        // program変更を適用
        chSynth.programChange(0, chProgram, chBank);

        // チャンネル番号を0にマッピングしてレンダリング
        std::vector<MidiNote> mapped = chNotes;
        for (auto& n : mapped) n.channel = 0;

        std::vector<float> left(totalSamples, 0.0f);
        std::vector<float> right(totalSamples, 0.0f);

        // レンダリング
        const int BS = 1024;
        for (int pos = 0; pos < totalSamples; pos += BS) {
            int blockEnd = std::min(pos + BS, totalSamples);
            int blockLen = blockEnd - pos;

            for (auto& n : mapped) {
                int noteStart = (int)(midi.tickToSeconds(n.startTime, n.track) * m_sampleRate);
                if (noteStart >= pos && noteStart < blockEnd) {
                    chSynth.noteOn(n.channel, std::clamp(n.note + pitchShift, 0, 127), n.velocity);
                }
            }

            // noteOff
            for (auto& n : mapped) {
                int noteEnd = (int)(midi.tickToSeconds(n.endTime, n.track) * m_sampleRate);
                if (noteEnd >= pos && noteEnd < blockEnd) {
                    chSynth.noteOff(n.channel, n.note);
                }
            }

            std::vector<float> bl(blockLen, 0.0f), br(blockLen, 0.0f);
            chSynth.processBlock(bl, br, blockLen);

            for (int i = 0; i < blockLen; i++) {
                left[pos + i] = bl[i];
                right[pos + i] = br[i];
            }
        }

        // 正規化
        float peak = 1e-6f;
        for (size_t i = 0; i < left.size(); i++) {
            float lv = std::isfinite(left[i]) ? std::abs(left[i]) : 0.0f;
            float rv = std::isfinite(right[i]) ? std::abs(right[i]) : 0.0f;
            peak = std::max(peak, std::max(lv, rv));
        }
        float scale = (peak > 1e-6f) ? (0.95f / peak) : 1.0f;
        for (size_t i = 0; i < left.size(); i++) {
            left[i] = std::isfinite(left[i]) ? left[i] * scale : 0.0f;
            right[i] = std::isfinite(right[i]) ? right[i] * scale : 0.0f;
        }

        // ファイル名: DAW読み込み用（チャンネル番号+楽器名+プログラム番号）
        std::string chName;
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%02d_", ch + 1);
            chName = buf;
            if (chBank == 128) {
                // GMドラムバンクのセット名
                const char* drumKits[] = {
                    "Standard_Kit","Room_Kit","Power_Kit","Electric_Kit",
                    "TR-808","Dance_Kit","Soundtrack_Kit","80s_Drums",
                    "CR-78","Lo-Fi","Jazz_Kit","Brush_Kit",
                    "Orchestra_Kit","Blast_Kit","SGMTX","KGMTX"
                };
                int kitIdx = std::clamp(chProgram, 0, 15);
                chName += drumKits[kitIdx];
            } else if (chProgram >= 0 && chProgram < 128) {
                chName += std::string(gmNames[chProgram]);
            } else {
                chName += "Program" + std::to_string(chProgram);
            }
            chName += "_P" + std::to_string(chProgram);
            if (chBank != 0) {
                chName += "_B" + std::to_string(chBank);
            }
            // トラブルになる文字をアンダースコアに変換
            std::string tmp;
            for (auto& c : chName) {
                if (c == '(' || c == ')') continue;
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                    tmp += '_';
                else
                    tmp += c;
            }
            chName = tmp;
        }
        std::string outPath = outputDir + "/" + safeBaseName + "_" + chName + ".wav";

        WavWriter::write(outPath, left, right, m_sampleRate);

        std::cout << "    Ch " << (ch + 1) << ": program=" << chProgram
                  << " bank=" << chBank
                  << " notes=" << noteCount << " -> " << safeBaseName << "_" << chName << ".wav" << std::endl;
    }

    auto t1 = std::chrono::steady_clock::now();
    std::cout << "  [Channel Split] Done (" << std::chrono::duration<double>(t1 - t0).count()
              << " sec)" << std::endl;
}

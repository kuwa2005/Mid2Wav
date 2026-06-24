#include "sf_synth.h"
#include "converter.h"
#include "wav_writer.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>
#include <tuple>
#include <iterator>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// GM/GS: Ch10 (index 9) defaults to drum bank 128 unless sysPartMode explicitly sets Melody (0).
int effectiveBankMSB(int ch, int bankMSB, int64_t tick, const MidiExpression& expr) {
    if (ch != 9) return bankMSB;
    if (expr.getValueAtTick(expr.sysPartMode[ch], tick, -1) == 0) return bankMSB;
    return 128;
}

} // namespace

// ─────────────────────────── utilities ───────────────────────────

// ─────────────────────────── init ───────────────────────────────

bool SFSynthesizer::init(const SoundFont& sf2, int sampleRate) {
    m_sf2 = &sf2;
    m_fallbackMode = false;
    m_sampleRate = sampleRate;
    m_channels.resize(16);
    m_voices.clear();
    m_voices.reserve(1024);
    m_reverb.init(sampleRate);
    m_chorus.init(sampleRate);
    m_delay.init(sampleRate);
    std::cout << "[Synth] Initialized (dynamic voices, " << sampleRate << " Hz)" << std::endl;
    return true;
}

bool SFSynthesizer::initFallback(int sampleRate) {
    m_sf2 = nullptr;
    m_fallbackMode = true;
    m_sampleRate = sampleRate;
    m_channels.resize(16);
    m_voices.clear();
    m_voices.reserve(1024);
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
    // 14bit bank: MSB>=128 なら直接値、それ以外は (MSB << 7) | LSB
    int fullBank = (ch.bank >= 128) ? ch.bank : ((ch.bank & 0x7F) << 7) | (ch.bankLSB & 0x7F);
    int pIdx = m_sf2->findPreset(fullBank, ch.program);
    if (pIdx < 0) pIdx = m_sf2->findPreset(ch.bank, ch.program); // fallback: MSB only
    if (pIdx < 0) pIdx = m_sf2->findPreset(0, ch.program); // fallback: bank 0
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

    struct GenState {
        ResolvedZone zone;
        int instIdx = -1;
        int64_t startOffset = 0;
        int64_t endOffset = 0;
        int64_t startLoopOffset = 0;
        int64_t endLoopOffset = 0;
        bool hasSample = false;
        bool hasInstrument = false;
        bool hasKeyRange = false;
        bool hasVelRange = false;
        bool hasRootKey = false;
        bool hasAttack = false;
        bool hasDecay = false;
        bool hasSustain = false;
        bool hasRelease = false;
        bool hasFilterQ = false;
        bool hasLoopMode = false;
    };

    auto applyGen = [&](GenState& st, const SF2Generator& gen) {
        int16_t a = gen.amount;
        uint16_t raw = (uint16_t)gen.amount;
        switch (gen.oper) {
            case 0: st.startOffset += a; break;
            case 1: st.endOffset += a; break;
            case 2: st.startLoopOffset += a; break;
            case 3: st.endLoopOffset += a; break;
            case 4: st.startOffset += (int64_t)a * 32768; break;
            case 5: st.zone.modLfoToPitch = a; break;      // SF2 gen 5: modLfoToPitch (cents)
            case 6: st.zone.vibLfoToPitch = a; break;      // SF2 gen 6: vibLfoToPitch (cents)
            case 7: st.zone.modEnvToPitch = a; break;      // SF2 gen 7: modEnvToPitch (cents)
            case 8: st.zone.filterFc = std::pow(2.0, a / 1200.0) * 8.176; st.zone.filterActive = true; break;
            case 9: st.zone.filterQ = a / 10.0; st.hasFilterQ = true; break;
            case 12: st.endOffset += (int64_t)a * 32768; break;
            case 10: st.zone.modLfoToFilterFc = a; break;   // SF2 gen 10: modLfoToFilterFc (cents)
            case 11: st.zone.modEnvToFilterFc = a; break;   // SF2 gen 11: modEnvToFilterFc (cents)
            case 13: st.zone.modLfoToVolume = a; break;     // SF2 gen 13: modLfoToVolume (centibels)
            case 17: st.zone.pan += a / 500.0; break;
            case 34: st.zone.attack = timecentsToSeconds(a); st.hasAttack = true; break;
            case 36: st.zone.decay = timecentsToSeconds(a); st.hasDecay = true; break;
            case 37: st.zone.sustain = attenuateDb(a / 10.0); st.hasSustain = true; break;
            case 38: st.zone.release = timecentsToSeconds(a); st.hasRelease = true; break;
            case 41: st.instIdx = a; st.hasInstrument = true; break;
            case 43: st.zone.keyLow = raw & 0xFF; st.zone.keyHigh = (raw >> 8) & 0xFF; st.hasKeyRange = true; break;
            case 44: st.zone.velLow = raw & 0xFF; st.zone.velHigh = (raw >> 8) & 0xFF; st.hasVelRange = true; break;
            case 45: st.startLoopOffset += (int64_t)a * 32768; break;
            case 48: st.zone.attenuation += a / 10.0; break;
            case 50: st.endLoopOffset += (int64_t)a * 32768; break;
            case 51: st.zone.coarseTune += a; break;
            case 52: st.zone.fineTune += a; break;
            case 53: st.zone.sampleIndex = a; st.hasSample = true; break;
            case 54: st.zone.loopMode = a & 0x3; st.hasLoopMode = true; break;
            case 57: st.zone.exclusiveClass = a; break;
            case 58: st.zone.rootKey = a; st.hasRootKey = true; break;
            // SF2 gen 15: chorusEffectsSend (0.1% units, 0-1000)
            case 15: st.zone.chorusSend = a / 1000.0; break;
            // SF2 gen 16: reverbEffectsSend (0.1% units, 0-1000)
            case 16: st.zone.reverbSend = a / 1000.0; break;
            // SF2 gen 21: delayModLFO (timecents)
            case 21: st.zone.delayModLFO = timecentsToSeconds(a); break;
            // SF2 gen 22: freqModLFO (absolute cents, 0=8.176Hz)
            case 22: st.zone.freqModLFO = std::pow(2.0, a / 1200.0) * 8.176; break;
            case 23: st.zone.delayVibLFO = timecentsToSeconds(a); break;
            // SF2 gen 24: freqVibLFO (absolute cents, 0=8.176Hz)
            case 24: st.zone.freqVibLFO = std::pow(2.0, a / 1200.0) * 8.176; break;
            // SF2 gen 25: delayModEnv (timecents)
            case 25: st.zone.delayModEnv = timecentsToSeconds(a); break;
            // SF2 gen 26: attackModEnv (timecents)
            case 26: st.zone.attackModEnv = timecentsToSeconds(a); break;
            // SF2 gen 27: holdModEnv (timecents)
            case 27: st.zone.holdModEnv = timecentsToSeconds(a); break;
            // SF2 gen 28: decayModEnv (timecents)
            case 28: st.zone.decayModEnv = timecentsToSeconds(a); break;
            // SF2 gen 29: sustainModEnv (-0.1% units, 0=attack peak)
            case 29: st.zone.sustainModEnv = a / 1000.0; break;
            // SF2 gen 30: releaseModEnv (timecents)
            case 30: st.zone.releaseModEnv = timecentsToSeconds(a); break;
            // SF2 gen 31: keynumToModEnvHold (timecents/key)
            case 31: st.zone.keynumToModEnvHold = a / 100.0; break;
            // SF2 gen 32: keynumToModEnvDecay (timecents/key)
            case 32: st.zone.keynumToModEnvDecay = a / 100.0; break;
            // SF2 gen 33: delayVolEnv (timecents)
            case 33: st.zone.delayVolEnv = timecentsToSeconds(a); break;
            // SF2 gen 35: holdVolEnv (timecents)
            case 35: st.zone.holdVolEnv = timecentsToSeconds(a); break;
            // SF2 gen 39: keynumToVolEnvHold (timecents/key)
            case 39: st.zone.keynumToVolEnvHold = a / 100.0; break;
            // SF2 gen 40: keynumToVolEnvDecay (timecents/key)
            case 40: st.zone.keynumToVolEnvDecay = a / 100.0; break;
            // SF2 gen 56: scaleTuning (cents/key, 100=equal temperament)
            case 56: st.zone.scaleTuning = a / 100.0; break;
            default: break;
        }
    };

    auto readBag = [&](const auto& bags, const auto& gens, size_t idx) {
        GenState st;
        if (idx >= bags.size()) return st;
        size_t genEnd = (idx + 1 < bags.size()) ? bags[idx + 1].genNdx : gens.size();
        for (size_t g = bags[idx].genNdx; g < genEnd && g < gens.size(); g++) {
            applyGen(st, gens[g]);
        }
        return st;
    };

    auto mergeState = [](const GenState& a, const GenState& b) {
        GenState r = a;
        if (b.hasKeyRange) {
            if (r.hasKeyRange) {
                // SF2 spec: preset narrows instrument range → intersection
                r.zone.keyLow = std::max(r.zone.keyLow, b.zone.keyLow);
                r.zone.keyHigh = std::min(r.zone.keyHigh, b.zone.keyHigh);
            } else {
                r.zone.keyLow = b.zone.keyLow;
                r.zone.keyHigh = b.zone.keyHigh;
            }
            r.hasKeyRange = true;
        }
        if (b.hasVelRange) {
            if (r.hasVelRange) {
                r.zone.velLow = std::max(r.zone.velLow, b.zone.velLow);
                r.zone.velHigh = std::min(r.zone.velHigh, b.zone.velHigh);
            } else {
                r.zone.velLow = b.zone.velLow;
                r.zone.velHigh = b.zone.velHigh;
            }
            r.hasVelRange = true;
        }
        r.zone.attenuation += b.zone.attenuation;
        r.zone.pan += b.zone.pan;
        if (b.hasRootKey) {
            r.zone.rootKey = b.zone.rootKey;
            r.hasRootKey = true;
        }
        r.zone.fineTune += b.zone.fineTune;
        r.zone.coarseTune += b.zone.coarseTune;
        if (b.hasAttack) { r.zone.attack = b.zone.attack; r.hasAttack = true; }
        if (b.hasDecay) { r.zone.decay = b.zone.decay; r.hasDecay = true; }
        if (b.hasSustain) { r.zone.sustain = b.zone.sustain; r.hasSustain = true; }
        if (b.hasRelease) { r.zone.release = b.zone.release; r.hasRelease = true; }
        if (b.zone.filterActive) {
            r.zone.filterFc = b.zone.filterFc;
            r.zone.filterActive = true;
        }
        if (b.hasFilterQ) { r.zone.filterQ = b.zone.filterQ; r.hasFilterQ = true; }
        if (b.zone.sampleIndex >= 0) r.zone.sampleIndex = b.zone.sampleIndex;
        if (b.hasLoopMode) { r.zone.loopMode = b.zone.loopMode; r.hasLoopMode = true; }
        if (b.zone.exclusiveClass != 0) r.zone.exclusiveClass = b.zone.exclusiveClass;
        if (b.hasInstrument) { r.instIdx = b.instIdx; r.hasInstrument = true; }
        if (b.hasSample) r.hasSample = true;
        // SF2 fields that need merge (not copied by default since GenState defaults to 0)
        if (b.zone.delayVolEnv > 0.0) r.zone.delayVolEnv = b.zone.delayVolEnv;
        if (b.zone.holdVolEnv > 0.0) r.zone.holdVolEnv = b.zone.holdVolEnv;
        if (b.zone.scaleTuning != 1.0) r.zone.scaleTuning = b.zone.scaleTuning;
        if (b.zone.reverbSend != 0.0) r.zone.reverbSend = b.zone.reverbSend;
        if (b.zone.chorusSend != 0.0) r.zone.chorusSend = b.zone.chorusSend;
        if (b.zone.freqVibLFO != 8.176) r.zone.freqVibLFO = b.zone.freqVibLFO;
        if (b.zone.delayVibLFO > 0.0) r.zone.delayVibLFO = b.zone.delayVibLFO;
        if (b.zone.freqModLFO != 8.176) r.zone.freqModLFO = b.zone.freqModLFO;
        if (b.zone.delayModLFO > 0.0) r.zone.delayModLFO = b.zone.delayModLFO;
        if (b.zone.modLfoToPitch != 0.0) r.zone.modLfoToPitch = b.zone.modLfoToPitch;
        if (b.zone.vibLfoToPitch != 0.0) r.zone.vibLfoToPitch = b.zone.vibLfoToPitch;
        if (b.zone.modEnvToPitch != 0.0) r.zone.modEnvToPitch = b.zone.modEnvToPitch;
        if (b.zone.modLfoToFilterFc != 0.0) r.zone.modLfoToFilterFc = b.zone.modLfoToFilterFc;
        if (b.zone.modLfoToVolume != 0.0) r.zone.modLfoToVolume = b.zone.modLfoToVolume;
        if (b.zone.delayModEnv > 0.0) r.zone.delayModEnv = b.zone.delayModEnv;
        if (b.zone.attackModEnv > 0.0) r.zone.attackModEnv = b.zone.attackModEnv;
        if (b.zone.holdModEnv > 0.0) r.zone.holdModEnv = b.zone.holdModEnv;
        if (b.zone.decayModEnv > 0.0) r.zone.decayModEnv = b.zone.decayModEnv;
        if (b.zone.sustainModEnv < 1.0) r.zone.sustainModEnv = b.zone.sustainModEnv;
        if (b.zone.releaseModEnv > 0.0) r.zone.releaseModEnv = b.zone.releaseModEnv;
        if (b.zone.keynumToModEnvHold != 0.0) r.zone.keynumToModEnvHold = b.zone.keynumToModEnvHold;
        if (b.zone.keynumToModEnvDecay != 0.0) r.zone.keynumToModEnvDecay = b.zone.keynumToModEnvDecay;
        if (b.zone.modEnvToFilterFc != 0.0) r.zone.modEnvToFilterFc = b.zone.modEnvToFilterFc;
        if (b.zone.modEnvToVolume != 0.0) r.zone.modEnvToVolume = b.zone.modEnvToVolume;
        if (b.zone.keynumToVolEnvHold != 0.0) r.zone.keynumToVolEnvHold = b.zone.keynumToVolEnvHold;
        if (b.zone.keynumToVolEnvDecay != 0.0) r.zone.keynumToVolEnvDecay = b.zone.keynumToVolEnvDecay;
        r.startOffset += b.startOffset;
        r.endOffset += b.endOffset;
        r.startLoopOffset += b.startLoopOffset;
        r.endLoopOffset += b.endLoopOffset;
        return r;
    };

    int zoneCount = 0;
    GenState presetGlobal;
    for (size_t b = bagStart; b < bagEnd && b < pBags.size(); b++) {
        GenState pLocal = readBag(pBags, pGens, b);
        if (!pLocal.hasInstrument) {
            presetGlobal = mergeState(presetGlobal, pLocal);
            continue;
        }
        GenState pState = mergeState(presetGlobal, pLocal);
        int instIdx = pState.instIdx;
        if (instIdx < 0 || instIdx >= (int)instruments.size()) continue;

        size_t ibStart = instruments[instIdx].instBagNdx;
        size_t ibEnd = (instIdx + 1 < (int)instruments.size()) ?
                       instruments[instIdx + 1].instBagNdx : iBags.size();

        GenState instGlobal;
        for (size_t ib = ibStart; ib < ibEnd && ib < iBags.size(); ib++) {
            GenState iLocal = readBag(iBags, iGens, ib);
            if (!iLocal.hasSample) {
                instGlobal = mergeState(instGlobal, iLocal);
                continue;
            }

            GenState state = mergeState(pState, mergeState(instGlobal, iLocal));
            ResolvedZone z = state.zone;
            if (z.sampleIndex < 0 || z.sampleIndex >= (int)samples.size()) continue;
            if (z.keyLow > z.keyHigh || z.velLow > z.velHigh) continue; // empty intersection → skip

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

            z.sampleRate = s.sampleRate > 0 ? s.sampleRate : 44100;
            if (z.rootKey < 0) z.rootKey = (s.originalKey < 128) ? s.originalKey : 60.0;
            z.rootKey += z.coarseTune + (z.fineTune + s.correction) / 100.0;

            int64_t start = (int64_t)s.start + state.startOffset;
            int64_t end = (int64_t)s.end + state.endOffset;
            int64_t loopStart = (int64_t)s.startLoop + state.startLoopOffset;
            int64_t loopEnd = (int64_t)s.endLoop + state.endLoopOffset;
            int64_t sdSize = (int64_t)m_sf2->sampleDataSize();
            start = std::clamp(start, (int64_t)0, sdSize);
            end = std::clamp(end, start + 1, sdSize);
            loopStart = std::clamp(loopStart, start, end);
            loopEnd = std::clamp(loopEnd, loopStart, end);

            z.sampleStart = (uint32_t)start;
            z.sampleEnd = (uint32_t)end;
            z.loopStart = (uint32_t)loopStart;
            z.loopEnd = (uint32_t)loopEnd;
            z.loop = (z.loopMode == 1 || z.loopMode == 3) && z.loopEnd > z.loopStart + 1;

            // Handle stereo sample pairs (SF2 sampleType: 1=mono, 2=right, 4=left, 5=linked)
            uint16_t sType = s.sampleType & 0xFF; // mask to get type (ignore ROM bits)
            if (sType == 4) { // left sample
                int linkIdx = s.sampleLink;
                if (linkIdx >= 0 && linkIdx < (int)samples.size()) {
                    const auto& linked = samples[linkIdx];
                    z.sampleIndexR = linkIdx;
                    // Linked (right) sample bounds
                    int64_t rStart = (int64_t)linked.start + state.startOffset;
                    int64_t rEnd = (int64_t)linked.end + state.endOffset;
                    int64_t rLoopStart = (int64_t)linked.startLoop + state.startLoopOffset;
                    int64_t rLoopEnd = (int64_t)linked.endLoop + state.endLoopOffset;
                    rStart = std::clamp(rStart, (int64_t)0, sdSize);
                    rEnd = std::clamp(rEnd, rStart + 1, sdSize);
                    rLoopStart = std::clamp(rLoopStart, rStart, rEnd);
                    rLoopEnd = std::clamp(rLoopEnd, rLoopStart, rEnd);
                    z.sampleStartR = (uint32_t)rStart;
                    z.sampleEndR = (uint32_t)rEnd;
                    z.loopStartR = (uint32_t)rLoopStart;
                    z.loopEndR = (uint32_t)rLoopEnd;
                }
            } else if (sType == 2) { // right sample
                int linkIdx = s.sampleLink;
                if (linkIdx >= 0 && linkIdx < (int)samples.size()) {
                    const auto& linked = samples[linkIdx];
                    z.sampleIndexR = (int)z.sampleIndex;
                    z.sampleIndex = linkIdx; // primary becomes left
                    // Copy left sample bounds from linked
                    int64_t lStart = (int64_t)linked.start + state.startOffset;
                    int64_t lEnd = (int64_t)linked.end + state.endOffset;
                    int64_t lLoopStart = (int64_t)linked.startLoop + state.startLoopOffset;
                    int64_t lLoopEnd = (int64_t)linked.endLoop + state.endLoopOffset;
                    lStart = std::clamp(lStart, (int64_t)0, sdSize);
                    lEnd = std::clamp(lEnd, lStart + 1, sdSize);
                    lLoopStart = std::clamp(lLoopStart, lStart, lEnd);
                    lLoopEnd = std::clamp(lLoopEnd, lLoopStart, lEnd);
                    z.sampleStart = (uint32_t)lStart;
                    z.sampleEnd = (uint32_t)lEnd;
                    z.loopStart = (uint32_t)lLoopStart;
                    z.loopEnd = (uint32_t)lLoopEnd;
                    // Right sample bounds are the current sample's bounds (already set)
                    z.sampleStartR = (uint32_t)start;
                    z.sampleEndR = (uint32_t)end;
                    z.loopStartR = (uint32_t)loopStart;
                    z.loopEndR = (uint32_t)loopEnd;
                }
            }

            m_channelZones[channel].push_back(z);
            zoneCount++;
        }
    }
    if (zoneCount > 0) {
        std::cout << "    [Zones] ch=" << channel << " " << zoneCount << " zones built" << std::endl;
    }
}

// ─────────────────────────── note resolution ─────────────────────

bool SFSynthesizer::resolveNote(int channel, int note, int velocity, ResolvedZone& out) {
    bool isDrum = (m_channels[channel].bank == 128);
    int bestRange = isDrum ? 999 : 0;
    int bestVelDist = 999;
    int bestRootKeyDist = 999;
    bool found = false;

    for (const auto& z : m_channelZones[channel]) {
        if (note < z.keyLow || note > z.keyHigh) continue;
        if (velocity < z.velLow || velocity > z.velHigh) continue;
        if (isDrum) {
            int range = z.keyHigh - z.keyLow;
            if (range < bestRange) {
                bestRange = range;
                out = z;
                found = true;
            }
        } else {
            int velDist = std::abs(velocity - (z.velLow + z.velHigh) / 2);
            int rootKeyDist = std::abs(note - (int)z.rootKey);
            if (rootKeyDist < bestRootKeyDist ||
                (rootKeyDist == bestRootKeyDist && velDist < bestVelDist)) {
                bestRootKeyDist = rootKeyDist;
                bestVelDist = velDist;
                out = z;
                found = true;
            }
        }
    }

    if (found && m_channels[channel].program == 0 && note == 60) {
        std::cout << "    [Resolve] ch=" << channel << " note=" << note << " vel=" << velocity
                  << " -> sample[" << out.sampleIndex << "] rootKey=" << out.rootKey
                  << " key=" << out.keyLow << "-" << out.keyHigh
                  << " vel=" << out.velLow << "-" << out.velHigh << std::endl;
    }
    return found;
}

void SFSynthesizer::startVoice(const ResolvedZone& zone, int channel, int note, int velocity) {
    auto freeIt = std::find_if(m_voices.begin(), m_voices.end(),
                               [](const SF2Voice& v) { return !v.active; });
    if (freeIt == m_voices.end()) {
        m_voices.emplace_back();
        freeIt = std::prev(m_voices.end());
    }

    SF2Voice& v = *freeIt;
    v = SF2Voice{};
    v.active = true;
    v.channel = channel;
    v.note = note;
    v.velocity = velocity;
    v.sampleIndex = zone.sampleIndex;
    v.sampleIndexR = zone.sampleIndexR;
    v.sampleStart = zone.sampleStart;
    v.sampleEnd = zone.sampleEnd;
    v.loopStart = zone.loopStart;
    v.loopEnd = zone.loopEnd;
    v.sampleStartR = zone.sampleStartR;
    v.sampleEndR = zone.sampleEndR;
    v.loopStartR = zone.loopStartR;
    v.loopEndR = zone.loopEndR;
    v.loop = zone.loop;
    v.loopMode = zone.loopMode;
    v.exclusiveClass = zone.exclusiveClass;
    v.sampleRate = zone.sampleRate;

    // Set scaleTuning BEFORE pitch calculation (was set after, causing default 1.0 to be used)
    v.scaleTuning = zone.scaleTuning;

    double noteFreq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    double rootFreq = 440.0 * std::pow(2.0, (zone.rootKey - 69) / 12.0);
    double pitchBend = getEffectivePitchBend(channel);
    double tuning = getEffectiveTuning(channel);
    bool isDrumChannel = (m_channels[channel].bank == 128);
    if (isDrumChannel) {
        v.pitchRatio = zone.sampleRate / (double)m_sampleRate;
    } else {
        // Scale tuning: cents per key deviation from equal temperament (100 cents/key)
        double scaleTuningCents = (v.scaleTuning - 1.0) * (note - zone.rootKey) * 100.0;
        v.pitchRatio = (noteFreq / rootFreq) * std::pow(2.0, (pitchBend + tuning + scaleTuningCents) / 12.0)
                       * (zone.sampleRate / (double)m_sampleRate);
    }

    // Velocity to amplitude: higher velocity = louder
    double velAmp = (velocity > 0) ? (double)velocity / 127.0 : 0.0;
    v.amplitude = velAmp * attenuateDb(zone.attenuation);
    // Boost drum channel volume (+9.5dB) to balance with melody parts
    if (m_channels[channel].bank == 128) {
        v.amplitude *= 3.0;
    }

    // SF2 default modulator: velocity to filter cutoff
    if (v.filterActive && velocity > 0) {
        double velToFilter = (velocity / 127.0) * 24.0; // 0-24 semitones
        v.filterFc = zone.filterFc * std::pow(2.0, velToFilter / 12.0);
        v.filterFc = std::clamp(v.filterFc, 20.0, m_sampleRate * 0.45);
    }

    double chPan = (m_channels[channel].pan - 64) / 64.0;
    v.zonePan = std::clamp(zone.pan, -1.0, 1.0);
    v.pan = std::clamp(v.zonePan + chPan, -1.0, 1.0);

    double aTime = std::max(zone.attack, 0.001);
    double dTime = std::max(zone.decay, 0.001);
    double rTime = std::max(zone.release, 0.001);
    // Cap release time to prevent extremely long tails (SF2 timpani can define 16-128+ sec)
    rTime = std::min(rTime, 5.0);
    v.attackRate = 1.0 / (aTime * m_sampleRate);
    v.decayRate = (1.0 - zone.sustain) / (dTime * m_sampleRate);
    v.sustainLevel = std::clamp(zone.sustain, 0.0, 1.0);
    v.releaseRate = 1.0 / (rTime * m_sampleRate);
    v.releaseLevel = 1.0;

    // VolEnv delay/hold
    v.envDelaySamples = (int)(zone.delayVolEnv * m_sampleRate);
    v.envHoldSamples = (int)(zone.holdVolEnv * m_sampleRate);
    v.envDelayCount = 0;
    v.envHoldCount = 0;
    v.envStage = (v.envDelaySamples > 0) ? 0 : 1; // start at delay or attack

    v.filterFc = std::clamp(zone.filterFc, 20.0, m_sampleRate * 0.49);
    // SF2 default modulator: velocity to filter Fc
    if (v.filterActive && velocity > 0) {
        double velShift = (velocity / 127.0) * 24.0; // up to 24 semitones
        v.filterFc *= std::pow(2.0, velShift / 12.0);
        v.filterFc = std::clamp(v.filterFc, 20.0, m_sampleRate * 0.45);
    }
    v.filterQ = std::max(0.5, zone.filterQ);
    v.filterActive = zone.filterActive;
    std::fill(v.filterState, v.filterState + 4, 0.0);

    // CC1 modulation → vibrato depth (SF2 spec: 0-1 semitones typical, not 2)
    // Reduced from 2.0 to 0.5 to match typical SF2 default modulator behavior
    // where CC1 maps to vibLFO-to-pitch with moderate depth
    v.vibratoDepth = m_channels[channel].modulation / 127.0 * 0.5;
    v.vibLFOFrequency = zone.freqVibLFO;
    v.vibLFODelay = zone.delayVibLFO;
    v.vibLFODelayCount = (int)(zone.delayVibLFO * m_sampleRate);
    v.modLFOFrequency = zone.freqModLFO;
    v.modLFODelay = zone.delayModLFO;
    v.modLFODelayCount = (int)(zone.delayModLFO * m_sampleRate);
    v.modLfoToPitch = zone.modLfoToPitch;
    v.vibLfoToPitch = zone.vibLfoToPitch;
    v.modLfoToFilterFc = zone.modLfoToFilterFc;
    v.modLfoToVolume = zone.modLfoToVolume;
    v.vibratoPhase = 0.0;
    v.modLFOPhase = 0.0;
    v.rootKey = zone.rootKey;
    v.basePitchRatio = v.pitchRatio;

    // SF2 reverb/chorus send (per-voice, from preset generators)
    v.reverbSend = zone.reverbSend;
    v.chorusSend = zone.chorusSend;

    // Modulation envelope (gen 39-42 for attack/decay/sustain/release)
    v.modEnvLevel = 0.0;
    v.modEnvToFilterFc = zone.modEnvToFilterFc;
    v.modEnvToVolume = zone.modEnvToVolume;
    // Use SF2 gen 39-42 values if set, otherwise fallback to VolEnv timing
    double mAttack = std::max(zone.attackModEnv, 0.001);
    double mDecay = std::max(zone.decayModEnv, 0.001);
    double mRelease = std::max(zone.releaseModEnv, 0.001);
    v.modEnvAttackRate = 1.0 / (mAttack * m_sampleRate);
    v.modEnvDecayRate = (1.0 - zone.sustainModEnv) / (mDecay * m_sampleRate);
    v.modEnvSustainLevel = zone.sustainModEnv;
    v.modEnvReleaseRate = 1.0 / (mRelease * m_sampleRate);
    v.modEnvStage = 1; // start at attack
    v.modEnvDelaySamples = v.envDelaySamples;
    v.modEnvHoldSamples = v.envHoldSamples;
    v.modEnvDelayCount = 0;
    v.modEnvHoldCount = 0;

    if (zone.exclusiveClass > 0 && m_channels[channel].bank == 128) {
        for (auto& other : m_voices) {
            if (&other != &v && other.active && other.channel == channel &&
                other.exclusiveClass == zone.exclusiveClass && !other.releasing) {
                other.releasing = true;
                other.releaseLevel = other.envLevel;
                other.releaseRate = 1.0 / (0.01 * m_sampleRate);
            }
        }
    }
}

// ─────────────────────────── MIDI events ─────────────────────────

void SFSynthesizer::noteOn(int channel, int note, int velocity) {
    if (m_fallbackMode) {
        auto freeIt = std::find_if(m_voices.begin(), m_voices.end(),
                                   [](const SF2Voice& v) { return !v.active; });
        if (freeIt == m_voices.end()) {
            m_voices.emplace_back();
            freeIt = std::prev(m_voices.end());
        }
        SF2Voice& v = *freeIt;
        v = SF2Voice{};
        v.active = true;
        v.channel = channel;
        v.note = note;
        v.velocity = velocity;
        v.pitchRatio = 440.0 * std::pow(2.0, (note - 69) / 12.0) / (double)m_sampleRate;
        v.amplitude = (double)velocity / 127.0 * 0.3;
        double chPan = (m_channels[channel].pan - 64) / 64.0;
        v.pan = std::clamp(chPan, -1.0, 1.0);
        v.attackRate = 1.0 / (0.002 * m_sampleRate);
        v.decayRate = 0.3 / (0.1 * m_sampleRate);
        v.sustainLevel = 0.7;
        v.releaseRate = 1.0 / (0.05 * m_sampleRate);
        v.releaseLevel = 1.0;
        v.sampleIndex = -1;
        return;
    }

    std::vector<ResolvedZone> zones;
    bool isDrum = (m_channels[channel].bank == 128);
    for (const auto& z : m_channelZones[channel]) {
        if (note < z.keyLow || note > z.keyHigh) continue;
        if (velocity < z.velLow || velocity > z.velHigh) continue;
        zones.push_back(z);
    }

    if (!zones.empty()) {
        // Kill previous voices on same channel+note to prevent overlap/beating
        for (auto& v : m_voices) {
            if (v.active && v.channel == channel && v.note == note && !v.releasing) {
                v.releasing = true;
                v.releaseLevel = v.envLevel;
                v.releaseRate = 1.0 / (0.01 * m_sampleRate); // 10ms quick release
            }
        }

        // Find previous voice on this channel for portamento
        double prevPitchRatio = -1.0;
        for (auto& v : m_voices) {
            if (v.active && v.channel == channel && v.age > 0) {
                prevPitchRatio = v.pitchRatio;
            }
        }

        for (const auto& zone : zones) {
            startVoice(zone, channel, note, velocity);
        }

        // Setup portamento if enabled
        const auto& ch = m_channels[channel];
        if (ch.portamentoOn >= 64 && prevPitchRatio > 0 && ch.portamentoTime > 0) {
            for (auto& v : m_voices) {
                if (v.active && v.channel == channel && v.note == note) {
                    v.basePitchRatio = prevPitchRatio;
                    v.portamentoProgress = 0.0;
                    break;
                }
            }
        }
        if (!isDrum && m_channels[channel].program == 0 && note == 60) {
            std::cout << "    [Resolve] ch=" << channel << " note=" << note
                      << " vel=" << velocity << " -> " << zones.size() << " zones" << std::endl;
        }
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
        int fullBank = (bank >= 128) ? bank : ((bank & 0x7F) << 7) | (m_channels[channel].bankLSB & 0x7F);
        int pIdx = m_sf2->findPreset(fullBank, program);
        if (pIdx < 0) pIdx = m_sf2->findPreset(bank, program);
        if (pIdx >= 0) {
            std::cout << "    [Preset] ch=" << channel << " bank=" << bank << " LSB=" << m_channels[channel].bankLSB << " prog=" << program 
                      << " -> preset[" << pIdx << "] \"" << m_sf2->presets()[pIdx].name << "\"" << std::endl;
        } else {
            std::cout << "    [Preset] ch=" << channel << " bank=" << bank << " LSB=" << m_channels[channel].bankLSB << " prog=" << program 
                      << " -> NOT FOUND" << std::endl;
        }
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
        case 5: ch.portamentoTime = value; break;
        case 7: ch.volume = value; break;
        case 10: ch.pan = value; break;
        case 11: ch.expression = value; break;
        case 38: {
            ch.rpnValue = (value & 0x7F) | ((ch.rpnValue & 0x7F00)); // Data Entry LSB
            // RPN再評価: LSB変更後にfine/coarse tune等を再計算
            if (ch.rpnMSB == 0 && ch.rpnLSB == 0) {
                ch.pitchBendRange = (ch.rpnValue >> 7) & 0x7F;
            } else if (ch.rpnMSB == 0 && ch.rpnLSB == 1) {
                ch.fineTune = ((ch.rpnValue - 8192) / 8192.0) * 100.0;
            } else if (ch.rpnMSB == 0 && ch.rpnLSB == 2) {
                ch.coarseTune = (ch.rpnValue >> 7) - 64;
            }
            break;
        }
        case 32: ch.bankLSB = value; break; // Bank Select LSB
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
        case 65: ch.portamentoOn = value; break;
        case 91: ch.reverb = value; break;
        case 93: ch.chorus = value; break;
        case 94: ch.delay = value; break;
        case 98: ch.nrpnLSB = value; break;  // NRPN LSB
        case 99: ch.nrpnMSB = value; break;  // NRPN MSB
        case 100: ch.rpnLSB = value; break; // RPN LSB
        case 101: ch.rpnMSB = value; break; // RPN MSB
        case 6: {  // CC6: Data Entry MSB (RPN)
            ch.rpnValue = (ch.rpnValue & 0x7F) | ((value & 0x7F) << 7); // 14bit構築
            // RPN処理
            if (ch.rpnMSB == 0 && ch.rpnLSB == 0) {
                // RPN 0: Pitch Bend Sensitivity (0-24半音)
                ch.pitchBendRange = std::clamp(value, 0, 24);
            } else if (ch.rpnMSB == 0 && ch.rpnLSB == 1) {
                // RPN 1: Channel Fine Tuning (14bit, 中心=8192=0 cents, ±100 cents)
                int val14 = ch.rpnValue; // 14bit (CC6 MSB + CC38 LSB)
                ch.fineTune = (val14 - 8192) * 100.0 / 8192.0;
            } else if (ch.rpnMSB == 0 && ch.rpnLSB == 2) {
                // RPN 2: Channel Coarse Tuning (0-127, 中心=64=0半音)
                ch.coarseTune = value - 64;
            }
            break;
        }
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
            ch.volume = 127; ch.expression = 127; ch.pan = 64;
            ch.pitchBend = 8192; ch.pitchBendRange = 2;
            ch.modulation = 0; ch.sustain = 0;
            ch.reverb = 0; ch.chorus = 0; ch.delay = 0;
            ch.breath = 0; ch.foot = 0;
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

    // Update vibrato depth from CC1 (SF2 spec: moderate depth, not excessive)
    if (!isDrumChannel) {
        v.vibratoDepth = m_channels[v.channel].modulation / 127.0 * 0.5;
    }

    // Live pitch ratio update (pitch bend, tuning)
    if (!isDrumChannel) {
        double noteFreq = 440.0 * std::pow(2.0, (v.note - 69) / 12.0);
        double rootFreq = 440.0 * std::pow(2.0, (v.rootKey - 69) / 12.0);
        double pitchBend = getEffectivePitchBend(v.channel);
        double tuning = getEffectiveTuning(v.channel);
        v.pitchRatio = (noteFreq / rootFreq) * std::pow(2.0, (pitchBend + tuning) / 12.0)
                       * (v.sampleRate / (double)m_sampleRate);
        if (v.portamentoProgress >= 1.0) v.basePitchRatio = v.pitchRatio;
    }

    // Live pan update (zone.pan + channel pan)
    double chPan = (m_channels[v.channel].pan - 64) / 64.0;
    v.pan = std::clamp(v.zonePan + chPan, -1.0, 1.0);

    // Update LFO phase for this voice (SF2 VibLFO with delay)
    double lfoValue = 0.0;
    if (v.vibratoDepth > 0.0 && !isDrumChannel) {
        if (v.vibLFODelayCount > 0) {
            v.vibLFODelayCount--;
        } else {
            v.vibratoPhase += 2.0 * M_PI * v.vibLFOFrequency / m_sampleRate;
            if (v.vibratoPhase > 2.0 * M_PI) v.vibratoPhase -= 2.0 * M_PI;
            lfoValue = std::sin(v.vibratoPhase);
        }
    }
    for (int i = 0; i < count; i++) {
        if (!v.active) break;

        // Update envelope with delay/hold stages
        if (v.releasing) {
            v.envLevel -= v.releaseRate;
            if (v.envLevel <= 0.0) { v.envLevel = 0.0; v.active = false; }
        } else {
            switch (v.envStage) {
                case 0: // delay
                    v.envDelayCount++;
                    if (v.envDelayCount >= v.envDelaySamples) v.envStage = 1;
                    break;
                case 1: // attack
                    v.envLevel += v.attackRate;
                    if (v.envLevel >= 1.0) { v.envLevel = 1.0; v.envStage = 2; }
                    break;
                case 2: // hold
                    v.envHoldCount++;
                    if (v.envHoldCount >= v.envHoldSamples) v.envStage = 3;
                    break;
                case 3: // decay
                    v.envLevel -= v.decayRate;
                    if (v.envLevel <= v.sustainLevel) { v.envLevel = v.sustainLevel; v.envStage = 4; }
                    break;
                case 4: // sustain
                    break;
            }
        }

        // Update modulation envelope (parallel to volume envelope)
        if (v.modEnvToFilterFc != 0.0 || v.modEnvToVolume != 0.0) {
            if (v.releasing) {
                v.modEnvLevel -= v.modEnvReleaseRate;
                if (v.modEnvLevel <= 0.0) v.modEnvLevel = 0.0;
            } else {
                switch (v.modEnvStage) {
                    case 0:
                        v.modEnvDelayCount++;
                        if (v.modEnvDelayCount >= v.modEnvDelaySamples) v.modEnvStage = 1;
                        break;
                    case 1:
                        v.modEnvLevel += v.modEnvAttackRate;
                        if (v.modEnvLevel >= 1.0) { v.modEnvLevel = 1.0; v.modEnvStage = 2; }
                        break;
                    case 2:
                        v.modEnvHoldCount++;
                        if (v.modEnvHoldCount >= v.modEnvHoldSamples) v.modEnvStage = 3;
                        break;
                    case 3:
                        v.modEnvLevel -= v.modEnvDecayRate;
                        if (v.modEnvLevel <= v.modEnvSustainLevel) { v.modEnvLevel = v.modEnvSustainLevel; v.modEnvStage = 4; }
                        break;
                    case 4: break;
                }
            }
        }

        if (!v.active) break;

        // Sample position
        double absPos = v.position + v.sampleStart;
        int idx = (int)absPos;

        // Loop / bounds check
        bool shouldLoop = v.loop && v.loopEnd > v.loopStart;
        // Mode 3: mark loop stop on release, complete current loop cycle
        if (v.loopMode == 3 && v.releasing && !v.loopStopPending) {
            v.loopStopPending = true;
        }
        if (shouldLoop && v.loopStopPending && idx >= (int)v.loopEnd) {
            shouldLoop = false;
        }
        if (shouldLoop) {
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

        // Lanczos-2 interpolation (high quality, reduced aliasing)
        const int16_t* sd = m_sf2->sampleData();
        size_t sdSize = m_sf2->sampleDataSize();
        bool isStereo = (v.sampleIndexR >= 0 && v.sampleIndexR != v.sampleIndex);
        auto getSample = [&](int pos, int sStart, int sEnd) -> double {
            if (pos < sStart) pos = sStart;
            if (pos >= sEnd) pos = sEnd - 1;
            if (pos >= 0 && pos < (int)sdSize) return sd[pos] / 32768.0;
            return 0.0;
        };
        double frac = absPos - std::floor(absPos);
        double sample;
        if (frac < 1e-10) {
            // Integer position: no interpolation needed
            sample = getSample(idx, v.sampleStart, v.sampleEnd);
        } else {
            // Lanczos-2 kernel (4 taps)
            double x = frac;
            auto lanczos = [](double t) -> double {
                if (std::abs(t) < 1e-10) return 1.0;
                if (std::abs(t) >= 2.0) return 0.0;
                double pi_t = M_PI * t;
                return std::sin(pi_t) * std::sin(pi_t / 2.0) / (pi_t * pi_t / 2.0);
            };
            double s0 = getSample(idx - 1, v.sampleStart, v.sampleEnd);
            double s1 = getSample(idx, v.sampleStart, v.sampleEnd);
            double s2 = getSample(idx + 1, v.sampleStart, v.sampleEnd);
            double s3 = getSample(idx + 2, v.sampleStart, v.sampleEnd);
            sample = s0 * lanczos(x + 1.0) + s1 * lanczos(x)
                   + s2 * lanczos(x - 1.0) + s3 * lanczos(x - 2.0);
        }
        // Right channel sample for stereo pairs
        double sampleR = sample;
        if (isStereo) {
            // Compute right channel position using its own bounds
            uint32_t rLoopLen = v.loopEndR - v.loopStartR;
            double rAbsPos = v.position + v.sampleStartR;
            if (v.loop && rLoopLen > 0) {
                double rOffset = std::fmod(rAbsPos - v.loopStartR, (double)rLoopLen);
                if (rOffset < 0) rOffset += rLoopLen;
                rAbsPos = v.loopStartR + rOffset;
            }
            int rIdx = (int)rAbsPos;
            if (rIdx < (int)v.sampleStartR) rIdx = (int)v.sampleStartR;
            if (rIdx >= (int)v.sampleEndR) rIdx = (int)v.sampleEndR - 1;
            double rFrac = rAbsPos - std::floor(rAbsPos);
            if (rFrac < 1e-10) {
                sampleR = getSample(rIdx, v.sampleStartR, v.sampleEndR);
            } else {
                auto lanczos = [](double t) -> double {
                    if (std::abs(t) < 1e-10) return 1.0;
                    if (std::abs(t) >= 2.0) return 0.0;
                    double pi_t = M_PI * t;
                    return std::sin(pi_t) * std::sin(pi_t / 2.0) / (pi_t * pi_t / 2.0);
                };
                double s0r = getSample(rIdx - 1, v.sampleStartR, v.sampleEndR);
                double s1r = getSample(rIdx, v.sampleStartR, v.sampleEndR);
                double s2r = getSample(rIdx + 1, v.sampleStartR, v.sampleEndR);
                double s3r = getSample(rIdx + 2, v.sampleStartR, v.sampleEndR);
                sampleR = s0r * lanczos(rFrac + 1.0) + s1r * lanczos(rFrac)
                        + s2r * lanczos(rFrac - 1.0) + s3r * lanczos(rFrac - 2.0);
            }
        }

        // ModLFO computation (shared by filter and volume)
        double modLfoValue = 0.0;
        if ((v.modLfoToFilterFc != 0.0 || v.modLfoToVolume != 0.0 || v.modLfoToPitch != 0.0) && !isDrumChannel) {
            if (v.modLFODelayCount > 0) v.modLFODelayCount--;
            else {
                v.modLFOPhase += 2.0 * M_PI * v.modLFOFrequency / m_sampleRate;
                if (v.modLFOPhase > 2.0 * M_PI) v.modLFOPhase -= 2.0 * M_PI;
            }
            modLfoValue = std::sin(v.modLFOPhase);
        }

        // Biquad low-pass filter
        double modulatedFc = v.filterFc;
        // ModLFO to filter Fc (gen 10)
        if (v.modLfoToFilterFc != 0.0) {
            modulatedFc *= std::pow(2.0, modLfoValue * v.modLfoToFilterFc / 1200.0);
        }
        // ModEnv to filter Fc (gen 11)
        if (v.modEnvToFilterFc != 0.0) {
            modulatedFc *= std::pow(2.0, v.modEnvLevel * v.modEnvToFilterFc / 1200.0);
        }
        modulatedFc = std::clamp(modulatedFc, 20.0, m_sampleRate * 0.45);
        // Skip filter if SF2 didn't explicitly set filterFc (default 13500 = no filter)
        if (modulatedFc < m_sampleRate * 0.45 && v.filterActive) {
            double w0 = 2.0 * M_PI * modulatedFc / m_sampleRate;
            double alpha = std::sin(w0) / (2.0 * v.filterQ);
            double a0 = 1.0 + alpha;
            double b0 = (1.0 - std::cos(w0)) / 2.0;
            double b1 = 1.0 - std::cos(w0);
            double b2 = (1.0 - std::cos(w0)) / 2.0;
            double a1 = -2.0 * std::cos(w0);
            double a2 = 1.0 - alpha;

            // Direct Form II Transposed
            double y = (b0 * sample + v.filterState[0]) / a0;
            v.filterState[0] = b1 * sample - a1 * y + v.filterState[1];
            v.filterState[1] = b2 * sample - a2 * y;
            sample = y;
        }

        // Amplitude with LFO modulation (tremolo) + ModEnv to volume
        double envAmp = v.envLevel * v.amplitude;
        // ModEnv to volume (gen 33: centibels, 0=none, 960=max attenuation)
        if (v.modEnvToVolume != 0.0) {
            envAmp *= attenuateDb(v.modEnvLevel * v.modEnvToVolume);
        }
        double chVol = m_channels[v.channel].volume / 127.0;
        double chExpr = m_channels[v.channel].expression / 127.0;
        double breathAmp = m_channels[v.channel].breath > 0 ? (0.5 + 0.5 * m_channels[v.channel].breath / 127.0) : 1.0;
        double footAmp = m_channels[v.channel].foot > 0 ? (0.5 + 0.5 * m_channels[v.channel].foot / 127.0) : 1.0;
        double vol = envAmp * chVol * chExpr * breathAmp * footAmp;
        // ModLFO to volume (gen 13: tremolo, centibels)
        if (v.modLfoToVolume != 0.0) {
            vol *= attenuateDb(modLfoValue * v.modLfoToVolume);
        }

        // Pan
        double panL = std::sqrt(0.5 * (1.0 - v.pan));
        double panR = std::sqrt(0.5 * (1.0 + v.pan));

        double outL = sample * vol * panL;
        double outR = sampleR * vol * panR;

        if (std::isfinite(outL)) left[i] += (float)outL;
        if (std::isfinite(outR)) right[i] += (float)outR;

        // Advance
        // Pitch modulation: CC1→VibLFO + SF2 vibLfoToPitch + modLfoToPitch
        double vibratoShift = lfoValue * v.vibratoDepth;
        if (v.vibLfoToPitch != 0.0) vibratoShift += lfoValue * v.vibLfoToPitch / 100.0;
        if (v.modLfoToPitch != 0.0) vibratoShift += modLfoValue * v.modLfoToPitch / 100.0;

        // Portamento: interpolate from old pitch to new pitch
        double effectivePitchRatio = v.basePitchRatio;
        if (v.portamentoProgress < 1.0) {
            const auto& ch = m_channels[v.channel];
            if (ch.portamentoOn >= 64 && ch.portamentoTime > 0 && !isDrumChannel) {
                // CC5 maps 0-127 to ~1ms-10s
                double portamentoRate = std::pow(10.0, (ch.portamentoTime / 127.0) * 4.0 - 3.0);
                v.portamentoProgress += 1.0 / (portamentoRate * m_sampleRate);
                if (v.portamentoProgress >= 1.0) v.portamentoProgress = 1.0;
            } else {
                v.portamentoProgress = 1.0;
            }
            // Smooth interpolation (ease-in-out)
            double t = v.portamentoProgress;
            t = t * t * (3.0 - 2.0 * t); // Hermite interpolation
            effectivePitchRatio = v.basePitchRatio + (v.pitchRatio - v.basePitchRatio) * t;
        }

        v.position += effectivePitchRatio * std::pow(2.0, vibratoShift / 12.0);
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

void SFSynthesizer::processBlock(std::vector<float>& left, std::vector<float>& right, int count, bool drumsOnly) {
    for (auto& v : m_voices) {
        if (!v.active) continue;
        bool isDrum = (m_channels[v.channel].bank == 128);
        if (drumsOnly && !isDrum) continue;
        v.age++;
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
    m_voices.clear();
    std::cout << "  [Debug] Voices reset" << std::endl;

    // 各チャンネルの初期program/bankを決定
    const auto& expr = midi.expression();
    std::cout << "  [Debug] Starting channel init" << std::endl;
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

        int partMode = expr.getValueAtTick(expr.sysPartMode[ch], firstNoteTick, -1);
        initBank = effectiveBankMSB(ch, initBank, firstNoteTick, expr);

        m_channels[ch].program = initProgram;
        m_channels[ch].bank = initBank;

        // Ch1-9/11-16: partMode==1 かつ CC0 未指定 → bank=128
        if (ch != 9 && partMode == 1 && initBank == 0) {
            // SysExがリズム指定 but bank selectなし → bank=128
            m_channels[ch].bank = 128; // GM drum default
        }

        // bankLSBを先に復元してからprogramChange（14bit bank解決が初回から有効に）
        m_channels[ch].bankLSB = expr.getValueAtTick(expr.bankSelectLSB[ch], firstNoteTick, 0);
        programChange(ch, m_channels[ch].program, m_channels[ch].bank);

        // 全CCを初期状態に復元
        m_channels[ch].volume = expr.getValueAtTick(expr.volume[ch], firstNoteTick, 127);
        m_channels[ch].expression = expr.getValueAtTick(expr.expression[ch], firstNoteTick, 127);
        m_channels[ch].pan = expr.getValueAtTick(expr.pan[ch], firstNoteTick, 64);
        m_channels[ch].sustain = expr.getValueAtTick(expr.sustain[ch], firstNoteTick, 0);
        m_channels[ch].modulation = expr.getValueAtTick(expr.modulation[ch], firstNoteTick, 0);
        m_channels[ch].pitchBend = expr.getValueAtTick(expr.pitchBend[ch], firstNoteTick, 8192);
        m_channels[ch].pitchBendRange = expr.getValueAtTick(expr.pitchBendRange[ch], firstNoteTick, 2);
        m_channels[ch].reverb = expr.getValueAtTick(expr.reverbDepth[ch], firstNoteTick, 0);
        m_channels[ch].chorus = expr.getValueAtTick(expr.chorusDepth[ch], firstNoteTick, 0);
        m_channels[ch].delay = expr.getValueAtTick(expr.delayDepth[ch], firstNoteTick, 0);
        m_channels[ch].portamentoTime = expr.getValueAtTick(expr.portamentoTime[ch], firstNoteTick, 0);
        m_channels[ch].portamentoOn = expr.getValueAtTick(expr.portamentoOn[ch], firstNoteTick, 0);
        m_channels[ch].breath = expr.getValueAtTick(expr.breath[ch], firstNoteTick, 0);
        m_channels[ch].foot = expr.getValueAtTick(expr.foot[ch], firstNoteTick, 0);

        std::vector<std::tuple<int64_t, int, int>> rpnInit;
        auto addInitCC = [&](const std::vector<std::pair<int64_t, int>>& src, int cc) {
            for (auto& [t, v] : src) if (t <= firstNoteTick) rpnInit.push_back({t, cc, v});
        };
        addInitCC(expr.nrpnMSB[ch], 99);
        addInitCC(expr.nrpnLSB[ch], 98);
        addInitCC(expr.rpnMSB[ch], 101);
        addInitCC(expr.rpnLSB[ch], 100);
        addInitCC(expr.dataEntryMSB[ch], 6);
        addInitCC(expr.dataEntryLSB[ch], 38);
        std::sort(rpnInit.begin(), rpnInit.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
            auto priority = [](int cc) {
                if (cc == 99 || cc == 98 || cc == 101 || cc == 100) return 0;
                return 1;
            };
            return priority(std::get<1>(a)) < priority(std::get<1>(b));
        });
        for (auto& [t, cc, v] : rpnInit) {
            (void)t;
            controlChange(ch, cc, v);
        }
    }
    std::cout << "  [Debug] Channel init done" << std::endl;

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

        // tickToSecondsの逆変換（区間BPM補間）
        const auto& tmap = midi.tempoMap();
        int ts = (int)tmap.size();
        auto tickForTime = [&](double sec) -> int64_t {
            if (ts == 0) {
                return (int64_t)(sec * midi.ticksPerQuarterNote() * 2.0);
            }
            double accumSec = 0;
            int64_t prevTick = 0;
            for (int i = 0; i < ts; i++) {
                double tickSec = midi.tickToSeconds(tmap[i].first);
                double intervalSec = tickSec - accumSec;
                if (intervalSec > 0 && accumSec + intervalSec > sec) {
                    double frac = (sec - accumSec) / intervalSec;
                    return prevTick + (int64_t)(frac * (tmap[i].first - prevTick));
                }
                accumSec = tickSec;
                prevTick = tmap[i].first;
            }
            if (ts > 0) {
                double lastBPM = tmap[ts - 1].second;
                double lastTickSec = midi.tickToSeconds(tmap[ts - 1].first);
                double excessSec = sec - lastTickSec;
                return tmap[ts - 1].first + (int64_t)(excessSec * midi.ticksPerQuarterNote() * lastBPM / 60.0);
            }
            return (int64_t)(sec * midi.ticksPerQuarterNote() * 2.0);
        };

        int64_t blockTickStart = 0, blockTickEnd = 0;
        {
            double blockSecStart = (double)pos / m_sampleRate;
            double blockSecEnd = (double)blockEnd / m_sampleRate;
            blockTickStart = tickForTime(blockSecStart);
            blockTickEnd = tickForTime(blockSecEnd);
        }

        const auto& expr = midi.expression();

        // Ch10: CC0=0 等で bank が melody に戻るのを防ぐ（partMode==0 のメロディ例外のみ許可）
        {
            int pm = expr.getValueAtTick(expr.sysPartMode[9], blockTickStart, -1);
            if (pm != 0 && m_channels[9].bank != 128) {
                m_channels[9].bank = 128;
                programChange(9, m_channels[9].program, 128);
                channelPresetKey[9] = m_channels[9].program * 256 + 128;
            }
        }

        auto tickToSampleOffset = [&](int64_t tick) -> int {
            int s = (int)(midi.tickToSeconds(tick, 0) * m_sampleRate) - pos;
            if (s < 0) s = 0;
            if (s >= blockLen) s = blockLen - 1;
            return s;
        };

        // Note On/Off / CC / bank / PC を正確なサンプル位置で処理
        struct BlockEvent { int sampleOffset; int channel; int type; int p1; int p2; int64_t tick; };
        // type: 0=noteOff, 1=CC, 2=pitchBend, 3=noteOn, 4=bankMSB, 5=bankLSB, 6=programChange, 7=sysPartMode
        std::vector<BlockEvent> events;

        // Note events
        for (auto& n : sorted) {
            int noteStart = (int)(midi.tickToSeconds(n.startTime, n.track) * m_sampleRate);
            if (noteStart >= pos && noteStart < blockEnd) {
                int needKey = m_channels[n.channel].program * 256 + m_channels[n.channel].bank;
                if (channelPresetKey[n.channel] != needKey) {
                    programChange(n.channel, m_channels[n.channel].program, m_channels[n.channel].bank);
                    channelPresetKey[n.channel] = needKey;
                }
                int shiftedNote = std::clamp(n.note + opts.pitchShift, 0, 127);
                int sampleOffset = noteStart - pos;
                if (sampleOffset < 0) sampleOffset = 0;
                if (sampleOffset >= blockLen) sampleOffset = blockLen - 1;
                events.push_back({sampleOffset, n.channel, 3, shiftedNote, n.velocity, n.startTime});
                active.push_back({&n, false});
            }
        }

        for (auto& a : active) {
            if (a.offDone) continue;
            int noteEnd = (int)(midi.tickToSeconds(a.note->endTime, a.note->track) * m_sampleRate);
            if (noteEnd >= pos && noteEnd < blockEnd) {
                int shiftedNote = std::clamp(a.note->note + opts.pitchShift, 0, 127);
                events.push_back({noteEnd - pos, a.note->channel, 0, shiftedNote, 0, a.note->endTime});
                a.offDone = true;
            }
        }
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [](const ActiveNote& a) { return a.offDone; }), active.end());

        // CC / bank / PC / part mode をサンプル精度で追加
        for (int ch = 0; ch < 16; ch++) {
            auto addCC = [&](const std::vector<std::pair<int64_t, int>>& ccVec, int ccNum) {
                for (auto& [t, v] : ccVec) {
                    if (t >= blockTickStart && t < blockTickEnd) {
                        events.push_back({tickToSampleOffset(t), ch, 1, ccNum, v, t});
                    }
                }
            };
            addCC(expr.volume[ch], 7);
            addCC(expr.expression[ch], 11);
            addCC(expr.breath[ch], 2);
            addCC(expr.modulation[ch], 1);
            addCC(expr.sustain[ch], 64);
            addCC(expr.pan[ch], 10);
            addCC(expr.foot[ch], 4);
            addCC(expr.portamentoTime[ch], 5);
            addCC(expr.portamentoOn[ch], 65);
            addCC(expr.dataEntryMSB[ch], 6);
            addCC(expr.dataEntryLSB[ch], 38);
            addCC(expr.nrpnLSB[ch], 98);
            addCC(expr.nrpnMSB[ch], 99);
            addCC(expr.rpnLSB[ch], 100);
            addCC(expr.rpnMSB[ch], 101);
            addCC(expr.reverbDepth[ch], 91);
            addCC(expr.chorusDepth[ch], 93);
            addCC(expr.delayDepth[ch], 94);
            for (auto& [t, v] : expr.bankSelectMSB[ch]) {
                if (t >= blockTickStart && t < blockTickEnd)
                    events.push_back({tickToSampleOffset(t), ch, 4, v, 0, t});
            }
            for (auto& [t, v] : expr.bankSelectLSB[ch]) {
                if (t >= blockTickStart && t < blockTickEnd)
                    events.push_back({tickToSampleOffset(t), ch, 5, v, 0, t});
            }
            for (auto& [t, v] : expr.programChange[ch]) {
                if (t >= blockTickStart && t < blockTickEnd)
                    events.push_back({tickToSampleOffset(t), ch, 6, v, 0, t});
            }
            for (auto& [t, v] : expr.sysPartMode[ch]) {
                if (t >= blockTickStart && t < blockTickEnd)
                    events.push_back({tickToSampleOffset(t), ch, 7, v, 0, t});
            }
            // Pitch Bend
            for (auto& [t, v] : expr.pitchBend[ch]) {
                if (t >= blockTickStart && t < blockTickEnd) {
                    events.push_back({tickToSampleOffset(t), ch, 2, v, 0, t});
                }
            }
        }

        // イベントをソート: noteOff → bank/PC → CC → pitchBend → noteOn
        auto eventPriority = [](int type) {
            switch (type) {
                case 0: return 0; // noteOff
                case 4: return 1; // bank MSB
                case 5: return 2; // bank LSB
                case 6: return 3; // program
                case 7: return 4; // part mode
                case 1: return 5; // CC
                case 2: return 6; // pitch bend
                case 3: return 7; // noteOn
                default: return 8;
            }
        };
        auto ccPriority = [](int cc) {
            if (cc == 99 || cc == 98 || cc == 101 || cc == 100) return 0;
            if (cc == 6 || cc == 38 || cc == 96 || cc == 97) return 1;
            if (cc == 7) return 2;
            if (cc == 11) return 3;
            if (cc == 10) return 4;
            return 5;
        };
        std::sort(events.begin(), events.end(), [&](const BlockEvent& a, const BlockEvent& b) {
            if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
            int pa = eventPriority(a.type), pb = eventPriority(b.type);
            if (pa != pb) return pa < pb;
            if (a.type == 1 && b.type == 1) return ccPriority(a.p1) < ccPriority(b.p1);
            return false;
        });

        // GS SysEx: グローバルエフェクトパラメータ（ブロック単位）
        for (auto& [t, v] : expr.sysReverbLevel) {
            if (t >= blockTickStart && t < blockTickEnd) m_reverbLevel = v / 127.0f;
        }
        for (auto& [t, v] : expr.sysChorusLevel) {
            if (t >= blockTickStart && t < blockTickEnd) m_chorusLevel = v / 127.0f;
        }
        for (auto& [t, v] : expr.sysDelayLevel) {
            if (t >= blockTickStart && t < blockTickEnd) m_delayLevel = v / 127.0f;
        }
        for (auto& [t, v] : expr.sysDelayTime) {
            if (t >= blockTickStart && t < blockTickEnd) m_delayTime = v / 127.0f;
        }
        for (auto& [t, v] : expr.sysDelayFeed) {
            if (t >= blockTickStart && t < blockTickEnd) m_delayFeedback = v / 127.0f;
        }

        auto applyFxSegment = [&](float* segL, float* segR, int len, int segEndSample,
                                  float* drumSegL, float* drumSegR) {
            int64_t segTick = tickForTime((double)(pos + segEndSample) / m_sampleRate);

            // Reverb: channel CC + GS send + SF2 voice sends (melody only)
            float reverbMix = m_reverbLevel;
            for (int ch = 0; ch < 16; ch++) {
                if (m_channels[ch].bank == 128) continue;
                int send = m_channels[ch].reverb;
                send = std::max(send, expr.getValueAtTick(expr.sysPartReverbSend[ch], segTick, 0));
                if (send > reverbMix * 127.0f) reverbMix = send / 127.0f;
            }
            float sf2ReverbSum = 0.0f;
            int sf2ReverbCount = 0;
            for (auto& v : m_voices) {
                if (v.active && v.reverbSend > 0.0f && m_channels[v.channel].bank != 128) {
                    sf2ReverbSum += v.reverbSend / 100.0f;
                    sf2ReverbCount++;
                }
            }
            if (sf2ReverbCount > 0) {
                float sf2Reverb = std::min(sf2ReverbSum / (float)sf2ReverbCount, 1.0f);
                reverbMix = std::max(reverbMix, sf2Reverb * 0.5f);
            }
            if (reverbMix > 0.001f) {
                m_reverb.process(segL, segR, len, reverbMix, false);
            }

            // Chorus: weighted average of send levels (melody only)
            float chorusSum = 0.0f;
            float chorusWeight = 0.0f;
            for (int ch = 0; ch < 16; ch++) {
                if (m_channels[ch].bank == 128) continue;
                float send = std::max((float)m_channels[ch].chorus,
                    (float)expr.getValueAtTick(expr.sysPartChorusSend[ch], segTick, 0)) / 127.0f;
                if (send > 0.001f) {
                    int noteCount = 0;
                    for (auto& v : m_voices) {
                        if (v.active && v.channel == ch) noteCount++;
                    }
                    float weight = std::min((float)noteCount, 4.0f) / 4.0f;
                    chorusSum += send * weight;
                    chorusWeight += weight;
                }
            }
            float chorusMix = (chorusWeight > 0.0f) ? chorusSum / chorusWeight : 0.0f;
            float sf2ChorusSum = 0.0f;
            int sf2ChorusCount = 0;
            for (auto& v : m_voices) {
                if (v.active && v.chorusSend > 0.0f && m_channels[v.channel].bank != 128) {
                    sf2ChorusSum += v.chorusSend / 100.0f;
                    sf2ChorusCount++;
                }
            }
            if (sf2ChorusCount > 0) {
                float sf2Chorus = std::min(sf2ChorusSum / (float)sf2ChorusCount, 1.0f);
                chorusMix = std::max(chorusMix, sf2Chorus * 0.3f);
            }
            if (chorusMix > 0.001f) m_chorus.process(segL, segR, len, chorusMix * 16.0f);

            // Delay (melody only)
            float delaySum = 0.0f;
            int delayCount = 0;
            for (int ch = 0; ch < 16; ch++) {
                if (m_channels[ch].bank == 128) continue;
                int send = m_channels[ch].delay;
                send = std::max(send, expr.getValueAtTick(expr.sysPartDelaySend[ch], segTick, 0));
                if (send > 0) {
                    delaySum += send / 127.0f;
                    delayCount++;
                }
            }
            float delayMix = (delayCount > 0) ? std::min(delaySum / (float)delayCount, 1.0f) : m_delayLevel;
            if (delayMix > 0.001f) {
                float delayTime = 0.1f + m_delayTime * 0.9f;
                float feedback = 0.2f + m_delayFeedback * 0.5f;
                m_delay.process(segL, segR, len, delayTime, feedback, delayMix * 0.4f);
            }
        };

        // ブロックをイベントで区切って処理
        std::vector<float> bl(blockLen, 0.0f), br(blockLen, 0.0f);
        int segStart = 0;
        int evIdx = 0;
        while (segStart < blockLen) {
            // 次のイベント位置を探す
            int segEnd = blockLen;
            if (evIdx < (int)events.size()) {
                segEnd = std::min(segEnd, events[evIdx].sampleOffset);
            }

            // このセグメントを処理
            if (segEnd > segStart) {
                int segLen = segEnd - segStart;
                std::vector<float> segL(segLen, 0.0f), segR(segLen, 0.0f);
                std::vector<float> drumL(segLen, 0.0f), drumR(segLen, 0.0f);

                // Check if any drum voices are active
                bool hasDrumActive = false;
                for (auto& v : m_voices) {
                    if (v.active && m_channels[v.channel].bank == 128) { hasDrumActive = true; break; }
                }

                if (hasDrumActive) {
                    // Pass 1: render drums only into separate buffers
                    processBlock(drumL, drumR, segLen, true);
                    // Pass 2: render full mix (melody + drums)
                    processBlock(segL, segR, segLen, false);
                    // Subtract drum contribution from full mix to isolate melody
                    for (int i = 0; i < segLen; i++) {
                        segL[i] -= drumL[i];
                        segR[i] -= drumR[i];
                    }
                    // Apply FX to melody only (reverb/chorus/delay skip drum channels)
                    applyFxSegment(segL.data(), segR.data(), segLen, segEnd,
                                   drumL.data(), drumR.data());
                    // Add dry drums back
                    for (int i = 0; i < segLen; i++) {
                        segL[i] += drumL[i];
                        segR[i] += drumR[i];
                    }
                } else {
                    // No drums: render full mix and apply FX normally
                    processBlock(segL, segR, segLen);
                    applyFxSegment(segL.data(), segR.data(), segLen, segEnd,
                                   nullptr, nullptr);
                }
                for (int i = 0; i < segLen; i++) {
                    bl[segStart + i] = segL[i];
                    br[segStart + i] = segR[i];
                }
            }

            // イベントを適用
            while (evIdx < (int)events.size() && events[evIdx].sampleOffset == segEnd) {
                auto& ev = events[evIdx];
                switch (ev.type) {
                    case 0: noteOff(ev.channel, ev.p1); break;
                    case 1: controlChange(ev.channel, ev.p1, ev.p2); break;
                    case 2: pitchBend(ev.channel, ev.p1); break;
                    case 3: noteOn(ev.channel, ev.p1, ev.p2); break;
                    case 4: {
                        int bank = effectiveBankMSB(ev.channel, ev.p1, ev.tick, expr);
                        m_channels[ev.channel].bank = bank;
                        programChange(ev.channel, m_channels[ev.channel].program, bank);
                        channelPresetKey[ev.channel] = m_channels[ev.channel].program * 256 + bank;
                        break;
                    }
                    case 5: {
                        m_channels[ev.channel].bankLSB = ev.p1;
                        int bank = effectiveBankMSB(ev.channel, m_channels[ev.channel].bank, ev.tick, expr);
                        m_channels[ev.channel].bank = bank;
                        programChange(ev.channel, m_channels[ev.channel].program, bank);
                        channelPresetKey[ev.channel] = m_channels[ev.channel].program * 256 + bank;
                        break;
                    }
                    case 6: {
                        int bank = effectiveBankMSB(ev.channel, m_channels[ev.channel].bank, ev.tick, expr);
                        m_channels[ev.channel].bank = bank;
                        programChange(ev.channel, ev.p1, bank);
                        channelPresetKey[ev.channel] = ev.p1 * 256 + bank;
                        break;
                    }
                    case 7:
                        if (ev.p1 == 1) {
                            m_channels[ev.channel].bank = 128;
                            programChange(ev.channel, m_channels[ev.channel].program, 128);
                            channelPresetKey[ev.channel] = m_channels[ev.channel].program * 256 + 128;
                        } else if (ev.p1 == 0 && ev.channel == 9) {
                            int restoredBank = 0;
                            for (auto& [bt, bv] : expr.bankSelectMSB[ev.channel]) {
                                if (bt <= ev.tick) restoredBank = bv;
                            }
                            m_channels[ev.channel].bank = restoredBank;
                            programChange(ev.channel, m_channels[ev.channel].program, restoredBank);
                            channelPresetKey[ev.channel] = m_channels[ev.channel].program * 256 + restoredBank;
                        }
                        break;
                }
                evIdx++;
            }

            segStart = segEnd;
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

    // Normalize to peak (skip if --no-normalize)
    if (!opts.noNormalize) {
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
    }

    // Master gain (dB)
    if (opts.gainDb != 0.0) {
        float gainLin = std::pow(10.0f, (float)opts.gainDb / 20.0f);
        for (size_t i = 0; i < left.size(); i++) {
            left[i] = std::isfinite(left[i]) ? left[i] * gainLin : 0.0f;
            right[i] = std::isfinite(right[i]) ? right[i] * gainLin : 0.0f;
        }
    }

    WavWriter::write(wavPath, left, right, m_sampleRate);
}

void SFSynthesizer::renderToWavPerChannel(const std::vector<MidiNote>& notes,
                                           const std::string& baseName,
                                           const std::string& outputDir,
                                           const MidiFile& midi,
                                           int pitchShift,
                                           bool noNormalize,
                                           const std::vector<int>& channelFilter) {
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

    // チャンネルフィルタ適用
    if (!channelFilter.empty()) {
        std::vector<int> filtered;
        for (int ch : usedChannels) {
            if (std::find(channelFilter.begin(), channelFilter.end(), ch) != channelFilter.end()) {
                filtered.push_back(ch);
            }
        }
        usedChannels = filtered;
    }

    // 最大テンポリ長を計算
    double totalSec = 0;
    for (auto& n : notes) {
        double t = midi.tickToSeconds(n.endTime, n.track) + 1.0;
        totalSec = std::max(totalSec, t);
    }
    int totalSamples = (int)(totalSec * m_sampleRate);
    std::cout << "    [Debug] totalSec=" << totalSec << " totalSamples=" << totalSamples << std::endl;
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

        int partMode = expr.getValueAtTick(expr.sysPartMode[ch], firstNoteTick, -1);
        chBank = effectiveBankMSB(ch, chBank, firstNoteTick, expr);
        if (ch == 9) {
            // GM: Ch10 is drums by default (bank=128)
            if (chBank == 0) chBank = 128;
        } else if (partMode == 1 && chBank == 0) {
            chBank = 128;
        }

        // このチャンネルのノートのみを抽出
        std::vector<MidiNote> chNotes;
        for (auto& n : notes) if (n.channel == ch) chNotes.push_back(n);

        // チャンネルごとにシンセを再初期化
        SFSynthesizer chSynth;
        chSynth.init(*m_sf2, m_sampleRate);

        // bankLSBを先に復元してからprogramChange（14bit bank解決が初回から有効に）
        chSynth.m_channels[0].bankLSB = expr.getValueAtTick(expr.bankSelectLSB[ch], firstNoteTick, 0);

        // program変更を適用
        chSynth.programChange(0, chProgram, chBank);

        // 全CCを初期状態に復元（メインパス :970-985 と同一）
        chSynth.m_channels[0].volume = expr.getValueAtTick(expr.volume[ch], firstNoteTick, 127);
        chSynth.m_channels[0].expression = expr.getValueAtTick(expr.expression[ch], firstNoteTick, 127);
        chSynth.m_channels[0].pan = expr.getValueAtTick(expr.pan[ch], firstNoteTick, 64);
        chSynth.m_channels[0].sustain = expr.getValueAtTick(expr.sustain[ch], firstNoteTick, 0);
        chSynth.m_channels[0].modulation = expr.getValueAtTick(expr.modulation[ch], firstNoteTick, 0);
        chSynth.m_channels[0].pitchBend = expr.getValueAtTick(expr.pitchBend[ch], firstNoteTick, 8192);
        chSynth.m_channels[0].pitchBendRange = expr.getValueAtTick(expr.pitchBendRange[ch], firstNoteTick, 2);
        chSynth.m_channels[0].reverb = expr.getValueAtTick(expr.reverbDepth[ch], firstNoteTick, 0);
        chSynth.m_channels[0].chorus = expr.getValueAtTick(expr.chorusDepth[ch], firstNoteTick, 0);
        chSynth.m_channels[0].delay = expr.getValueAtTick(expr.delayDepth[ch], firstNoteTick, 0);
        chSynth.m_channels[0].breath = expr.getValueAtTick(expr.breath[ch], firstNoteTick, 0);
        chSynth.m_channels[0].foot = expr.getValueAtTick(expr.foot[ch], firstNoteTick, 0);

        std::vector<std::tuple<int64_t, int, int>> rpnInit;
        auto addInitCC = [&](const std::vector<std::pair<int64_t, int>>& src, int cc) {
            for (auto& [t, v] : src) if (t <= firstNoteTick) rpnInit.push_back({t, cc, v});
        };
        addInitCC(expr.nrpnMSB[ch], 99);
        addInitCC(expr.nrpnLSB[ch], 98);
        addInitCC(expr.rpnMSB[ch], 101);
        addInitCC(expr.rpnLSB[ch], 100);
        addInitCC(expr.dataEntryMSB[ch], 6);
        addInitCC(expr.dataEntryLSB[ch], 38);
        std::sort(rpnInit.begin(), rpnInit.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
            auto priority = [](int cc) {
                if (cc == 99 || cc == 98 || cc == 101 || cc == 100) return 0;
                return 1;
            };
            return priority(std::get<1>(a)) < priority(std::get<1>(b));
        });
        for (auto& [t, cc, v] : rpnInit) {
            (void)t;
            chSynth.controlChange(0, cc, v);
        }

        std::cout << "    Ch " << (ch + 1) << ": program=" << chProgram
                  << " bank=" << chBank
                  << " notes=" << noteCount << std::endl;

        // チャンネル番号を0にマッピングしてレンダリング
        std::vector<MidiNote> mapped = chNotes;
        for (auto& n : mapped) n.channel = 0;

        std::cout << "    [Debug] Rendering ch " << (ch + 1) << "..." << std::endl;

        std::vector<float> left(totalSamples, 0.0f);
        std::vector<float> right(totalSamples, 0.0f);
        std::cout << "    [Debug] Allocated " << (totalSamples * 2 * sizeof(float) / 1024 / 1024) << "MB for ch " << (ch+1) << std::endl;

        // レンダリング
        const int BS = 1024;
        for (int pos = 0; pos < totalSamples; pos += BS) {
            int blockEnd = std::min(pos + BS, totalSamples);
            int blockLen = blockEnd - pos;

            // CC適用（このブロックの範囲）— テンポマップ対応
            const auto& tmap = midi.tempoMap();
            int ts = (int)tmap.size();
            auto tickForTime = [&](double sec) -> int64_t {
                if (ts == 0) return (int64_t)(sec * midi.ticksPerQuarterNote() * 2.0);
                double accumSec = 0;
                int64_t prevTick = 0;
                for (int i = 0; i < ts; i++) {
                    double tickSec = midi.tickToSeconds(tmap[i].first);
                    double intervalSec = tickSec - accumSec;
                    if (intervalSec > 0 && accumSec + intervalSec > sec) {
                        double frac = (sec - accumSec) / intervalSec;
                        return prevTick + (int64_t)(frac * (tmap[i].first - prevTick));
                    }
                    accumSec = tickSec;
                    prevTick = tmap[i].first;
                }
                if (ts > 0) {
                    double lastBPM = tmap[ts - 1].second;
                    double lastTickSec = midi.tickToSeconds(tmap[ts - 1].first);
                    double excessSec = sec - lastTickSec;
                    return tmap[ts - 1].first + (int64_t)(excessSec * midi.ticksPerQuarterNote() * lastBPM / 60.0);
                }
                return (int64_t)(sec * midi.ticksPerQuarterNote() * 2.0);
            };
            double blockSecStart = (double)pos / m_sampleRate;
            double blockSecEnd = (double)blockEnd / m_sampleRate;
            int64_t blockTickStart = tickForTime(blockSecStart);
            int64_t blockTickEnd = tickForTime(blockSecEnd);

            if (ch == 9) {
                int pm = expr.getValueAtTick(expr.sysPartMode[ch], blockTickStart, -1);
                if (pm != 0 && chSynth.m_channels[0].bank != 128) {
                    chSynth.m_channels[0].bank = 128;
                    chSynth.programChange(0, chSynth.m_channels[0].program, 128);
                }
            }

            auto tickToSampleOffset = [&](int64_t tick) -> int {
                int s = (int)(midi.tickToSeconds(tick, 0) * m_sampleRate) - pos;
                if (s < 0) s = 0;
                if (s >= blockLen) s = blockLen - 1;
                return s;
            };

            struct ChEvent { int sampleOffset; int type; int p1; int p2; };
            std::vector<ChEvent> chEvents;
            for (auto& n : mapped) {
                int noteStart = (int)(midi.tickToSeconds(n.startTime, n.track) * m_sampleRate);
                if (noteStart >= pos && noteStart < blockEnd) {
                    chEvents.push_back({noteStart - pos, 3, std::clamp(n.note + pitchShift, 0, 127), n.velocity});
                }
                int noteEnd = (int)(midi.tickToSeconds(n.endTime, n.track) * m_sampleRate);
                if (noteEnd >= pos && noteEnd < blockEnd) {
                    chEvents.push_back({noteEnd - pos, 0, std::clamp(n.note + pitchShift, 0, 127), 0});
                }
            }
            auto addCC = [&](const std::vector<std::pair<int64_t, int>>& ccVec, int ccNum) {
                for (auto& [t, v] : ccVec) {
                    if (t >= blockTickStart && t < blockTickEnd)
                        chEvents.push_back({tickToSampleOffset(t), 1, ccNum, v});
                }
            };
            addCC(expr.volume[ch], 7);
            addCC(expr.expression[ch], 11);
            addCC(expr.breath[ch], 2);
            addCC(expr.foot[ch], 4);
            addCC(expr.sustain[ch], 64);
            addCC(expr.modulation[ch], 1);
            addCC(expr.pan[ch], 10);
            addCC(expr.portamentoTime[ch], 5);
            addCC(expr.portamentoOn[ch], 65);
            addCC(expr.dataEntryMSB[ch], 6);
            addCC(expr.dataEntryLSB[ch], 38);
            addCC(expr.nrpnLSB[ch], 98);
            addCC(expr.nrpnMSB[ch], 99);
            addCC(expr.rpnLSB[ch], 100);
            addCC(expr.rpnMSB[ch], 101);
            addCC(expr.reverbDepth[ch], 91);
            addCC(expr.chorusDepth[ch], 93);
            addCC(expr.delayDepth[ch], 94);
            for (auto& [t, v] : expr.pitchBend[ch]) {
                if (t >= blockTickStart && t < blockTickEnd)
                    chEvents.push_back({tickToSampleOffset(t), 2, v, 0});
            }

            auto chEventPriority = [](int type) {
                switch (type) {
                    case 0: return 0;
                    case 1: return 5;
                    case 2: return 6;
                    case 3: return 7;
                    default: return 8;
                }
            };
            auto ccPriority = [](int cc) {
                if (cc == 99 || cc == 98 || cc == 101 || cc == 100) return 0;
                if (cc == 6 || cc == 38) return 1;
                if (cc == 7) return 2;
                if (cc == 11) return 3;
                if (cc == 10) return 4;
                return 5;
            };
            std::sort(chEvents.begin(), chEvents.end(), [&](const ChEvent& a, const ChEvent& b) {
                if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
                int pa = chEventPriority(a.type), pb = chEventPriority(b.type);
                if (pa != pb) return pa < pb;
                if (a.type == 1 && b.type == 1) return ccPriority(a.p1) < ccPriority(b.p1);
                return false;
            });

            auto applyChFx = [&](float* segL, float* segR, int len, int segEndSample) {
                int64_t segTick = tickForTime((double)(pos + segEndSample) / m_sampleRate);
                int sendRev = chSynth.m_channels[0].reverb;
                sendRev = std::max(sendRev, expr.getValueAtTick(expr.sysPartReverbSend[ch], segTick, 0));
                float chRev = sendRev / 127.0f;
                int sendChr = chSynth.m_channels[0].chorus;
                sendChr = std::max(sendChr, expr.getValueAtTick(expr.sysPartChorusSend[ch], segTick, 0));
                float chChr = sendChr / 127.0f;
                int sendDly = chSynth.m_channels[0].delay;
                sendDly = std::max(sendDly, expr.getValueAtTick(expr.sysPartDelaySend[ch], segTick, 0));
                float chDly = sendDly / 127.0f;
                if (chRev > 0.001f) chSynth.m_reverb.process(segL, segR, len, chRev * 0.6f);
                if (chChr > 0.001f) chSynth.m_chorus.process(segL, segR, len, chChr * 127.0f);
                if (chDly > 0.001f) {
                    float dlyTime = 0.1f + chSynth.m_delayTime * 0.9f;
                    float feedback = 0.2f + chSynth.m_delayFeedback * 0.5f;
                    chSynth.m_delay.process(segL, segR, len, dlyTime, feedback, chDly * 0.4f);
                }
            };

            std::vector<float> bl(blockLen, 0.0f), br(blockLen, 0.0f);
            int segStart = 0;
            int evIdx = 0;
            while (segStart < blockLen) {
                int segEnd = blockLen;
                if (evIdx < (int)chEvents.size()) segEnd = std::min(segEnd, chEvents[evIdx].sampleOffset);
                if (segEnd > segStart) {
                    std::vector<float> segL(segEnd - segStart, 0.0f);
                    std::vector<float> segR(segEnd - segStart, 0.0f);
                    chSynth.processBlock(segL, segR, segEnd - segStart);
                    applyChFx(segL.data(), segR.data(), segEnd - segStart, segEnd);
                    for (int i = 0; i < segEnd - segStart; i++) {
                        bl[segStart + i] = segL[i];
                        br[segStart + i] = segR[i];
                    }
                }
                while (evIdx < (int)chEvents.size() && chEvents[evIdx].sampleOffset == segEnd) {
                    auto& ev = chEvents[evIdx];
                    switch (ev.type) {
                        case 0: chSynth.noteOff(0, ev.p1); break;
                        case 1: chSynth.controlChange(0, ev.p1, ev.p2); break;
                        case 2: chSynth.pitchBend(0, ev.p1); break;
                        case 3: chSynth.noteOn(0, ev.p1, ev.p2); break;
                    }
                    evIdx++;
                }
                segStart = segEnd;
            }

            for (int i = 0; i < blockLen; i++) {
                left[pos + i] = bl[i];
                right[pos + i] = br[i];
            }
        }

        // 正規化 (--no-normalize対応)
        if (!noNormalize) {
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

bool SFSynthesizer::mixFromChannelWavs(const std::string& channelDir,
                                       const std::string& baseName,
                                       const std::string& outputPath,
                                       int sampleRate,
                                       bool noNormalize) {
    if (!std::filesystem::is_directory(channelDir)) return false;

    std::vector<float> mixL, mixR;
    int mixSamples = 0;
    int filesFound = 0;

    for (auto& entry : std::filesystem::directory_iterator(channelDir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname.find(baseName + "_") != 0) continue;
        if (fname.substr(fname.size() - 4) != ".wav") continue;

        std::vector<float> chL, chR;
        int chRate = 0;
        if (!WavReader::read(entry.path().string(), chL, chR, chRate)) continue;

        if (filesFound == 0) {
            mixL.resize(chL.size(), 0.0f);
            mixR.resize(chR.size(), 0.0f);
            mixSamples = (int)chL.size();
        } else {
            if ((int)chL.size() > mixSamples) {
                mixL.resize(chL.size(), 0.0f);
                mixR.resize(chR.size(), 0.0f);
                mixSamples = (int)chL.size();
            }
        }

        for (int i = 0; i < (int)chL.size(); i++) {
            mixL[i] += chL[i];
            mixR[i] += chR[i];
        }
        filesFound++;
    }

    if (filesFound == 0) return false;

    // Normalize
    if (!noNormalize) {
        float peak = 1e-6f;
        for (int i = 0; i < mixSamples; i++) {
            float lv = std::isfinite(mixL[i]) ? std::abs(mixL[i]) : 0.0f;
            float rv = std::isfinite(mixR[i]) ? std::abs(mixR[i]) : 0.0f;
            peak = std::max(peak, std::max(lv, rv));
        }
        float gain = (peak > 1e-6f) ? (0.95f / peak) : 1.0f;
        for (int i = 0; i < mixSamples; i++) {
            mixL[i] = std::isfinite(mixL[i]) ? mixL[i] * gain : 0.0f;
            mixR[i] = std::isfinite(mixR[i]) ? mixR[i] * gain : 0.0f;
        }
    }

    return WavWriter::write(outputPath, mixL, mixR, sampleRate);
}

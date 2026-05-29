
#define _USE_MATH_DEFINES
#include "SynthInstrument.h"
#include "Filters.h"
#include "System/Console/Trace.h"
#include <string.h>
#include <math.h>

static const char *waveNames[]    = { "sine", "tri", "saw", "square" };
static const char *osc2Names[]    = { "sine", "tri", "saw", "square", "off" };
static const char *fltModeNames[] = { "lp", "hp", "bp", "notch" };
static const char *lfoDestNames[] = { "pitch", "fcut", "amp" };
static const char *onOffNames[]   = { "off", "on" };

fixed SynthInstrument::noteTable_[128];
fixed SynthInstrument::sineTable_[256];
bool  SynthInstrument::tablesBuilt_ = false;

void SynthInstrument::BuildNoteTable() {
    if (tablesBuilt_) return;
    for (int i = 0; i < 128; i++) {
        float freq = (float)(440.0 * pow(2.0, (i - 69) / 12.0));
        noteTable_[i] = fl2fp(freq / 44100.0f);
    }
    for (int i = 0; i < 256; i++) {
        sineTable_[i] = fl2fp((float)sin(2.0 * M_PI * i / 256.0));
    }
    tablesBuilt_ = true;
}

SynthInstrument::SynthInstrument() {
    strcpy(name_, "0");
    initialized_ = false;
    memset(voice_, 0, sizeof(voice_));
    tableState_.Reset();

    volume_     = new Variable("volume",       SYIP_VOL,  200);
    osc1Wave_   = new Variable("osc1 wave",    SYIP_O1WV, waveNames,    SWF_LAST, SWF_SAW);
    osc2Wave_   = new Variable("osc2 wave",    SYIP_O2WV, osc2Names,    SO2_LAST, SO2_OFF);
    osc2Detune_ = new Variable("osc2 detune",  SYIP_O2DT, 128);
    osc2Mix_    = new Variable("osc2 mix",     SYIP_O2MX, 128);
    noteLength_ = new Variable("note length",  SYIP_NLEN, 0);
    envAttack_  = new Variable("env attack",   SYIP_EATK, 0);
    envDecay_   = new Variable("env decay",    SYIP_EDEC, 40);
    envSustain_ = new Variable("env sustain",  SYIP_ESUS, 200);
    envRelease_ = new Variable("env release",  SYIP_EREL, 20);
    fltCutoff_  = new Variable("flt cutoff",   SYIP_FCUT, 255);
    fltReso_    = new Variable("flt reso",     SYIP_FRES, 0);
    fltMode_    = new Variable("flt mode",     SYIP_FMOD, fltModeNames, SFM_LAST, SFM_LP);
    fltEnvAmt_  = new Variable("flt env amt",  SYIP_FEAM, 128);
    fltEnvAtk_  = new Variable("flt env atk",  SYIP_FEAT, 0);
    lfoRate_    = new Variable("lfo rate",     SYIP_LRAT, 16);
    lfoDepth_   = new Variable("lfo depth",    SYIP_LDPT, 0);
    lfoDest_    = new Variable("lfo dest",     SYIP_LDST, lfoDestNames, SLD_LAST, SLD_PITCH);
    tableAuto_  = new Variable("table auto",   SYIP_TBLA, onOffNames,   2,        0);
    table_      = new Variable("table",        SYIP_TABL, -1);

    Insert(volume_);
    Insert(osc1Wave_);
    Insert(osc2Wave_);
    Insert(osc2Detune_);
    Insert(osc2Mix_);
    Insert(noteLength_);
    Insert(envAttack_);
    Insert(envDecay_);
    Insert(envSustain_);
    Insert(envRelease_);
    Insert(fltCutoff_);
    Insert(fltReso_);
    Insert(fltMode_);
    Insert(fltEnvAmt_);
    Insert(fltEnvAtk_);
    Insert(lfoRate_);
    Insert(lfoDepth_);
    Insert(lfoDest_);
    Insert(tableAuto_);
    Insert(table_);
}

SynthInstrument::~SynthInstrument() {
}

bool SynthInstrument::Init() {
    tableState_.Reset();
    initialized_ = true;
    return true;
}

bool SynthInstrument::Start(int channel, unsigned char note, bool retrigger) {
    if (channel < 0 || channel >= SONG_CHANNEL_COUNT) return false;
    if (note >= 128) return true;
    SynthVoiceState &v = voice_[channel];
    if (retrigger || v.envStage == SES_IDLE) {
        v.osc1Phase   = 0;
        v.osc2Phase   = 0;
        v.envStage    = SES_ATTACK;
        v.envTick     = 0;
        v.envLevel    = 0;
        v.fltEnvLevel = 0;
        v.fltEnvTick  = 0;
        v.lfoPhase    = 0;
        v.noteTick    = 0;
    }
    v.osc1Inc = noteTable_[note];
    // osc2 detune: param 128=center, each unit = 1 cent (1/100 semitone)
    int centOffset = osc2Detune_->GetInt() - 128;
    float ratio    = (float)pow(2.0, centOffset / 1200.0);
    v.osc2Inc = fp_mul(v.osc1Inc, fl2fp(ratio));
    v.note    = note;
    v.active  = true;
    Trace::Log("SYNTH","Start ch=%d note=%d retrig=%d stage=%d level=%d",
               channel, note, retrigger, v.envStage, v.envLevel);
    return true;
}

void SynthInstrument::Stop(int channel) {
    if (channel < 0 || channel >= SONG_CHANNEL_COUNT) return;
    SynthVoiceState &v = voice_[channel];
    if (v.envStage != SES_IDLE) {
        v.envStage = SES_RELEASE;
        v.envTick  = 0;
    }
}

void SynthInstrument::OnStart() {
    tableState_.Reset();
}

// Returns one sample in [-FP_ONE, FP_ONE] for the given waveform and phase in [0, FP_ONE).
fixed SynthInstrument::sampleForOsc(int waveform, fixed phase) {
    switch (waveform) {
        case SWF_SINE: {
            // use upper 8 bits of phase as table index
            int idx = (phase >> (FIXED_SHIFT - 8)) & 0xFF;
            return sineTable_[idx];
        }
        case SWF_TRI:
            // -FP_ONE at 0, +FP_ONE at FP_ONE/2, -FP_ONE at FP_ONE
            if (phase < FP_ONE / 2)
                return fp_mul(fl2fp(4.0f), phase) - FP_ONE;
            else
                return fl2fp(3.0f) - fp_mul(fl2fp(4.0f), phase);
        case SWF_SAW:
            // linear ramp from -FP_ONE to +FP_ONE
            return (phase << 1) - FP_ONE;
        case SWF_SQUARE:
        default:
            return (phase < FP_ONE / 2) ? FP_ONE : -FP_ONE;
    }
}

bool SynthInstrument::Render(int channel, fixed *buffer, int size, bool updateTick) {
    if (channel < 0 || channel >= SONG_CHANNEL_COUNT) return false;
    SynthVoiceState &v = voice_[channel];
    if (!v.active) return false;

    // Read params once — avoids repeated vtable lookups in the sample loop
    fixed volumeFP   = fl2fp(volume_->GetInt()     / 255.0f);
    int   iO1W       = osc1Wave_->GetInt();
    int   iO2W       = osc2Wave_->GetInt();
    fixed osc2MixFP  = fl2fp(osc2Mix_->GetInt()    / 255.0f);
    int   iNLen      = noteLength_->GetInt();
    int   iEAtk      = envAttack_->GetInt();
    int   iEDec      = envDecay_->GetInt();
    fixed sustainFP  = fl2fp(envSustain_->GetInt()  / 255.0f);
    int   iERel      = envRelease_->GetInt();
    int   iFCut      = fltCutoff_->GetInt();
    int   iFRes      = fltReso_->GetInt();
    int   iFEAm      = fltEnvAmt_->GetInt();
    int   iFEAtk     = fltEnvAtk_->GetInt();
    int   iLRate     = lfoRate_->GetInt();
    fixed lfoDepthFP = fl2fp(lfoDepth_->GetInt()   / 255.0f);
    int   iLDst      = lfoDest_->GetInt();

    // ----------------------------------------------------------------
    // Tick-level state updates (k-rate: once per buffer)
    // ----------------------------------------------------------------
    if (updateTick) {
        // LFO phase
        if (iLRate > 0) {
            v.lfoPhase += FP_ONE / iLRate;
            if (v.lfoPhase >= FP_ONE) v.lfoPhase -= FP_ONE;
        }

        // Amplitude envelope
        switch (v.envStage) {
            case SES_ATTACK:
                if (iEAtk == 0) {
                    v.envLevel = FP_ONE;
                    v.envStage = SES_DECAY;
                    Trace::Log("SYNTH","ch=%d ATK->DEC level=%d",channel,v.envLevel);
                } else {
                    v.envLevel += FP_ONE / iEAtk;
                    if (v.envLevel >= FP_ONE) {
                        v.envLevel = FP_ONE;
                        v.envStage = SES_DECAY;
                        Trace::Log("SYNTH","ch=%d ATK->DEC level=%d",channel,v.envLevel);
                    }
                }
                break;
            case SES_DECAY:
                if (iEDec == 0 || v.envLevel <= sustainFP) {
                    v.envLevel = sustainFP;
                    v.envStage = SES_SUSTAIN;
                    Trace::Log("SYNTH","ch=%d DEC->SUS level=%d",channel,v.envLevel);
                } else {
                    v.envLevel -= (FP_ONE - sustainFP) / iEDec;
                    if (v.envLevel <= sustainFP) {
                        v.envLevel = sustainFP;
                        v.envStage = SES_SUSTAIN;
                        Trace::Log("SYNTH","ch=%d DEC->SUS level=%d",channel,v.envLevel);
                    }
                }
                break;
            case SES_SUSTAIN:
                v.envLevel = sustainFP;
                break;
            case SES_RELEASE:
                if (iERel == 0) {
                    v.envLevel = 0;
                    v.envStage = SES_IDLE;
                    v.active   = false;
                    Trace::Log("SYNTH","ch=%d REL->IDLE (instant)",channel);
                } else {
                    v.envLevel -= v.envLevel / iERel;
                    if (v.envLevel < 32) {
                        v.envLevel = 0;
                        v.envStage = SES_IDLE;
                        v.active   = false;
                        Trace::Log("SYNTH","ch=%d REL->IDLE level<%d",channel,32);
                    }
                }
                break;
            case SES_IDLE:
                v.active = false;
                Trace::Log("SYNTH","ch=%d IDLE->deactivated",channel);
                break;
        }

        // Filter envelope (attack-only, holds at peak)
        if (v.fltEnvLevel < FP_ONE) {
            if (iFEAtk == 0) {
                v.fltEnvLevel = FP_ONE;
            } else {
                v.fltEnvLevel += FP_ONE / iFEAtk;
                if (v.fltEnvLevel > FP_ONE) v.fltEnvLevel = FP_ONE;
            }
        }

        // Self-gate: fire release after noteLength ticks (0 = hold)
        if (iNLen > 0) {
            v.noteTick++;
            if (v.noteTick >= iNLen &&
                v.envStage != SES_RELEASE && v.envStage != SES_IDLE) {
                v.envStage = SES_RELEASE;
                v.envTick  = 0;
                Trace::Log("SYNTH","ch=%d self-gate RELEASE (nLen=%d tick=%d)",channel,iNLen,v.noteTick);
            }
        }
    }

    // ----------------------------------------------------------------
    // LFO value (k-rate, held constant across this buffer)
    // ----------------------------------------------------------------
    fixed lfoVal = 0;
    if (iLRate > 0 && lfoDepth_->GetInt() > 0) {
        int idx = (v.lfoPhase >> (FIXED_SHIFT - 8)) & 0xFF;
        lfoVal  = fp_mul(sineTable_[idx], lfoDepthFP);
        // lfoVal is now in [-lfoDepthFP, +lfoDepthFP]
    }

    // ----------------------------------------------------------------
    // Filter cutoff with envelope and LFO modulation
    // ----------------------------------------------------------------
    fixed cutFP = fl2fp(iFCut / 255.0f);  // [0, FP_ONE]

    if (iFEAm != 128) {
        // fltEnvAmt 128 = neutral, 0 = max negative, 255 = max positive
        fixed envAmt = fl2fp((iFEAm - 128) / 128.0f);  // [-FP_ONE, +FP_ONE]
        cutFP += fp_mul(envAmt, v.fltEnvLevel);
    }
    if (iLDst == SLD_FCUT) {
        cutFP += lfoVal;  // already depth-scaled
    }
    if (cutFP < 0)       cutFP = 0;
    if (cutFP > FP_ONE)  cutFP = FP_ONE;

    fixed resFP = fl2fp(iFRes / 255.0f);

    // Always use FLT_LOWPASS — HP/BP/notch modes are not yet implemented in Filters.cpp
    set_filter(channel, FLT_LOWPASS, cutFP, resFP, 0, false);
    filter_t *flt    = get_filter(channel);
    bool filtering   = (cutFP < FP_ONE) || (iFRes > 0);
    fixed fltFreq    = flt->freq;
    fixed fltReso    = flt->reso;

    // ----------------------------------------------------------------
    // Pitch LFO: fractional increment applied per-sample
    // ----------------------------------------------------------------
    fixed pitchMod = 0;
    if (iLDst == SLD_PITCH) {
        // ±2% pitch deviation at full depth
        pitchMod = fp_mul(v.osc1Inc, fp_mul(lfoVal, fl2fp(0.02f)));
    }

    // ----------------------------------------------------------------
    // Sample loop (a-rate)
    // ----------------------------------------------------------------
    fixed *out = buffer;

    for (int i = 0; i < size; i++) {
        // --- Oscillator 1 ---
        fixed s1 = sampleForOsc(iO1W, v.osc1Phase);
        v.osc1Phase += v.osc1Inc + pitchMod;
        if (v.osc1Phase >= FP_ONE) v.osc1Phase -= FP_ONE;
        if (v.osc1Phase <  0)      v.osc1Phase += FP_ONE;

        // --- Oscillator 2 + crossfade mix ---
        fixed sig;
        if (iO2W != SO2_OFF) {
            fixed s2 = sampleForOsc(iO2W, v.osc2Phase);
            v.osc2Phase += v.osc2Inc + pitchMod;
            if (v.osc2Phase >= FP_ONE) v.osc2Phase -= FP_ONE;
            if (v.osc2Phase <  0)      v.osc2Phase += FP_ONE;
            sig = fp_mul(s1, FP_ONE - osc2MixFP) + fp_mul(s2, osc2MixFP);
        } else {
            sig = s1;
        }

        // --- Amplitude envelope ---
        sig = fp_mul(sig, v.envLevel);

        // --- Amplitude LFO: tremolo around unity ---
        if (iLDst == SLD_AMP) {
            // lfoVal in [-depth, +depth]; tremolo: 0..1 amplitude
            fixed ampFactor = FP_ONE + lfoVal;
            if (ampFactor < 0)       ampFactor = 0;
            if (ampFactor > FP_ONE)  ampFactor = FP_ONE;
            sig = fp_mul(sig, ampFactor);
        }

        // --- Volume ---
        sig = fp_mul(sig, volumeFP);

        // --- Filter (1-pole resonant LP, mix=0 → pure LP path) ---
        fixed sL, sR;
        if (filtering) {
            // Left channel
            flt->speed[0]  = fp_mul(flt->speed[0], fltReso) + fp_mul(fp_sub(sig, flt->height[0]), fltFreq);
            flt->height[0] += flt->speed[0];
            sL = flt->height[0];
            // Right channel (same mono input)
            flt->speed[1]  = fp_mul(flt->speed[1], fltReso) + fp_mul(fp_sub(sig, flt->height[1]), fltFreq);
            flt->height[1] += flt->speed[1];
            sR = flt->height[1];
        } else {
            sL = sR = sig;
        }

        *out++ = sL << FIXED_SHIFT;
        *out++ = sR << FIXED_SHIFT;
    }

    return v.active;
}

bool SynthInstrument::IsInitialized() {
    return initialized_;
}

bool SynthInstrument::IsEmpty() {
    return false;
}

const char *SynthInstrument::GetName() {
    return name_;
}

void SynthInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
}

void SynthInstrument::Purge() {
}

int SynthInstrument::GetTable() {
    return table_->GetInt();
}

bool SynthInstrument::GetTableAutomation() {
    return tableAuto_->GetInt() != 0;
}

void SynthInstrument::GetTableState(TableSaveState &state) {
    memcpy(&state, &tableState_, sizeof(TableSaveState));
}

void SynthInstrument::SetTableState(TableSaveState &state) {
    memcpy(&tableState_, &state, sizeof(TableSaveState));
}

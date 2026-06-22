#include "ReverbBus.h"
#include "Application/Model/Project.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include <string.h>
#include <math.h>

// Reverb bus FourCCs for real-time polling
#define VAR_RV0SZ MAKE_FOURCC('R','V','0','S')
#define VAR_RV0DM MAKE_FOURCC('R','V','0','D')
#define VAR_RV0WT MAKE_FOURCC('R','V','0','W')
#define VAR_RV1SZ MAKE_FOURCC('R','V','1','S')
#define VAR_RV1DM MAKE_FOURCC('R','V','1','D')
#define VAR_RV1WT MAKE_FOURCC('R','V','1','W')
#define VAR_RV2SZ MAKE_FOURCC('R','V','2','S')
#define VAR_RV2DM MAKE_FOURCC('R','V','2','D')
#define VAR_RV2WT MAKE_FOURCC('R','V','2','W')

// ------------------------------------------------------------------
// Static storage
// ------------------------------------------------------------------

bool ReverbBus::inited_ = false;

fixed ReverbBus::gAccum_[REVERB_BUS_COUNT][RVB_MAX_FRAME * 2];

fixed ReverbBus::combMem_  [REVERB_BUS_COUNT][8][RVB_COMB_MAX];
fixed ReverbBus::apMem_    [REVERB_BUS_COUNT][4][RVB_ALLPASS_MAX];
fixed ReverbBus::erMem_    [REVERB_BUS_COUNT][2][RVB_ER_MAX];

ReverbBus::CombState    ReverbBus::combs_   [REVERB_BUS_COUNT][8];
ReverbBus::AllPassState ReverbBus::allpasses_[REVERB_BUS_COUNT][4];
ReverbBus::ERState      ReverbBus::er_      [REVERB_BUS_COUNT][2];
ReverbBusConfig         ReverbBus::configs_[REVERB_BUS_COUNT];
bool                    ReverbBus::active_  [REVERB_BUS_COUNT];
bool                    ReverbBus::dirty_   [REVERB_BUS_COUNT];

// ------------------------------------------------------------------
// Base delay lengths (samples @ 44.1 kHz)
// ------------------------------------------------------------------

static const int combBaseLen[8] = {
    1559, 1613, 1493, 1427,   // left channel combs
    1567, 1607, 1487, 1433    // right channel combs
};

static const int apBaseLen[4] = {
    227, 557,   // left allpasses
    233, 541    // right allpasses
};

static const int erBaseTaps[RVB_ER_TAPS] = {
    347, 631, 947, 1283, 1583, 1913, 2243, 2579
};

// ------------------------------------------------------------------
// Constructor / Init
// ------------------------------------------------------------------

ReverbBus::ReverbBus() {
    if (!inited_) {
        inited_ = true;

        // Zero all memory
        SYS_MEMSET(combMem_,   0, sizeof(combMem_));
        SYS_MEMSET(apMem_,     0, sizeof(apMem_));
        SYS_MEMSET(erMem_,     0, sizeof(erMem_));
        SYS_MEMSET(gAccum_,    0, sizeof(gAccum_));
        SYS_MEMSET(active_,    0, sizeof(active_));
        SYS_MEMSET(dirty_,     0, sizeof(dirty_));

        // Default configs
        for (int b = 0; b < REVERB_BUS_COUNT; b++) {
            configs_[b].size = 64;
            configs_[b].damp = 64;
            configs_[b].wet  = 64;
            dirty_[b] = true;
        }

        // Wire up delay-line pointers
        for (int b = 0; b < REVERB_BUS_COUNT; b++) {
            for (int c = 0; c < 8; c++) {
                combs_[b][c].buffer = combMem_[b][c];
                combs_[b][c].size   = combBaseLen[c] / 2; // default = half size
                combs_[b][c].index  = 0;
                combs_[b][c].feedback = fl2fp(0.5f);
                combs_[b][c].damping  = fl2fp(0.5f);
                combs_[b][c].store    = 0;
            }
            for (int a = 0; a < 4; a++) {
                allpasses_[b][a].buffer = apMem_[b][a];
                allpasses_[b][a].size   = apBaseLen[a];
                allpasses_[b][a].index  = 0;
                allpasses_[b][a].feedback = fl2fp(0.5f);
            }
            for (int ch = 0; ch < 2; ch++) {
                er_[b][ch].buffer = erMem_[b][ch];
                er_[b][ch].index  = 0;
                for (int t = 0; t < RVB_ER_TAPS; t++) {
                    // Stagger L/R taps slightly for stereo width
                    int offset = (ch == 0) ? 0 : 7;
                    er_[b][ch].taps[t] = erBaseTaps[t] + offset;
                    // Exponentially decaying gains, louder than before
                    float g = powf(0.75f, (float)t) * 1.5f;
                    if (g > 1.0f) g = 1.0f;
                    er_[b][ch].gains[t] = fl2fp(g);
                }
            }
        }
    }
}

ReverbBus::~ReverbBus() {
}

// ------------------------------------------------------------------
// Config API
// ------------------------------------------------------------------

void ReverbBus::SetBusConfig(int bus, const ReverbBusConfig &config) {
    if (bus < 0 || bus >= REVERB_BUS_COUNT) return;
    configs_[bus] = config;
    dirty_[bus] = true;
}

ReverbBusConfig ReverbBus::GetBusConfig(int bus) {
    if (bus < 0 || bus >= REVERB_BUS_COUNT) return ReverbBusConfig{0,0,0};
    return configs_[bus];
}

bool ReverbBus::IsBusActive(int bus) {
    if (bus < 0 || bus >= REVERB_BUS_COUNT) return false;
    return active_[bus];
}

void ReverbBus::ResetAll() {
    SYS_MEMSET(combMem_, 0, sizeof(combMem_));
    SYS_MEMSET(apMem_,   0, sizeof(apMem_));
    SYS_MEMSET(erMem_,   0, sizeof(erMem_));
    SYS_MEMSET(active_,  0, sizeof(active_));
    for (int b = 0; b < REVERB_BUS_COUNT; b++) {
        for (int c = 0; c < 8; c++) {
            combs_[b][c].index = 0;
            combs_[b][c].store = 0;
        }
        for (int a = 0; a < 4; a++) {
            allpasses_[b][a].index = 0;
        }
        for (int ch = 0; ch < 2; ch++) {
            er_[b][ch].index = 0;
        }
        dirty_[b] = true;
    }
}

// ------------------------------------------------------------------
// Accumulation (called from instrument Render)
// ------------------------------------------------------------------

void ReverbBus::Accumulate(int bus, fixed *buffer, int size, int send) {
    if (bus < 0 || bus >= REVERB_BUS_COUNT) return;
    if (send <= 0) return;
    if (size > RVB_MAX_FRAME) size = RVB_MAX_FRAME;

    fixed sendFP = fl2fp(send / 255.0f);
    fixed *accum = gAccum_[bus];

    for (int i = 0; i < size; i++) {
        accum[i * 2]     += fp_mul(buffer[i * 2],     sendFP);
        accum[i * 2 + 1] += fp_mul(buffer[i * 2 + 1], sendFP);
    }
    active_[bus] = true;
}

// ------------------------------------------------------------------
// Configure a bus from its current config variables
// ------------------------------------------------------------------

void ReverbBus::configureBus(int bus) {
    if (!dirty_[bus]) return;
    dirty_[bus] = false;

    ReverbBusConfig &cfg = configs_[bus];

    // Map size (0-255) to delay scaling 0.3 .. 1.5 and feedback 0.65 .. 0.98
    float sizeNorm = cfg.size / 255.0f;
    float scale    = 0.3f + sizeNorm * 1.2f;
    float fb       = 0.65f + sizeNorm * 0.33f;

    // Map damp (0-255) to damping coefficient 0.0 .. 0.5
    float dampNorm = cfg.damp / 255.0f;
    float damping  = dampNorm * 0.5f;

    fixed fbFP  = fl2fp(fb);
    fixed dmFP  = fl2fp(damping);

    for (int c = 0; c < 8; c++) {
        int len = (int)(combBaseLen[c] * scale);
        if (len < 16) len = 16;
        if (len >= RVB_COMB_MAX) len = RVB_COMB_MAX - 1;
        combs_[bus][c].size     = len;
        combs_[bus][c].feedback = fbFP;
        combs_[bus][c].damping  = dmFP;
        // don't reset index/store — allow smooth parameter changes
    }

    // Allpass feedback is fixed (controls diffusion, not decay)
    fixed apFB = fl2fp(0.5f);
    for (int a = 0; a < 4; a++) {
        allpasses_[bus][a].feedback = apFB;
        // size stays at base length (could also scale slightly)
    }
}

// ------------------------------------------------------------------
// Per-sample processing helpers (inline for speed)
// ------------------------------------------------------------------

static inline fixed processComb(ReverbBus::CombState &cs, fixed input) {
    fixed output = cs.buffer[cs.index];

    // One-pole lowpass in feedback path
    fixed damped = fp_mul(output, FP_ONE - cs.damping) + fp_mul(cs.store, cs.damping);
    cs.store = output;

    cs.buffer[cs.index] = input + fp_mul(damped, cs.feedback);
    cs.index++;
    if (cs.index >= cs.size) cs.index = 0;

    return output;
}

static inline fixed processAllPass(ReverbBus::AllPassState &aps, fixed input) {
    fixed bufout = aps.buffer[aps.index];
    fixed feedback = aps.feedback;

    aps.buffer[aps.index] = input + fp_mul(bufout, feedback);
    fixed output = bufout - fp_mul(aps.buffer[aps.index], feedback);

    aps.index++;
    if (aps.index >= aps.size) aps.index = 0;
    return output;
}

static inline fixed processER(ReverbBus::ERState &ers, fixed input) {
    ers.buffer[ers.index] = input;

    fixed out = 0;
    for (int t = 0; t < RVB_ER_TAPS; t++) {
        int tapPos = ers.index - ers.taps[t];
        if (tapPos < 0) tapPos += RVB_ER_MAX;
        out += fp_mul(ers.buffer[tapPos], ers.gains[t]);
    }

    ers.index++;
    if (ers.index >= RVB_ER_MAX) ers.index = 0;
    return out;
}

// ------------------------------------------------------------------
// Process one bus into the output buffer
// ------------------------------------------------------------------

void ReverbBus::processBus(int bus, fixed *out, int size) {
    configureBus(bus);

    ReverbBusConfig &cfg = configs_[bus];
    fixed wetFP = fl2fp(cfg.wet / 255.0f);

    fixed *accum = gAccum_[bus];

    CombState    *cb = combs_[bus];
    AllPassState *ap = allpasses_[bus];
    ERState      *er = er_[bus];

    bool hasInput = active_[bus];

    for (int i = 0; i < size; i++) {
        // Read accumulated send for this sample
        fixed inL = accum[i * 2];
        fixed inR = accum[i * 2 + 1];

        // If no new input this frame, use silence
        if (!hasInput) {
            inL = 0;
            inR = 0;
        }

        // Mono-sum for the reverb tank (divide by 2 for stereo->mono)
        fixed monoIn = (inL + inR) >> 1;

        // --- Early reflections (stereo) ---
        fixed erL = processER(er[0], monoIn);
        fixed erR = processER(er[1], monoIn);

        fixed tankInL = monoIn + erL;
        fixed tankInR = monoIn + erR;

        // --- Comb bank (4 per channel) ---
        fixed combSumL = 0;
        fixed combSumR = 0;
        for (int c = 0; c < 4; c++) {
            combSumL += processComb(cb[c],   tankInL);
            combSumR += processComb(cb[c+4], tankInR);
        }
        combSumL >>= 2; // average of 4 combs
        combSumR >>= 2;

        // --- All-pass diffusion (2 per channel) ---
        fixed apL = processAllPass(ap[0], combSumL);
        apL = processAllPass(ap[1], apL);
        fixed apR = processAllPass(ap[2], combSumR);
        apR = processAllPass(ap[3], apR);

        // --- Wet output with master wet level ---
        fixed wetL = fp_mul(apL, wetFP);
        fixed wetR = fp_mul(apR, wetFP);

        out[i * 2]     += wetL;
        out[i * 2 + 1] += wetR;

    }

    // Determine if tail is still ringing (simple energy threshold)
    bool stillRinging = false;
    if (!hasInput) {
        // Quick check: is there significant energy in any comb?
        for (int c = 0; c < 8 && !stillRinging; c++) {
            if (cb[c].store > 32 || cb[c].store < -32) stillRinging = true;
        }
    } else {
        stillRinging = true;
    }
    active_[bus] = stillRinging;
}

// ------------------------------------------------------------------
// Main Render — called by master AudioMixer
// ------------------------------------------------------------------

bool ReverbBus::Render(fixed *buffer, int samplecount) {
    if (samplecount > RVB_MAX_FRAME) samplecount = RVB_MAX_FRAME;

    // Poll project variables for real-time parameter changes
    Project *project = Project::GetInstance();
    if (project) {
        static const FourCC sizeIDs[3] = { VAR_RV0SZ, VAR_RV1SZ, VAR_RV2SZ };
        static const FourCC dampIDs[3] = { VAR_RV0DM, VAR_RV1DM, VAR_RV2DM };
        static const FourCC wetIDs[3]  = { VAR_RV0WT, VAR_RV1WT, VAR_RV2WT };

        for (int b = 0; b < REVERB_BUS_COUNT; b++) {
            Variable *vSize = project->FindVariable(sizeIDs[b]);
            Variable *vDamp = project->FindVariable(dampIDs[b]);
            Variable *vWet  = project->FindVariable(wetIDs[b]);

            int sz = vSize ? vSize->GetInt() : 64;
            int dm = vDamp ? vDamp->GetInt() : 64;
            int wt = vWet  ? vWet->GetInt()  : 64;

            if (sz != configs_[b].size || dm != configs_[b].damp || wt != configs_[b].wet) {
                configs_[b].size = sz;
                configs_[b].damp = dm;
                configs_[b].wet  = wt;
                dirty_[b] = true;
            }
        }
    }

    bool anyOutput = false;

    // Zero output buffer — we accumulate wet from all 3 buses into it
    SYS_MEMSET(buffer, 0, samplecount * 2 * sizeof(fixed));

    for (int b = 0; b < REVERB_BUS_COUNT; b++) {
        if (configs_[b].wet > 0) {
            processBus(b, buffer, samplecount);
            anyOutput = true;
        }
    }

    // Clear accumulations for next frame
    for (int b = 0; b < REVERB_BUS_COUNT; b++) {
        SYS_MEMSET(gAccum_[b], 0, samplecount * 2 * sizeof(fixed));
    }

    return anyOutput;
}

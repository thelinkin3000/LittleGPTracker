#include "DelayBus.h"
#include "Application/Model/Project.h"
#include "System/System/System.h"
#include <string.h>

// ------------------------------------------------------------------
// Static storage
// ------------------------------------------------------------------

bool DelayBus::inited_ = false;

fixed DelayBus::gAccum_[DELAY_BUS_COUNT][DLB_MAX_FRAME * 2];

fixed DelayBus::delayMem_[DELAY_BUS_COUNT][2][DLB_MAX_DELAY_SAMPLES];

int    DelayBus::writeIndex_[DELAY_BUS_COUNT][2];
int    DelayBus::delayLen_[DELAY_BUS_COUNT][2];
fixed  DelayBus::feedbackFP_[DELAY_BUS_COUNT];
fixed  DelayBus::wetFP_[DELAY_BUS_COUNT];
bool   DelayBus::pingPong_[DELAY_BUS_COUNT];
DelayBusConfig DelayBus::configs_[DELAY_BUS_COUNT];
bool   DelayBus::active_[DELAY_BUS_COUNT];
bool   DelayBus::dirty_[DELAY_BUS_COUNT];

// ------------------------------------------------------------------
// Constructor / Init
// ------------------------------------------------------------------

DelayBus::DelayBus() {
    if (!inited_) {
        inited_ = true;

        SYS_MEMSET(delayMem_, 0, sizeof(delayMem_));
        SYS_MEMSET(gAccum_,   0, sizeof(gAccum_));
        SYS_MEMSET(writeIndex_, 0, sizeof(writeIndex_));
        SYS_MEMSET(active_,   0, sizeof(active_));
        SYS_MEMSET(dirty_,    0, sizeof(dirty_));

        for (int b = 0; b < DELAY_BUS_COUNT; b++) {
            configs_[b].time     = 64;
            configs_[b].feedback = 64;
            configs_[b].wet      = 64;
            configs_[b].mode     = 0;
            dirty_[b] = true;
        }
    }
}

DelayBus::~DelayBus() {
}

// ------------------------------------------------------------------
// Config API
// ------------------------------------------------------------------

void DelayBus::SetBusConfig(int bus, const DelayBusConfig &config) {
    if (bus < 0 || bus >= DELAY_BUS_COUNT) return;
    configs_[bus] = config;
    dirty_[bus] = true;
}

DelayBusConfig DelayBus::GetBusConfig(int bus) {
    if (bus < 0 || bus >= DELAY_BUS_COUNT) return DelayBusConfig{0,0,0,0};
    return configs_[bus];
}

bool DelayBus::IsBusActive(int bus) {
    if (bus < 0 || bus >= DELAY_BUS_COUNT) return false;
    return active_[bus];
}

void DelayBus::ResetAll() {
    SYS_MEMSET(delayMem_, 0, sizeof(delayMem_));
    SYS_MEMSET(active_,   0, sizeof(active_));
    SYS_MEMSET(writeIndex_, 0, sizeof(writeIndex_));
    for (int b = 0; b < DELAY_BUS_COUNT; b++) {
        dirty_[b] = true;
    }
}

// ------------------------------------------------------------------
// Accumulation (called from instrument Render)
// ------------------------------------------------------------------

void DelayBus::Accumulate(int bus, fixed *buffer, int size, int send) {
    if (bus < 0 || bus >= DELAY_BUS_COUNT) return;
    if (send <= 0) return;
    if (size > DLB_MAX_FRAME) size = DLB_MAX_FRAME;

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

void DelayBus::configureBus(int bus) {
    if (!dirty_[bus]) return;
    dirty_[bus] = false;

    DelayBusConfig &cfg = configs_[bus];

    // Map time (0-255) to delay length 0 .. DLB_MAX_DELAY_SAMPLES-1
    // Use a curve that gives finer control at short times
    float timeNorm = cfg.time / 255.0f;
    int len = (int)(timeNorm * timeNorm * (DLB_MAX_DELAY_SAMPLES - 1));
    if (len < 1) len = 1;
    if (len >= DLB_MAX_DELAY_SAMPLES) len = DLB_MAX_DELAY_SAMPLES - 1;

    delayLen_[bus][0] = len;
    delayLen_[bus][1] = len;

    // Map feedback (0-255) to 0.0 .. 0.98 (avoid 1.0 to prevent runaway)
    float fb = (cfg.feedback / 255.0f) * 0.98f;
    feedbackFP_[bus] = fl2fp(fb);

    // Wet
    wetFP_[bus] = fl2fp(cfg.wet / 255.0f);

    // Mode
    pingPong_[bus] = (cfg.mode != 0);
}

// ------------------------------------------------------------------
// Process one bus into the output buffer
// ------------------------------------------------------------------

void DelayBus::processBus(int bus, fixed *out, int size) {
    configureBus(bus);

    fixed *accum = gAccum_[bus];
    fixed wet = wetFP_[bus];
    fixed fb  = feedbackFP_[bus];
    bool pp   = pingPong_[bus];

    fixed *bufL = delayMem_[bus][0];
    fixed *bufR = delayMem_[bus][1];
    int wL = writeIndex_[bus][0];
    int wR = writeIndex_[bus][1];
    int dL = delayLen_[bus][0];
    int dR = delayLen_[bus][1];

    bool hasInput = active_[bus];

    for (int i = 0; i < size; i++) {
        fixed inL = hasInput ? accum[i * 2]     : 0;
        fixed inR = hasInput ? accum[i * 2 + 1] : 0;

        // Read from delay line (current write index - delay length)
        int rL = wL - dL;
        if (rL < 0) rL += DLB_MAX_DELAY_SAMPLES;
        int rR = wR - dR;
        if (rR < 0) rR += DLB_MAX_DELAY_SAMPLES;

        fixed readL = bufL[rL];
        fixed readR = bufR[rR];

        // Write to delay line: input + feedback
        if (pp) {
            // Ping-pong: input to L (halved to keep level), cross-feedback L->R->L
            bufL[wL] = ((inL + inR) >> 1) + fp_mul(readR, fb);
            bufR[wR] = fp_mul(readL, fb);
        } else {
            bufL[wL] = inL + fp_mul(readL, fb);
            bufR[wR] = inR + fp_mul(readR, fb);
        }

        // Wet output
        out[i * 2]     += fp_mul(readL, wet);
        out[i * 2 + 1] += fp_mul(readR, wet);

        wL++;
        if (wL >= DLB_MAX_DELAY_SAMPLES) wL = 0;
        wR++;
        if (wR >= DLB_MAX_DELAY_SAMPLES) wR = 0;
    }

    writeIndex_[bus][0] = wL;
    writeIndex_[bus][1] = wR;

    // Tail detect: if no input, check if delay lines still have audible energy
    if (!hasInput) {
        bool stillRinging = false;
        // Check a few samples around the read head
        for (int s = 0; s < 4 && !stillRinging; s++) {
            int chkL = wL - 1 - s;
            if (chkL < 0) chkL += DLB_MAX_DELAY_SAMPLES;
            int chkR = wR - 1 - s;
            if (chkR < 0) chkR += DLB_MAX_DELAY_SAMPLES;
            if (bufL[chkL] > 32 || bufL[chkL] < -32) stillRinging = true;
            if (bufR[chkR] > 32 || bufR[chkR] < -32) stillRinging = true;
        }
        active_[bus] = stillRinging;
    }
}

// ------------------------------------------------------------------
// Main Render — called by master AudioMixer
// ------------------------------------------------------------------

bool DelayBus::Render(fixed *buffer, int samplecount) {
    if (samplecount > DLB_MAX_FRAME) samplecount = DLB_MAX_FRAME;

    // Poll project variables for real-time parameter changes
    Project *project = Project::GetInstance();
    if (project) {
        static const FourCC timeIDs[3] = { VAR_DL0TM, VAR_DL1TM, VAR_DL2TM };
        static const FourCC fbIDs[3]   = { VAR_DL0FB, VAR_DL1FB, VAR_DL2FB };
        static const FourCC wetIDs[3]  = { VAR_DL0WT, VAR_DL1WT, VAR_DL2WT };
        static const FourCC modeIDs[3] = { VAR_DL0MD, VAR_DL1MD, VAR_DL2MD };

        for (int b = 0; b < DELAY_BUS_COUNT; b++) {
            Variable *vTime = project->FindVariable(timeIDs[b]);
            Variable *vFb   = project->FindVariable(fbIDs[b]);
            Variable *vWet  = project->FindVariable(wetIDs[b]);
            Variable *vMode = project->FindVariable(modeIDs[b]);

            int tm = vTime ? vTime->GetInt() : 64;
            int f  = vFb   ? vFb->GetInt()   : 64;
            int w  = vWet  ? vWet->GetInt()  : 64;
            int m  = vMode ? vMode->GetInt() : 0;

            if (tm != configs_[b].time || f != configs_[b].feedback ||
                w != configs_[b].wet || m != configs_[b].mode) {
                configs_[b].time     = tm;
                configs_[b].feedback = f;
                configs_[b].wet      = w;
                configs_[b].mode     = m;
                dirty_[b] = true;
            }
        }
    }

    bool anyOutput = false;

    // Zero output buffer — we accumulate wet from all 3 buses into it
    SYS_MEMSET(buffer, 0, samplecount * 2 * sizeof(fixed));

    for (int b = 0; b < DELAY_BUS_COUNT; b++) {
        if (configs_[b].wet > 0) {
            processBus(b, buffer, samplecount);
            anyOutput = true;
        }
    }

    // Clear accumulations for next frame
    for (int b = 0; b < DELAY_BUS_COUNT; b++) {
        SYS_MEMSET(gAccum_[b], 0, samplecount * 2 * sizeof(fixed));
    }

    return anyOutput;
}

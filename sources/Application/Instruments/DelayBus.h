#ifndef _DELAY_BUS_H_
#define _DELAY_BUS_H_

#include "Services/Audio/AudioModule.h"
#include "Application/Utils/fixed.h"

#define DELAY_BUS_COUNT 3

// Maximum delay time: 2 seconds @ 44.1 kHz
#define DLB_MAX_DELAY_SAMPLES 88200

// Maximum samples per audio callback frame
#define DLB_MAX_FRAME 2048

// Per-bus configuration
struct DelayBusConfig {
    int time;     // 0-255 -> delay time 0..2000ms
    int feedback; // 0-255 -> feedback amount
    int wet;      // 0-255 -> wet return level
    int mode;     // 0=mono, 1=ping-pong
};

class DelayBus : public AudioModule {
public:
    DelayBus();
    virtual ~DelayBus();

    // AudioModule: render wet output into buffer (added to dry mix by caller)
    virtual bool Render(fixed *buffer, int samplecount);

    // Called by instruments during their Render() to feed the delay
    static void Accumulate(int bus, fixed *buffer, int size, int send);

    // Called by Project/Mixer to update bus configs
    static void SetBusConfig(int bus, const DelayBusConfig &config);
    static DelayBusConfig GetBusConfig(int bus);

    static bool IsBusActive(int bus);
    static void ResetAll();

private:
    void processBus(int bus, fixed *out, int size);
    void configureBus(int bus);

    static bool inited_;

    // Global send accumulation (cleared each frame after processing)
    static fixed gAccum_[DELAY_BUS_COUNT][DLB_MAX_FRAME * 2];

    // Delay-line memory (allocated once, never freed)
    static fixed delayMem_[DELAY_BUS_COUNT][2][DLB_MAX_DELAY_SAMPLES];

    // Runtime state
    static int    writeIndex_[DELAY_BUS_COUNT][2];
    static int    delayLen_[DELAY_BUS_COUNT][2];   // current delay length in samples
    static fixed  feedbackFP_[DELAY_BUS_COUNT];
    static fixed  wetFP_[DELAY_BUS_COUNT];
    static bool   pingPong_[DELAY_BUS_COUNT];
    static DelayBusConfig configs_[DELAY_BUS_COUNT];
    static bool   active_[DELAY_BUS_COUNT];
    static bool   dirty_[DELAY_BUS_COUNT];
};

#endif

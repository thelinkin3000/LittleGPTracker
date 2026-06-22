#ifndef _REVERB_BUS_H_
#define _REVERB_BUS_H_

#include "Services/Audio/AudioModule.h"
#include "Application/Utils/fixed.h"

#define REVERB_BUS_COUNT 3

// Maximum delay-line sizes (at 44.1 kHz, these give ~0.75s per comb)
#define RVB_COMB_MAX      32768
#define RVB_ALLPASS_MAX   8192
#define RVB_ER_MAX        16384
#define RVB_ER_TAPS       8

// Maximum samples per audio callback frame
#define RVB_MAX_FRAME     2048

// Per-bus configuration (mirrors project-level variables)
struct ReverbBusConfig {
    int size;   // 0-255  -> room size / decay time
    int damp;   // 0-255  -> high-frequency damping
    int wet;    // 0-255  -> master wet return level
};

class ReverbBus : public AudioModule {
public:
    ReverbBus();
    virtual ~ReverbBus();

    // AudioModule: render wet output into buffer (added to dry mix by caller)
    virtual bool Render(fixed *buffer, int samplecount);

    // Called by instruments during their Render() to feed the reverb
    static void Accumulate(int bus, fixed *buffer, int size, int send);

    // Called by Project/Mixer to update bus configs
    static void SetBusConfig(int bus, const ReverbBusConfig &config);
    static ReverbBusConfig GetBusConfig(int bus);

    // Query whether a bus is currently producing any wet signal
    static bool IsBusActive(int bus);

    // Reset all delay lines (e.g. on song start)
    static void ResetAll();

    // --- Internal state structs (public so standalone helpers can use them) ---
    struct CombState {
        fixed *buffer;     // delay line
        int    size;       // current delay length
        int    index;      // write pointer
        fixed  feedback;   // feedback coefficient
        fixed  damping;    // damping coefficient
        fixed  store;      // lowpass filter z^-1 state
    };

    struct AllPassState {
        fixed *buffer;
        int    size;
        int    index;
        fixed  feedback;
    };

    struct ERState {
        fixed *buffer;
        int    index;
        int    taps[RVB_ER_TAPS];
        fixed  gains[RVB_ER_TAPS];
    };

private:
    void processBus(int bus, fixed *out, int size);
    void configureBus(int bus);

    static bool inited_;

    // Global send accumulation (cleared each frame after processing)
    static fixed gAccum_[REVERB_BUS_COUNT][RVB_MAX_FRAME * 2];

    // Delay-line memory (allocated once, never freed)
    static fixed combMem_  [REVERB_BUS_COUNT][8][RVB_COMB_MAX];
    static fixed apMem_    [REVERB_BUS_COUNT][4][RVB_ALLPASS_MAX];
    static fixed erMem_    [REVERB_BUS_COUNT][2][RVB_ER_MAX];

    // Runtime state
    static CombState    combs_   [REVERB_BUS_COUNT][8];
    static AllPassState allpasses_[REVERB_BUS_COUNT][4];
    static ERState      er_      [REVERB_BUS_COUNT][2];
    static ReverbBusConfig configs_[REVERB_BUS_COUNT];
    static bool         active_  [REVERB_BUS_COUNT];
    static bool         dirty_   [REVERB_BUS_COUNT];
};

#endif

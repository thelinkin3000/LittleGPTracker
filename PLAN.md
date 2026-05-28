# SynthInstrument Implementation Plan

## Engine Choice: 2-Oscillator Subtractive Synth

A subtractive synth is the right first engine because it reuses the existing SVF filter
(`Filters.h/.cpp`) directly, requires no new math beyond basic phase accumulators, and maps
cleanly to the parameter count that fits on lgpt's screen. FM can be layered on later as a
second engine type or a separate instrument type.

**Parameter set (20 params):**

| # | Name | Range | FourCC |
|---|---|---|---|
| 1 | volume | 0–255 | `SYIP_VOL` |
| 2 | osc1 wave | sine/tri/saw/square | `SYIP_O1WV` |
| 3 | osc2 wave | sine/tri/saw/square/off | `SYIP_O2WV` |
| 4 | osc2 detune | 0–255 (semitone cents, 128 = center) | `SYIP_O2DT` |
| 5 | osc2 mix | 0–255 | `SYIP_O2MX` |
| 6 | note length | 0–255 ticks (0 = hold until next note) | `SYIP_NLEN` |
| 7 | env attack | 0–255 (ticks) | `SYIP_EATK` |
| 8 | env decay | 0–255 (ticks) | `SYIP_EDEC` |
| 9 | env sustain | 0–255 (level) | `SYIP_ESUS` |
| 10 | env release | 0–255 (ticks) | `SYIP_EREL` |
| 11 | filter cutoff | 0–255 | `SYIP_FCUT` |
| 12 | filter reso | 0–255 | `SYIP_FRES` |
| 13 | filter mode | LP/HP/BP/notch | `SYIP_FMOD` |
| 14 | flt env amt | 0–255 (signed 128=center, 0=neg, 255=pos) | `SYIP_FEAM` |
| 15 | flt env atk | 0–255 (ticks) | `SYIP_FEAT` |
| 16 | lfo rate | 0–255 (ticks per cycle) | `SYIP_LRAT` |
| 17 | lfo depth | 0–255 | `SYIP_LDPT` |
| 18 | lfo dest | pitch/flt cutoff/amp | `SYIP_LDST` |
| 19 | table | 00–7F / off | `SYIP_TABL` (= `SIP_TABLE` FourCC, see note) |
| 20 | automation | on/off | `SYIP_TBLA` (= `SIP_TABLEAUTO` FourCC) |

> **Gate time context:** One sequencer row = 6 ticks. At 120 BPM each tick ≈ 20ms, so one
> row ≈ 125ms. The player calls `Stop()` (triggering release) every time a new note fires in
> the same channel. Without `noteLength`, attack + decay must complete within one row or the
> release fires before sustain is reached. `noteLength = 0` means hold until the next note
> fires (classic tracker gate); any nonzero value self-gates after that many ticks, matching
> the `MidiInstrument` `MIP_NOTELENGTH` pattern.

> **FourCC note for table/automation:** Use the same FourCC values as `SIP_TABLE`
> (`MAKE_FOURCC('T','A','B','L')`) and `SIP_TABLEAUTO` (`MAKE_FOURCC('T','B','L','A')`) so
> that the existing `ProcessButtonMask` logic in `InstrumentView.cpp:392` and
> `:374` that does `FindVariable(SIP_TABLE)` with a null-unchecked dereference continues to
> work without modification.

---

## Fixed-Point Clarification

`fixed.h` uses `FIXED_SHIFT = 15`, not 16. `FP_ONE = 32768`. The `audio-engine.md` calls it
"16.16" but the type is effectively 16.15 (signed 32-bit, 15 fractional bits). All DSP
math below targets this representation.

---

## Phase 1 — Enum and Slot Layout

**`sources/Application/Instruments/I_Instrument.h`**

Add `IT_SYNTH` before `IT_LAST`:

```cpp
enum InstrumentType {
    IT_SAMPLE = 0,
    IT_MIDI,
    IT_SYNTH,   // ← add
    IT_LAST
};
```

**`sources/Application/Model/Song.h`**

Add synth slot count and update `MAX_INSTRUMENT_COUNT`:

```cpp
#define MAX_SAMPLEINSTRUMENT_COUNT  0x80   // 128 — slots 0..127
#define MAX_MIDIINSTRUMENT_COUNT    0x10   //  16 — slots 128..143
#define MAX_SYNTHINSTRUMENT_COUNT   0x10   //  16 — slots 144..159  ← add
#define MAX_INSTRUMENT_COUNT \
    (MAX_SAMPLEINSTRUMENT_COUNT + MAX_MIDIINSTRUMENT_COUNT + MAX_SYNTHINSTRUMENT_COUNT)
```

Synth slots sit above MIDI slots so existing MIDI slot numbers (128–143) are unchanged.
No migration guard is needed for the slot boundary itself, but the `RestoreContent()`
legacy fallback (line 122: `it=(id<MAX_SAMPLEINSTRUMENT_COUNT)?IT_SAMPLE:IT_MIDI`) must
be updated to also check for the MIDI range:

```cpp
// was:
it = (id < MAX_SAMPLEINSTRUMENT_COUNT) ? IT_SAMPLE : IT_MIDI;
// becomes:
if (id < MAX_SAMPLEINSTRUMENT_COUNT)       it = IT_SAMPLE;
else if (id < MAX_SAMPLEINSTRUMENT_COUNT + MAX_MIDIINSTRUMENT_COUNT) it = IT_MIDI;
else                                        it = IT_SYNTH;
```

---

## Phase 2 — SynthInstrument Class

### Header: `sources/Application/Instruments/SynthInstrument.h`

```cpp
#ifndef _SYNTH_INSTRUMENT_H_
#define _SYNTH_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"
#include "Application/Utils/fixed.h"
#include "Foundation/Variables/Variable.h"

// FourCC parameter IDs
#define SYIP_VOL    MAKE_FOURCC('S','Y','V','L')
#define SYIP_O1WV   MAKE_FOURCC('O','1','W','V')
#define SYIP_O2WV   MAKE_FOURCC('O','2','W','V')
#define SYIP_O2DT   MAKE_FOURCC('O','2','D','T')
#define SYIP_O2MX   MAKE_FOURCC('O','2','M','X')
#define SYIP_EATK   MAKE_FOURCC('E','A','T','K')
#define SYIP_EDEC   MAKE_FOURCC('E','D','E','C')
#define SYIP_ESUS   MAKE_FOURCC('E','S','U','S')
#define SYIP_EREL   MAKE_FOURCC('E','R','E','L')
#define SYIP_FCUT   MAKE_FOURCC('S','F','C','T')
#define SYIP_FRES   MAKE_FOURCC('S','F','R','S')
#define SYIP_FMOD   MAKE_FOURCC('S','F','M','D')
#define SYIP_FEAM   MAKE_FOURCC('F','E','A','M')
#define SYIP_FEAT   MAKE_FOURCC('F','E','A','T')
#define SYIP_NLEN   MAKE_FOURCC('N','L','E','N')
#define SYIP_LRAT   MAKE_FOURCC('L','R','A','T')
#define SYIP_LDPT   MAKE_FOURCC('L','D','P','T')
#define SYIP_LDST   MAKE_FOURCC('L','D','S','T')
// table/automation reuse SIP_ FourCCs intentionally — see plan note
#define SYIP_TABL   MAKE_FOURCC('T','A','B','L')
#define SYIP_TBLA   MAKE_FOURCC('T','B','L','A')

enum SynthWaveform { SWF_SINE=0, SWF_TRI, SWF_SAW, SWF_SQUARE, SWF_LAST };
enum SynthOsc2Wave { SO2_SINE=0, SO2_TRI, SO2_SAW, SO2_SQUARE, SO2_OFF, SO2_LAST };
enum SynthLfoDest  { SLD_PITCH=0, SLD_FCUT, SLD_AMP, SLD_LAST };
enum SynthFltMode  { SFM_LP=0, SFM_HP, SFM_BP, SFM_NOTCH, SFM_LAST };

enum SynthEnvStage { SES_IDLE=0, SES_ATTACK, SES_DECAY, SES_SUSTAIN, SES_RELEASE };

struct SynthVoiceState {
    fixed  osc1Phase;
    fixed  osc2Phase;
    fixed  osc1Inc;          // phase increment per sample for this note
    fixed  osc2Inc;          // osc1Inc with detune applied
    fixed  envLevel;         // current amplitude envelope [0, FP_ONE]
    SynthEnvStage envStage;
    int    envTick;          // ticks elapsed in current stage
    fixed  fltEnvLevel;      // current filter envelope [0, FP_ONE]
    int    fltEnvTick;
    fixed  lfoPhase;
    int    noteTick;         // ticks since note started; triggers gate-off when == noteLength
    bool   active;
    unsigned char note;
};

class SynthInstrument : public I_Instrument {
public:
    SynthInstrument();
    virtual ~SynthInstrument();

    virtual bool Init();
    virtual bool Start(int channel, unsigned char note, bool retrigger = true);
    virtual void Stop(int channel);
    virtual void OnStart();
    virtual bool Render(int channel, fixed *buffer, int size, bool updateTick);
    virtual bool IsInitialized();
    virtual bool IsEmpty();
    virtual InstrumentType GetType() { return IT_SYNTH; }
    virtual const char *GetName();
    virtual void ProcessCommand(int channel, FourCC cc, ushort value);
    virtual void Purge();
    virtual int GetTable();
    virtual bool GetTableAutomation();
    virtual void GetTableState(TableSaveState &state);
    virtual void SetTableState(TableSaveState &state);

    static void BuildNoteTable();   // call once at startup

private:
    fixed sampleForOsc(int waveform, fixed phase);

    SynthVoiceState voice_[SONG_CHANNEL_COUNT];
    bool initialized_;
    char name_[20];
    TableSaveState tableState_;

    Variable *volume_;
    Variable *osc1Wave_;
    Variable *osc2Wave_;
    Variable *osc2Detune_;
    Variable *osc2Mix_;
    Variable *noteLength_;
    Variable *envAttack_;
    Variable *envDecay_;
    Variable *envSustain_;
    Variable *envRelease_;
    Variable *fltCutoff_;
    Variable *fltReso_;
    Variable *fltMode_;
    Variable *fltEnvAmt_;
    Variable *fltEnvAtk_;
    Variable *lfoRate_;
    Variable *lfoDepth_;
    Variable *lfoDest_;
    Variable *table_;
    Variable *tableAuto_;

    static fixed noteTable_[128];  // phase increment per sample for each MIDI note
    static fixed sineTable_[256];  // precomputed sine, range [-FP_ONE, FP_ONE]
    static bool  tablesBuilt_;
};

#endif
```

---

## Phase 3 — DSP Implementation

### `sources/Application/Instruments/SynthInstrument.cpp`

#### Static tables (build once at startup)

```
sineTable_[i] = fl2fp( sin(2π * i / 256) )    for i in 0..255

noteTable_[n] = fl2fp( 440.0 * pow(2.0, (n - 69) / 12.0) / 44100.0 )
                for n in 0..127
```

`BuildNoteTable()` is called from `InstrumentBank::OnStart()` — add the call there alongside
`init_filters()`.

#### `sampleForOsc(waveform, phase)` — returns one sample in [-FP_ONE, FP_ONE]

- `SWF_SINE`:   index into `sineTable_` using upper 8 bits of phase
- `SWF_SAW`:    `(phase >> (FIXED_SHIFT - 15)) - FP_ONE`  (linear ramp -1..+1)
- `SWF_SQUARE`: phase < FP_ONE/2 ? FP_ONE : -FP_ONE
- `SWF_TRI`:    triangle from phase using two linear segments

#### `Start(channel, note, retrigger)`

1. Look up `noteTable_[note]` → `voice_[channel].osc1Inc`
2. Apply detune to `osc2Inc`:
   - detune param 0–255, 128 = no detune
   - offset = `(detune - 128) * FP_ONE / 1200` ≈ cents
   - `osc2Inc = osc1Inc * (1 + cents_in_fp)`  (use `fp_mul`)
3. If `retrigger`: reset `osc1Phase = osc2Phase = 0`, set `envStage = SES_ATTACK`,
   `envTick = 0`, `fltEnvTick = 0`, `lfoPhase = 0`, `noteTick = 0`
4. `voice_[channel].active = true`
5. `voice_[channel].note = note`

#### `Stop(channel)`

Set `envStage = SES_RELEASE`, `envTick = 0`.

#### `Render(channel, buffer, size, updateTick)`

For each sample `i` in `[0, size)`:

1. **LFO sample** (computed once per buffer if `updateTick`, else held):
   - advance `lfoPhase += lfoInc` (where `lfoInc = FP_ONE / (lfoRate * tickSamples)`)
   - wrap phase
   - `lfoVal = sineTable_[lfoPhase >> (FIXED_SHIFT - 8) & 0xFF]`
   - scale by `lfoDepth`

2. **Oscillator 1:**
   - `s1 = sampleForOsc(osc1Wave, osc1Phase)`
   - `osc1Phase += osc1Inc` (+ pitch LFO if dest == SLD_PITCH)
   - wrap `osc1Phase` at `FP_ONE`

3. **Oscillator 2** (skip if wave == SO2_OFF):
   - `s2 = sampleForOsc(osc2Wave, osc2Phase)`
   - `osc2Phase += osc2Inc`
   - mix: `mixed = fp_mul(s1, FP_ONE - osc2MixFP) + fp_mul(s2, osc2MixFP)`
   - else: `mixed = s1`

4. **Amplitude envelope** (advance only when `updateTick`):
   - ATTACK: `envLevel += FP_ONE / envAttack`; when >= FP_ONE → DECAY
   - DECAY: `envLevel -= (FP_ONE - sustainFP) / envDecay`; when <= sustainFP → SUSTAIN
   - SUSTAIN: `envLevel = sustainFP`
   - RELEASE: `envLevel -= envLevel / envRelease`; when <= tiny threshold → IDLE, set `active = false`
   - Apply: `mixed = fp_mul(mixed, envLevel)`

5. **Apply amplitude LFO** if `lfoDest == SLD_AMP`:
   - `mixed = fp_mul(mixed, FP_ONE - fp_mul(lfoDepthFP, lfoVal))`

6. **Filter:**
   - Compute effective cutoff with filter envelope and LFO:
     - `fltCurrent = fltCutoff + fp_mul(fltEnvAmt, fltEnvLevel) + (LFO contribution)`
     - clamp 0..255
   - Call `set_filter(channel, fltMode, fltCurrent/255, fltReso/255, 255, false)`
   - Apply: use the filter from `get_filter(channel)` — look at `Filters.cpp` for the
     per-sample update loop (copy the pattern from `SampleInstrument::Render`)

7. **Volume and output:**
   - `out = fp_mul(mixed, volumeFP)`
   - Write interleaved: `buffer[2*i] = out; buffer[2*i+1] = out` (mono → stereo)
   - (Pan can be added in a later iteration)

8. **Filter envelope advance** (when `updateTick`):
   - ATTACK: `fltEnvLevel += FP_ONE / fltEnvAtk`; clamp at FP_ONE, then hold (no decay/release
     for filter env in v1 — keep it simple)

9. **Note length gate** (when `updateTick`):
   - Increment `noteTick`
   - If `noteLength > 0` and `noteTick >= noteLength`: set `envStage = SES_RELEASE`, `envTick = 0`
   - `noteLength = 0` means hold in sustain until `Stop()` is called externally

Return `voice_[channel].active`.

#### `IsEmpty()`

Return `true` if no voice is active AND all params are at their default values. Simplest
implementation: just return `false` to always save (same as `MidiInstrument`).

---

## Phase 4 — InstrumentBank Wiring

**`sources/Application/Instruments/InstrumentBank.cpp`**

1. Add `#include "SynthInstrument.h"` at the top.

2. `InstrumentTypeData[]` at line 12:
```cpp
char *InstrumentTypeData[IT_LAST] = {
    "Sample",
    "Midi",
    "Synth"   // ← add
};
```

3. Constructor — allocate synth slots after MIDI slots:
```cpp
for (int i = 0; i < MAX_SYNTHINSTRUMENT_COUNT; i++) {
    SynthInstrument *s = new SynthInstrument();
    instrument_[MAX_SAMPLEINSTRUMENT_COUNT + MAX_MIDIINSTRUMENT_COUNT + i] = s;
}
```

4. `RestoreContent()` — add `case IT_SYNTH:` in the switch at line 128:
```cpp
case IT_SYNTH:
    instr = new SynthInstrument();
    break;
```

5. `RestoreContent()` — update legacy boundary fallback (line 122, see Phase 1).

6. `Clone()` — add synth case in the switch at line 224. Synth instruments clone into the
   next available synth slot. `GetNext()` currently only scans sample slots and is not used
   for synth. Simplest approach for v1: return `NO_MORE_INSTRUMENT` for synth clone (same
   behavior as MIDI). Document this limitation.

7. `OnStart()` — add after `init_filters()`:
```cpp
SynthInstrument::BuildNoteTable();
```

---

## Phase 5 — UI

**`sources/Application/Views/InstrumentView.h`**

Add `fillSynthParameters()` to the protected section:
```cpp
void fillSynthParameters();
```

**`sources/Application/Views/InstrumentView.cpp`**

1. Add `#include "Application/Instruments/SynthInstrument.h"` at the top.

2. `onInstrumentChange()` — add case in the switch at line 50:
```cpp
case IT_SYNTH:
    fillSynthParameters();
    break;
```

3. Implement `fillSynthParameters()`:
```cpp
void InstrumentView::fillSynthParameters() {
    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    SynthInstrument *instr = (SynthInstrument *)bank->GetInstrument(i);
    GUIPoint position = GetAnchor();

    Variable *v = instr->FindVariable(SYIP_VOL);
    UIIntVarField *f = new UIIntVarField(position, *v, "volume: %d", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    f->SetFocus();

    position._y += 2;
    v = instr->FindVariable(SYIP_O1WV);
    f = new UIIntVarField(position, *v, "osc1: %s", 0, SWF_LAST-1, 1, 1);
    T_SimpleList<UIField>::Insert(f);

    position._y += 1;
    v = instr->FindVariable(SYIP_O2WV);
    f = new UIIntVarField(position, *v, "osc2: %s", 0, SO2_LAST-1, 1, 1);
    T_SimpleList<UIField>::Insert(f);

    position._x += 10;
    v = instr->FindVariable(SYIP_O2DT);
    f = new UIIntVarField(position, *v, "det:%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x -= 10;

    position._y += 1;
    v = instr->FindVariable(SYIP_O2MX);
    f = new UIIntVarField(position, *v, "osc2 mix: %2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);

    position._y += 2;
    v = instr->FindVariable(SYIP_NLEN);
    f = new UIIntVarField(position, *v, "length: %2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);

    position._y += 1;
    UIStaticField *sf = new UIStaticField(position, "A D S R:");
    T_SimpleList<UIField>::Insert(sf);

    position._y += 1;
    v = instr->FindVariable(SYIP_EATK);
    f = new UIIntVarField(position, *v, "%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x += 3;
    v = instr->FindVariable(SYIP_EDEC);
    f = new UIIntVarField(position, *v, "%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x += 3;
    v = instr->FindVariable(SYIP_ESUS);
    f = new UIIntVarField(position, *v, "%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x += 3;
    v = instr->FindVariable(SYIP_EREL);
    f = new UIIntVarField(position, *v, "%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x -= 9;

    position._y += 2;
    UIStaticField *sf2 = new UIStaticField(position, "flt cut/res:");
    T_SimpleList<UIField>::Insert(sf2);
    position._x += 13;
    v = instr->FindVariable(SYIP_FCUT);
    f = new UIIntVarField(position, *v, "%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x += 3;
    v = instr->FindVariable(SYIP_FRES);
    f = new UIIntVarField(position, *v, "%2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._x -= 16;

    position._y += 1;
    v = instr->FindVariable(SYIP_FMOD);
    f = new UIIntVarField(position, *v, "flt mode: %s", 0, SFM_LAST-1, 1, 1);
    T_SimpleList<UIField>::Insert(f);

    position._y += 1;
    v = instr->FindVariable(SYIP_FEAT);
    f = new UIIntVarField(position, *v, "flt env atk: %2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._y += 1;
    v = instr->FindVariable(SYIP_FEAM);
    f = new UIIntVarField(position, *v, "flt env amt: %2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);

    position._y += 2;
    v = instr->FindVariable(SYIP_LRAT);
    f = new UIIntVarField(position, *v, "lfo rate: %2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._y += 1;
    v = instr->FindVariable(SYIP_LDPT);
    f = new UIIntVarField(position, *v, "lfo depth: %2.2X", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(f);
    position._y += 1;
    v = instr->FindVariable(SYIP_LDST);
    f = new UIIntVarField(position, *v, "lfo dest: %s", 0, SLD_LAST-1, 1, 1);
    T_SimpleList<UIField>::Insert(f);

    position._y += 2;
    v = instr->FindVariable(SYIP_TBLA);
    f = new UIIntVarField(position, *v, "automation: %s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f);
    position._y += 1;
    v = instr->FindVariable(SYIP_TABL);
    f = new UIIntVarOffField(position, *v, "table: %2.2X", 0x00, 0x7F, 1, 16);
    T_SimpleList<UIField>::Insert(f);
}
```

4. **`ProcessButtonMask` — VM_NEW handler** (line ~392): add `SYIP_TABL` to the table FourCC
   check. Since `SYIP_TABL == SIP_TABLE` (same FourCC bytes), no code change is needed —
   the existing check already covers it.

5. **`ProcessButtonMask` — B+A clear-table handler** (line ~374): same reasoning — since the
   synth table variable uses FourCC `MAKE_FOURCC('T','A','B','L')`, `FindVariable(SIP_TABLE)`
   will find it and the null-unchecked dereference is safe. No change needed.

---

## Phase 6 — Variable String Mappings

Each enum-backed `Variable` needs string arrays for display. Add these as static arrays in
`SynthInstrument.cpp` and pass them to the `Variable` constructor (follow the pattern used
in `SampleInstrument.cpp` for `loopMode_`):

```cpp
static const char *waveNames[]  = { "sine", "tri", "saw", "square", NULL };
static const char *osc2Names[]  = { "sine", "tri", "saw", "square", "off", NULL };
static const char *fltModeNames[]= { "lp", "hp", "bp", "notch", NULL };
static const char *lfoDestNames[]= { "pitch", "fcut", "amp", NULL };
static const char *onOffNames[]  = { "off", "on", NULL };
```

---

## Phase 7 — Persistence Verification

`SaveContent()` in `InstrumentBank.cpp` is fully generic — it iterates `GetIterator()` and
writes every `Variable`. No change required.

`RestoreContent()` restores by name via string match. Variable names must be stable. Do not
change parameter names after the first release.

The version guard strategy: bump the project version constant (wherever it is defined) and
add a `doc->version_` check in `RestoreContent()` if default values for new parameters need
to differ from the `Variable` constructor defaults for old projects. For v1 this is not
required as long as constructor defaults produce silent/safe output.

---

## Implementation Order

1. Phase 1 (enum + Song.h) — compiles immediately, no behavior change
2. Phase 4 constructor only — allocates synth slots with stub class
3. Phase 2 header + stub `.cpp` (all virtuals return safe no-ops, `Render` returns false)
4. Phase 4 remainder (RestoreContent, Clone, InstrumentTypeData, OnStart)
5. Phase 5 UI — synth screen now visible and editable
6. Phase 3 DSP — bring oscillators up first, then envelope, then filter, then LFO
7. Phase 6 string mappings — polish
8. Phase 7 persistence smoke-test (save a project, reload, verify params round-trip)

---

## Known Limitations / Deferred

- **Clone** — synth instruments cannot be cloned in v1 (returns `NO_MORE_INSTRUMENT`).
  Implementing clone requires extending `GetNext()` or adding a separate `GetNextSynth()`.
- **Pan** — omitted from v1 parameter set for screen space; add as `SYIP_PAN` later.
- **Portamento / glide** — not in v1.
- **Command table commands** — `ProcessCommand` stub can return without action; implement
  specific `FourCC` commands (e.g., filter cutoff sweep) in a follow-up.
- **FM engine** — a separate `IT_FM` type or a mode selector within `SynthInstrument`
  (`engine: sub/fm`) can be added once the subtractive path is solid.

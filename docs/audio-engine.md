# LittleGPTracker Audio Engine

A reference for contributors, with emphasis on the parts required to implement a new synthesizer instrument type.

---

## Table of Contents

1. [Overview](#overview)
2. [Fixed-Point Arithmetic](#fixed-point-arithmetic)
3. [Audio Pipeline — End to End](#audio-pipeline--end-to-end)
4. [Core Audio Abstractions](#core-audio-abstractions)
5. [Platform Backends](#platform-backends)
6. [Mix Coordinator — MixerService](#mix-coordinator--mixerservice)
7. [Sequencer & Sync](#sequencer--sync)
8. [Instrument System ⬅ primary integration point](#instrument-system--primary-integration-point)
9. [UI Binding ⬅ primary integration point](#ui-binding--primary-integration-point)
10. [Persistence ⬅ primary integration point](#persistence--primary-integration-point)
11. [Adding a New Instrument Type — Checklist](#adding-a-new-instrument-type--checklist)

---

## Overview

LittleGPTracker is a **sample-based tracker** with an 8-channel sequencer. Audio is mixed in software at **44100 Hz**, **interleaved stereo**, **16-bit PCM** output. All internal DSP math uses a 16.16 fixed-point type called `fixed`; conversion to `short` PCM happens only at the very end of the pipeline.

The mix graph is a tree of `AudioModule` objects summed by `AudioMixer` nodes. The hardware driver fires an event when it needs more data; `MixerService` responds by triggering a full render of the tree.

---

## Fixed-Point Arithmetic

**File:** `sources/Application/Utils/fixed.h`

All audio buffers and DSP computations use the `fixed` type (16.16 signed fixed-point). Key helpers:

| Helper | Purpose |
|---|---|
| `i2fp(n)` | integer → fixed |
| `fp2i(f)` | fixed → integer (truncate) |
| `fl2fp(f)` | float literal → fixed (compile-time use) |
| `fp2fl(f)` | fixed → float (debugging only) |
| `fp_mul(a, b)` | fixed × fixed → fixed |

> **Synth note:** All oscillator, envelope, and filter math in a new synth instrument must use `fixed`. Avoid `float` in render-path code — it is slow on targets without an FPU and inconsistent across platforms. For FM synthesis, prefer a sine wavetable over `sin()` to avoid both float cost and precision drift in feedback loops.

---

## Audio Pipeline — End to End

```
[Song / Chain / Phrase data]
        │
      Player          — sequencer, advances positions, fires note events
        │
   PlayerMixer        — owns PlayerChannel[0..7], manages MixerService
        │
  MixerService        — singleton hub; responds to ADET_BUFFERNEEDED
        │
  AudioOutDriver      — top of mix graph
   ├── master MixBus (AudioMixer)
   │    ├── MixBus[0..7]  →  PlayerChannel[N]  →  I_Instrument::Render()
   │    └── MixBus[8]     →  AudioFileStreamer  (live WAV stream)
   ├── softClip + masterVolume applied
   ├── (optional) WavFileWriter  — stems / mixdown recording
   └── AudioDriver ring buffer
              │
   [SDL / SDL2 / JACK / RtAudio hardware callback]  →  speakers
```

Each `PlayerChannel` calls its assigned instrument's `Render()` once per buffer. The result is interleaved stereo `fixed` samples, summed into the bus, clipped, then handed to the driver as `short` PCM.

---

## Core Audio Abstractions

**Directory:** `sources/Services/Audio/`

### `AudioModule`
`sources/Services/Audio/AudioModule.h`

The fundamental render interface. Every audio-producing object — instrument channel, bus, file streamer — implements it:

```cpp
virtual bool Render(fixed *buffer, int samplecount) = 0;
```

`buffer` is interleaved stereo: `[L0, R0, L1, R1, ...]`. Returns `false` if the module produced silence (allows the mixer to skip adding zeros).

### `AudioMixer`
`sources/Services/Audio/AudioMixer.h/.cpp`

Extends `AudioModule` and owns a list of child `AudioModule` objects. On `Render()`:
1. Iterates children, calls each `Render()`, sums results into the output buffer.
2. Applies per-bus volume.
3. Applies soft-clipping (4 levels, cubic algorithm) and hard-clipping.
4. Optionally writes output to a `WavFileWriter` (for stem/mixdown recording).

### `AudioOut` / `AudioOutDriver`
`sources/Services/Audio/AudioOut.h`, `AudioOutDriver.h/.cpp`

`AudioOut` adds lifecycle (`Init/Close/Start/Stop`) and a `Trigger()` method to `AudioMixer`. `AudioOutDriver` is the concrete bridge to a hardware `AudioDriver`:

1. `prepareMixBuffers()` — queries `SyncMaster` for the current slice's sample count.
2. `AudioMixer::Render(primarySoundBuffer_, sampleCount_)` — renders the full tree as `fixed`.
3. `clipToMix()` — converts `fixed` → `short` into `mixBuffer_`.
4. `driver_->AddBuffer(mixBuffer_, sampleCount_)` — hands PCM to the OS driver.

### `AudioDriver`
`sources/Services/Audio/AudioDriver.h`

Abstract hardware driver. Maintains a ring buffer of `AudioBufferData` (`SOUND_BUFFER_COUNT = 50` slots). Fires `ADET_BUFFERNEEDED` events to observers when it needs more data.

---

## Platform Backends

**Directory:** `sources/Adapters/`

All backends implement `AudioDriver`. They spawn a background thread that signals the main mix path via semaphore when a buffer is consumed.

| Backend | Directory | Notes |
|---|---|---|
| SDL 1.x | `Adapters/SDL/Audio/` | Callback-based, `SDLAudioDriverThread` |
| SDL 2.x | `Adapters/SDL2/Audio/` | Same structure, SDL2 API |
| JACK | `Adapters/Jack/Audio/` | Float buffers, separate L/R ports |
| RtAudio | `Adapters/RTAudio/` | ASIO / CoreAudio / ALSA via `RtAudio::Api` |

All backends output **interleaved stereo 16-bit PCM at 44100 Hz**.

---

## Mix Coordinator — MixerService

**File:** `sources/Application/Mixer/MixerService.h/.cpp`

Singleton. The central hub connecting the sequencer to the audio hardware.

- Owns `master MixBus` and `bus_[0..9]` (10 sub-buses, `MAX_BUS_COUNT = 10`).
- All sub-buses are inserted into `master_`; `master_` is inserted into `AudioOut` — the full mix graph.
- Observes `AudioOut` for `ADET_BUFFERNEEDED`; responds with `out_->Trigger()`.
- Thread safety: SDL mutex `sync_` wraps `Lock()/Unlock()`.
- Render modes: `MSRM_PLAYBACK` (live), `MSRM_STEREO` (mixdown WAV), `MSRM_STEMS` (per-channel WAV).

A synth instrument requires no changes here. It slots in as a render leaf under an existing `MixBus`.

---

## Sequencer & Sync

**Directory:** `sources/Application/Player/`

### `SyncMaster`
`sources/Application/Player/SyncMaster.h/.cpp`

Tempo oracle. Computes `playSampleCount_` (samples per playback slice) and `tickSampleCount_` (samples per sequencer tick) from BPM. Drives three k-rate decision points queried during render:

- `TableSlice()` — whether to step automation table rows this buffer
- `MajorSlice()` — whether to step the sequencer this buffer
- `MidiSlice()` — whether to send MIDI clock this buffer

### `Player`
`sources/Application/Player/Player.h`

Advances Song → Chain → Phrase positions. Calls `PlayerMixer::StartInstrument()` / `StopInstrument()` which map to `I_Instrument::Start()` / `Stop()`.

### `PlayerChannel`
`sources/Application/Player/PlayerChannel.h/.cpp`

One per sequencer lane. On `Render()`:
1. Queries `SyncMaster::TableSlice()` for automation state (`updateTick` flag).
2. Calls `instr_->Render(channel, buffer, samplecount, updateTick)`.
3. If muted, returns `false` regardless of instrument output.

`updateTick` tells the instrument whether to advance its internal automation/envelope state this buffer. A synth instrument must respect this flag — advance envelopes and LFOs only when `updateTick` is `true`.

---

## Instrument System ⬅ primary integration point

**Directory:** `sources/Application/Instruments/`

This is where a new synth instrument lives.

### `I_Instrument`
`sources/Application/Instruments/I_Instrument.h`

Abstract base class. Inherits `VariableContainer` (parameter storage) and `Observable` (change notification).

```cpp
enum InstrumentType {
    IT_SAMPLE = 0,
    IT_MIDI,
    IT_LAST       // ← insert IT_SYNTH before this
};

class I_Instrument : public VariableContainer, public Observable {
public:
    virtual bool Init() = 0;
    virtual bool Start(int channel, unsigned char note, bool retrigger = true) = 0;
    virtual void Stop(int channel) = 0;
    virtual void OnStart() = 0;
    virtual bool Render(int channel, fixed *buffer, int size, bool updateTick) = 0;
    virtual bool IsInitialized() = 0;
    virtual bool IsEmpty() = 0;
    virtual InstrumentType GetType() = 0;
    virtual const char *GetName() = 0;
    virtual void ProcessCommand(int channel, FourCC cc, ushort value) = 0;
    virtual void Purge() = 0;
    virtual int GetTable() = 0;
    virtual bool GetTableAutomation() = 0;
    virtual void GetTableState(TableSaveState &state) = 0;
    virtual void SetTableState(TableSaveState &state) = 0;
};
```

Every method must be implemented. The most important for a synth:

| Method | Synth responsibility |
|---|---|
| `Start(channel, note, retrigger)` | Convert MIDI note to frequency, reset/retrigger voice state for this channel |
| `Stop(channel)` | Gate off — begin envelope release for this channel's voice |
| `Render(channel, buffer, size, updateTick)` | Generate `size` interleaved stereo samples into `buffer`; advance envelopes/LFOs if `updateTick` |
| `Init()` | One-time setup (wavetable generation, filter coefficient init, etc.) |
| `OnStart()` | Called when the sequencer starts playback — reset global state if needed |
| `IsEmpty()` | Return `true` if the instrument has not been configured (used to skip saving) |

### `Variable` and `VariableContainer`
`sources/Foundation/Variables/Variable.h`
`sources/Foundation/Variables/VariableContainer.h`

Each instrument parameter is a `Variable` object with:
- A human-readable name string (used for serialization).
- A `FourCC` ID (used by the UI to look up specific fields).
- A typed value (`int`, with optional string mappings for enums).

Instruments register parameters in their constructor via `Insert(variable_ptr)`:

```cpp
// Example from SampleInstrument constructor pattern
cutoff_ = new Variable("filter cut", SIP_FILTCUTOFF, 0xFF);
Insert(cutoff_);
```

`WatchedVariable` extends `Variable` with `Observable` — use it for parameters whose changes must immediately recalculate render state (e.g., wavetable index, engine type selector).

`VariableContainer::FindVariable(FourCC)` does a linear scan — keep the parameter count reasonable.

### `InstrumentBank`
`sources/Application/Instruments/InstrumentBank.h/.cpp`

Owns the fixed array `I_Instrument *instrument_[MAX_INSTRUMENT_COUNT]`. Instrument type is currently determined by **slot position**:

```
Song.h:
  MAX_SAMPLEINSTRUMENT_COUNT = 0x80   (slots 0..127   → SampleInstrument)
  MAX_MIDIINSTRUMENT_COUNT   = 0x10   (slots 128..143 → MidiInstrument)
  MAX_INSTRUMENT_COUNT       = 0x90   (144 total)
```

**To add a synth type**, you must:

1. Add `MAX_SYNTHINSTRUMENT_COUNT` to `Song.h` and update `MAX_INSTRUMENT_COUNT`.
2. Update `InstrumentBank` constructor to allocate synth slots.
3. Update `RestoreContent()` to handle the legacy fallback (`it=(id<MAX_SAMPLEINSTRUMENT_COUNT)?IT_SAMPLE:IT_MIDI`) — the boundary check will need updating.
4. Add a `case IT_SYNTH:` branch to `RestoreContent()` and `Clone()`.
5. Add `"Synth"` to the `InstrumentTypeData[]` string array (used for XML serialization).

> **Save format note:** Bumping `MAX_INSTRUMENT_COUNT` shifts the MIDI slot range. Existing projects with MIDI instruments in slots 128–143 will load incorrectly unless a migration is applied. The `RestoreContent()` method already handles version-gated migrations (see `doc->version_` checks) — add one here.

### Existing Filter Code
`sources/Application/Instruments/Filters.h/.cpp`

`SampleInstrument` uses a state-variable filter (lowpass / highpass / bandpass / notch) with resonance. The filter state struct and coefficient functions are available for reuse in a subtractive synth engine — no need to rewrite them.

`init_filters()` is called from `InstrumentBank::OnStart()` and must be called before any filter is used.

### Per-Channel Voice State Pattern

`SampleInstrument` stores per-channel render state in a `renderParams[SONG_CHANNEL_COUNT]` array — one struct per tracker channel. A synth instrument must follow the same pattern, since up to 8 channels can render the same instrument concurrently. Never store voice state as single instance members on the instrument class.

```cpp
// Pattern to follow
struct SynthVoiceState {
    fixed phase;          // oscillator phase accumulator
    fixed frequency;      // current note frequency
    fixed envLevel;       // current envelope amplitude
    EnvelopeStage stage;  // ATTACK / DECAY / SUSTAIN / RELEASE / IDLE
    // ... filter state, LFO phase, FM operator phases, etc.
};

SynthVoiceState voiceState_[SONG_CHANNEL_COUNT];
```

---

## UI Binding ⬅ primary integration point

**Directory:** `sources/Application/Views/`

### `InstrumentView`
`sources/Application/Views/InstrumentView.h/.cpp`

The only instrument UI class. Extends `FieldView` (a `View` + `T_SimpleList<UIField>`).

On every instrument or instrument-type switch, `onInstrumentChange()` is called:
1. Gets `viewData_->currentInstrument_` index.
2. Fetches `I_Instrument*` from `InstrumentBank`.
3. Calls `GetType()` and switches:
   ```cpp
   switch (it) {
       case IT_MIDI:   fillMidiParameters();   break;
       case IT_SAMPLE: fillSampleParameters(); break;
       // ← add: case IT_SYNTH: fillSynthParameters(); break;
   }
   ```
4. Clears all `UIField` objects and rebuilds them entirely.

**To add a synth UI**, implement `fillSynthParameters()` following the pattern of `fillSampleParameters()`:

```cpp
void InstrumentView::fillSynthParameters() {
    int i = viewData_->currentInstrument_;
    I_Instrument *instr = viewData_->project_->GetInstrumentBank()->GetInstrument(i);
    GUIPoint position = GetAnchor();

    Variable *v = instr->FindVariable(SYIP_CUTOFF);   // your FourCC constant
    UIIntVarField *f = new UIIntVarField(position, *v, "cutoff: %d", 0, 255, 1, 10);
    T_SimpleList<UIField>::Insert(f);
    // ... repeat for each parameter
}
```

### `UIIntVarField`
`sources/Application/Views/BaseClasses/UIIntVarField.h/.cpp`

The standard widget for integer parameters. Stores `Variable &src_` directly:
- `Draw()` calls `src_.GetInt()` / `src_.GetString()` to render the current value.
- `ProcessArrow()` calls `src_.SetInt(newValue)` to write the model in place.

Other available field types: `UIBigHexVarField` (7-digit hex, for sample offsets), `UINoteVarField` (note picker), `UIIntVarOffField` (integer with an "off" state).

### `ViewData`
`sources/Application/Views/ViewData.h`

Shared state across all views. `currentInstrument_` (int) is the active instrument index — this is how `InstrumentView` knows which instrument to display.

---

## Persistence ⬅ primary integration point

### XML Serialization
`sources/Application/Instruments/InstrumentBank.cpp` — `SaveContent()` / `RestoreContent()`

Instruments are saved as XML via TinyXML. Each instrument's variables are iterated and written as `<PARAM NAME="..." VALUE="..."/>` nodes. The instrument type is written as a string attribute (`TYPE="Sample"`, `TYPE="Midi"`).

`SaveContent()` already handles all types generically via `VariableContainer::GetIterator()` — no changes needed there for the new type.

`RestoreContent()` needs a new `case IT_SYNTH:` in the switch that constructs the right object:

```cpp
case IT_SYNTH:
    instr = new SynthInstrument();
    break;
```

The variable-by-name restoration loop (`v.GetName()` / `v.SetString()`) is generic and will work automatically for any new `Variable` objects registered in the synth constructor.

### `InstrumentTypeData[]`
`sources/Application/Instruments/InstrumentBank.cpp:12`

String array indexed by `InstrumentType` enum value. Used for XML `TYPE` attribute serialization. Must stay in sync with the enum order:

```cpp
char *InstrumentTypeData[IT_LAST] = {
    "Sample",
    "Midi",
    "Synth"   // ← add
};
```

---

## Adding a New Instrument Type — Checklist

Use this as a task list when implementing a `SynthInstrument`.

### 1. Enum & Constants
- [ ] Add `IT_SYNTH` to `InstrumentType` enum in `sources/Application/Instruments/I_Instrument.h` (before `IT_LAST`)
- [ ] Add `MAX_SYNTHINSTRUMENT_COUNT` to `sources/Application/Model/Song.h`
- [ ] Update `MAX_INSTRUMENT_COUNT` in `Song.h`

### 2. New Instrument Class
- [ ] Create `sources/Application/Instruments/SynthInstrument.h/.cpp`
- [ ] Implement all pure virtuals from `I_Instrument`
- [ ] Register all parameters as `Variable` objects in the constructor with unique `FourCC` IDs
- [ ] Store all voice state in `SynthVoiceState voiceState_[SONG_CHANNEL_COUNT]`
- [ ] Implement `Render()` using `fixed`-point math only in the hot path
- [ ] Respect `updateTick` flag — advance envelopes/LFOs only when `true`
- [ ] Return `IT_SYNTH` from `GetType()`

### 3. InstrumentBank
- [ ] Allocate synth slots in the constructor
- [ ] Add `"Synth"` to `InstrumentTypeData[]`
- [ ] Add `case IT_SYNTH:` to `RestoreContent()` switch
- [ ] Add `case IT_SYNTH:` to `Clone()` switch
- [ ] Update legacy slot-boundary fallback in `RestoreContent()` if MIDI slots shifted
- [ ] Add a `doc->version_` migration guard if slot ranges changed

### 4. UI
- [ ] Add `case IT_SYNTH: fillSynthParameters(); break;` to `InstrumentView::onInstrumentChange()`
- [ ] Implement `InstrumentView::fillSynthParameters()`
- [ ] Add `#include` for `SynthInstrument.h` in `InstrumentView.cpp`

### 5. DSP Considerations
- [ ] Use a precomputed sine wavetable for FM / any trigonometric oscillator
- [ ] Reuse `Filters.h/.cpp` SVF for subtractive engine filter
- [ ] Ensure `init_filters()` is called before first use (it already is via `InstrumentBank::OnStart()`)
- [ ] Benchmark render cost across all 8 channels on the lowest-spec target before finalizing operator count (FM) or oscillator count (subtractive)

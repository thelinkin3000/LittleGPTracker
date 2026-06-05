# Audio Effects — Design Notes

## What already exists

| Effect | Where | Notes |
|---|---|---|
| 1-pole resonant lowpass filter | Both instruments | HP/BP/notch modes stubbed but not wired |
| Bit crush + pre-drive | SampleInstrument only | `crush` + `crushdrive` parameters |
| Feedback comb buffer | SampleInstrument only | 3500-sample circular buffer per channel, ADD/SUB modes |
| LFO | SynthInstrument only | Sine LFO, destinations: pitch, filter cutoff, amplitude |
| Soft clipper | Master bus only | Cubic waveshaper in `AudioMixer::Render()` |
| Convolution / IR reverb | SampleInstrument (`FxPrinter`) | Offline only — renders via ffmpeg to WAV, not real-time |

---

## Planned: Distortion + Delay for both instruments

### Distortion

**Approach:** Cubic soft-clip waveshaper — `y = x * (1.5 - 0.5 * x²)` applied after pre-gain.

- Drive parameter `0–255`: 0 = bypass, 255 = ~8× pre-gain before the waveshaper
- Output naturally bounded to `[-FP_ONE, FP_ONE]` after waveshaping
- For SampleInstrument: applied inside the inner render loop, after crush/filter, before panning
- For SynthInstrument: applied to `sig` inside the sample loop, after volume, before the filter

Fixed-point math is safe: all intermediate values stay within `int32` range when input is
clamped to `[-FP_ONE, FP_ONE]` before applying the polynomial.

**New parameters:**
- `SIP_DSTDRV` (`MAKE_FOURCC('D','S','T','D')`) — SampleInstrument distortion drive
- `SYIP_DSTDRV` (`MAKE_FOURCC('S','D','S','T')`) — SynthInstrument distortion drive

---

### Delay

**Approach:** Simple feedback delay with wet/dry mix. Circular buffer per channel.

```
delay_buffer[head] = dry + feedback * delay_buffer[read_pos]
output             = dry + wet * delay_buffer[read_pos]
```

- Buffer size: `22050 samples` (0.5 seconds at 44100 Hz) per channel, stereo interleaved
- Memory cost: `22050 × 2 × 8 channels × 4 bytes = ~1.4 MB` per instrument — fine for Windows
- Time parameter `0–255` maps to `0..22049` samples
- Feedback capped at `0.75` to prevent int32 overflow in the circular buffer
- Applied as a post-pass on the output buffer after the instrument's inner render loop

**New parameters:**
- `SIP_DLYTM` / `SYIP_DLYTM` — delay time (0–255)
- `SIP_DLYFB` / `SYIP_DLYFB` — delay feedback (0–255)
- `SIP_DLYWT` / `SYIP_DLYWT` — delay wet mix (0–255)

---

## Signal scale notes (important for DSP math)

- `FIXED_SHIFT = 15`, `FP_ONE = 32768`
- `fp_mul(a, b) = (long long)a * (long long)b >> 15`
- SampleInstrument output buffer: signals in approximately `[-FP_ONE, FP_ONE]` after volume + pan
- SynthInstrument output buffer: signals stored as `sL << FIXED_SHIFT`, i.e. `[-2^30, 2^30]`
  — distortion must be applied **before** the final `<< FIXED_SHIFT`, delay buffer must also
  operate at pre-shift scale to avoid int32 overflow

---

## Implementation plan (not yet done)

1. `sources/Application/Instruments/InstrumentFx.h` — shared inline DSP helpers
2. `sources/Application/Instruments/SampleRenderingParams.h` — add `delayHead_` field
3. `sources/Application/Instruments/SampleInstrument.h` — add FourCCs, delay buffer static, Variable members
4. `sources/Application/Instruments/SampleInstrument.cpp` — init variables, wire effects into Render()
5. `sources/Application/Instruments/SynthInstrument.h` — add FourCCs, delay buffer static, `delayHead_` in SynthVoiceState, Variable members
6. `sources/Application/Instruments/SynthInstrument.cpp` — init variables, wire effects into Render()
7. `sources/Application/Views/InstrumentView.cpp` — add UI fields to `fillSampleParameters()` and `fillSynthParameters()`

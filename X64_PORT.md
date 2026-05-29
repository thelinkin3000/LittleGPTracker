# Windows x64 Port

This document describes the bugs found and fixed when porting LittleGPTracker to
Windows x64 (Visual Studio, `Debug|x64` / `Release|x64` configurations).

---

## Background

The project previously supported two Windows targets:

| Platform | GUI | Audio | Timer/Process |
|----------|-----|-------|---------------|
| Win32 (x86) | SDL1 / WSDL | RTAudio / DirectSound | W32Timer, W32Process |
| ARM64 | SDL3 | SDLAudio | SDLTimer, SDLProcess |

The x64 port reuses the Win32 audio/timer/process stack (RTAudio / DirectSound) and
the ARM64 GUI stack (SDL3), giving:

| Platform | GUI | Audio | Timer/Process |
|----------|-----|-------|---------------|
| x64 | SDL3 | RTAudio / DirectSound | W32Timer, W32Process |

SDL3 audio was tried for x64 first but showed persistent underruns (WASAPI 441-frame
fragment vs expected 512), so the proven RTAudio path was kept.

---

## Bugs Fixed

### 1. `AppWindow::Print` — `strlen` size_t arithmetic crash (NAssert)

**File:** `sources/Application/AppWindow.cpp`

**Symptom:** App crashed during sample loading with no log output past `[Load]`.

**Cause:** `strlen` returns `size_t` (64-bit unsigned on x64). Subtracting it from an
`int` promoted the expression to `size_t`, wrapping on overflow. The resulting `pos._x`
exceeded 40, firing `NAssert((pos._x < 40) && (pos._y < 30))` → `assert(0)` → abort.

**Fix:** Cast `strlen` results to `int` before arithmetic:
```cpp
position -= (int)strlen(_statusLine);
pos._x = (40 - (int)strlen(buildString)) / 2;
```

---

### 2. SDL3 window surface smaller than requested (out-of-bounds write)

**File:** `sources/Adapters/SDL3/GUI/SDLGUIWindowImp.cpp`

**Symptom:** App crashed inside `DrawChar` when rendering the build-version string
(row 28), but not when rendering the status line (row 12).

**Cause:** `SDL_GetWindowSurface` is called again in `ProcessExpose` after the window
is first shown. At that point the OS has constrained the client area to fit on screen
(title bar ~31 px + taskbar ~40 px on a 1440p display), returning `surf_h = 1318`
instead of the requested 1440. However `mult_ = 6` was computed from the display mode
height (1440), so `appHeight × mult_ = 1440 > surf_h`. Characters on row 28 mapped to
`yy = 1344`, writing 48 pixel rows that exceeded the actual surface bounds.

**Fix:** Added a safety guard in `DrawChar` that skips any character whose full pixel
extent would exceed the actual surface dimensions:
```cpp
if (yy + 8*mult_ > surface_->h || xx + 8*mult_ > surface_->w) return;
```

---

### 3. `WSDLmain.cpp` — wrong SDL entry point for x64

**File:** `sources/Adapters/W32/Main/WSDLmain.cpp`

**Symptom:** App showed a black window and hung indefinitely after fonts were prepared.

**Cause:** The non-ARM64 branch included the old SDL1 headers and provided its own
`WinMain` that called `SDL_main(__argc, __argv)`. SDL3 requires `SDL_main.h` for its
`WinMain → SDL_RunApp → main` bridge. Without it, SDL3 internal initialization was
incomplete, causing `SDL_WaitEvent` / `SDL_UpdateWindowSurfaceRects` to deadlock.

**Fix:** Removed the architecture conditional; both ARM64 and x64 now include
`<SDL3/SDL_main.h>`.

---

### 4. `W32Timer.cpp` — `DWORD` callback parameters (x64 crash on timer tick)

**File:** `sources/Adapters/W32\Timer/W32Timer.cpp`

**Symptom:** Timer callbacks crashed immediately on x64.

**Cause:** `timeSetEvent` on x64 uses `DWORD_PTR` for user-data and callback
parameters. The old code cast `this` and callbacks to `DWORD` (32-bit), truncating the
upper 32 bits. The callback then tried to cast the truncated 32-bit value back to a
pointer, producing a garbage address.

**Fix:** Changed all `DWORD` parameter types and casts to `DWORD_PTR`:
```cpp
// callback signature
void CALLBACK TimerProc(UINT, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR)

// registration
timer_ = timeSetEvent(newcb, 0, &TimerProc, (DWORD_PTR)this, TIME_ONESHOT);
```

---

### 5. `W32AudioDriver.cpp` — `DWORD` waveOut callback parameters (crash on playback)

**File:** `sources/Adapters/W32/Audio/W32AudioDriver.cpp`

**Symptom:** App crashed immediately when pressing Space to start playback.

**Cause:** `waveOutOpen` on x64 requires `DWORD_PTR` for the callback pointer and
instance data. The old code cast both to `DWORD`, truncating the `W32AudioDriver*`
pointer. The `WOM_DONE` callback received a corrupted 32-bit instance and dereferenced
it as a 64-bit pointer — instant access violation. `WAVEHDR::dwUser` also stores a
`W32SoundBuffer*` that was similarly truncated.

**Fix:**
```cpp
void CALLBACK winmm_cback(HWAVEOUT, UINT uMsg,
                           DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR) { … }

waveOutOpen(&waveOut_, index_, &fx,
    (DWORD_PTR)winmm_cback, (DWORD_PTR)this, CALLBACK_FUNCTION);

sbuffer->wavHeader_->dwUser = (DWORD_PTR)sbuffer;
```

---

### 6. `RTAudioDriver.cpp` — pointer-to-`int` cast for buffer alignment

**File:** `sources/Adapters/RTAudio/RTAudioDriver.cpp`

**Symptom:** Potential bad audio buffer pointer on x64 (upper 32 bits zeroed).

**Cause:** The non-`_64BIT` branch aligned `mainBuffer_` by casting the heap pointer to
`int`, zeroing the upper 32 bits of a 64-bit address:
```cpp
mainBuffer_ = (char *)((((int)unalignedMain_) + 1) & 0xFFFFFFFC);
```

**Fix:** Use `uintptr_t` for pointer-width arithmetic, preserving all address bits:
```cpp
mainBuffer_ = (char *)(((uintptr_t)unalignedMain_ + 3) & ~(uintptr_t)3);
```

---

### 7. `SampleInstrument.cpp` — dirty downsampling pointer truncation (garbled audio)

**File:** `sources/Application/Instruments/SampleInstrument.cpp`

**Symptom:** Sample instruments using dirty downsampling produced corrupted/garbage
audio on x64.

**Cause:** Dirty downsampling quantised the sample read position by masking the
*absolute pointer*. The `#ifdef _64BIT` branch used `long` (still 32-bit on Windows
LLP64), and the `#else` branch used `unsigned int`. Both zero the upper 32 bits of a
64-bit address, producing a pointer into a completely wrong memory region.

**Fix:** Removed the `#ifdef _64BIT` block entirely. Downsampling now works on the
*relative offset* within the buffer (same approach as the non-dirty path), which is
pointer-width–safe on all targets:
```cpp
uintptr_t distance = ((uintptr_t)(input - dsBasePtr) / channelCount) & (uintptr_t)dsMask;
i1 = dsBasePtr + distance * channelCount;
```

---

### 8. 24-bit PCM WAV support

**File:** `sources/Application/Instruments/WavFile.cpp`

**Symptom:** Samples from the `8R8FreePack` (and other 24-bit PCM packs) failed to
load or stream with `[*ERROR*] Only 8/16 bit supported`.

**Cause:** `WavFile::Open` rejected `bitPerSample == 24` and `WavFile::GetBuffer` had
no conversion path for it. Attempting to use the 8/16-bit in-place path with 24-bit
data would also overflow the output buffer (3 bytes/sample raw vs 2 bytes/sample output).

**Fix:** Added a dedicated 24-bit PCM conversion path that reads 3-byte samples in
chunks and extracts the top 16 bits:
```cpp
// little-endian [b0, b1, b2] → keep top 16 bits (b1 | b2<<8)
samples_[outOffset++] = (short)(s[1] | ((signed char)s[2] << 8));
```

---

## Project File Changes (`lgpt.vcxproj`)

- Added `Debug|x64` and `Release|x64` platform configurations.
- x64 configs use SDL3 includes/libs (`libs/SDL3/lib/x64`) for the GUI, and link
  against the Windows SDK `dsound.lib`, `winmm.lib` for RTAudio.
- SDL3 audio/process/timer files remain ARM64-only; legacy Win32 adapter files
  (W32AudioDriver, W32Timer, W32Process, RTAudio externals) are compiled for all
  non-ARM64 platforms.
- SDL3 GUI files (GUIFactory, SDLEventManager, SDLGUIWindowImp) are compiled for all
  Windows platforms unconditionally.
- The `build_debug.ps1` script now targets `Debug|x64` by default.

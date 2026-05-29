# SDL3 Adapter Implementation Plan

Goal: build LittleGPTracker on Windows ARM64 by replacing the SDL1 Windows adapter with a
new `Adapters/SDL3/` layer that targets SDL3, which ships official ARM64 Windows binaries.
The existing `Adapters/SDL2/` files are left untouched (still used by Linux/handheld builds).

---

## 0. Prerequisites — obtain SDL3

Download the SDL3 Windows development package (VC flavour) from the SDL3 GitHub releases.
It includes ARM64 binaries.

Place the files as follows so the vcxproj can find them:

```
libs/
  SDL3/
    include/
      SDL3/
        SDL.h
        SDL_main.h
        SDL_thread.h
        ... (all headers)
    lib/
      ARM64/
        SDL3.lib
      x64/
        SDL3.lib
    bin/
      ARM64/
        SDL3.dll
```

---

## 1. New directory structure

Create `sources/Adapters/SDL3/` mirroring the SDL2 layout:

```
sources/Adapters/SDL3/
  Audio/
    SDLAudio.h
    SDLAudio.cpp
    SDLAudioDriver.h
    SDLAudioDriver.cpp
  GUI/
    GUIFactory.h
    GUIFactory.cpp
    SDLEventManager.h
    SDLEventManager.cpp
    SDLGUIWindowImp.h
    SDLGUIWindowImp.cpp
  Process/
    SDLProcess.h
    SDLProcess.cpp
  Timer/
    SDLTimer.h
    SDLTimer.cpp
```

Each file starts as a copy of its `SDL2/` counterpart, then receives the SDL3 changes
described in the sections below.

---

## 2. `GUI/SDLGUIWindowImp.h`

One change: replace the SDL include.

```diff
-#include <SDL2/SDL.h>
+#include <SDL3/SDL.h>
```

---

## 3. `GUI/SDLGUIWindowImp.cpp`

### 3a. Include

```diff
-#include <SDL2/SDL.h>   // (transitively via header)
+// (header already pulls <SDL3/SDL.h>)
```

### 3b. Display mode query

SDL3 removed the index-based `SDL_GetDisplayMode`. Use the desktop mode of the primary display.
Return type changed from `int` to `bool`.

```diff
-  int displayModeRet = SDL_GetDisplayMode(0, 0, &displayMode);
-  if (displayModeRet < 0) {
-    Trace::Error("DISPLAY","No display mode found.  Error Code: %d.", displayModeRet);
-  }
-  NAssert(displayModeRet >= 0);
+  bool displayModeRet = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay(), &displayMode);
+  if (!displayModeRet) {
+    Trace::Error("DISPLAY","No display mode found: %s", SDL_GetError());
+  }
+  NAssert(displayModeRet);
```

### 3c. `SDL_CreateWindow` — position parameters removed

SDL3 dropped the x/y arguments. `SDL_WINDOW_SHOWN` was also removed (shown is the default).

```diff
-  window_ = SDL_CreateWindow("LittleGPTracker",
-                             SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
-                             screenRect_.Width(), screenRect_.Height(),
-                             fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN);
+  window_ = SDL_CreateWindow("LittleGPTracker",
+                             screenRect_.Width(), screenRect_.Height(),
+                             fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
```

### 3d. Cursor hiding

```diff
-  SDL_ShowCursor(SDL_DISABLE);
+  SDL_HideCursor();
```

### 3e. `SDL_CreateRGBSurface` replaced by `SDL_CreateSurface`

`SDL_SWSURFACE` and the mask arguments are gone. Use the window surface's pixel format directly.

```diff
-  fonts[i] = SDL_CreateRGBSurface(
-               SDL_SWSURFACE,
-               8*mult_, 8*mult_,
-               bitDepth_,
-               0, 0, 0, 0);
+  fonts[i] = SDL_CreateSurface(8*mult_, 8*mult_, surface_->format);
```

### 3f. `Invalidate()` — window events are now top-level

SDL3 removed `SDL_WINDOWEVENT`; each window event is its own event type. The `event.window.event`
sub-type field is gone; `windowID` still exists and must be set.

```diff
 void SDLGUIWindowImp::Invalidate()
 {
     SDL_Event event;
-    event.type = SDL_WINDOWEVENT;
-    event.window.event = SDL_WINDOWEVENT_EXPOSED;
+    event.type = SDL_EVENT_WINDOW_EXPOSED;
+    event.window.windowID = SDL_GetWindowID(window_);
     SDL_PushEvent(&event);
 }
```

### 3g. `SDL_USEREVENT` rename

```diff
-  sdlevent.type = SDL_USEREVENT;
+  sdlevent.type = SDL_EVENT_USER;
```

---

## 4. `GUI/SDLEventManager.h`

```diff
-#include <SDL2/SDL.h>
+#include <SDL3/SDL.h>
```

---

## 5. `GUI/SDLEventManager.cpp`

### 5a. `SDL_Init` return type changed to `bool`

```diff
-  if ( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER) < 0 )
+  if ( !SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER) )
```

### 5b. Cursor hiding

```diff
-  SDL_ShowCursor(SDL_DISABLE);
+  SDL_HideCursor();
```

### 5c. Joystick enumeration API changed

`SDL_NumJoysticks()` + `SDL_JoystickOpen(index)` are replaced by
`SDL_GetJoysticks(&count)` which returns an array of `SDL_JoystickID` values.

```diff
-  int joyCount = SDL_NumJoysticks();
-  joyCount = (joyCount > MAX_JOY_COUNT) ? MAX_JOY_COUNT : joyCount;
-  ...
-  for (int i = 0; i < joyCount; i++) {
-    joystick_[i] = SDL_JoystickOpen(i);
+  int joyCount = 0;
+  SDL_JoystickID *joyIDs = SDL_GetJoysticks(&joyCount);
+  joyCount = (joyCount > MAX_JOY_COUNT) ? MAX_JOY_COUNT : joyCount;
+  ...
+  for (int i = 0; i < joyCount; i++) {
+    joystick_[i] = SDL_OpenJoystick(joyIDs[i]);
     ...
   }
+  SDL_free(joyIDs);
```

### 5d. Key event — `keysym` struct removed

The `event.key.keysym` indirection is gone in SDL3; `scancode` is now a direct field.

```diff
-  SDL_GetScancodeName(event.key.keysym.scancode)
-  keyboardCS_->SetKey((int)event.key.keysym.scancode, true/false)
+  SDL_GetScancodeName(event.key.scancode)
+  keyboardCS_->SetKey((int)event.key.scancode, true/false)
```

### 5e. Event constant renames

| SDL2 | SDL3 |
|---|---|
| `SDL_KEYDOWN` | `SDL_EVENT_KEY_DOWN` |
| `SDL_KEYUP` | `SDL_EVENT_KEY_UP` |
| `SDL_JOYBUTTONDOWN` | `SDL_EVENT_JOYSTICK_BUTTON_DOWN` |
| `SDL_JOYBUTTONUP` | `SDL_EVENT_JOYSTICK_BUTTON_UP` |
| `SDL_JOYAXISMOTION` | `SDL_EVENT_JOYSTICK_AXIS_MOTION` |
| `SDL_JOYHATMOTION` | `SDL_EVENT_JOYSTICK_HAT_MOTION` |
| `SDL_JOYBALLMOTION` | `SDL_EVENT_JOYSTICK_BALL_MOTION` |
| `SDL_QUIT` | `SDL_EVENT_QUIT` |
| `SDL_USEREVENT` | `SDL_EVENT_USER` |

### 5f. Window events — top-level in SDL3

Replace the nested `SDL_WINDOWEVENT` + `event.window.event` sub-type check with direct
top-level event types. `SDL_WINDOWEVENT_SIZE_CHANGED` was removed; use
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` instead.

```diff
-  case SDL_WINDOWEVENT:
-    switch (event.window.event) {
-      case SDL_WINDOWEVENT_EXPOSED:
-      case SDL_WINDOWEVENT_RESIZED:
-      case SDL_WINDOWEVENT_SIZE_CHANGED:
-        sdlWindow->ProcessExpose();
-        break;
-    }
-    break;
+  case SDL_EVENT_WINDOW_EXPOSED:
+  case SDL_EVENT_WINDOW_RESIZED:
+  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
+    sdlWindow->ProcessExpose();
+    break;
```

---

## 6. `GUI/GUIFactory.h` and `GUIFactory.cpp`

Only the SDL include changes. The factory implementation is identical to SDL2.

---

## 7. `Audio/SDLAudio.h` and `SDLAudio.cpp`

No SDL API calls directly — just includes `SDLAudioDriver.h`. Only the include guard and
header path matter. Identical to SDL2 version.

---

## 8. `Audio/SDLAudioDriver.h`

### 8a. Include

```diff
-#include <SDL2/SDL.h>
+#include <SDL3/SDL.h>
```

### 8b. Add `audioStream_` member

`SDL_OpenAudio()` is gone in SDL3; the driver now owns an `SDL_AudioStream*`.

```diff
 private:
   int fragSize_;
   char *unalignedMain_;
   char *mainBuffer_;
   char *miniBlank_;
   int bufferPos_;
   int bufferSize_;
   SDLAudioDriverThread *thread_;
   Uint32 startTime_;
+  SDL_AudioStream *audioStream_;
```

---

## 9. `Audio/SDLAudioDriver.cpp`

This is the most significant change. `SDL_OpenAudio`, `SDL_CloseAudio`, and `SDL_PauseAudio`
are all removed in SDL3.

### 9a. Include + audio format constant

```diff
-#include <SDL2/SDL.h>  // via header
-    input.format = AUDIO_S16SYS;
+    // SDL3: SDL_AUDIO_S16 replaces AUDIO_S16SYS
```

### 9b. New SDL3-compatible callback

The SDL3 audio callback receives an `SDL_AudioStream*` and writes into it via
`SDL_PutAudioStreamData`. Wrap the existing `OnChunkDone` logic:

```cpp
// Replace sdl_callback with:
static void sdl3_callback(void *userdata, SDL_AudioStream *stream,
                           int additional_amount, int /*total_amount*/) {
    if (additional_amount > 0) {
        SDLAudioDriver *driver = (SDLAudioDriver *)userdata;
        Uint8 *buf = (Uint8 *)SDL_malloc(additional_amount);
        if (buf) {
            driver->OnChunkDone(buf, additional_amount);
            SDL_PutAudioStreamData(stream, buf, additional_amount);
            SDL_free(buf);
        }
    }
}
```

### 9c. `InitDriver()` rewrite

```cpp
bool SDLAudioDriver::InitDriver() {
    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq     = 44100;

    SDL_SetHint("APP_NAME", "LittleGPTracker");
    SDL_SetHint("AUDIO_DEVICE_APP_NAME", "LittleGPTracker");

    audioStream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdl3_callback, this);
    if (!audioStream_) {
        Trace::Error("Couldn't open SDL3 audio: %s\n", SDL_GetError());
        return false;
    }

    // Determine fragment size from the actual device format
    SDL_AudioSpec obtained;
    int sampleFrames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audioStream_),
                             &obtained, &sampleFrames);
    fragSize_ = sampleFrames * obtained.channels
                * SDL_AUDIO_BYTESIZE(obtained.format);

    const char *driverName = SDL_GetCurrentAudioDriver();
    Trace::Log("AUDIO", "%s opened: %d sample frames, %d Hz",
               driverName, sampleFrames, obtained.freq);

    unalignedMain_ = (char *)SYS_MALLOC(fragSize_ + SOUND_BUFFER_MAX);
    mainBuffer_    = (char *)unalignedMain_;   // SDL3 guarantees alignment

    miniBlank_ = (char *)malloc(fragSize_);
    SYS_MEMSET(miniBlank_, 0, fragSize_);

    return true;
}
```

### 9d. `CloseDriver()`

```diff
-    SDL_CloseAudio();
+    if (audioStream_) {
+        SDL_DestroyAudioStream(audioStream_);
+        audioStream_ = nullptr;
+    }
```

### 9e. `StartDriver()` / `StopDriver()`

```diff
-    SDL_PauseAudio(0);
+    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(audioStream_));

-    SDL_PauseAudio(1);
+    SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(audioStream_));
```

---

## 10. `Process/SDLProcess.h`

```diff
-#include <SDL2/SDL.h>
+#include <SDL3/SDL.h>
```

---

## 11. `Process/SDLProcess.cpp`

### 11a. Include

```diff
-#include <SDL2/SDL_thread.h>
+#include <SDL3/SDL_thread.h>
```

### 11b. `SDL_CreateThread` — name argument was always required

The existing SDL2 call was already broken (missing the required `name` argument):

```diff
-  SDL_CreateThread(_SDLStartThread, &thread);
+  SDL_CreateThread(_SDLStartThread, "lgpt", (void *)&thread);
```

### 11c. Semaphore function renames

| SDL2 | SDL3 |
|---|---|
| `SDL_SemWait(h)` | `SDL_WaitSemaphore(h)` |
| `SDL_SemPost(h)` | `SDL_PostSemaphore(h)` |

```diff
-  return (SysSemaphoreResult)SDL_SemWait(handle_);
+  return (SysSemaphoreResult)SDL_WaitSemaphore(handle_);

-  return (SysSemaphoreResult)SDL_SemPost(handle_);
+  return (SysSemaphoreResult)SDL_PostSemaphore(handle_);
```

---

## 12. `Timer/SDLTimer.h`

```diff
-#include <SDL2/SDL.h>
+#include <SDL3/SDL.h>
```

---

## 13. `Timer/SDLTimer.cpp`

### 13a. Timer callback signature changed

SDL3 swapped the parameter order of `SDL_TimerCallback`:
- SDL2: `Uint32 callback(Uint32 interval, void *userdata)`
- SDL3: `Uint32 callback(void *userdata, SDL_TimerID timerID, Uint32 interval)`

```diff
-Uint32 SDLTimerCallback(Uint32 interval, void *param) {
-    SDLTimer *timer = (SDLTimer *)param;
+Uint32 SDLTimerCallback(void *param, SDL_TimerID /*timerID*/, Uint32 interval) {
+    SDLTimer *timer = (SDLTimer *)param;
     return timer->OnTimerTick();
 }

-Uint32 SDLTriggerCallback(Uint32 interval, void *param) {
-    timerCallback tc = (timerCallback)param;
+Uint32 SDLTriggerCallback(void *param, SDL_TimerID /*timerID*/, Uint32 interval) {
+    timerCallback tc = (timerCallback)param;
     (*tc)();
     return 0;
 }
```

---

## 14. Modify `WSDLSystem.h`

Replace the bundled SDL1 header with SDL3.

```diff
-#include "Externals/SDL/SDL.h"
+#include <SDL3/SDL.h>
```

---

## 15. Modify `WSDLSystem.cpp`

### 15a. Update includes (SDL1 → SDL3 adapter paths)

```diff
-#include "Adapters/SDL/GUI/SDLEventManager.h"
-#include "Adapters/SDL/GUI/GUIFactory.h"
-#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"
+#include "Adapters/SDL3/GUI/SDLEventManager.h"
+#include "Adapters/SDL3/GUI/GUIFactory.h"
+#include "Adapters/SDL3/GUI/SDLGUIWindowImp.h"
```

### 15b. Remove SDL1-only calls

```diff
-    SDL_putenv("SDL_VIDEODRIVER=directx");   // SDL1 DirectX hint, not needed in SDL3
 
-    if ( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER) < 0 ) {
+    if ( !SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER) ) {
         return;
     }
-    SDL_EnableUNICODE(1);   // removed in SDL2+, Unicode always on
-    SDL_ShowCursor(SDL_DISABLE);
+    SDL_HideCursor();
```

---

## 16. Modify `WSDLmain.cpp`

### 16a. Update includes

```diff
-#include "Externals/SDL/SDL.h"
-#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"
+#include <SDL3/SDL_main.h>
+#include "Adapters/SDL3/GUI/SDLGUIWindowImp.h"
```

### 16b. Remove the manual `WinMain`

In SDL3, `SDL_main.h` provides the WinMain-to-main bridge automatically on Windows.
The custom WinMain shim is no longer needed and must be removed (SDL3's macros will
conflict with a user-defined WinMain).

```diff
-// SDL.h renames main→SDL_main. We provide WinMain ourselves so sdlmain.lib is not needed.
-int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
-    return SDL_main(__argc, __argv);
-}
```

The `main()` function below it remains unchanged.

---

## 17. Update `lgpt.vcxproj`

### 17a. Replace SDL1 GUI source files

```diff
-<ClCompile Include="..\sources\Adapters\Sdl\Gui\GUIFactory.cpp" />
-<ClCompile Include="..\sources\Adapters\Sdl\Gui\SDLEventManager.cpp" />
-<ClCompile Include="..\sources\Adapters\Sdl\Gui\SDLGUIWindowImp.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\GUI\GUIFactory.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\GUI\SDLEventManager.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\GUI\SDLGUIWindowImp.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\Audio\SDLAudio.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\Audio\SDLAudioDriver.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\Process\SDLProcess.cpp" />
+<ClCompile Include="..\sources\Adapters\SDL3\Timer\SDLTimer.cpp" />
```

Note: SDL3 Audio/Process/Timer replace W32Audio, W32Process, W32Timer for the ARM64
configuration. W32Audio uses DirectSound/MMSYSTEM which are x86/x64 only. The SDL3
equivalents are cross-architecture.

### 17b. Remove W32 audio/process/timer from ARM64 config

```diff
-<ClCompile Include="..\sources\Adapters\W32\Process\W32Process.cpp" />
-<ClCompile Include="..\sources\Adapters\W32\Audio\W32Audio.cpp" />
-<ClCompile Include="..\sources\Adapters\W32\Audio\W32AudioDriver.cpp" />
-<ClCompile Include="..\sources\Adapters\W32\Timer\W32Timer.cpp" />
```

And update `WSDLSystem.cpp` to install SDL3-based process/timer services instead of
W32-specific ones (see Step 15 note below).

### 17c. Add SDL3 include directory (all configurations)

```diff
-<AdditionalIncludeDirectories>../sources/Externals;../sources/;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
+<AdditionalIncludeDirectories>../sources/Externals;../sources/;../libs/SDL3/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
```

### 17d. Replace SDL library references

```diff
 Debug configuration:
-  <AdditionalDependencies>sdl.lib;winmm.lib;dsound.lib;SDL_image.lib;legacy_stdio_definitions.lib;%(AdditionalDependencies)</AdditionalDependencies>
-  <AdditionalLibraryDirectories>../libs/WSDL;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
+  <AdditionalDependencies>SDL3.lib;winmm.lib;legacy_stdio_definitions.lib;%(AdditionalDependencies)</AdditionalDependencies>
+  <AdditionalLibraryDirectories>../libs/SDL3/lib/ARM64;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>

 Release configuration:
-  <AdditionalDependencies>sdl-release.lib;winmm.lib;dsound.lib;legacy_stdio_definitions.lib;%(AdditionalDependencies)</AdditionalDependencies>
-  <AdditionalLibraryDirectories>../libs/WSDL;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
+  <AdditionalDependencies>SDL3.lib;winmm.lib;%(AdditionalDependencies)</AdditionalDependencies>
+  <AdditionalLibraryDirectories>../libs/SDL3/lib/ARM64;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
```

`SDL3main.lib` is not needed — SDL3 removed the SDLmain library entirely.
`dsound.lib` is no longer needed once W32Audio is removed.

### 17e. Change target platform to ARM64

Each configuration's `<TargetMachine>` currently reads `MachineX86`. Update:

```diff
-<TargetMachine>MachineX86</TargetMachine>
+<TargetMachine>MachineARM64</TargetMachine>
```

Also add `ARM64` platform configurations alongside the existing `Win32` ones, or change the
existing ones (the simplest approach for a clean ARM64-only build).

---

## 18. Additional `WSDLSystem.cpp` wiring for SDL3 services

When W32Process and W32Timer are removed, `WSDLSystem::Boot` must install SDL3-based
replacements. Add includes and swap the service installs:

```diff
-#include "Adapters/W32/Process/W32Process.h"
-#include "Adapters/W32/Timer/W32Timer.h"
+#include "Adapters/SDL3/Process/SDLProcess.h"
+#include "Adapters/SDL3/Timer/SDLTimer.h"

-    TimerService::GetInstance()->Install(new W32TimerService());
+    TimerService::GetInstance()->Install(new SDLTimerService());

-    SysProcessFactory::Install(new W32ProcessFactory());
+    SysProcessFactory::Install(new SDLProcessFactory());
```

The SDL3 audio is wired through `SDLAudio`; remove the W32Audio/RTAudioStub conditional
and replace with:

```diff
-    if (api && (!_stricmp(api,"MMSYSTEM"))) {
-        ...W32Audio...
-    } else {
-        ...RTAudioStub...
-    }
+    AudioSettings hints;
+    hints.audioAPI_    = "SDL";
+    hints.bufferSize_  = 512;
+    hints.preBufferCount_ = 4;
+    audio = new SDLAudio(hints);
```

Also add the SDLAudio include:

```diff
+#include "Adapters/SDL3/Audio/SDLAudio.h"
```

---

## 19. Implementation order

1. **Step 0** — obtain SDL3 ARM64 dev package, place under `libs/SDL3/`
2. **Steps 1–13** — create `Adapters/SDL3/` files (all pure source work, no build changes yet)
3. **Steps 14–16** — update `WSDLSystem.h`, `WSDLSystem.cpp`, `WSDLmain.cpp`
4. **Step 17** — update `lgpt.vcxproj` (add ARM64 platform, swap sources and libs)
5. **Step 18** — wire SDL3 audio/process/timer services in WSDLSystem
6. **First build** — resolve any remaining compile errors
7. **Smoke test** — verify audio plays, keyboard input works, window renders correctly

---

## Known gaps / deferred

- **RTMidi on ARM64** — `RTMidiService` and `W32MidiService` use Win32 MIDI APIs that exist
  on ARM64 Windows, but should be verified. If they fail, SDL3 has no MIDI subsystem and a
  stub `MidiService` would be needed.
- **RTAudio removal** — `RTAudioDriver.cpp` / `RTAudioStub.cpp` are still compiled. They
  reference DirectSound which may not link cleanly on ARM64. Remove them from the ARM64
  configuration once `SDLAudio` is confirmed working.
- **`SDL_image`** — the Debug vcxproj links `SDL_image.lib` for BMP/PNG loading. SDL3_image
  has its own ARM64 package; defer this and use SDL3's built-in `SDL_LoadBMP` until needed.
- **`/SAFESEH:NO` linker flag** — this is an x86-only flag and will warn/error on ARM64.
  Remove it from the ARM64 configuration's `AdditionalOptions`.
- **`/fixed:no` linker flag** — also x86-only; remove from ARM64 configuration.

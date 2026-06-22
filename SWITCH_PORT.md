# Nintendo Switch Homebrew Port Plan

## Overview

LittleGPTracker already has a clean adapter pattern supporting PSP, NDS, GP2X, Miyoo, RG35XX+, and others. The Switch port follows the exact same pattern: a new `sources/Adapters/NX/` directory with system class and entry point, reusing the existing SDL2 adapter suite (audio, GUI, timer, process) nearly wholesale, and a new Makefile target using devkitPro's cross-compiler.

The Switch runs ARM64 (Cortex-A57). devkitPro ships SDL2 for Switch via libnx. LGPT's logical 320×240 resolution scales cleanly to 1280×720 (3×) in handheld mode and 1920×1080 (4×) docked. No new DSP, sequencer, or UI code is needed — only platform glue.

---

## Files to Create / Modify

| Action | File |
|--------|------|
| Create | `sources/Adapters/NX/System/NXSystem.h` |
| Create | `sources/Adapters/NX/System/NXSystem.cpp` |
| Create | `sources/Adapters/NX/Main/NXMain.cpp` |
| Create | `projects/Makefile.SWITCH` |
| Create | `projects/resources/SWITCH/mapping.xml` |
| Create | `projects/resources/SWITCH/icon.jpg` (256×256 JPEG) |
| Modify | `projects/Makefile` — add SWITCHDIRS / SWITCHFILES |
| Modify | `sources/Adapters/SDL2/GUI/SDLGUIWindowImp.cpp` — add `PLATFORM_SWITCH` resolution block |
| Modify | `sources/Adapters/SDL2/Process/SDLProcess.cpp` — fix `SDL_CreateThread` 3-arg bug |

---

## Phase 0: Environment Setup

Building on **Ubuntu under WSL2**. devkitPro has a native Linux package repository — no MSYS2 or Windows toolchain needed.

### Install devkitPro on Ubuntu (WSL2)

```bash
# Add devkitPro apt repository
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x install-devkitpro-pacman
sudo ./install-devkitpro-pacman

# Install Switch toolchain and libraries
sudo dkp-pacman -S devkitA64 libnx switch-sdl2 switch-tools

# Add devkitPro to your shell environment (add to ~/.bashrc)
export DEVKITPRO=/opt/devkitpro
export DEVKITARM64=$DEVKITPRO/devkitA64
export PATH=$DEVKITARM64/bin:$DEVKITPRO/tools/bin:$PATH
```

Verify:
```bash
aarch64-none-elf-g++ --version
elf2nro --help
# Confirm libnx version is 4.11.1 or later:
dkp-pacman -Q libnx
```

> **Firmware 21.2 note:** libnx 4.10.0 added "basic support for 21.0.0"; the latest
> release (4.11.1) is what `dkp-pacman` installs. There is no explicit 21.2 entry in
> the changelog, but minor firmware point releases (21.0 → 21.2) rarely break libnx
> compatibility. LGPT does not use any cutting-edge Switch-specific system calls, so
> "basic support" for 21.x is sufficient. If builds fail with unresolved libnx symbols,
> check the libnx GitHub for a newer release before debugging further.

Standard install paths (same on Linux and WSL2):
```
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++
/opt/devkitpro/portlibs/switch/include/SDL2/SDL.h
/opt/devkitpro/libnx/include/switch.h
```

### Accessing the repo from WSL2

The repo lives at `C:\Users\carlo\Projects\LittleGPTracker` on Windows, which maps to
`/mnt/c/Users/carlo/Projects/LittleGPTracker` in WSL2. Build from there:

```bash
cd /mnt/c/Users/carlo/Projects/LittleGPTracker/projects
make PLATFORM=SWITCH
```

> **Note:** Building directly on the Windows filesystem (`/mnt/c/...`) is slower than
> building in the WSL2 native filesystem. For faster iteration, clone or copy the repo
> to `~/projects/LittleGPTracker` inside WSL2 and copy the resulting `.nro` back to
> Windows when done.

---

## Phase 1: Build System

### Modify `projects/Makefile`

Add after the existing RG35XXPLUS entries:

```makefile
SWITCHDIRS := $(LINUXDIRS) $(DUMMYMIDIDIRS) $(SDL2DIRS) $(SDL2AUDIODIRS) \
	../sources/Adapters/NX/Main \
	../sources/Adapters/NX/System

SWITCHFILES := $(LINUXFILES) $(SDLAUDIOFILES) $(DUMMYMIDIFILES) \
	NXMain.o \
	NXSystem.o
```

`LINUXFILES` already covers: `UnixFileSystem.o`, `GUIFactory.o`, `SDLGUIWindowImp.o`,
`SDLEventManager.o`, `SDLTimer.o`, `Process.o`, `UnixProcess.o`. Only the NX entry point
and system class are added on top.

### Create `projects/Makefile.SWITCH`

```makefile
-include $(PWD)/rules_base

DEVKITPRO  := /opt/devkitpro
DEVKITARM64 := $(DEVKITPRO)/devkitA64
PORTLIBS   := $(DEVKITPRO)/portlibs/switch
LIBNX      := $(DEVKITPRO)/libnx

TOOLPATH   := $(DEVKITARM64)/bin
PREFIX     := aarch64-none-elf-
CC         := $(PREFIX)gcc
CXX        := $(PREFIX)g++
STRIP      := $(PREFIX)strip

ARCH_FLAGS := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE

DEFINES := \
	-DPLATFORM_SWITCH \
	-D_64BIT \
	-DBUFFERED \
	-DCPP_MEMORY \
	-DHAVE_STDINT_H \
	-D_NDEBUG \
	-D_NO_JACK_ \
	-DSDL2 \
	-DSDLAUDIO \
	-DDUMMYMIDI

SDL_CFLAGS := -I$(PORTLIBS)/include/SDL2 -D_REENTRANT
SDL_LIBS   := -L$(PORTLIBS)/lib -lSDL2

INCLUDES := \
	$(SDL_CFLAGS) \
	-I$(LIBNX)/include \
	-I$(PORTLIBS)/include \
	-I$(PWD)/../sources

OPT_FLAGS := -O2

CFLAGS   := $(DEFINES) $(INCLUDES) $(ARCH_FLAGS) $(OPT_FLAGS) -Wall
CXXFLAGS := $(CFLAGS) -std=gnu++03

LDFLAGS  := -specs=$(LIBNX)/switch.specs -g $(ARCH_FLAGS) -Wl,-Map,$(OUTPUT).map

LIBS := \
	$(SDL_LIBS) \
	-L$(LIBNX)/lib -lnx \
	-L$(PORTLIBS)/lib \
	-lpthread -lm -lz

OUTPUT    := ../lgpt-switch
EXTENSION := elf

# Build the ELF
%.elf: $(OFILES)
	$(CXX) $(LDFLAGS) -o $@ $(OFILES) $(LIBS)

# Package as NRO (Switch homebrew executable)
%.nro: %.elf
	nacptool --create "LittleGPTracker" "LGPT" "0.1.0" lgpt.nacp
	elf2nro $< $@ --nacp=lgpt.nacp --icon=$(PWD)/resources/SWITCH/icon.jpg

all: $(OUTPUT).$(EXTENSION) $(OUTPUT).nro
```

---

## Phase 2: NX System Class

### `sources/Adapters/NX/System/NXSystem.h`

Mirror `sources/Adapters/LINUX/System/LINUXSystem.h` — rename class to `NXSystem`,
swap `<SDL2/SDL.h>` for `<switch.h>` + `<SDL2/SDL.h>`:

```cpp
#ifndef _NX_SYSTEM_H_
#define _NX_SYSTEM_H_

#include <switch.h>
#include <SDL2/SDL.h>
#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

class NXSystem: public System {
public:
    static void Boot(int argc, char **argv);
    static void Shutdown();
    static int MainLoop();

public:
    virtual unsigned long GetClock();
    virtual void Sleep(int millisec);
    virtual void *Malloc(unsigned size);
    virtual void Free(void *);
    virtual void Memset(void *addr, char val, int size);
    virtual void *Memcpy(void *s1, const void *s2, int n);
    virtual void AddUserLog(const char *);
    virtual int GetBatteryLevel() { return -1; };
    virtual void PostQuitMessage();
    virtual unsigned int GetMemoryUsage();

private:
    static bool finished_;
    static EventManager *eventManager_;
};
#endif
```

### `sources/Adapters/NX/System/NXSystem.cpp`

Copy `LINUXSystem.cpp` as base. Apply these differences:

**1. Path aliases** — Switch uses SD card paths instead of `/proc/self/exe`:
```cpp
Path::SetAlias("bin",  "sdmc:/switch/lgpt");
Path::SetAlias("root", "sdmc:/switch/lgpt");
```

**2. Audio buffer size** — larger initial value for Switch audio stack stability:
```cpp
AudioSettings hint;
hint.bufferSize_    = 2048;   // tune down to 1024/512 after hardware testing
hint.preBufferCount_ = 4;
Audio::Install(new SDLAudio(hint));
```

**3. SDL_Init** — add `SDL_INIT_GAMECONTROLLER`, remove X11 env var:
```cpp
SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);
```

**4. GetClock** — use `SDL_GetTicks()` (avoids `gettimeofday` portability concern):
```cpp
unsigned long NXSystem::GetClock() {
    return SDL_GetTicks();
}
```

Everything else (FileSystem → `UnixFileSystem`, GUIFactory → SDL2 GUIFactory,
TimerService → `SDLTimerService`, MidiService → `DummyMidi`, ProcessFactory →
`UnixProcessFactory`) is identical to `LINUXSystem.cpp`.

---

## Phase 3: NX Entry Point

### `sources/Adapters/NX/Main/NXMain.cpp`

```cpp
#include <switch.h>
#include "Adapters/NX/System/NXSystem.h"
#include "Adapters/SDL2/GUI/SDLGUIWindowImp.h"
#include "Application/Application.h"

int main(int argc, char *argv[]) {
    // Uncomment if bundling assets via romfs instead of SD card:
    // romfsInit();

    NXSystem::Boot(argc, argv);

    SDLCreateWindowParams params;
    params.title       = "littlegptracker";
    params.cacheFonts_  = true;
    params.framebuffer_ = false;

    Application::GetInstance()->Init(params);

    int result = NXSystem::MainLoop();

    NXSystem::Shutdown();
    // romfsExit();
    return result;
}

void _assert() {}
```

libnx handles startup via `__libnx_init` automatically — no explicit
`appletInitialize()` is needed when using devkitPro's standard SDL2 setup.

---

## Phase 4: GUI Resolution Block

### Modify `sources/Adapters/SDL2/GUI/SDLGUIWindowImp.cpp`

The constructor already has this block:
```cpp
#if defined(PLATFORM_PSP)
    screenWidth = 480; screenHeight = 272;
#elif defined(RS97)
    screenWidth = 320; screenHeight = 240;
#else
    SDL_GetDisplayMode(0, 0, &mode);
    screenWidth = mode.w; screenHeight = mode.h;
#endif
```

Insert a Switch case **before** the `#else`:
```cpp
#elif defined(PLATFORM_SWITCH)
    SDL_GetDisplayMode(0, 0, &mode);
    screenWidth  = mode.w;   // 1280 handheld / 1920 docked (SDL reports correct value)
    screenHeight = mode.h;   // 720 handheld  / 1080 docked
    windowed_ = false;       // always fullscreen on Switch
```

SDL2 on devkitPro reports the actual display resolution correctly, so `mult_` computes
automatically: `MIN(720/240, 1280/320)` = **3** handheld, `MIN(1080/240, 1920/320)` = **4** docked.

---

## Phase 5: SDL2 Process Thread Name Fix

### Modify `sources/Adapters/SDL2/Process/SDLProcess.cpp`

Current call is missing the required thread name argument (SDL2 API requires 3 args;
this is a latent bug that becomes a hard compile error on the devkitPro compiler):

```cpp
// Before:
SDL_CreateThread(_SDLStartThread, &thread);

// After:
SDL_CreateThread(_SDLStartThread, "lgpt_thread", &thread);
```

---

## Phase 6: Button Mapping

### Create `projects/resources/SWITCH/mapping.xml`

Switch Joy-Con / Pro Controller button indices via devkitPro SDL2:

| SDL Index | Physical Button |
|-----------|-----------------|
| 0         | A               |
| 1         | B               |
| 2         | X               |
| 3         | Y               |
| 4         | + (Plus/Start)  |
| 5         | - (Minus/Select)|
| 6         | L               |
| 7         | R               |
| 8         | ZL              |
| 9         | ZR              |
| hat 0     | D-pad           |

```xml
<MAPPINGS>
    <MAP src="hat:0:0:up"    dst="/event/up"        />
    <MAP src="hat:0:0:down"  dst="/event/down"       />
    <MAP src="hat:0:0:left"  dst="/event/left"       />
    <MAP src="hat:0:0:right" dst="/event/right"      />
    <MAP src="but:0:0"       dst="/event/a"          />
    <MAP src="but:0:1"       dst="/event/b"          />
    <MAP src="but:0:6"       dst="/event/lshoulder"  />
    <MAP src="but:0:7"       dst="/event/rshoulder"  />
    <MAP src="but:0:4"       dst="/event/start"      />
    <MAP src="but:0:5"       dst="/event/select"     />
</MAPPINGS>
```

Copy this file to `sdmc:/switch/lgpt/mapping.xml` on the SD card.

---

## Phase 7: SD Card Layout

On the Switch SD card (running Atmosphère CFW):
```
sdmc:/
  switch/
    lgpt/
      lgpt-switch.nro        ← the homebrew executable
      lgpt/                  ← LGPT data directory (root alias)
        mapping.xml
        config.xml           ← optional: override audio buffer size etc.
        samples/
        projects/
```

`NXSystem::Boot` sets `Path::SetAlias("root", "sdmc:/switch/lgpt")`, so LGPT resolves
all paths (songs, samples, config) relative to that directory.

---

## Phase 8: NRO Packaging Assets

Create a 256×256 JPEG icon at `projects/resources/SWITCH/icon.jpg`.
The `Makefile.SWITCH` `all` target runs `nacptool` + `elf2nro` automatically.

To build everything:
```bash
cd projects
make PLATFORM=SWITCH
# Produces: lgpt-switch.elf and lgpt-switch.nro
```

---

## Implementation Order

Execute in this sequence for incremental testability:

1. Phase 0 — Install devkitPro, verify `aarch64-none-elf-g++ --version` works
2. Phase 1 — Write `Makefile.SWITCH` + update `Makefile`; run `make PLATFORM=SWITCH` to see initial compile errors
3. Phase 5 — Fix SDL_CreateThread 3-arg bug (unblocks SDL2 compilation)
4. Phase 2 — Write `NXSystem.h` + `NXSystem.cpp`
5. Phase 3 — Write `NXMain.cpp`
6. Phase 4 — Add `PLATFORM_SWITCH` block to `SDLGUIWindowImp.cpp`
7. Iterate on remaining compile errors
8. Phase 7 — Add icon, build the NRO
9. Phase 6 — Add `mapping.xml`
10. Test on **Ryujinx** emulator first, then real hardware

---

## Verification

### Emulator (Ryujinx — open source, free)
Load `lgpt-switch.nro` in Ryujinx. Verify window renders at 1280×720 scaled correctly,
audio plays, keyboard input (mapped to gamepad) navigates the tracker UI.

### Real hardware (Atmosphère CFW)
- Copy `.nro` to `sdmc:/switch/lgpt/`
- Copy sample/project data to `sdmc:/switch/lgpt/lgpt/`
- Launch via hbmenu (Homebrew Menu)
- Verify: boots, fullscreen, audio without dropouts, D-pad + buttons work

### Audio tuning
Start `bufferSize_ = 2048`. Reduce to 1024 then 512 if latency is noticeable and
no dropouts occur. Edit `config.xml` on the SD card to avoid recompiling.

---

## Build Command Reference

All commands run inside the WSL2 Ubuntu terminal.

```bash
# One-time: set environment (or add to ~/.bashrc)
export DEVKITPRO=/opt/devkitpro
export DEVKITARM64=$DEVKITPRO/devkitA64
export PATH=$DEVKITARM64/bin:$DEVKITPRO/tools/bin:$PATH

# Build
cd /mnt/c/Users/carlo/Projects/LittleGPTracker/projects
make PLATFORM=SWITCH

# Output
ls ../lgpt-switch.elf
ls ../lgpt-switch.nro
```

#include "NXSystem.h"
// switch.h is intentionally omitted: libnx init is handled by -specs=switch.specs
// and its typedefs (Result, AudioDriver) conflict with LGPT's own class names.
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "Adapters/SDL2/GUI/GUIFactory.h"
#include "Adapters/SDL2/GUI/SDLEventManager.h"
#include "Adapters/SDL2/GUI/SDLGUIWindowImp.h"
#include "Adapters/SDL2/Timer/SDLTimer.h"
#include "Adapters/Unix/FileSystem/UnixFileSystem.h"
#include "Adapters/NX/Process/NXProcess.h"
#include "Application/Model/Config.h"
#include "System/Console/Logger.h"

#ifdef DUMMYMIDI
#include "Adapters/Dummy/Midi/DummyMidi.h"
#endif

#ifdef SDLAUDIO
#include "Adapters/SDL2/Audio/SDLAudio.h"
#endif

EventManager *NXSystem::eventManager_ = NULL;
bool NXSystem::finished_ = false;

/*
 * starts the main loop
 */
int NXSystem::MainLoop() {
	eventManager_->InstallMappings();
	return eventManager_->MainLoop();
};

/*
 * initializes the application
 */
void NXSystem::Boot(int argc, char **argv) {

	// Redirect stdout/stderr to SD card log file so Trace output is visible
	freopen("sdmc:/switch/lgpt/lgpt.log", "w", stdout);
	freopen("sdmc:/switch/lgpt/lgpt.log", "a", stderr);

	// Install System
	System::Install(new NXSystem());

	// Install FileSystem
	FileSystem::Install(new UnixFileSystem());

	// Install aliases — all data lives on the SD card
	Path::SetAlias("bin",  "sdmc:/switch/lgpt");
	Path::SetAlias("root", "sdmc:/switch/lgpt");

	// Log to stderr (visible in Atmosphère crash reports / nxlink)
	Trace::GetInstance()->SetLogger(*(new StdOutLogger()));

	// Process arguments
	Config::GetInstance()->ProcessArguments(argc, argv);

	// Install GUI Factory
	I_GUIWindowFactory::Install(new GUIFactory());

	// Install Timers
	TimerService::GetInstance()->Install(new SDLTimerService());

#ifdef SDLAUDIO
	Trace::Log("System", "Installing SDL audio");
	AudioSettings hint;
	hint.bufferSize_     = 2048;
	hint.preBufferCount_ = 4;
	Audio::Install(new SDLAudio(hint));
#endif

#ifdef DUMMYMIDI
	Trace::Log("System", "Installing DUMMY MIDI");
	MidiService::Install(new DummyMidi());
#endif

	// Install Threads
	SysProcessFactory::Install(new NXProcessFactory());

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) < 0) {
		return;
	}
	SDL_ShowCursor(SDL_DISABLE);

	atexit(SDL_Quit);

	eventManager_ = I_GUIWindowFactory::GetInstance()->GetEventManager();
	eventManager_->Init();
};

void NXSystem::Shutdown() {};

/*
 * get current time in milliseconds
 */
unsigned long NXSystem::GetClock() {
	return SDL_GetTicks();
}

/*
 * wraps sleep
 */
void NXSystem::Sleep(int millisec) {
	SDL_Delay(millisec);
}

/*
 * wraps malloc
 */
void *NXSystem::Malloc(unsigned size) {
	return malloc(size);
}

/*
 * wraps free
 */
void NXSystem::Free(void *ptr) {
	free(ptr);
}

/*
 * wraps memset
 */
void NXSystem::Memset(void *addr, char val, int size) {
	unsigned int ad = (intptr_t)addr;
	if (((ad & 0x3) == 0) && ((size & 0x3) == 0)) {
		unsigned int intVal = 0;
		for (int i = 0; i < 4; i++) {
			intVal = (intVal << 8) + val;
		}
		unsigned int *dst = (unsigned int *)addr;
		size_t intSize = size >> 2;
		for (unsigned int i = 0; i < intSize; i++) {
			*dst++ = intVal;
		}
	} else {
		memset(addr, val, size);
	}
};

/*
 * wraps memcpy
 */
void *NXSystem::Memcpy(void *s1, const void *s2, int n) {
	return memcpy(s1, s2, n);
};

/*
 * logprint
 */
void NXSystem::AddUserLog(const char *msg) {
	FILE *f = fopen("sdmc:/switch/lgpt/lgpt.log", "a");
	if (f) {
		fprintf(f, "%s\n", msg);
		fclose(f);
	}
};

/*
 * post quit message
 */
void NXSystem::PostQuitMessage() {
	SDLEventManager::GetInstance()->PostQuitMessage();
};

/*
 * get memory usage
 */
unsigned int NXSystem::GetMemoryUsage() { return 0; };

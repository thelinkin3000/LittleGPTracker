#include "Adapters/NX/System/NXSystem.h"
#include "Adapters/SDL2/GUI/SDLGUIWindowImp.h"
#include "Application/Application.h"

int main(int argc, char *argv[]) {
	NXSystem::Boot(argc, argv);

	SDLCreateWindowParams params;
	params.title       = "littlegptracker";
	params.cacheFonts_ = true;
	params.framebuffer_ = false;

	Application::GetInstance()->Init(params);

	int result = NXSystem::MainLoop();

	NXSystem::Shutdown();
	return result;
}

void _assert() {}

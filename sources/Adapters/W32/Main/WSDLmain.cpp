// VC6GUI.cpp : Defines the entry point for the application.
//

#include <windows.h>
#include "Application/Application.h"
#include "Adapters/WSDLSystem/WSDLSystem.h"
#include "Externals/SDL/SDL.h"
#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"
#include <string.h>

// SDL.h renames main→SDL_main. We provide WinMain ourselves so sdlmain.lib is not needed.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return SDL_main(__argc, __argv);
}

int main(int argc,char *argv[])
{
	bool fullscreen=false ;
	if (argc>1) {
		fullscreen=!strcmp(argv[1],"-fullscreen") ;
	}
	WSDLSystem::Boot(argc,argv) ;

#ifdef _SHOW_GP2X_
	GUIRect rect(0,0,640,480) ;
#else
	GUIRect rect(0,0,320,240) ;
#endif
	SDLCreateWindowParams params ;
	params.title="littlegptracker" ;
	params.cacheFonts_=true ;

	Application::GetInstance()->Init(params) ;

	int retval=WSDLSystem::MainLoop() ;

	WSDLSystem::Shutdown() ;
	return retval ;
}




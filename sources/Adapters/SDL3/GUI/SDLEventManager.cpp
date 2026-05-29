
#include "SDLEventManager.h"
#include "Application/Application.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "UIFramework/BasicDatas/GUIEvent.h"
#include "SDLGUIWindowImp.h"

bool SDLEventManager::finished_=false ;
bool SDLEventManager::dumpEvent_=false ;

SDLEventManager::SDLEventManager()
{
}

SDLEventManager::~SDLEventManager()
{
}

bool SDLEventManager::Init()
{
	EventManager::Init() ;

	// SDL3: SDL_Init returns bool (true = success); SDL_INIT_TIMER removed in SDL3
	if ( !SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK) )
	{
		return false;
	}

	SDL_HideCursor();

	atexit(SDL_Quit) ;

	SDL_InitSubSystem(SDL_INIT_JOYSTICK);

	// SDL3: SDL_GetJoysticks returns an array of SDL_JoystickID
	int joyCount = 0;
	SDL_JoystickID *joyIDs = SDL_GetJoysticks(&joyCount);
	joyCount = (joyCount > MAX_JOY_COUNT) ? MAX_JOY_COUNT : joyCount;

	keyboardCS_=new KeyboardControllerSource("keyboard") ;
	const char *dumpIt=Config::GetInstance()->GetValue("DUMPEVENT") ;
	if ((dumpIt)&&(!strcmp(dumpIt,"YES")))
	{
		dumpEvent_=true ;
	}

	for (int i=0;i<MAX_JOY_COUNT;i++)
	{
		joystick_[i]=0 ;
		buttonCS_[i]=0 ;
		joystickCS_[i]=0 ;
	}

	for (int i=0;i<joyCount;i++)
	{
		char sourceName[128] ;
		joystick_[i]=SDL_OpenJoystick(joyIDs[i]) ;
		Trace::Log("EVENT","joystick[%d]=%p",i,joystick_[i]) ;
		Trace::Log("EVENT","Number of axis:%d",SDL_GetNumJoystickAxes(joystick_[i])) ;
		Trace::Log("EVENT","Number of buttons:%d",SDL_GetNumJoystickButtons(joystick_[i])) ;
		Trace::Log("EVENT","Number of hats:%d",SDL_GetNumJoystickHats(joystick_[i])) ;
		sprintf(sourceName,"buttonJoy%d",i) ;
		buttonCS_[i]=new ButtonControllerSource(sourceName) ;
		sprintf(sourceName,"axisJoy%d",i) ;
		joystickCS_[i]=new JoystickControllerSource(sourceName) ;
		sprintf(sourceName,"hatJoy%d",i) ;
		hatCS_[i]=new HatControllerSource(sourceName) ;
	}

	if (joyIDs) {
		SDL_free(joyIDs);
	}

	return true ;
}

int SDLEventManager::MainLoop()
{
	GUIWindow *appWindow=Application::GetInstance()->GetWindow() ;
	SDLGUIWindowImp *sdlWindow=(SDLGUIWindowImp *)appWindow->GetImpWindow() ;
	while (!finished_)
	{
		SDL_Event event;
		if (SDL_WaitEvent(&event))
		{
			switch (event.type) {
				// SDL3: event.key.keysym.scancode -> event.key.scancode
				case SDL_EVENT_KEY_DOWN:
					if (dumpEvent_)
					{
						Trace::Log("EVENT","key(%s:%d):%d",SDL_GetScancodeName(event.key.scancode),event.key.scancode,1) ;
					}
					keyboardCS_->SetKey((int)event.key.scancode,true) ;
					break ;

				case SDL_EVENT_KEY_UP:
					if (dumpEvent_)
					{
						Trace::Log("EVENT","key(%s:%d):%d",SDL_GetScancodeName(event.key.scancode),event.key.scancode,0) ;
					}
					keyboardCS_->SetKey((int)event.key.scancode,false) ;
					break ;

				case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
					buttonCS_[event.jbutton.which]->SetButton(event.jbutton.button,true) ;
					break ;
				case SDL_EVENT_JOYSTICK_BUTTON_UP:
					if (dumpEvent_) {
						Trace::Log("EVENT","but(%d):%d",event.jbutton.which,event.jbutton.button) ;
					}
					buttonCS_[event.jbutton.which]->SetButton(event.jbutton.button,false) ;
					break ;
				case SDL_EVENT_JOYSTICK_AXIS_MOTION:
					if (dumpEvent_) {
						Trace::Log("EVENT","joy(%d)::%d=%d",event.jaxis.which,event.jaxis.axis,event.jaxis.value) ;
					}
					joystickCS_[event.jaxis.which]->SetAxis(event.jaxis.axis,float(event.jaxis.value)/32767.0f) ;
					break ;
				case SDL_EVENT_JOYSTICK_HAT_MOTION:
					if (dumpEvent_)
					{
						for (int i=0;i<4;i++)
						{
							int mask = 1<<i ;
							if (event.jhat.value&mask)
							{
								Trace::Log("EVENT","hat(%d)::%d::%d",event.jhat.which,event.jhat.hat,i) ;
							}
						}
					}
					hatCS_[event.jhat.which]->SetHat(event.jhat.hat,event.jhat.value) ;
					break ;
				case SDL_EVENT_JOYSTICK_BALL_MOTION:
					if (dumpEvent_)
					{
						Trace::Log("EVENT","ball(%d)::%d=(%d,%d)",event.jball.which,event.jball.ball,event.jball.xrel,event.jball.yrel) ;
					}
					break ;
			}

			switch (event.type)
			{
				case SDL_EVENT_QUIT:
					sdlWindow->ProcessQuit() ;
					break ;
				// SDL3: SDL_WINDOWEVENT is gone; each window event is its own top-level type
				case SDL_EVENT_WINDOW_EXPOSED:
				case SDL_EVENT_WINDOW_RESIZED:
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
					sdlWindow->ProcessExpose() ;
					break ;
				case SDL_EVENT_USER:
					sdlWindow->ProcessUserEvent(event) ;
					break ;
			}
		}
	}
	return 0 ;
} ;

void SDLEventManager::PostQuitMessage()
{
	Trace::Log("EVENT","SDEM:PostQuitMessage()") ;
	finished_=true  ;
} ;

int SDLEventManager::GetKeyCode(const char *key)
{
	return SDL_GetScancodeFromName(key);
}

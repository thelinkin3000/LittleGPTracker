#ifndef _EFFECTS_VIEW_H_
#define _EFFECTS_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "ViewData.h"

class EffectsView: public FieldView {
public:
	EffectsView(GUIWindow &w,ViewData *data) ;
	virtual ~EffectsView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() {} ;

protected:
	const char *GetFocusDescription() ;
private:
  Project *project_;
} ;
#endif

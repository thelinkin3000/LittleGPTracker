#include "EffectsView.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Application/Model/Project.h"

EffectsView::EffectsView(GUIWindow &w,ViewData *data):FieldView(w,data) {

	project_=data->project_ ;

	GUIPoint position=GetAnchor() ;

    // Compact reverb: 3 parameters per row to save screen space
    auto addReverbRow = [&](int fourccSize, int fourccDamp, int fourccWet, const char *label) {
        position._y += 1;
        Variable *rvSz = project_->FindVariable(fourccSize);
        Variable *rvDm = project_->FindVariable(fourccDamp);
        Variable *rvWt = project_->FindVariable(fourccWet);
        if (rvSz && rvDm && rvWt) {
            UIStaticField *lab = new UIStaticField(position, label);
            T_SimpleList<UIField>::Insert(lab);
            position._x += 5;
            UIIntVarField *f = new UIIntVarField(position, *rvSz, "%2.2X", 0, 255, 1, 16);
            T_SimpleList<UIField>::Insert(f);
            position._x += 4;
            f = new UIIntVarField(position, *rvDm, "%2.2X", 0, 255, 1, 16);
            T_SimpleList<UIField>::Insert(f);
            position._x += 4;
            f = new UIIntVarField(position, *rvWt, "%2.2X", 0, 255, 1, 16);
            T_SimpleList<UIField>::Insert(f);
            position._x -= 13;
        }
    };

    addReverbRow(VAR_RV0SZ, VAR_RV0DM, VAR_RV0WT, "rv0:");
    addReverbRow(VAR_RV1SZ, VAR_RV1DM, VAR_RV1WT, "rv1:");
    addReverbRow(VAR_RV2SZ, VAR_RV2DM, VAR_RV2WT, "rv2:");

    position._y += 1;

    // Compact delay: 4 parameters per row (time, feedback, wet, mode)
    auto addDelayRow = [&](int fourccTime, int fourccFb, int fourccWet, int fourccMode, const char *label) {
        position._y += 1;
        Variable *dlTm = project_->FindVariable(fourccTime);
        Variable *dlFb = project_->FindVariable(fourccFb);
        Variable *dlWt = project_->FindVariable(fourccWet);
        Variable *dlMd = project_->FindVariable(fourccMode);
        if (dlTm && dlFb && dlWt && dlMd) {
            UIStaticField *lab = new UIStaticField(position, label);
            T_SimpleList<UIField>::Insert(lab);
            position._x += 5;
            UIIntVarField *f = new UIIntVarField(position, *dlTm, "%2.2X", 0, 255, 1, 16);
            T_SimpleList<UIField>::Insert(f);
            position._x += 4;
            f = new UIIntVarField(position, *dlFb, "%2.2X", 0, 255, 1, 16);
            T_SimpleList<UIField>::Insert(f);
            position._x += 4;
            f = new UIIntVarField(position, *dlWt, "%2.2X", 0, 255, 1, 16);
            T_SimpleList<UIField>::Insert(f);
            position._x += 4;
            f = new UIIntVarField(position, *dlMd, "%s", 0, 1, 1, 1);
            T_SimpleList<UIField>::Insert(f);
            position._x -= 17;
        }
    };

    addDelayRow(VAR_DL0TM, VAR_DL0FB, VAR_DL0WT, VAR_DL0MD, "dl0:");
    addDelayRow(VAR_DL1TM, VAR_DL1FB, VAR_DL1WT, VAR_DL1MD, "dl1:");
    addDelayRow(VAR_DL2TM, VAR_DL2FB, VAR_DL2WT, VAR_DL2MD, "dl2:");
}

EffectsView::~EffectsView() {
}

const char *EffectsView::GetFocusDescription() {
    UIField *focus = GetFocus();
    if (!focus) return "";

    GUIPoint fpos = focus->GetPosition();
    GUIPoint anchor = GetAnchor();
    int dx = fpos._x - anchor._x;
    int dy = fpos._y - anchor._y;

    if (dy == 1) {
        if (dx == 5)  return "RV0:SIZE - Comb delay line length";
        if (dx == 9)  return "RV0:DAMP - High frequency damping";
        if (dx == 13) return "RV0:WET  - Wet/dry mix level";
    } else if (dy == 2) {
        if (dx == 5)  return "RV1:SIZE - Comb delay line length";
        if (dx == 9)  return "RV1:DAMP - High frequency damping";
        if (dx == 13) return "RV1:WET  - Wet/dry mix level";
    } else if (dy == 3) {
        if (dx == 5)  return "RV2:SIZE - Comb delay line length";
        if (dx == 9)  return "RV2:DAMP - High frequency damping";
        if (dx == 13) return "RV2:WET  - Wet/dry mix level";
    } else if (dy == 5) {
        if (dx == 5)  return "DL0:TIME - Delay time (0-2s)";
        if (dx == 9)  return "DL0:FB   - Feedback amount";
        if (dx == 13) return "DL0:WET  - Wet/dry mix level";
        if (dx == 17) return "DL0:MODE - Mono or ping-pong";
    } else if (dy == 6) {
        if (dx == 5)  return "DL1:TIME - Delay time (0-2s)";
        if (dx == 9)  return "DL1:FB   - Feedback amount";
        if (dx == 13) return "DL1:WET  - Wet/dry mix level";
        if (dx == 17) return "DL1:MODE - Mono or ping-pong";
    } else if (dy == 7) {
        if (dx == 5)  return "DL2:TIME - Delay time (0-2s)";
        if (dx == 9)  return "DL2:FB   - Feedback amount";
        if (dx == 13) return "DL2:WET  - Wet/dry mix level";
        if (dx == 17) return "DL2:MODE - Mono or ping-pong";
    }
    return "";
}

void EffectsView::ProcessButtonMask(unsigned short mask,bool pressed) {

    if (!pressed)
        return;

    FieldView::ProcessButtonMask(mask);

    if (mask&EPBM_R) {
        if (mask&EPBM_UP) {
            ViewType vt=VT_SONG;
            ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
            SetChanged();
            NotifyObservers(&ve) ;
        }
    }
}

void EffectsView::DrawView() {

    Clear() ;

	GUITextProperties props ;
	GUIPoint pos=GetTitlePosition() ;

    SetColor(CD_NORMAL);
    DrawString(pos._x,pos._y,"EFFECTS",props) ;

    const char *desc = GetFocusDescription();
    if (desc[0] != '\0') {
        pos._y += 1;
        SetColor(CD_HILITE1);
        DrawString(pos._x,pos._y,desc,props) ;
    }

    FieldView::Redraw();
    drawMap();
}

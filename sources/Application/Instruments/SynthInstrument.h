
#ifndef _SYNTH_INSTRUMENT_H_
#define _SYNTH_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"
#include "Application/Utils/fixed.h"
#include "Foundation/Variables/Variable.h"

// FourCC parameter IDs
#define SYIP_VOL    MAKE_FOURCC('S','Y','V','L')
#define SYIP_O1WV   MAKE_FOURCC('O','1','W','V')
#define SYIP_O2WV   MAKE_FOURCC('O','2','W','V')
#define SYIP_O2DT   MAKE_FOURCC('O','2','D','T')
#define SYIP_O2MX   MAKE_FOURCC('O','2','M','X')
#define SYIP_EATK   MAKE_FOURCC('E','A','T','K')
#define SYIP_EDEC   MAKE_FOURCC('E','D','E','C')
#define SYIP_ESUS   MAKE_FOURCC('E','S','U','S')
#define SYIP_EREL   MAKE_FOURCC('E','R','E','L')
#define SYIP_FCUT   MAKE_FOURCC('S','F','C','T')
#define SYIP_FRES   MAKE_FOURCC('S','F','R','S')
#define SYIP_FMOD   MAKE_FOURCC('S','F','M','D')
#define SYIP_FEAM   MAKE_FOURCC('F','E','A','M')
#define SYIP_FEAT   MAKE_FOURCC('F','E','A','T')
#define SYIP_NLEN   MAKE_FOURCC('N','L','E','N')
#define SYIP_LRAT   MAKE_FOURCC('L','R','A','T')
#define SYIP_LDPT   MAKE_FOURCC('L','D','P','T')
#define SYIP_LDST   MAKE_FOURCC('L','D','S','T')
// table/automation reuse SIP_ FourCCs intentionally for InstrumentView compatibility
#define SYIP_TABL   MAKE_FOURCC('T','A','B','L')
#define SYIP_TBLA   MAKE_FOURCC('T','B','L','A')

enum SynthWaveform { SWF_SINE=0, SWF_TRI, SWF_SAW, SWF_SQUARE, SWF_LAST };
enum SynthOsc2Wave { SO2_SINE=0, SO2_TRI, SO2_SAW, SO2_SQUARE, SO2_OFF, SO2_LAST };
enum SynthLfoDest  { SLD_PITCH=0, SLD_FCUT, SLD_AMP, SLD_LAST };
enum SynthFltMode  { SFM_LP=0, SFM_HP, SFM_BP, SFM_NOTCH, SFM_LAST };

enum SynthEnvStage { SES_IDLE=0, SES_ATTACK, SES_DECAY, SES_SUSTAIN, SES_RELEASE };

struct SynthVoiceState {
    fixed  osc1Phase;
    fixed  osc2Phase;
    fixed  osc1Inc;
    fixed  osc2Inc;
    fixed  envLevel;
    SynthEnvStage envStage;
    int    envTick;
    fixed  fltEnvLevel;
    int    fltEnvTick;
    fixed  lfoPhase;
    int    noteTick;
    bool   active;
    unsigned char note;
};

class SynthInstrument : public I_Instrument {
public:
    SynthInstrument();
    virtual ~SynthInstrument();

    virtual bool Init();
    virtual bool Start(int channel, unsigned char note, bool retrigger = true);
    virtual void Stop(int channel);
    virtual void OnStart();
    virtual bool Render(int channel, fixed *buffer, int size, bool updateTick);
    virtual bool IsInitialized();
    virtual bool IsEmpty();
    virtual InstrumentType GetType() { return IT_SYNTH; }
    virtual const char *GetName();
    virtual void ProcessCommand(int channel, FourCC cc, ushort value);
    virtual void Purge();
    virtual int GetTable();
    virtual bool GetTableAutomation();
    virtual void GetTableState(TableSaveState &state);
    virtual void SetTableState(TableSaveState &state);

    static void BuildNoteTable();

private:
    fixed sampleForOsc(int waveform, fixed phase);

    SynthVoiceState voice_[SONG_CHANNEL_COUNT];
    bool initialized_;
    char name_[20];
    TableSaveState tableState_;

    Variable *volume_;
    Variable *osc1Wave_;
    Variable *osc2Wave_;
    Variable *osc2Detune_;
    Variable *osc2Mix_;
    Variable *noteLength_;
    Variable *envAttack_;
    Variable *envDecay_;
    Variable *envSustain_;
    Variable *envRelease_;
    Variable *fltCutoff_;
    Variable *fltReso_;
    Variable *fltMode_;
    Variable *fltEnvAmt_;
    Variable *fltEnvAtk_;
    Variable *lfoRate_;
    Variable *lfoDepth_;
    Variable *lfoDest_;
    Variable *table_;
    Variable *tableAuto_;

    static fixed noteTable_[128];
    static fixed sineTable_[256];
    static bool  tablesBuilt_;
};

#endif

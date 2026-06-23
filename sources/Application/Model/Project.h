#ifndef _PROJECT_H_
#define _PROJECT_H_

#include "Song.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Persistency/Persistent.h"
#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Observable.h"

#define VAR_TEMPO MAKE_FOURCC('T', 'M', 'P', 'O')
#define VAR_MASTERVOL   	MAKE_FOURCC('M', 'S', 'T', 'R')
#define VAR_WRAP        	MAKE_FOURCC('W', 'R', 'A', 'P')
#define VAR_MIDIDEVICE  	MAKE_FOURCC('M', 'I', 'D', 'I')
#define VAR_TRANSPOSE   	MAKE_FOURCC('T', 'R', 'S', 'P')
#define VAR_SOFTCLIP 		MAKE_FOURCC('S', 'F', 'T', 'C')
#define VAR_SOFTCLIP_GAIN 	MAKE_FOURCC('S', 'F', 'G', 'N')
#define VAR_PREGAIN   		MAKE_FOURCC('P', 'R', 'G', 'N')
#define VAR_SCALE 			MAKE_FOURCC('S', 'C', 'A', 'L')
#define VAR_RENDER MAKE_FOURCC('R', 'N', 'D', 'R')

// Reverb bus global configs (3 buses × size/damp/wet)
#define VAR_RV0SZ MAKE_FOURCC('R','V','0','S')
#define VAR_RV0DM MAKE_FOURCC('R','V','0','D')
#define VAR_RV0WT MAKE_FOURCC('R','V','0','W')
#define VAR_RV1SZ MAKE_FOURCC('R','V','1','S')
#define VAR_RV1DM MAKE_FOURCC('R','V','1','D')
#define VAR_RV1WT MAKE_FOURCC('R','V','1','W')
#define VAR_RV2SZ MAKE_FOURCC('R','V','2','S')
#define VAR_RV2DM MAKE_FOURCC('R','V','2','D')
#define VAR_RV2WT MAKE_FOURCC('R','V','2','W')

// Delay bus global configs (3 buses × time/fb/wet/mode)
#define VAR_DL0TM MAKE_FOURCC('D','L','0','T')
#define VAR_DL0FB MAKE_FOURCC('D','L','0','F')
#define VAR_DL0WT MAKE_FOURCC('D','L','0','W')
#define VAR_DL0MD MAKE_FOURCC('D','L','0','M')
#define VAR_DL1TM MAKE_FOURCC('D','L','1','T')
#define VAR_DL1FB MAKE_FOURCC('D','L','1','F')
#define VAR_DL1WT MAKE_FOURCC('D','L','1','W')
#define VAR_DL1MD MAKE_FOURCC('D','L','1','M')
#define VAR_DL2TM MAKE_FOURCC('D','L','2','T')
#define VAR_DL2FB MAKE_FOURCC('D','L','2','F')
#define VAR_DL2WT MAKE_FOURCC('D','L','2','W')
#define VAR_DL2MD MAKE_FOURCC('D','L','2','M')

#define PROJECT_NUMBER "1"
#define PROJECT_RELEASE "6"
#define BUILD_COUNT "0-bacon15"

#define MAX_TAP 3

class Project: public Persistent,public VariableContainer,I_Observer  {
public:
  Project();
  ~Project();
  static Project *GetInstance() { return instance_; }
  void Purge();
  void PurgeInstruments(bool removeFromDisk);

  Song *song_;

  int GetMasterVolume();
  bool Wrap();
  void OnTempoTap();
  void NudgeTempo(int value);
  int GetScale();
  int GetTempo(); // Takes nudging into account
  int GetTranspose();
  int GetSoftclip();
  int GetSoftclipGain();
  int GetPregain();
  int GetRenderMode();
  void Trigger();

  static const unsigned int MAX_RENDER_MODE = 3;
  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);

  InstrumentBank *GetInstrumentBank();
  virtual void SaveContent(TiXmlNode *node);
  virtual void RestoreContent(TiXmlElement *element);

  void LoadFirstGen(const char *root);

protected:
  void buildMidiDeviceList();

private:
  InstrumentBank *instrumentBank_;
  char **midiDeviceList_;
  int midiDeviceListSize_;
  int tempoNudge_;
  unsigned long lastTap_[MAX_TAP];
  unsigned int tempoTapCount_;

  static Project *instance_;
};
#endif

#include "MixerService.h"
#include "Application/Audio/DummyAudioOut.h"
#include "Application/Model/Config.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Project.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"

// Reverb bus FourCCs for lookup
#define VAR_RV0SZ MAKE_FOURCC('R','V','0','S')
#define VAR_RV0DM MAKE_FOURCC('R','V','0','D')
#define VAR_RV0WT MAKE_FOURCC('R','V','0','W')
#define VAR_RV1SZ MAKE_FOURCC('R','V','1','S')
#define VAR_RV1DM MAKE_FOURCC('R','V','1','D')
#define VAR_RV1WT MAKE_FOURCC('R','V','1','W')
#define VAR_RV2SZ MAKE_FOURCC('R','V','2','S')
#define VAR_RV2DM MAKE_FOURCC('R','V','2','D')
#define VAR_RV2WT MAKE_FOURCC('R','V','2','W')

MixerService::MixerService() : out_(0), sync_(0), isRendering_(false) {
    mode_ = MSRM_PLAYBACK;
};

MixerService::~MixerService(){};

/*
 * initializes the mixer service, config changes depending if we're in sequencer or render mode
 */
bool MixerService::Init() {
    // create the output depending on rendering mode
    out_ = 0;
	switch (mode_) {
    case MSRM_STEREO:
    case MSRM_STEMS:
        out_ = new DummyAudioOut();
        break;
    default:
        Audio *audio = Audio::GetInstance();
        out_ = audio->GetFirst();
        break;
	}

	for (int i=0;i<MAX_BUS_COUNT;i++) {
		master_.Insert(bus_[i]);
	}
	master_.Insert(reverbBus_);
	master_.Insert(delayBus_);

	bool result = false;
	if (out_) {
		result = out_->Init();
		if (result) {
			out_->Insert(master_);
		}

        initRendering(mode_);
        out_->AddObserver(*MidiService::GetInstance());
	}

	sync_=SDL_CreateMutex();
	NAssert(sync_);

	if (result) {
		Trace::Log("MixerService", "output initialized");
	} else {
		Trace::Log("MixerService", "failed to initialize output");
	}
	return (result);
};

void MixerService::initRendering(MixerServiceRenderMode mode) {
    switch(mode) {
    case MSRM_PLAYBACK:
        break;
    case MSRM_STEREO:
        out_->SetFileRenderer("project:mixdown.wav");
        break;
    case MSRM_STEMS:
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            char buffer[1024];
            sprintf(buffer, "project:channel%d.wav", i);
            bus_[i].SetFileRenderer(buffer);
        }
        break;
    }
}

void MixerService::Close() {
	if (out_) {
    out_->RemoveObserver(*MidiService::GetInstance());
		out_->Close() ;
		out_->Empty() ;
		master_.Empty() ;

		switch(mode_) {
        case MSRM_STEMS:
        case MSRM_STEREO:
            break;
        default:
            break;
        }
    }
   for (int i=0;i<MAX_BUS_COUNT;i++) {
	   bus_[i].Empty() ;
   }
	out_=0 ;
	SDL_DestroyMutex(sync_) ;
	sync_=0 ;
} ;

void MixerService::SetRenderMode(int mode) {
    mode_ = MixerServiceRenderMode(mode);
}

bool MixerService::IsRendering() { return isRendering_; }

bool MixerService::Start() {
    MidiService::GetInstance()->Start();
    if (out_) {
        out_->AddObserver(*this);
        out_->Start();
     }
	return true ;
} ;

void MixerService::Stop() {
	MidiService::GetInstance()->Stop() ;
     if (out_) {
      out_->Stop() ;
      out_->RemoveObserver(*this) ;
     }
}

MixBus *MixerService::GetMixBus(int i) {
	return &(bus_[i]) ;
} ;

void MixerService::Update(Observable &o,I_ObservableData *d)  {

  AudioDriver::Event *event=(AudioDriver::Event *)d;
  if (event->type_ == AudioDriver::Event::ADET_BUFFERNEEDED)
  {
    static int mixCount_ = 0;
    mixCount_++;
    if (mixCount_ == 1)
      Trace::Log("MIXER","first BUFFERNEEDED event");
    else if (mixCount_ % 500 == 0)
      Trace::Log("MIXER","BUFFERNEEDED count=%d", mixCount_);
    Lock() ;
    SetChanged() ;
    NotifyObservers() ;

    out_->Trigger();
    Unlock();
  }
}

bool MixerService::Clipped() {
     return out_->Clipped() ;
} ;

void MixerService::SetPregain(int vol) {
    Mixer *mixer = Mixer::GetInstance();

    fixed masterVolume = fp_mul(i2fp(vol), fl2fp(0.01f));

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        bus_[i].SetVolume(masterVolume);
  }
};

void MixerService::SetSoftclip(int clip, int gain) {
    out_->SetSoftclip(clip, gain);
}

void MixerService::SetMasterVolume(int attn) { out_->SetMasterVolume(attn); }

int MixerService::GetPlayedBufferPercentage() {
	return out_->GetPlayedBufferPercentage() ;
}

void MixerService::toggleRendering(bool enable) {
    isRendering_ = enable;
    switch (mode_) {
    case MSRM_PLAYBACK:
        initRendering(MSRM_PLAYBACK);
        break;
    case MSRM_STEREO:
        initRendering(MSRM_STEREO);
        out_->EnableRendering(enable);
        break;
    case MSRM_STEMS:
        initRendering(MSRM_STEMS);
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            bus_[i].EnableRendering(enable);
        };
        break;
    }
}

void MixerService::OnPlayerStart() {
	toggleRendering(true) ;
	SyncReverbConfigs();
	SyncDelayConfigs();
} ;

void MixerService::OnPlayerStop() {
	toggleRendering(false) ;
} ;

void MixerService::SyncReverbConfigs() {
    Project *project = Project::GetInstance();
    if (!project) return;

    static const FourCC sizeIDs[3] = { VAR_RV0SZ, VAR_RV1SZ, VAR_RV2SZ };
    static const FourCC dampIDs[3] = { VAR_RV0DM, VAR_RV1DM, VAR_RV2DM };
    static const FourCC wetIDs[3]  = { VAR_RV0WT, VAR_RV1WT, VAR_RV2WT };

    for (int b = 0; b < 3; b++) {
        Variable *vSize = project->FindVariable(sizeIDs[b]);
        Variable *vDamp = project->FindVariable(dampIDs[b]);
        Variable *vWet  = project->FindVariable(wetIDs[b]);

        ReverbBusConfig cfg;
        cfg.size = vSize ? vSize->GetInt() : 64;
        cfg.damp = vDamp ? vDamp->GetInt() : 64;
        cfg.wet  = vWet  ? vWet->GetInt()  : 64;
        ReverbBus::SetBusConfig(b, cfg);
    }
}

void MixerService::SyncDelayConfigs() {
    Project *project = Project::GetInstance();
    if (!project) return;

    static const FourCC timeIDs[3] = { VAR_DL0TM, VAR_DL1TM, VAR_DL2TM };
    static const FourCC fbIDs[3]   = { VAR_DL0FB, VAR_DL1FB, VAR_DL2FB };
    static const FourCC wetIDs[3]  = { VAR_DL0WT, VAR_DL1WT, VAR_DL2WT };
    static const FourCC modeIDs[3] = { VAR_DL0MD, VAR_DL1MD, VAR_DL2MD };

    for (int b = 0; b < 3; b++) {
        Variable *vTime = project->FindVariable(timeIDs[b]);
        Variable *vFb   = project->FindVariable(fbIDs[b]);
        Variable *vWet  = project->FindVariable(wetIDs[b]);
        Variable *vMode = project->FindVariable(modeIDs[b]);

        DelayBusConfig cfg;
        cfg.time     = vTime ? vTime->GetInt() : 64;
        cfg.feedback = vFb   ? vFb->GetInt()   : 64;
        cfg.wet      = vWet  ? vWet->GetInt()  : 64;
        cfg.mode     = vMode ? vMode->GetInt() : 0;
        DelayBus::SetBusConfig(b, cfg);
    }
}

void MixerService::Execute(FourCC id,float value) {
     if (value>0.5) {
        Audio *audio=Audio::GetInstance() ;
        int volume=audio->GetMixerVolume() ;
        switch(id) {
           case TRIG_VOLUME_INCREASE:
                if (volume<100) volume+=1 ;
                break ;
           case TRIG_VOLUME_DECREASE:
                if (volume>0) volume-=1 ;
                break ;                       
        } ;
        audio->SetMixerVolume(volume) ;
     } ;
}

AudioOut *MixerService::GetAudioOut() {
	return out_ ;
} ;


void MixerService::Lock() {
	if (sync_) SDL_LockMutex(sync_) ;
}

void MixerService::Unlock() {
	if (sync_) SDL_UnlockMutex(sync_) ;
}

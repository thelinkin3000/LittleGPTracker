#include "SDLAudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

// SDL3 audio callback: writes into an SDL_AudioStream instead of a raw buffer.
// We allocate a temporary buffer, fill it via OnChunkDone, then push it to the stream.
static void sdl3_callback(void *userdata, SDL_AudioStream *stream,
                           int additional_amount, int /*total_amount*/) {
    if (additional_amount > 0) {
        SDLAudioDriver *driver = (SDLAudioDriver *)userdata;
        Uint8 *buf = (Uint8 *)SDL_malloc(additional_amount);
        if (buf) {
            driver->OnChunkDone(buf, additional_amount);
            SDL_PutAudioStreamData(stream, buf, additional_amount);
            SDL_free(buf);
        }
    }
}

SDLAudioDriverThread::SDLAudioDriverThread(SDLAudioDriver *driver) {
    semaphore_=SysSemaphore::Create(0,1024);
    driver_ = driver;
};

bool SDLAudioDriverThread::Execute() {
    static int threadCount_ = 0;
    Trace::Log("AUDIO","driver thread started");
    while (!shouldTerminate()) {
        semaphore_->Wait();
        int n = ++threadCount_;
        if (n <= 5 || n % 100 == 0)
            Trace::Log("AUDIO","thread wake #%d, calling OnNewBufferNeeded", n);
        driver_->OnNewBufferNeeded();
        if (n <= 5 || n % 100 == 0)
            Trace::Log("AUDIO","thread wake #%d, OnNewBufferNeeded done", n);
    };
    SysSemaphore *semaphore = semaphore_;
    semaphore_ = 0;
    delete semaphore;
    return true;
};

void SDLAudioDriverThread::Notify() {
    if (semaphore_) {
        semaphore_->Post();
    }
};

void SDLAudioDriverThread::RequestTermination() {
    SysThread::RequestTermination();
    semaphore_->Post();
    SDL_Delay(10);
}

//-------------------------------------------------------------------------------------------------

SDLAudioDriver::SDLAudioDriver(AudioSettings &settings)
    : AudioDriver(settings), unalignedMain_(0), miniBlank_(0) {
    isPlaying_ = false;
    thread_ = 0;
    audioStream_ = 0;
}

SDLAudioDriver::~SDLAudioDriver() {}

bool SDLAudioDriver::InitDriver() {
    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq     = 44100;

    SDL_SetHint("APP_NAME", "LittleGPTracker");
    SDL_SetHint("AUDIO_DEVICE_APP_NAME", "LittleGPTracker");

    audioStream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdl3_callback, this);
    if (!audioStream_) {
        Trace::Error("Couldn't open SDL3 audio: %s\n", SDL_GetError());
        return false;
    }

    // Query the actual device format to determine fragment size
    SDL_AudioSpec obtained;
    int sampleFrames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audioStream_),
                             &obtained, &sampleFrames);
    if (sampleFrames <= 0) {
        sampleFrames = settings_.bufferSize_;
    }
    fragSize_ = sampleFrames * obtained.channels
                * SDL_AUDIO_BYTESIZE(obtained.format);

    const char *driverName = SDL_GetCurrentAudioDriver();
    Trace::Log("AUDIO", "%s opened: %d sample frames, %d Hz, fragSize=%d",
               driverName ? driverName : "unknown",
               sampleFrames, obtained.freq, fragSize_);

    unalignedMain_ = (char *)SYS_MALLOC(fragSize_ + SOUND_BUFFER_MAX);
    mainBuffer_    = (char *)unalignedMain_;

    miniBlank_ = (char *)malloc(fragSize_);
    SYS_MEMSET(miniBlank_, 0, fragSize_);

    return true;
};

void SDLAudioDriver::CloseDriver() {
    if (miniBlank_) {
        SYS_FREE(miniBlank_);
        miniBlank_ = 0;
    }
    if (unalignedMain_) {
        SYS_FREE(unalignedMain_);
        unalignedMain_ = 0;
    }
    if (audioStream_) {
        SDL_DestroyAudioStream(audioStream_);
        audioStream_ = 0;
    }
};

bool SDLAudioDriver::StartDriver() {
    thread_ = new SDLAudioDriverThread(this);
    thread_->Start();

    short blank[4000];
    SYS_MEMSET(blank, 0, 4000);
    bufferPos_ = 0;
    bufferSize_ = 0;

    for (int i = 0; i < settings_.preBufferCount_; i++) {
        AddBuffer((short *)miniBlank_, fragSize_ / 4);
        MidiService::GetInstance()->AdvancePlayQueue();
    }
    if (settings_.preBufferCount_ == 0) {
        thread_->Notify();
    }

    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(audioStream_));
    startTime_ = SDL_GetTicks();

    return 1;
};

void SDLAudioDriver::StopDriver() {
    if (thread_) {
        thread_->RequestTermination();
        SysThread *thread = thread_;
        thread_ = 0;
        SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(audioStream_));
        delete thread;
    };
};

double SDLAudioDriver::GetStreamTime() {
    return (SDL_GetTicks() - startTime_) / 1000.0;
}

void SDLAudioDriver::OnChunkDone(Uint8 *stream, int len) {
    while (bufferSize_ - bufferPos_ < len) {
        memmove(mainBuffer_, mainBuffer_ + bufferPos_,
                bufferSize_ - bufferPos_);

        // Acquire load: ensures we see all data written before the release store in AddBuffer()
        char *slotBuf = pool_[poolPlayPosition_].buffer_.load(std::memory_order_acquire);
        if (slotBuf == 0) {
            static int underrunCount = 0;
            underrunCount++;
            if (underrunCount <= 10 || underrunCount % 100 == 0)
                Trace::Log("AUDIO", "pool underrun #%d at pos=%d", underrunCount, poolPlayPosition_);
            SYS_MEMCPY(mainBuffer_+bufferSize_-bufferPos_, miniBlank_, fragSize_);
            bufferSize_=bufferSize_-bufferPos_+fragSize_ ;
            bufferPos_ = 0;
            // Notify the driver thread so it can generate new buffers;
            // without this, the thread stays blocked on the semaphore forever.
            if (thread_)
                thread_->Notify();
        } else {
            memcpy(mainBuffer_ + bufferSize_ - bufferPos_,
                   slotBuf,
                   pool_[poolPlayPosition_].size_);

            bufferSize_ =
                bufferSize_ - bufferPos_ + pool_[poolPlayPosition_].size_;
            bufferPos_ = 0;

            SYS_FREE(slotBuf);
            // Release store: signal to producer that this slot is free
            pool_[poolPlayPosition_].buffer_.store(0, std::memory_order_release);
            poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
            if (thread_)
                thread_->Notify();
            onAudioBufferTick();
            MidiService::GetInstance()->Flush() ;
        }
    }
    SYS_MEMCPY(stream, (short *)(mainBuffer_ + bufferPos_), len);
    onAudioBufferTick();
    bufferPos_ += len;
}

int SDLAudioDriver::GetPlayedBufferPercentage() {
    return 0;
};

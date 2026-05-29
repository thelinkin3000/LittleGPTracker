# ARM64 Audio Freeze Fix

## Symptom

On Windows ARM64, the audio playhead freezes permanently and no sound is produced after:

1. Playing a single-step pattern in phrase view
2. Navigating to InstrumentView and changing volume
3. Returning to phrase view, adding notes, pressing SPACE

The playhead appears at position 0 and never moves. The application must be restarted.

---

## Root Cause: ARM64 Weak Memory Model

The audio pipeline uses a lockless single-producer / single-consumer circular pool (`pool_[]`, 50 slots) to transfer audio buffers between two threads:

- **Producer**: `SDLAudioDriverThread` — calls `AudioDriver::AddBuffer()` to write filled audio buffers into pool slots
- **Consumer**: SDL audio callback (`sdl3_callback`) — calls `SDLAudioDriver::OnChunkDone()` to read and play those buffers

The producer signals a slot is ready by writing a non-null pointer to `pool_[poolQueuePosition_].buffer_`. The consumer polls that pointer to detect available data.

**The problem**: `buffer_` was a plain `char*`. On x86 (strong TSO memory model), stores are immediately visible to other cores. On ARM64 (weak memory model), a store by the producer thread can remain in the CPU's store buffer indefinitely — the consumer on a different core may never see the updated value without an explicit memory barrier.

### What the logs showed

```
[AUDIO] thread wake #100, calling OnNewBufferNeeded   <- thread running fine
[PLAYER] Start mode=4 ...                             <- user presses play
[AUDIO] pool underrun #1 at pos=49                    <- callback sees buffer_==null
[AUDIO] pool underrun #2 at pos=49                    <- SAME slot, forever
...
[AUDIO] thread wake #200, calling OnNewBufferNeeded   <- thread IS waking up
[AUDIO] pool underrun #100 at pos=49                  <- but callback never sees the write
```

The thread was running and writing to `pool_[49].buffer_`, but the callback on the ARM64 audio core never saw that write — its cache line remained stale. With no real buffer consumed, the thread was never notified again (`thread_->Notify()` is only called on a successful consume), creating a permanent deadlock:

```
callback → underrun → no Notify() → thread sleeps → pool stays empty → underrun → ...
```

---

## Secondary Bug Fixed (OOB Array Access)

In `Player::updatePhrasePos()`, when `currentPlayPhrase_[channel] == 0xFF` (no phrase assigned), the code accessed:

```cpp
phrase_->cmd1_[0xFF * 16 + pos]  // = cmd1_[4080]
```

`PHRASE_COUNT = 255`, so the array has `255 * 16 = 4080` elements (indices 0–4079). Index 4080 is one past the end — undefined behaviour. Fixed with an early return guard.

---

## The Fix

### `sources/Services/Audio/AudioDriver.h`

Changed `buffer_` in `AudioBufferData` from a plain pointer to an atomic:

```cpp
#include <atomic>

struct AudioBufferData {
    std::atomic<char*> buffer_;  // was: char* buffer_
    int size_;
    void *driverData_;
};
```

### `sources/Services/Audio/AudioDriver.cpp` — `AddBuffer()`

Write all data before publishing the pointer with a **release store**:

```cpp
char *newbuf = (char*)SYS_MALLOC(len);
SYS_MEMCPY(newbuf, (char*)buffer, len);
pool_[poolQueuePosition_].size_ = len;
// Release: ensures size_ and data are visible before buffer_ is published
pool_[poolQueuePosition_].buffer_.store(newbuf, std::memory_order_release);
poolQueuePosition_ = (poolQueuePosition_ + 1) % SOUND_BUFFER_COUNT;
```

### `sources/Adapters/SDL3/Audio/SDLAudioDriver.cpp` — `OnChunkDone()`

Read the pointer with an **acquire load**:

```cpp
// Acquire: ensures we see all writes made before the release store
char *slotBuf = pool_[poolPlayPosition_].buffer_.load(std::memory_order_acquire);
if (slotBuf == 0) {
    // underrun
} else {
    memcpy(..., slotBuf, pool_[poolPlayPosition_].size_);
    SYS_FREE(slotBuf);
    // Release: signal to producer that slot is free
    pool_[poolPlayPosition_].buffer_.store(0, std::memory_order_release);
    ...
}
```

The release store in `AddBuffer()` and the acquire load in `OnChunkDone()` form a **synchronizes-with** relationship: when the consumer sees a non-null pointer, it is guaranteed to also see the correct `size_` and buffer contents written by the producer.

---

## Performance Impact

None meaningful.

| Platform | `store(release)` | `load(acquire)` |
|----------|-----------------|----------------|
| ARM64    | `stlr` (one-way barrier, no pipeline drain) | `ldar` (one-way barrier) |
| x86      | plain `mov` (TSO gives acquire/release free) | plain `mov` |

`OnChunkDone` is called ~100 times/second. Each call does substantial work (memmove, memcpy, DSP mixing). The two atomic operations per call are negligible.

The init/teardown paths (`AudioDriver::Init()`, `AudioDriver::Start()`) use implicit `seq_cst` semantics via the `std::atomic` assignment operator, but these run once on the main thread and are not on the audio hot path.

---

## Diagnostics Added (can be removed)

- `SDLAudioDriverThread::Execute()`: logs thread wake count (first 5, then every 100)
- `SDLAudioDriver::OnChunkDone()`: logs pool underruns (first 10, then every 100)
- `MixerService::Update()`: logs first BUFFERNEEDED event and count every 500
- `Player::Start()`: logs mode, songX/Y, chainRow, playPos
- `Player::updateChainPos()`: logs when channel stops due to missing chain/phrase
- `Player::updatePhrasePos()`: early return guard for `phrase == 0xFF`

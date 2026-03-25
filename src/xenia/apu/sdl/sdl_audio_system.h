/**

-----

- Xenia : Xbox 360 Emulator Research Project                                 *

-----

- Copyright 2020 Ben Vanik. All rights reserved.                             *
- Released under the BSD license - see LICENSE in the root for more details. *

-----

*/

#ifndef XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_
#define XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_

#include “xenia/apu/audio_system.h”

#if XE_PLATFORM_IOS
#include <atomic>
#include <mutex>
#include <queue>
#include <stack>
#include “SDL.h”
#include “xenia/base/threading.h”
#endif  // XE_PLATFORM_IOS

namespace xe {
namespace apu {
namespace sdl {

class SDLAudioSystem : public AudioSystem {
public:
explicit SDLAudioSystem(cpu::Processor* processor);
~SDLAudioSystem() override;

static bool IsAvailable() { return true; }

static std::unique_ptr<AudioSystem> Create(cpu::Processor* processor);

std::string name() const override { return “SDL”; }

X_RESULT CreateDriver(size_t index, xe::threading::Semaphore* semaphore,
AudioDriver** out_driver) override;
AudioDriver* CreateDriver(xe::threading::Semaphore* semaphore,
uint32_t frequency, uint32_t channels,
bool need_format_conversion) override;
void DestroyDriver(AudioDriver* driver) override;

protected:
void Initialize() override;

#if XE_PLATFORM_IOS
// ——————————————————————
// iOS software mixer
//
// iOS/CoreAudio only allows one SDL audio device to be open at a time.
// Rather than silently dropping all secondary guest audio clients (the
// old behaviour), we open a single SDL device and mix all client frames
// together in software before handing them to SDL.
//
// All guest clients submit 6-channel sequential big-endian PCM frames
// (the native Xbox 360 format). The mix callback converts each frame to
// the device’s interleaved little-endian layout, accumulates them into a
// float accumulation buffer, clamps to [-1, 1], and writes to the stream.
// ——————————————————————
public:
// One slot per possible guest audio client.
static constexpr size_t kMixSlotCount = kMaximumClientCount;

struct MixSlot {
// Frames waiting to be consumed by the mix callback.
std::queue<float*> frames_queued;
// Recycled frame buffers (avoids per-frame heap allocation).
std::stack<float*> frames_unused;
std::mutex mutex;
// Set true while the slot has a live SDLMixingAudioDriver.
std::atomic<bool> active{false};
// Set true while the guest client is paused.
std::atomic<bool> paused{false};
// Semaphore released after each frame is consumed, signalling the guest
// audio worker that it may submit the next frame.
xe::threading::Semaphore* semaphore = nullptr;
};

// Called by SDLMixingAudioDriver to push a decoded guest frame.
void MixSlotSubmit(size_t slot_index, float* frame);
// Called by SDLMixingAudioDriver on Pause / Resume.
void MixSlotSetPaused(size_t slot_index, bool paused);
// Called by SDLMixingAudioDriver on Shutdown — drains and releases buffers.
void MixSlotShutdown(size_t slot_index);

private:
// Opens the single SDL audio device used by all mix slots.
// Called once, on the first CreateDriver(index=0) invocation.
bool InitializeMixDevice();
void ShutdownMixDevice();

// SDL audio callback — mixes all active slots and writes to |stream|.
static void MixCallback(void* userdata, Uint8* stream, int len);

MixSlot mix_slots_[kMixSlotCount];
SDL_AudioDeviceID mix_device_id_ = -1;
bool mix_sdl_initialized_ = false;
uint8_t mix_device_channels_ = 0;
// Samples per channel per SDL callback invocation (matches the value
// negotiated with SDL_OpenAudioDevice).
uint32_t mix_channel_samples_ = 0;
#endif  // XE_PLATFORM_IOS
};

}  // namespace sdl
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_
#define XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_

#include "xenia/apu/audio_system.h"

#if XE_PLATFORM_IOS
#include <atomic>
#include <mutex>
#include <queue>
#include <stack>
#include "xenia/base/threading.h"
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

  std::string name() const override { return "SDL"; }

  X_RESULT CreateDriver(size_t index, xe::threading::Semaphore* semaphore,
                        AudioDriver** out_driver) override;
  AudioDriver* CreateDriver(xe::threading::Semaphore* semaphore,
                            uint32_t frequency, uint32_t channels,
                            bool need_format_conversion) override;
  void DestroyDriver(AudioDriver* driver) override;

 protected:
  void Initialize() override;

#if XE_PLATFORM_IOS
 public:
  // Guest audio clients occupy slots 0 .. kMaximumClientCount-1.
  // The media player gets its own dedicated slot so it never conflicts.
  static constexpr size_t kMediaPlayerMixSlot = kMaximumClientCount;
  static constexpr size_t kMixSlotCount = kMaximumClientCount + 1;

  struct MixSlot {
    std::queue<float*> frames_queued;
    std::stack<float*> frames_unused;
    std::mutex mutex;
    std::atomic<bool> active{false};
    std::atomic<bool> paused{false};
    xe::threading::Semaphore* semaphore = nullptr;
    // True for XMA guest clients (6ch sequential BE -> interleaved LE needed).
    // False for the media player slot (FFmpeg output is already interleaved LE).
    bool needs_format_conversion = true;
    // Actual channel count for this slot. Guest XMA clients always use
    // kFrameChannelsDefault (6). The media player uses the channel count
    // reported by FFmpeg (commonly 2 for stereo music).
    uint32_t actual_channels = 6;  // kFrameChannelsDefault
  };

  void MixSlotSubmit(size_t slot_index, float* frame);
  void MixSlotSetPaused(size_t slot_index, bool paused);
  void MixSlotShutdown(size_t slot_index);

  MixSlot mix_slots_[kMixSlotCount];
  uint32_t mix_device_id_ = static_cast<uint32_t>(-1);
  bool mix_sdl_initialized_ = false;
  uint8_t mix_device_channels_ = 0;
  uint32_t mix_channel_samples_ = 0;

 private:
  bool SDLAudioSystem::InitializeMixDevice() {
    if (!xe::helper::sdl::SDLHelper::Prepare()) {
      return false;
    }

    static bool sdl_audio_ever_initialized = false;
    if (!sdl_audio_ever_initialized) {
      if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        XELOGE("SDLAudioSystem (iOS mix): SDL_InitSubSystem(AUDIO) failed: {}",
               SDL_GetError());
        return false;
      }
      sdl_audio_ever_initialized = true;
    }
    mix_sdl_initialized_ = true;
    // ... rest of the function unchanged
  void SDLAudioSystem::ShutdownMixDevice() {
    if (mix_device_id_ != static_cast<uint32_t>(-1)) {
      SDL_CloseAudioDevice(mix_device_id_);
      mix_device_id_ = static_cast<uint32_t>(-1);
    }
    // Do not call SDL_QuitSubSystem(SDL_INIT_AUDIO) here.
    // On iOS, the CoreAudio backend cannot survive a subsystem
    // quit/reinit within the same process lifetime.
    mix_sdl_initialized_ = false;
    for (size_t i = 0; i < kMixSlotCount; ++i) {
      MixSlotShutdown(i);
    }
  }

  static void MixCallback(void* userdata, uint8_t* stream, int len);
#endif  // XE_PLATFORM_IOS
};

}  // namespace sdl
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_

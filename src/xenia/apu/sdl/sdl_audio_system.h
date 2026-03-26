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
#include "SDL.h"
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
  static constexpr size_t kMixSlotCount = kMaximumClientCount;

  struct MixSlot {
    std::queue<float*> frames_queued;
    std::stack<float*> frames_unused;
    std::mutex mutex;
    std::atomic<bool> active{false};
    std::atomic<bool> paused{false};
    xe::threading::Semaphore* semaphore = nullptr;
  };

  void MixSlotSubmit(size_t slot_index, float* frame);
  void MixSlotSetPaused(size_t slot_index, bool paused);
  void MixSlotShutdown(size_t slot_index);

 private:
  bool InitializeMixDevice();
  void ShutdownMixDevice();

  static void MixCallback(void* userdata, Uint8* stream, int len);

  MixSlot mix_slots_[kMixSlotCount];
  SDL_AudioDeviceID mix_device_id_ = -1;
  bool mix_sdl_initialized_ = false;
  uint8_t mix_device_channels_ = 0;
  uint32_t mix_channel_samples_ = 0;
#endif  // XE_PLATFORM_IOS
};

}  // namespace sdl
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_SDL_SDL_AUDIO_SYSTEM_H_

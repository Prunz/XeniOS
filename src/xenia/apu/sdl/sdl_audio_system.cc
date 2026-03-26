/**

-----

- Xenia : Xbox 360 Emulator Research Project                                 *

-----

- Copyright 2020 Ben Vanik. All rights reserved.                             *
- Released under the BSD license - see LICENSE in the root for more details. *

-----

*/

#include “xenia/apu/sdl/sdl_audio_system.h”

#include <algorithm>
#include <atomic>
#include <cstring>

#include “xenia/apu/apu_flags.h”
#include “xenia/apu/conversion.h”
#include “xenia/apu/sdl/sdl_audio_driver.h”
#include “xenia/base/logging.h”
#include “xenia/base/threading.h”
#include “xenia/helper/sdl/sdl_helper.h”

namespace xe {
namespace apu {
namespace sdl {

#if XE_PLATFORM_IOS
namespace {

class SDLMixingAudioDriver final : public AudioDriver {
public:
SDLMixingAudioDriver(SDLAudioSystem* system, size_t slot_index,
xe::threading::Semaphore* semaphore)
: system_(system), slot_index_(slot_index) {
auto& slot = system_->mix_slots_[slot_index_];
slot.semaphore = semaphore;
slot.active.store(true, std::memory_order_release);
}

bool Initialize() override { return true; }

void Shutdown() override { system_->MixSlotShutdown(slot_index_); }

void SubmitFrame(float* frame) override {
system_->MixSlotSubmit(slot_index_, frame);
}

void Pause() override { system_->MixSlotSetPaused(slot_index_, true); }

void Resume() override { system_->MixSlotSetPaused(slot_index_, false); }

void SetVolume(float) override {}

private:
SDLAudioSystem* system_;
size_t slot_index_;
};

}  // namespace
#endif  // XE_PLATFORM_IOS

std::unique_ptr<AudioSystem> SDLAudioSystem::Create(cpu::Processor* processor) {
return std::make_unique<SDLAudioSystem>(processor);
}

SDLAudioSystem::SDLAudioSystem(cpu::Processor* processor)
: AudioSystem(processor) {}

SDLAudioSystem::~SDLAudioSystem() {
#if XE_PLATFORM_IOS
ShutdownMixDevice();
#endif
}

void SDLAudioSystem::Initialize() { AudioSystem::Initialize(); }

X_STATUS SDLAudioSystem::CreateDriver(size_t index,
xe::threading::Semaphore* semaphore,
AudioDriver** out_driver) {
assert_not_null(out_driver);

#if XE_PLATFORM_IOS
if (index >= kMixSlotCount) {
XELOGE(
“SDLAudioSystem: requested mix slot {} exceeds maximum ({}); “
“dropping client”,
index, kMixSlotCount);
return X_STATUS_UNSUCCESSFUL;
}

if (index == 0) {
if (!InitializeMixDevice()) {
XELOGW(
“SDLAudioSystem: SDL mix device init failed; “
“audio will be silent”);
}
}

// Mark the media player slot so the mix callback skips BE->LE conversion.
// FFmpeg output is already interleaved little-endian float PCM.
// actual_channels is set later by the media player via SetMixSlotChannels.
if (index == kMediaPlayerMixSlot) {
mix_slots_[index].needs_format_conversion = false;
}
*out_driver = new SDLMixingAudioDriver(this, index, semaphore);
return X_STATUS_SUCCESS;
#else
auto driver = std::make_unique<SDLAudioDriver>(semaphore);
if (!driver->Initialize()) {
driver->Shutdown();
return X_STATUS_UNSUCCESSFUL;
}
*out_driver = driver.release();
return X_STATUS_SUCCESS;
#endif
}

AudioDriver* SDLAudioSystem::CreateDriver(xe::threading::Semaphore* semaphore,
uint32_t frequency, uint32_t channels,
bool need_format_conversion) {
return new SDLAudioDriver(semaphore, frequency, channels,
need_format_conversion);
}

void SDLAudioSystem::DestroyDriver(AudioDriver* driver) {
assert_not_null(driver);
#if XE_PLATFORM_IOS
if (auto* mix_driver = dynamic_cast<SDLMixingAudioDriver*>(driver)) {
mix_driver->Shutdown();
delete mix_driver;
return;
}
#endif
auto* sdl_driver = dynamic_cast<SDLAudioDriver*>(driver);
assert_not_null(sdl_driver);
sdl_driver->Shutdown();
delete sdl_driver;
}

#if XE_PLATFORM_IOS

bool SDLAudioSystem::InitializeMixDevice() {
if (!xe::helper::sdl::SDLHelper::Prepare()) {
return false;
}
if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
XELOGE(“SDLAudioSystem (iOS mix): SDL_InitSubSystem(AUDIO) failed: {}”,
SDL_GetError());
return false;
}
mix_sdl_initialized_ = true;

SDL_AudioSpec desired = {};
SDL_AudioSpec obtained = {};
desired.freq     = AudioDriver::kFrameFrequencyDefault;
desired.format   = AUDIO_F32;
desired.channels = AudioDriver::kFrameChannelsDefault;
desired.samples  = AudioDriver::kChannelSamplesDefault;
desired.callback = MixCallback;
desired.userdata = this;

int allowed_change = SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
for (int attempt = 0; attempt < 2; ++attempt) {
mix_device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired,
&obtained, allowed_change);
if (mix_device_id_ == static_cast<uint32_t>(-1) || mix_device_id_ == 0) {
XELOGE(
“SDLAudioSystem (iOS mix): SDL_OpenAudioDevice failed: {} “
“(freq={}, channels={}, samples={})”,
SDL_GetError(), desired.freq, desired.channels, desired.samples);
return false;
}
if (obtained.channels == 2 || obtained.channels == 6) {
break;
}
allowed_change = 0;
SDL_CloseAudioDevice(mix_device_id_);
mix_device_id_ = static_cast<uint32_t>(-1);
}

if (mix_device_id_ == static_cast<uint32_t>(-1) || mix_device_id_ == 0) {
XELOGE(“SDLAudioSystem (iOS mix): no compatible audio device available”);
return false;
}

mix_device_channels_ = obtained.channels;
mix_channel_samples_ = obtained.samples;

XELOGI(
“SDLAudioSystem (iOS mix): opened device id={} channels={} samples={}”,
mix_device_id_, mix_device_channels_, mix_channel_samples_);

SDL_PauseAudioDevice(mix_device_id_, 0);
return true;
}

void SDLAudioSystem::ShutdownMixDevice() {
if (mix_device_id_ != static_cast<uint32_t>(-1)) {
SDL_CloseAudioDevice(mix_device_id_);
mix_device_id_ = static_cast<uint32_t>(-1);
}
if (mix_sdl_initialized_) {
SDL_QuitSubSystem(SDL_INIT_AUDIO);
mix_sdl_initialized_ = false;
}
for (size_t i = 0; i < kMixSlotCount; ++i) {
MixSlotShutdown(i);
}
}

void SDLAudioSystem::MixSlotSubmit(size_t slot_index, float* frame) {
assert_true(slot_index < kMixSlotCount);
auto& slot = mix_slots_[slot_index];

// Use the slot’s actual channel count. Guest XMA clients use 6ch;
// the media player uses the FFmpeg-reported channel count (often 2).
const size_t frame_samples =
slot.actual_channels * mix_channel_samples_;
float* buf;
{
std::unique_lock<std::mutex> guard(slot.mutex);
if (slot.frames_unused.empty()) {
buf = new float[frame_samples];
} else {
buf = slot.frames_unused.top();
slot.frames_unused.pop();
}
}

std::memcpy(buf, frame, frame_samples * sizeof(float));

std::unique_lock<std::mutex> guard(slot.mutex);
slot.frames_queued.push(buf);
}

void SDLAudioSystem::MixSlotSetPaused(size_t slot_index, bool paused) {
assert_true(slot_index < kMixSlotCount);
mix_slots_[slot_index].paused.store(paused, std::memory_order_release);
}

void SDLAudioSystem::MixSlotShutdown(size_t slot_index) {
assert_true(slot_index < kMixSlotCount);
auto& slot = mix_slots_[slot_index];

slot.active.store(false, std::memory_order_release);
slot.semaphore = nullptr;

std::unique_lock<std::mutex> guard(slot.mutex);
while (!slot.frames_queued.empty()) {
delete[] slot.frames_queued.front();
slot.frames_queued.pop();
}
while (!slot.frames_unused.empty()) {
delete[] slot.frames_unused.top();
slot.frames_unused.pop();
}
}

void SDLAudioSystem::MixCallback(void* userdata, uint8_t* stream, int len) {
auto* system = static_cast<SDLAudioSystem*>(userdata);

if (!stream || len <= 0) {
return;
}

const size_t out_samples = static_cast<size_t>(len) / sizeof(float);
float* out = reinterpret_cast<float*>(stream);

std::memset(out, 0, static_cast<size_t>(len));

float tmp[AudioDriver::kFrameSamplesMax];

bool any_frame_mixed = false;

for (size_t i = 0; i < SDLAudioSystem::kMixSlotCount; ++i) {
auto& slot = system->mix_slots_[i];

```
if (!slot.active.load(std::memory_order_acquire)) continue;
if (slot.paused.load(std::memory_order_acquire)) continue;

float* frame = nullptr;
{
  std::unique_lock<std::mutex> guard(slot.mutex);
  if (!slot.frames_queued.empty()) {
    frame = slot.frames_queued.front();
    slot.frames_queued.pop();
  }
}

if (!frame) {
  continue;
}

const uint32_t ch_samples = system->mix_channel_samples_;
if (slot.needs_format_conversion) {
  // XMA guest audio: 6-channel sequential big-endian -> interleaved LE.
  if (system->mix_device_channels_ == 2) {
    conversion::sequential_6_BE_to_interleaved_2_LE(tmp, frame, ch_samples);
  } else {
    conversion::sequential_6_BE_to_interleaved_6_LE(tmp, frame, ch_samples);
  }
} else {
  // Media player audio (FFmpeg): already interleaved LE.
  // The frame contains actual_channels * ch_samples floats.
  // Copy only what fits into the output, distributing into available
  // output channels. For stereo media player into a 6ch device, FL and
  // FR map to out[0] and out[1] per sample; remaining channels stay zero.
  const uint32_t src_ch = slot.actual_channels;
  const uint32_t dst_ch = system->mix_device_channels_;
  const uint32_t copy_ch = (src_ch < dst_ch) ? src_ch : dst_ch;
  std::memset(tmp, 0, out_samples * sizeof(float));
  for (uint32_t s = 0; s < ch_samples; ++s) {
    for (uint32_t c = 0; c < copy_ch; ++c) {
      tmp[s * dst_ch + c] = frame[s * src_ch + c];
    }
  }
}

for (size_t s = 0; s < out_samples; ++s) {
  out[s] += tmp[s];
}

any_frame_mixed = true;

{
  std::unique_lock<std::mutex> guard(slot.mutex);
  slot.frames_unused.push(frame);
}

if (slot.semaphore) {
  slot.semaphore->Release(1, nullptr);
}
```

}

if (any_frame_mixed) {
for (size_t s = 0; s < out_samples; ++s) {
out[s] = std::clamp(out[s], -1.0f, 1.0f);
}
}
}

#endif  // XE_PLATFORM_IOS

}  // namespace sdl
}  // namespace apu
}  // namespace xe
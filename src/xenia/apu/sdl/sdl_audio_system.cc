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

// SDLMixingAudioDriver
// ====================
// Represents one guest audio client in the iOS software mixer. Instead of
// opening its own SDL device (which iOS forbids for secondary clients), it
// pushes decoded frames into a slot of the parent SDLAudioSystem’s mix table.
// The single shared SDL device then pulls from all active slots and sums them.
class SDLMixingAudioDriver final : public AudioDriver {
public:
SDLMixingAudioDriver(SDLAudioSystem* system, size_t slot_index,
xe::threading::Semaphore* semaphore)
: system_(system), slot_index_(slot_index) {
// Register this driver with the mix system.
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

void SetVolume(float /*volume*/) override {
// Per-slot volume is not implemented; global volume is handled by the
// host OS / SDL layer. Add per-slot scaling here if needed.
}

private:
SDLAudioSystem* system_;
size_t slot_index_;
};

}  // namespace
#endif  // XE_PLATFORM_IOS

// —————————————————————————
// SDLAudioSystem
// —————————————————————————

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
// –– iOS path: software mixing ––––––––––––––––––––
//
// All guest clients share a single SDL audio device. Client 0 triggers
// device initialisation; subsequent clients reuse the open device.
// Every client gets an SDLMixingAudioDriver that writes into its own slot
// of the mix table. The MixCallback sums all active slots on each tick.
if (index >= kMixSlotCount) {
XELOGE(
“SDLAudioSystem: requested mix slot {} exceeds maximum ({}); “
“dropping client”,
index, kMixSlotCount);
return X_STATUS_UNSUCCESSFUL;
}

if (index == 0) {
// First client — open the SDL device now.
if (!InitializeMixDevice()) {
// Device open failed. The mix callback won’t fire, so all submitted
// frames will simply be drained by MixSlotShutdown. Still allow the
// guest to run (silently) rather than hard-failing.
XELOGW(
“SDLAudioSystem: SDL mix device init failed; “
“audio will be silent”);
}
}

*out_driver = new SDLMixingAudioDriver(this, index, semaphore);
return X_STATUS_SUCCESS;
// ———————————————————————–
#else
// –– Non-iOS path: one SDL device per client –––––––––––––
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

// —————————————————————————
// iOS software mixer implementation
// —————————————————————————

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

// Request the standard Xbox 360 audio format: 48 kHz, 6-channel surround,
// 256 samples per channel. SDL_AUDIO_ALLOW_CHANNELS_CHANGE lets CoreAudio
// fall back to stereo on devices that don’t support 5.1.
SDL_AudioSpec desired = {};
SDL_AudioSpec obtained = {};
desired.freq     = AudioDriver::kFrameFrequencyDefault;  // 48000
desired.format   = AUDIO_F32;
desired.channels = AudioDriver::kFrameChannelsDefault;   // 6
desired.samples  = AudioDriver::kChannelSamplesDefault;  // 256
desired.callback = MixCallback;
desired.userdata = this;

int allowed_change = SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
for (int attempt = 0; attempt < 2; ++attempt) {
mix_device_id_ = SDL_OpenAudioDevice(nullptr, /*iscapture=*/0, &desired,
&obtained, allowed_change);
if (mix_device_id_ <= 0) {
XELOGE(
“SDLAudioSystem (iOS mix): SDL_OpenAudioDevice failed: {} “
“(freq={}, channels={}, samples={})”,
SDL_GetError(), desired.freq, desired.channels, desired.samples);
return false;
}
if (obtained.channels == 2 || obtained.channels == 6) {
break;  // Acceptable channel count.
}
// 4- or 7.1-channel device — ask SDL to do the channel conversion for us.
allowed_change = 0;
SDL_CloseAudioDevice(mix_device_id_);
mix_device_id_ = -1;
}

if (mix_device_id_ <= 0) {
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
if (mix_device_id_ > 0) {
SDL_CloseAudioDevice(mix_device_id_);
mix_device_id_ = -1;
}
if (mix_sdl_initialized_) {
SDL_QuitSubSystem(SDL_INIT_AUDIO);
mix_sdl_initialized_ = false;
}
// Drain all slot buffers.
for (size_t i = 0; i < kMixSlotCount; ++i) {
MixSlotShutdown(i);
}
}

// Called by SDLMixingAudioDriver::SubmitFrame on the guest audio worker thread.
void SDLAudioSystem::MixSlotSubmit(size_t slot_index, float* frame) {
assert_true(slot_index < kMixSlotCount);
auto& slot = mix_slots_[slot_index];

// Allocate or reuse a frame buffer.
const size_t frame_samples =
AudioDriver::kFrameChannelsDefault * mix_channel_samples_;
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

// Called by SDLMixingAudioDriver::Shutdown — drains pending frames and marks
// the slot inactive so the mix callback skips it.
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

// SDL audio callback — runs on the SDL audio thread.
//
// For each active, unpaused slot:
//   1. Pop the oldest queued frame (6ch sequential BE).
//   2. Convert to the device’s interleaved LE layout into a temp buffer.
//   3. Accumulate (add) into the float mix buffer.
//   4. Release the semaphore so the guest worker submits the next frame.
//
// After all slots are processed, clamp the mix buffer to [-1, 1] and copy
// to the SDL stream. If no slots had a frame ready, zero the stream to
// avoid stale audio.
void SDLAudioSystem::MixCallback(void* userdata, Uint8* stream, int len) {
auto* system = static_cast<SDLAudioSystem*>(userdata);

if (!stream || len <= 0) {
return;
}

const size_t out_samples = static_cast<size_t>(len) / sizeof(float);
float* out = reinterpret_cast<float*>(stream);

// Zero the accumulation buffer (stack-allocated; max is 6*256 floats = 6KB).
// Using the stream itself as the accumulator avoids a separate allocation.
std::memset(out, 0, static_cast<size_t>(len));

// Temporary buffer for one converted frame — large enough for 6ch * 256.
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
  // Underrun for this slot — skip silently. The stream already has zeros
  // for this slot's contribution.
  continue;
}

// Convert 6ch sequential BE → device interleaved LE.
const uint32_t ch_samples = system->mix_channel_samples_;
if (system->mix_device_channels_ == 2) {
  conversion::sequential_6_BE_to_interleaved_2_LE(tmp, frame, ch_samples);
} else {
  // 6-channel (or SDL-converted other layouts).
  conversion::sequential_6_BE_to_interleaved_6_LE(tmp, frame, ch_samples);
}

// Accumulate into output buffer.
for (size_t s = 0; s < out_samples; ++s) {
  out[s] += tmp[s];
}

any_frame_mixed = true;

// Return the frame buffer to the free list.
{
  std::unique_lock<std::mutex> guard(slot.mutex);
  slot.frames_unused.push(frame);
}

// Signal the guest audio worker that it may submit the next frame.
if (slot.semaphore) {
  slot.semaphore->Release(1, nullptr);
}
```

}

if (any_frame_mixed) {
// Clamp the summed signal to [-1.0, 1.0] to prevent clipping distortion
// when multiple loud streams are active simultaneously.
for (size_t s = 0; s < out_samples; ++s) {
out[s] = std::clamp(out[s], -1.0f, 1.0f);
}
}
// If no frame was mixed the stream is already zeroed — no further action.
}

#endif  // XE_PLATFORM_IOS

}  // namespace sdl
}  // namespace apu
}  // namespace xe
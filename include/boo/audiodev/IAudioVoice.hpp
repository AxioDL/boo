#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "boo/BooObject.hpp"

namespace boo {
struct IAudioSubmix;

enum class AudioChannelSet { Stereo, Quad, Surround51, Surround71, Unknown = 0xff };

enum class AudioChannel {
  FrontLeft,
  FrontRight,
  RearLeft,
  RearRight,
  FrontCenter,
  LFE,
  SideLeft,
  SideRight,
  Unknown = 0xff
};

struct ChannelMap {
  unsigned m_channelCount = 0;
  AudioChannel m_channels[8] = {};
};

static inline unsigned ChannelCount(AudioChannelSet layout) {
  switch (layout) {
  case AudioChannelSet::Stereo:
    return 2;
  case AudioChannelSet::Quad:
    return 4;
  case AudioChannelSet::Surround51:
    return 6;
  case AudioChannelSet::Surround71:
    return 8;
  default:
    break;
  }
  return 0;
}

struct IAudioVoice : IObj {
  /** Set sample rate into voice (may result in audio discontinuities) */
  virtual void resetSampleRate(double sampleRate) = 0;

  /** Reset channel-levels to silence; unbind all submixes */
  virtual void resetChannelLevels() = 0;

  /** Set channel-levels for mono audio source (AudioChannel enum for array index) */
  virtual void setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew) = 0;

  /** Set channel-levels for stereo audio source (AudioChannel enum for array index) */
  virtual void setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew) = 0;

  /** Called by client to dynamically adjust the pitch of voices with dynamic pitch enabled */
  virtual void setPitchRatio(double ratio, bool slew) = 0;

  /** Instructs platform to begin consuming sample data; invoking callback as needed */
  virtual void start() = 0;

  /** Instructs platform to stop consuming sample data */
  virtual void stop() = 0;
};

struct IAudioVoiceCallback {
  /** boo calls this on behalf of the audio platform to proactively invoke potential
   *  pitch or panning changes before processing samples */
  virtual void preSupplyAudio(boo::IAudioVoice& voice, double dt) = 0;

  /** boo calls this on behalf of the audio platform to request more audio
   *  frames from the client */
  virtual size_t supplyAudio(IAudioVoice& voice, size_t frames, int16_t* data) = 0;

  /** after resampling, boo calls this for each submix that this voice targets;
   *  client performs volume processing and bus-routing this way */
  virtual void routeAudio(size_t frames, size_t channels, double dt, int busId, int16_t* in, int16_t* out) {
    memmove(out, in, frames * channels * 2);
  }

  virtual void routeAudio(size_t frames, size_t channels, double dt, int busId, int32_t* in, int32_t* out) {
    memmove(out, in, frames * channels * 4);
  }

  virtual void routeAudio(size_t frames, size_t channels, double dt, int busId, float* in, float* out) {
    memmove(out, in, frames * channels * 4);
  }
};

} // namespace boo

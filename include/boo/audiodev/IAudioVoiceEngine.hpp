#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "boo/BooObject.hpp"
#include "boo/audiodev/IAudioSubmix.hpp"
#include "boo/audiodev/IAudioVoice.hpp"
#include "boo/audiodev/IMIDIPort.hpp"

namespace boo {
struct IAudioVoiceEngine;

/** Time-sensitive event callback for synchronizing the client with rendered audio waveform */
struct IAudioVoiceEngineCallback {
  /** All mixing occurs in virtual 5ms intervals;
   *  this is called at the start of each interval for all mixable entities */
  virtual void on5MsInterval(IAudioVoiceEngine& engine, double dt) {}

  /** When a pumping cycle is complete this is called to allow the client to
   *  perform periodic cleanup tasks */
  virtual void onPumpCycleComplete(IAudioVoiceEngine& engine) {}
};

/** Mixing and sample-rate-conversion system. Allocates voices and mixes them
 *  before sending the final samples to an OS-supplied audio-queue */
struct IAudioVoiceEngine {
  virtual ~IAudioVoiceEngine() = default;

  /** Client calls this to request allocation of new mixer-voice.
   *  Returns empty unique_ptr if necessary resources aren't available.
   *  ChannelLayout automatically reduces to maximum-supported layout by HW.
   *
   *  Client must be prepared to supply audio frames via the callback when this is called;
   *  the backing audio-buffers are primed with initial data for low-latency playback start
   */
  virtual ObjToken<IAudioVoice> allocateNewMonoVoice(double sampleRate, IAudioVoiceCallback* cb,
                                                     bool dynamicPitch = false) = 0;

  /** Same as allocateNewMonoVoice, but source audio is stereo-interleaved */
  virtual ObjToken<IAudioVoice> allocateNewStereoVoice(double sampleRate, IAudioVoiceCallback* cb,
                                                       bool dynamicPitch = false) = 0;

  /** Client calls this to allocate a Submix for gathering audio together for effects processing */
  virtual ObjToken<IAudioSubmix> allocateNewSubmix(bool mainOut, IAudioSubmixCallback* cb, int busId) = 0;

  /** Client can register for key callback events from the mixing engine this way */
  virtual void setCallbackInterface(IAudioVoiceEngineCallback* cb) = 0;

  /** Client may use this to determine current speaker-setup */
  virtual AudioChannelSet getAvailableSet() = 0;

  /** Ensure backing platform buffer is filled as much as possible with mixed samples */
  virtual void pumpAndMixVoices() = 0;

  /** Set total volume of engine */
  virtual void setVolume(float vol) = 0;

  /** Enable or disable Lt/Rt surround encoding. If successful, getAvailableSet() will return Surround51 */
  virtual bool enableLtRt(bool enable) = 0;

  /** Get current Audio output in use */
  virtual std::string getCurrentAudioOutput() const = 0;

  /** Set current Audio output to use */
  virtual bool setCurrentAudioOutput(const char* name) = 0;

  /** Get list of Audio output devices found on system */
  virtual std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const = 0;

  /** Get list of MIDI input devices found on system */
  virtual std::vector<std::pair<std::string, std::string>> enumerateMIDIInputs() const = 0;

  /** Query if system supports creating a virtual MIDI input */
  virtual bool supportsVirtualMIDIIn() const = 0;

  /** Create ad-hoc MIDI in port and register with system */
  virtual std::unique_ptr<IMIDIIn> newVirtualMIDIIn(ReceiveFunctor&& receiver) = 0;

  /** Create ad-hoc MIDI out port and register with system */
  virtual std::unique_ptr<IMIDIOut> newVirtualMIDIOut() = 0;

  /** Create ad-hoc MIDI in/out port and register with system */
  virtual std::unique_ptr<IMIDIInOut> newVirtualMIDIInOut(ReceiveFunctor&& receiver) = 0;

  /** Open named MIDI in port, name format depends on OS */
  virtual std::unique_ptr<IMIDIIn> newRealMIDIIn(const char* name, ReceiveFunctor&& receiver) = 0;

  /** Open named MIDI out port, name format depends on OS */
  virtual std::unique_ptr<IMIDIOut> newRealMIDIOut(const char* name) = 0;

  /** Open named MIDI in/out port, name format depends on OS */
  virtual std::unique_ptr<IMIDIInOut> newRealMIDIInOut(const char* name, ReceiveFunctor&& receiver) = 0;

  /** If this returns true, MIDI callbacks are assumed to be *not* thread-safe; need protection via mutex */
  virtual bool useMIDILock() const = 0;

  /** Get canonical count of frames for each 5ms output block */
  virtual size_t get5MsFrames() const = 0;
};

/** Construct host platform's voice engine */
std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine();

/** Construct WAV-rendering voice engine */
std::unique_ptr<IAudioVoiceEngine> NewWAVAudioVoiceEngine(const char* path, double sampleRate, int numChans);

} // namespace boo

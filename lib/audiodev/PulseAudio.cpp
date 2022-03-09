#include "lib/audiodev/AudioVoiceEngine.hpp"
#include "lib/audiodev/LinuxMidi.hpp"

#include <logvisor/logvisor.hpp>
#include <pulse/pulseaudio.h>
#include <unistd.h>

namespace boo {
static logvisor::Module Log("boo::PulseAudio");
logvisor::Module ALSALog("boo::ALSA");

static const uint64_t StereoChans = (1 << PA_CHANNEL_POSITION_FRONT_LEFT) | (1 << PA_CHANNEL_POSITION_FRONT_RIGHT);

static const uint64_t QuadChans = (1 << PA_CHANNEL_POSITION_FRONT_LEFT) | (1 << PA_CHANNEL_POSITION_FRONT_RIGHT) |
                                  (1 << PA_CHANNEL_POSITION_REAR_LEFT) | (1 << PA_CHANNEL_POSITION_REAR_RIGHT);

static const uint64_t S51Chans = (1 << PA_CHANNEL_POSITION_FRONT_LEFT) | (1 << PA_CHANNEL_POSITION_FRONT_RIGHT) |
                                 (1 << PA_CHANNEL_POSITION_REAR_LEFT) | (1 << PA_CHANNEL_POSITION_REAR_RIGHT) |
                                 (1 << PA_CHANNEL_POSITION_FRONT_CENTER) | (1 << PA_CHANNEL_POSITION_LFE);

static const uint64_t S71Chans = (1 << PA_CHANNEL_POSITION_FRONT_LEFT) | (1 << PA_CHANNEL_POSITION_FRONT_RIGHT) |
                                 (1 << PA_CHANNEL_POSITION_REAR_LEFT) | (1 << PA_CHANNEL_POSITION_REAR_RIGHT) |
                                 (1 << PA_CHANNEL_POSITION_FRONT_CENTER) | (1 << PA_CHANNEL_POSITION_LFE) |
                                 (1 << PA_CHANNEL_POSITION_SIDE_LEFT) | (1 << PA_CHANNEL_POSITION_SIDE_RIGHT);

struct PulseAudioVoiceEngine : LinuxMidi {
  pa_mainloop* m_mainloop = nullptr;
  pa_context* m_ctx = nullptr;
  pa_stream* m_stream = nullptr;
  std::string m_sinkName;
  bool m_handleMove = false;
  pa_sample_spec m_sampleSpec = {};
  pa_channel_map m_chanMap = {};

  int _paWaitReady() {
    int retval = 0;
    while (pa_context_get_state(m_ctx) < PA_CONTEXT_READY)
      pa_mainloop_iterate(m_mainloop, 1, &retval);
    return retval;
  }

  int _paStreamWaitReady() {
    int retval = 0;
    while (pa_stream_get_state(m_stream) < PA_STREAM_READY)
      pa_mainloop_iterate(m_mainloop, 1, &retval);
    return retval;
  }

  int _paIterate(pa_operation* op) const {
    int retval = 0;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
      pa_mainloop_iterate(m_mainloop, 1, &retval);
    return retval;
  }

  bool _setupSink() {
    if (m_stream) {
      pa_stream_disconnect(m_stream);
      pa_stream_unref(m_stream);
      m_stream = nullptr;
    }

    pa_operation* op;
    m_sampleSpec.format = PA_SAMPLE_INVALID;
    op = pa_context_get_sink_info_by_name(m_ctx, m_sinkName.c_str(), pa_sink_info_cb_t(_getSinkInfoReply), this);
    _paIterate(op);
    pa_operation_unref(op);

    if (m_sampleSpec.format == PA_SAMPLE_INVALID) {
      Log.report(logvisor::Error, FMT_STRING("Unable to setup audio stream"));
      goto err;
    }

    m_5msFrames = m_sampleSpec.rate * 5 / 1000;

    m_mixInfo.m_sampleRate = m_sampleSpec.rate;
    m_mixInfo.m_sampleFormat = SOXR_FLOAT32;
    m_mixInfo.m_bitsPerSample = 32;
    m_mixInfo.m_periodFrames = m_5msFrames;
    if (!(m_stream = pa_stream_new(m_ctx, "master", &m_sampleSpec, &m_chanMap))) {
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_stream_new(): {}"), pa_strerror(pa_context_errno(m_ctx)));
      goto err;
    }

    pa_buffer_attr bufAttr;
    bufAttr.minreq = uint32_t(m_5msFrames * m_sampleSpec.channels * sizeof(float));
    bufAttr.maxlength = bufAttr.minreq * 24;
    bufAttr.tlength = bufAttr.maxlength;
    bufAttr.prebuf = UINT32_MAX;
    bufAttr.fragsize = UINT32_MAX;

    if (pa_stream_connect_playback(m_stream, m_sinkName.c_str(), &bufAttr,
                                   pa_stream_flags_t(PA_STREAM_START_UNMUTED | PA_STREAM_EARLY_REQUESTS), nullptr,
                                   nullptr)) {
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_stream_connect_playback()"));
      goto err;
    }

    pa_stream_set_moved_callback(m_stream, pa_stream_notify_cb_t(_streamMoved), this);

    _paStreamWaitReady();

    _resetSampleRate();
    return true;
  err:
    if (m_stream) {
      pa_stream_disconnect(m_stream);
      pa_stream_unref(m_stream);
      m_stream = nullptr;
    }
    return false;
  }

  PulseAudioVoiceEngine(const char* uniqueName, const char* friendlyName) {
    if (!(m_mainloop = pa_mainloop_new())) {
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_mainloop_new()"));
      return;
    }

    pa_mainloop_api* mlApi = pa_mainloop_get_api(m_mainloop);
    pa_proplist* propList = pa_proplist_new();
    pa_proplist_sets(propList, PA_PROP_APPLICATION_ICON_NAME, uniqueName);
    pa_proplist_sets(propList, PA_PROP_APPLICATION_PROCESS_ID, fmt::format(FMT_STRING("{}"), int(getpid())).c_str());
    if (!(m_ctx = pa_context_new_with_proplist(mlApi, friendlyName, propList))) {
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_context_new_with_proplist()"));
      pa_mainloop_free(m_mainloop);
      m_mainloop = nullptr;
      return;
    }
    pa_proplist_free(propList);

    pa_operation* op;

    if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr)) {
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_context_connect()"));
      goto err;
    }

    _paWaitReady();

    op = pa_context_get_server_info(m_ctx, pa_server_info_cb_t(_getServerInfoReply), this);
    _paIterate(op);
    pa_operation_unref(op);

    if (!_setupSink())
      goto err;

    return;
  err:
    pa_context_disconnect(m_ctx);
    pa_context_unref(m_ctx);
    m_ctx = nullptr;
    pa_mainloop_free(m_mainloop);
    m_mainloop = nullptr;
  }

  ~PulseAudioVoiceEngine() override {
    if (m_stream) {
      pa_stream_disconnect(m_stream);
      pa_stream_unref(m_stream);
    }
    if (m_ctx) {
      pa_context_disconnect(m_ctx);
      pa_context_unref(m_ctx);
    }
    if (m_mainloop) {
      pa_mainloop_free(m_mainloop);
    }
  }

  static void _streamMoved(pa_stream* p, PulseAudioVoiceEngine* userdata) {
    userdata->m_sinkName = pa_stream_get_device_name(p);
    userdata->m_handleMove = true;
  }

  static void _getServerInfoReply(pa_context* c, const pa_server_info* i, PulseAudioVoiceEngine* userdata) {
    userdata->m_sinkName = i->default_sink_name;
  }

  void _parseAudioChannelSet(const pa_channel_map* chm) {
    m_chanMap = *chm;

    ChannelMap& chmapOut = m_mixInfo.m_channelMap;
    m_mixInfo.m_channels = AudioChannelSet::Unknown;

    uint64_t chBits = 0;
    chmapOut.m_channelCount = chm->channels;
    for (unsigned c = 0; c < chm->channels; ++c) {
      chBits |= 1 << chm->map[c];
      switch (chm->map[c]) {
      case PA_CHANNEL_POSITION_FRONT_LEFT:
        chmapOut.m_channels[c] = AudioChannel::FrontLeft;
        break;
      case PA_CHANNEL_POSITION_FRONT_RIGHT:
        chmapOut.m_channels[c] = AudioChannel::FrontRight;
        break;
      case PA_CHANNEL_POSITION_REAR_LEFT:
        chmapOut.m_channels[c] = AudioChannel::RearLeft;
        break;
      case PA_CHANNEL_POSITION_REAR_RIGHT:
        chmapOut.m_channels[c] = AudioChannel::RearRight;
        break;
      case PA_CHANNEL_POSITION_FRONT_CENTER:
        chmapOut.m_channels[c] = AudioChannel::FrontCenter;
        break;
      case PA_CHANNEL_POSITION_LFE:
        chmapOut.m_channels[c] = AudioChannel::LFE;
        break;
      case PA_CHANNEL_POSITION_SIDE_LEFT:
        chmapOut.m_channels[c] = AudioChannel::SideLeft;
        break;
      case PA_CHANNEL_POSITION_SIDE_RIGHT:
        chmapOut.m_channels[c] = AudioChannel::SideRight;
        break;
      default:
        chmapOut.m_channels[c] = AudioChannel::Unknown;
        break;
      }
    }

    static const std::array<AudioChannelSet, 4> testSets = {
        {AudioChannelSet::Surround71, AudioChannelSet::Surround51, AudioChannelSet::Quad, AudioChannelSet::Stereo}};
    for (AudioChannelSet set : testSets) {
      switch (set) {
      case AudioChannelSet::Stereo: {
        if ((chBits & StereoChans) == StereoChans) {
          m_mixInfo.m_channels = AudioChannelSet::Stereo;
          return;
        }
        break;
      }
      case AudioChannelSet::Quad: {
        if ((chBits & QuadChans) == QuadChans) {
          m_mixInfo.m_channels = AudioChannelSet::Quad;
          return;
        }
        break;
      }
      case AudioChannelSet::Surround51: {
        if ((chBits & S51Chans) == S51Chans) {
          m_mixInfo.m_channels = AudioChannelSet::Surround51;
          return;
        }
        break;
      }
      case AudioChannelSet::Surround71: {
        if ((chBits & S71Chans) == S71Chans) {
          m_mixInfo.m_channels = AudioChannelSet::Surround71;
          return;
        }
        break;
      }
      default:
        break;
      }
    }
  }

  static void _getSinkInfoReply(pa_context* c, const pa_sink_info* i, int eol, PulseAudioVoiceEngine* userdata) {
    if (!i)
      return;
    userdata->m_sampleSpec.format = PA_SAMPLE_FLOAT32;
    userdata->m_sampleSpec.rate = i->sample_spec.rate;
    userdata->m_sampleSpec.channels = i->sample_spec.channels;
    userdata->_parseAudioChannelSet(&i->channel_map);
  }

  mutable std::vector<std::pair<std::string, std::string>> m_sinks;
  static void _getSinkInfoListReply(pa_context* c, const pa_sink_info* i, int eol, PulseAudioVoiceEngine* userdata) {
    if (i)
      userdata->m_sinks.push_back(std::make_pair(i->name, i->description));
  }
  std::vector<std::pair<std::string, std::string>> enumerateAudioOutputs() const override {
    pa_operation* op = pa_context_get_sink_info_list(m_ctx, pa_sink_info_cb_t(_getSinkInfoListReply), (void*)this);
    _paIterate(op);
    pa_operation_unref(op);
    std::vector<std::pair<std::string, std::string>> ret;
    ret.swap(m_sinks);
    return ret;
  }

  std::string getCurrentAudioOutput() const override { return m_sinkName; }

  bool m_sinkOk = false;
  static void _checkAudioSinkReply(pa_context* c, const pa_sink_info* i, int eol, PulseAudioVoiceEngine* userdata) {
    if (i)
      userdata->m_sinkOk = true;
  }
  bool setCurrentAudioOutput(const char* name) override {
    m_sinkOk = false;
    pa_operation* op;
    op = pa_context_get_sink_info_by_name(m_ctx, name, pa_sink_info_cb_t(_checkAudioSinkReply), this);
    _paIterate(op);
    pa_operation_unref(op);
    if (m_sinkOk) {
      m_sinkName = name;
      return _setupSink();
    }
    return false;
  }

  void _doIterate() {
    int retval;
    pa_mainloop_iterate(m_mainloop, 1, &retval);
    if (m_handleMove) {
      m_handleMove = false;
      _setupSink();
    }
  }

  void pumpAndMixVoices() override {
    if (!m_stream) {
      /* Dummy pump mode - use failsafe defaults for 1/60sec of samples */
      m_mixInfo.m_sampleRate = 32000.0;
      m_mixInfo.m_sampleFormat = SOXR_FLOAT32_I;
      m_mixInfo.m_bitsPerSample = 32;
      m_5msFrames = 32000 / 60;
      m_mixInfo.m_periodFrames = m_5msFrames;
      m_mixInfo.m_channels = AudioChannelSet::Stereo;
      m_mixInfo.m_channelMap.m_channelCount = 2;
      m_mixInfo.m_channelMap.m_channels[0] = AudioChannel::FrontLeft;
      m_mixInfo.m_channelMap.m_channels[1] = AudioChannel::FrontRight;
      _pumpAndMixVoices(m_5msFrames, (float*)nullptr);
      return;
    }

    size_t writableSz = pa_stream_writable_size(m_stream);
    size_t frameSz = m_mixInfo.m_channelMap.m_channelCount * sizeof(float);
    size_t writableFrames = writableSz / frameSz;
    size_t writablePeriods = writableFrames / m_mixInfo.m_periodFrames;

    if (!writablePeriods) {
      _doIterate();
      return;
    }

    void* data = nullptr;
    size_t periodSz = m_mixInfo.m_periodFrames * frameSz;
    size_t nbytes = writablePeriods * periodSz;
    if (pa_stream_begin_write(m_stream, &data, &nbytes)) {
      pa_stream_state_t st = pa_stream_get_state(m_stream);
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_stream_begin_write(): {} {}"),
                 pa_strerror(pa_context_errno(m_ctx)), st);
      _doIterate();
      return;
    }

    writablePeriods = nbytes / periodSz;
    _pumpAndMixVoices(m_mixInfo.m_periodFrames * writablePeriods, reinterpret_cast<float*>(data));

    if (pa_stream_write(m_stream, data, nbytes, nullptr, 0, PA_SEEK_RELATIVE))
      Log.report(logvisor::Error, FMT_STRING("Unable to pa_stream_write()"));

    _doIterate();
  }
};

std::unique_ptr<IAudioVoiceEngine> NewAudioVoiceEngine(const char* uniqueName, const char* friendlyName) {
  return std::make_unique<PulseAudioVoiceEngine>(uniqueName, friendlyName);
}

} // namespace boo

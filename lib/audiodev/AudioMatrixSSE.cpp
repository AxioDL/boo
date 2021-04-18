#include "lib/audiodev/AudioMatrix.hpp"
#include "lib/audiodev/AudioVoiceEngine.hpp"

namespace boo {

union TVectorUnion {
  float v[4];
#if __SSE__
  alignas(16) __m128 q;
  alignas(16) __m64 d[2];
#endif
};

static constexpr TVectorUnion Min32Vec = {{INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN}};
static constexpr TVectorUnion Max32Vec = {{INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}};

void AudioMatrixMono::setDefaultMatrixCoefficients(AudioChannelSet acSet) {
  m_curSlewFrame = 0;
  m_slewFrames = 0;
  m_coefs.q[0] = _mm_xor_ps(m_coefs.q[0], m_coefs.q[0]);
  m_coefs.q[1] = _mm_xor_ps(m_coefs.q[1], m_coefs.q[1]);
  switch (acSet) {
  case AudioChannelSet::Stereo:
  case AudioChannelSet::Quad:
    m_coefs.v[int(AudioChannel::FrontLeft)] = 1.0;
    m_coefs.v[int(AudioChannel::FrontRight)] = 1.0;
    break;
  case AudioChannelSet::Surround51:
  case AudioChannelSet::Surround71:
    m_coefs.v[int(AudioChannel::FrontCenter)] = 1.0;
    break;
  default:
    break;
  }
}

int16_t* AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info, const int16_t* dataIn,
                                            int16_t* dataOut, size_t samples) {
  const ChannelMap& chmap = info.m_channelMap;
  for (size_t s = 0; s < samples; ++s, ++dataIn) {
    if (m_slewFrames && m_curSlewFrame < m_slewFrames) {
      double t = m_curSlewFrame / double(m_slewFrames);
      double omt = 1.0 - t;

      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = Clamp16(*dataOut + *dataIn * (m_coefs.v[int(ch)] * t + m_oldCoefs.v[int(ch)] * omt));
          ++dataOut;
        }
      }

      ++m_curSlewFrame;
    } else {
      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = Clamp16(*dataOut + *dataIn * m_coefs.v[int(ch)]);
          ++dataOut;
        }
      }
    }
  }
  return dataOut;
}

int32_t* AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info, const int32_t* dataIn,
                                            int32_t* dataOut, size_t samples) {
  const ChannelMap& chmap = info.m_channelMap;
  for (size_t s = 0; s < samples; ++s, ++dataIn) {
    if (m_slewFrames && m_curSlewFrame < m_slewFrames) {
      float t = m_curSlewFrame / float(m_slewFrames);
      float omt = 1.f - t;

      switch (chmap.m_channelCount) {
      case 2: {
        ++m_curSlewFrame;
        float t2 = m_curSlewFrame / float(m_slewFrames);
        float omt2 = 1.f - t2;

        TVectorUnion coefs, samps;
        coefs.q = _mm_add_ps(
            _mm_mul_ps(_mm_shuffle_ps(m_coefs.q[0], m_coefs.q[0], _MM_SHUFFLE(1, 0, 1, 0)), _mm_set_ps(t, t, t2, t2)),
            _mm_mul_ps(_mm_shuffle_ps(m_oldCoefs.q[0], m_oldCoefs.q[0], _MM_SHUFFLE(1, 0, 1, 0)),
                       _mm_set_ps(omt, omt, omt2, omt2)));
        samps.q = _mm_cvtepi32_ps(_mm_set_epi32(dataIn[1], dataIn[0], dataIn[1], dataIn[0]));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;
        ++s;
        ++dataIn;
        break;
      }
      case 4: {
        TVectorUnion coefs, samps;
        coefs.q = _mm_add_ps(_mm_mul_ps(m_coefs.q[0], _mm_set1_ps(t)), _mm_mul_ps(m_oldCoefs.q[0], _mm_set1_ps(omt)));
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;
        break;
      }
      case 6: {
        TVectorUnion coefs, samps;
        coefs.q = _mm_add_ps(_mm_mul_ps(m_coefs.q[0], _mm_set1_ps(t)), _mm_mul_ps(m_oldCoefs.q[0], _mm_set1_ps(omt)));
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;

        coefs.q = _mm_add_ps(_mm_mul_ps(m_coefs.q[1], _mm_set1_ps(t)), _mm_mul_ps(m_oldCoefs.q[1], _mm_set1_ps(omt)));
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        out = reinterpret_cast<__m128i*>(dataOut);
        __m128i loadOut = _mm_loadu_si128(out);
        pre = _mm_add_ps(_mm_cvtepi32_ps(loadOut), _mm_mul_ps(coefs.q, samps.q));
        _mm_storel_epi64(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 2;
        break;
      }
      case 8: {
        TVectorUnion coefs, samps;
        coefs.q = _mm_add_ps(_mm_mul_ps(m_coefs.q[0], _mm_set1_ps(t)), _mm_mul_ps(m_oldCoefs.q[0], _mm_set1_ps(omt)));
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;

        coefs.q = _mm_add_ps(_mm_mul_ps(m_coefs.q[1], _mm_set1_ps(t)), _mm_mul_ps(m_oldCoefs.q[1], _mm_set1_ps(omt)));
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        out = reinterpret_cast<__m128i*>(dataOut);
        pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;
        break;
      }
      default: {
        for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
          AudioChannel ch = chmap.m_channels[c];
          if (ch != AudioChannel::Unknown) {
            *dataOut = Clamp32(*dataOut + *dataIn * (m_coefs.v[int(ch)] * t + m_oldCoefs.v[int(ch)] * omt));
            ++dataOut;
          }
        }
        break;
      }
      }

      ++m_curSlewFrame;
    } else {
      switch (chmap.m_channelCount) {
      case 2: {
        TVectorUnion coefs, samps;
        coefs.q = _mm_shuffle_ps(m_coefs.q[0], m_coefs.q[0], _MM_SHUFFLE(1, 0, 1, 0));
        samps.q = _mm_cvtepi32_ps(_mm_set_epi32(dataIn[1], dataIn[0], dataIn[1], dataIn[0]));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128i huh2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(out));
        __m128 huh3 = _mm_cvtepi32_ps(huh2);
        __m128 pre = _mm_add_ps(huh3, _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;
        ++s;
        ++dataIn;
        break;
      }
      case 4: {
        TVectorUnion samps;
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(m_coefs.q[0], samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;
        break;
      }
      case 6: {
        TVectorUnion samps;
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(m_coefs.q[0], samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;

        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        out = reinterpret_cast<__m128i*>(dataOut);
        __m128i loadOut = _mm_loadu_si128(out);
        pre = _mm_add_ps(_mm_cvtepi32_ps(loadOut), _mm_mul_ps(m_coefs.q[1], samps.q));
        _mm_storel_epi64(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 2;
        break;
      }
      case 8: {
        TVectorUnion samps;
        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        __m128i* out = reinterpret_cast<__m128i*>(dataOut);
        __m128 pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(m_coefs.q[0], samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;

        samps.q = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(dataIn)));

        out = reinterpret_cast<__m128i*>(dataOut);
        pre = _mm_add_ps(_mm_cvtepi32_ps(_mm_loadu_si128(out)), _mm_mul_ps(m_coefs.q[1], samps.q));
        _mm_storeu_si128(out, _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(pre, Min32Vec.q), Max32Vec.q)));

        dataOut += 4;
        break;
      }
      default: {
        for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
          AudioChannel ch = chmap.m_channels[c];
          if (ch != AudioChannel::Unknown) {
            *dataOut = Clamp32(*dataOut + *dataIn * m_coefs.v[int(ch)]);
            ++dataOut;
          }
        }
        break;
      }
      }
    }
  }
  return dataOut;
}

float* AudioMatrixMono::mixMonoSampleData(const AudioVoiceEngineMixInfo& info, const float* dataIn, float* dataOut,
                                          size_t samples) {
  const ChannelMap& chmap = info.m_channelMap;
  for (size_t s = 0; s < samples; ++s, ++dataIn) {
    if (m_slewFrames && m_curSlewFrame < m_slewFrames) {
      float t = m_curSlewFrame / float(m_slewFrames);
      float omt = 1.f - t;

      switch (chmap.m_channelCount) {
      case 2: {
        ++m_curSlewFrame;
        float t2 = m_curSlewFrame / float(m_slewFrames);
        float omt2 = 1.f - t2;

        TVectorUnion coefs, samps;
        coefs.q = _mm_add_ps(
            _mm_mul_ps(_mm_shuffle_ps(m_coefs.q[0], m_coefs.q[0], _MM_SHUFFLE(1, 0, 1, 0)), _mm_set_ps(t, t, t2, t2)),
            _mm_mul_ps(_mm_shuffle_ps(m_oldCoefs.q[0], m_oldCoefs.q[0], _MM_SHUFFLE(1, 0, 1, 0)),
                       _mm_set_ps(omt, omt, omt2, omt2)));
        samps.q = _mm_loadu_ps(dataIn);
        samps.q = _mm_shuffle_ps(samps.q, samps.q, _MM_SHUFFLE(1, 0, 1, 0));

        __m128 pre = _mm_add_ps(_mm_loadu_ps(dataOut), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_ps(dataOut, pre);

        dataOut += 4;
        ++s;
        ++dataIn;
        break;
      }
      default: {
        for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
          AudioChannel ch = chmap.m_channels[c];
          if (ch != AudioChannel::Unknown) {
            *dataOut = *dataOut + *dataIn * (m_coefs.v[int(ch)] * t + m_oldCoefs.v[int(ch)] * omt);
            ++dataOut;
          }
        }
        break;
      }
      }

      ++m_curSlewFrame;
    } else {
      switch (chmap.m_channelCount) {
      case 2: {
        TVectorUnion coefs, samps;
        coefs.q = _mm_shuffle_ps(m_coefs.q[0], m_coefs.q[0], _MM_SHUFFLE(1, 0, 1, 0));
        samps.q = _mm_loadu_ps(dataIn);
        samps.q = _mm_shuffle_ps(samps.q, samps.q, _MM_SHUFFLE(1, 0, 1, 0));

        __m128 pre = _mm_add_ps(_mm_loadu_ps(dataOut), _mm_mul_ps(coefs.q, samps.q));
        _mm_storeu_ps(dataOut, pre);

        dataOut += 4;
        ++s;
        ++dataIn;
        break;
      }
      default: {
        for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
          AudioChannel ch = chmap.m_channels[c];
          if (ch != AudioChannel::Unknown) {
            *dataOut = *dataOut + *dataIn * m_coefs.v[int(ch)];
            ++dataOut;
          }
        }
        break;
      }
      }
    }
  }
  return dataOut;
}

void AudioMatrixStereo::setDefaultMatrixCoefficients(AudioChannelSet acSet) {
  m_curSlewFrame = 0;
  m_slewFrames = 0;
  m_coefs.q[0] = _mm_xor_ps(m_coefs.q[0], m_coefs.q[0]);
  m_coefs.q[1] = _mm_xor_ps(m_coefs.q[1], m_coefs.q[1]);
  m_coefs.q[2] = _mm_xor_ps(m_coefs.q[2], m_coefs.q[2]);
  m_coefs.q[3] = _mm_xor_ps(m_coefs.q[3], m_coefs.q[3]);
  switch (acSet) {
  case AudioChannelSet::Stereo:
  case AudioChannelSet::Quad:
    m_coefs.v[int(AudioChannel::FrontLeft)][0] = 1.0;
    m_coefs.v[int(AudioChannel::FrontRight)][1] = 1.0;
    break;
  case AudioChannelSet::Surround51:
  case AudioChannelSet::Surround71:
    m_coefs.v[int(AudioChannel::FrontLeft)][0] = 1.0;
    m_coefs.v[int(AudioChannel::FrontRight)][1] = 1.0;
    break;
  default:
    break;
  }
}

int16_t* AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info, const int16_t* dataIn,
                                                int16_t* dataOut, size_t frames) {
  const ChannelMap& chmap = info.m_channelMap;
  for (size_t f = 0; f < frames; ++f, dataIn += 2) {
    if (m_slewFrames && m_curSlewFrame < m_slewFrames) {
      double t = m_curSlewFrame / double(m_slewFrames);
      double omt = 1.0 - t;

      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = Clamp16(*dataOut + *dataIn * (m_coefs.v[int(ch)][0] * t + m_oldCoefs.v[int(ch)][0] * omt) +
                             *dataIn * (m_coefs.v[int(ch)][1] * t + m_oldCoefs.v[int(ch)][1] * omt));
          ++dataOut;
        }
      }

      ++m_curSlewFrame;
    } else {
      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = Clamp16(*dataOut + dataIn[0] * m_coefs.v[int(ch)][0] + dataIn[1] * m_coefs.v[int(ch)][1]);
          ++dataOut;
        }
      }
    }
  }
  return dataOut;
}

int32_t* AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info, const int32_t* dataIn,
                                                int32_t* dataOut, size_t frames) {
  const ChannelMap& chmap = info.m_channelMap;
  for (size_t f = 0; f < frames; ++f, dataIn += 2) {
    if (m_slewFrames && m_curSlewFrame < m_slewFrames) {
      double t = m_curSlewFrame / double(m_slewFrames);
      double omt = 1.0 - t;

      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = Clamp32(*dataOut + *dataIn * (m_coefs.v[int(ch)][0] * t + m_oldCoefs.v[int(ch)][0] * omt) +
                             *dataIn * (m_coefs.v[int(ch)][1] * t + m_oldCoefs.v[int(ch)][1] * omt));
          ++dataOut;
        }
      }

      ++m_curSlewFrame;
    } else {
      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = Clamp32(*dataOut + dataIn[0] * m_coefs.v[int(ch)][0] + dataIn[1] * m_coefs.v[int(ch)][1]);
          ++dataOut;
        }
      }
    }
  }
  return dataOut;
}

float* AudioMatrixStereo::mixStereoSampleData(const AudioVoiceEngineMixInfo& info, const float* dataIn, float* dataOut,
                                              size_t frames) {
  const ChannelMap& chmap = info.m_channelMap;
  for (size_t f = 0; f < frames; ++f, dataIn += 2) {
    if (m_slewFrames && m_curSlewFrame < m_slewFrames) {
      double t = m_curSlewFrame / double(m_slewFrames);
      double omt = 1.0 - t;

      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = *dataOut + *dataIn * (m_coefs.v[int(ch)][0] * t + m_oldCoefs.v[int(ch)][0] * omt) +
                     *dataIn * (m_coefs.v[int(ch)][1] * t + m_oldCoefs.v[int(ch)][1] * omt);
          ++dataOut;
        }
      }

      ++m_curSlewFrame;
    } else {
      for (unsigned c = 0; c < chmap.m_channelCount; ++c) {
        AudioChannel ch = chmap.m_channels[c];
        if (ch != AudioChannel::Unknown) {
          *dataOut = *dataOut + dataIn[0] * m_coefs.v[int(ch)][0] + dataIn[1] * m_coefs.v[int(ch)][1];
          ++dataOut;
        }
      }
    }
  }
  return dataOut;
}

} // namespace boo

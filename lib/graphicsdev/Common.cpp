#include "Common.hpp"

#include <cmath>
#include <numeric>
#include <thread>

namespace boo {

void UpdateGammaLUT(ITextureD* tex, float gamma) {
  void* data = tex->map(65536 * 2);
  for (int i = 0; i < 65536; ++i) {
    float level = std::pow(i / 65535.f, gamma);
    reinterpret_cast<uint16_t*>(data)[i] = level * 65535.f;
  }
  tex->unmap();
}

void Limiter::Sleep(nanotime_t targetFrameTime) {
  OPTICK_EVENT();
  if (targetFrameTime == 0) {
    return;
  }

  auto start = delta_clock::now();
  nanotime_t sleepTime = targetFrameTime - TimeSince(m_oldTime);
  m_overhead = std::accumulate(m_overheadTimes.begin(), m_overheadTimes.end(), nanotime_t{}) /
               static_cast<nanotime_t>(m_overheadTimes.size());
  if (sleepTime > m_overhead) {
    nanotime_t adjustedSleepTime = sleepTime - m_overhead;
    std::this_thread::sleep_for(std::chrono::nanoseconds(adjustedSleepTime));
    nanotime_t overslept = TimeSince(start) - adjustedSleepTime;
    if (overslept < targetFrameTime) {
      m_overheadTimes[m_overheadTimeIdx] = overslept;
      m_overheadTimeIdx = (m_overheadTimeIdx + 1) % m_overheadTimes.size();
    }
  }
  m_oldTime = delta_clock::now();
}

} // namespace boo

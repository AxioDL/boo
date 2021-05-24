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

} // namespace boo

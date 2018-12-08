#pragma once
#if __APPLE__

#if !__has_feature(objc_arc)
#error ARC Required
#endif

#if BOO_HAS_METAL

#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <unordered_map>
#include <mutex>

namespace boo {
class IWindow;
struct MetalContext {
  id<MTLDevice> m_dev = nullptr;
  id<MTLCommandQueue> m_q = nullptr;
  struct Window {
    CAMetalLayer* m_metalLayer = nullptr;
    std::mutex m_resizeLock;
    bool m_needsResize;
    CGSize m_size;
  };
  std::unordered_map<IWindow*, Window> m_windows;
  uint32_t m_sampleCount = 1;
  uint32_t m_anisotropy = 1;
  MTLPixelFormat m_pixelFormat = MTLPixelFormatBGRA8Unorm;
};
} // namespace boo

#else
namespace boo {
struct MetalContext {};
} // namespace boo
#endif

#endif // __APPLE__

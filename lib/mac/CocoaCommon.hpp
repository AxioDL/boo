#ifndef BOO_COCOACOMMON_HPP
#define BOO_COCOACOMMON_HPP
#if __APPLE__

#if !__has_feature(objc_arc)
#error ARC Required
#endif

#if BOO_HAS_METAL

#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <unordered_map>
#include <mutex>

namespace boo
{
class IWindow;
struct MetalContext
{
    id<MTLDevice> m_dev = nullptr;
    id<MTLCommandQueue> m_q = nullptr;
    struct Window
    {
        CAMetalLayer* m_metalLayer = nullptr;
        std::mutex m_resizeLock;
        bool m_needsResize;
        CGSize m_size;
    };
    std::unordered_map<IWindow*, Window> m_windows;
};
}

#else
namespace boo
{
    struct MetalContext {};
}
#endif

#endif // __APPLE__
#endif // BOO_COCOACOMMON_HPP

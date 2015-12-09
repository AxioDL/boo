#ifndef BOO_COCOACOMMON_HPP
#define BOO_COCOACOMMON_HPP
#if __APPLE__

#include <Availability.h>

template <class T>
class NSPtr
{
    T m_ptr = 0;
public:
    NSPtr() = default;
    ~NSPtr() {[m_ptr release];}
    NSPtr(T&& recv) : m_ptr(recv) {}
    NSPtr& operator=(T&& recv) {[m_ptr release]; m_ptr = recv; return *this;}
    NSPtr(const NSPtr& other) = delete;
    NSPtr(NSPtr&& other) = default;
    NSPtr& operator=(const NSPtr& other) = delete;
    NSPtr& operator=(NSPtr&& other) = default;
    operator bool() const {return m_ptr != 0;}
    T get() const {return m_ptr;}
    void reset() {[m_ptr release]; m_ptr = 0;}
};

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101100
#define BOO_HAS_METAL 1

#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <unordered_map>
#include <mutex>

namespace boo
{
class IWindow;
struct MetalContext
{
    NSPtr<id<MTLDevice>> m_dev;
    NSPtr<id<MTLCommandQueue>> m_q;
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
#define BOO_HAS_METAL 0
namespace boo
{
    struct MetalContext {};
}
#endif

#endif // __APPLE__
#endif // BOO_COCOACOMMON_HPP

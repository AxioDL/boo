#include "boo/inputdev/IHIDListener.hpp"

namespace boo
{

class HIDListenerNX : public IHIDListener
{
    DeviceFinder& m_finder;

public:
    HIDListenerNX(DeviceFinder& finder)
    : m_finder(finder)
    {}

    bool startScanning() { return false; }
    bool stopScanning() { return false; }
    bool scanNow() { return false; }
};

std::unique_ptr<IHIDListener> IHIDListenerNew(DeviceFinder& finder)
{
    return std::make_unique<HIDListenerNX>(finder);
}

}

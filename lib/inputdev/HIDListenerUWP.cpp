#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#include "boo/inputdev/IHIDListener.hpp"
#include "boo/inputdev/DeviceFinder.hpp"

namespace boo
{

class HIDListenerUWP : public IHIDListener
{
public:
    HIDListenerUWP(DeviceFinder& finder) {}

    /* Automatic device scanning */
    bool startScanning() { return false; }
    bool stopScanning() { return false; }

    /* Manual device scanning */
    bool scanNow() { return false; }
};

std::unique_ptr<IHIDListener> IHIDListenerNew(DeviceFinder& finder)
{
    return std::make_unique<HIDListenerUWP>(finder);
}

}

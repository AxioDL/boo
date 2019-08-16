#include "boo/inputdev/IHIDListener.hpp"
#include "boo/inputdev/DeviceFinder.hpp"

namespace boo {

class HIDListenerBSD final : public IHIDListener {
  DeviceFinder& m_finder;

public:
  HIDListenerBSD(DeviceFinder& finder) : m_finder(finder) {}
  ~HIDListenerBSD() override = default;

  bool startScanning() override { return false; }
  bool stopScanning() override { return false; }

  bool scanNow() override { return false; }
};

IHIDListener* IHIDListenerNew(DeviceFinder& finder) { return new HIDListenerBSD(finder); }
} // namespace boo

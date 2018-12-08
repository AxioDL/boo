#include "boo/inputdev/IHIDListener.hpp"
#include "boo/inputdev/DeviceFinder.hpp"

namespace boo {

class HIDListenerBSD final : public IHIDListener {
  DeviceFinder& m_finder;

public:
  HIDListenerBSD(DeviceFinder& finder) : m_finder(finder) {}

  ~HIDListenerBSD() {}

  bool startScanning() { return false; }
  bool stopScanning() { return false; }

  bool scanNow() { return false; }
};

IHIDListener* IHIDListenerNew(DeviceFinder& finder) { return new HIDListenerBSD(finder); }
} // namespace boo

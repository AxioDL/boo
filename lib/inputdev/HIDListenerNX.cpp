#include "boo/inputdev/IHIDListener.hpp"

namespace boo {

class HIDListenerNX : public IHIDListener {
  DeviceFinder& m_finder;

public:
  HIDListenerNX(DeviceFinder& finder) : m_finder(finder) {}

  bool startScanning() override { return false; }
  bool stopScanning() override { return false; }
  bool scanNow() override { return false; }
};

std::unique_ptr<IHIDListener> IHIDListenerNew(DeviceFinder& finder) { return std::make_unique<HIDListenerNX>(finder); }

} // namespace boo

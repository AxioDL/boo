#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

namespace boo {

enum class DeviceType { None = 0, USB = 1, Bluetooth = 2, HID = 3, XInput = 4 };

class DeviceToken;
class DeviceBase;

#define dev_typeid(type) std::hash<std::string>()(#type)

struct DeviceSignature {
  using TDeviceSignatureSet = std::vector<const DeviceSignature*>;
  using TFactoryLambda = std::function<std::shared_ptr<DeviceBase>(DeviceToken*)>;

  const char* m_name = nullptr;
  uint64_t m_typeHash = 0;
  unsigned m_vid = 0;
  unsigned m_pid = 0;
  TFactoryLambda m_factory;
  DeviceType m_type{};
  DeviceSignature() : m_typeHash(dev_typeid(DeviceSignature)) {} /* Sentinel constructor */
  DeviceSignature(const char* name, uint64_t typeHash, unsigned vid, unsigned pid, TFactoryLambda&& factory,
                  DeviceType type = DeviceType::None)
  : m_name(name), m_typeHash(typeHash), m_vid(vid), m_pid(pid), m_factory(factory), m_type(type) {}
  static bool DeviceMatchToken(const DeviceToken& token, const TDeviceSignatureSet& sigSet);
  static std::shared_ptr<DeviceBase> DeviceNew(DeviceToken& token);
};

#define DEVICE_SIG(name, vid, pid, type)                                                                               \
  DeviceSignature(#name, dev_typeid(name), vid, pid,                                                                   \
                  [](DeviceToken* tok) -> std::shared_ptr<DeviceBase> { return std::make_shared<name>(tok); }, type)
#define DEVICE_SIG_SENTINEL() DeviceSignature()

extern const DeviceSignature BOO_DEVICE_SIGS[];

} // namespace boo

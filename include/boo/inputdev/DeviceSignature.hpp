#ifndef SDeviceSignature_HPP
#define SDeviceSignature_HPP

#include <vector>
#include <functional>
#include <typeindex>
#include <memory>

namespace boo
{

enum class DeviceType
{
    None       = 0,
    USB        = 1,
    Bluetooth  = 2,
    HID        = 3,
    XInput     = 4
};

class DeviceToken;
class DeviceBase;

struct DeviceSignature
{
    typedef std::vector<const DeviceSignature*> TDeviceSignatureSet;
    typedef std::function<std::shared_ptr<DeviceBase>(DeviceToken*)> TFactoryLambda;
    const char* m_name;
    std::type_index m_typeIdx;
    unsigned m_vid, m_pid;
    TFactoryLambda m_factory;
    DeviceType m_type;
    DeviceSignature() : m_name(NULL), m_typeIdx(typeid(DeviceSignature)) {} /* Sentinel constructor */
    DeviceSignature(const char* name, std::type_index&& typeIdx, unsigned vid, unsigned pid,
                    TFactoryLambda&& factory, DeviceType type=DeviceType::None)
        : m_name(name), m_typeIdx(typeIdx), m_vid(vid), m_pid(pid),
          m_factory(factory), m_type(type) {}
    static bool DeviceMatchToken(const DeviceToken& token, const TDeviceSignatureSet& sigSet);
    static std::shared_ptr<DeviceBase> DeviceNew(DeviceToken& token);
};

#define DEVICE_SIG(name, vid, pid, type) \
    DeviceSignature(#name, typeid(name), vid, pid,\
    [](DeviceToken* tok) -> std::shared_ptr<DeviceBase> {return std::make_shared<name>(tok);}, type)
#define DEVICE_SIG_SENTINEL() DeviceSignature()

extern const DeviceSignature BOO_DEVICE_SIGS[];

}

#endif // SDeviceSignature_HPP


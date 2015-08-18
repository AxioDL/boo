#ifndef SDeviceSignature_HPP
#define SDeviceSignature_HPP

#include <vector>
#include <functional>
#include <typeindex>

namespace boo
{

class DeviceToken;
class DeviceBase;

struct DeviceSignature
{
    typedef std::vector<const DeviceSignature*> TDeviceSignatureSet;
    typedef std::function<DeviceBase*(DeviceToken*)> TFactoryLambda;
    const char* m_name;
    std::type_index m_typeIdx;
    unsigned m_vid, m_pid;
    TFactoryLambda m_factory;
    DeviceSignature() : m_name(NULL), m_typeIdx(typeid(DeviceSignature)) {} /* Sentinel constructor */
    DeviceSignature(const char* name, std::type_index&& typeIdx, unsigned vid, unsigned pid, TFactoryLambda&& factory)
        : m_name(name), m_typeIdx(typeIdx), m_vid(vid), m_pid(pid), m_factory(factory) {}
    static bool DeviceMatchToken(const DeviceToken& token, const TDeviceSignatureSet& sigSet);
    static DeviceBase* DeviceNew(DeviceToken& token);
};

#define DEVICE_SIG(name, vid, pid) \
    DeviceSignature(#name, typeid(name), vid, pid, [](DeviceToken* tok) -> DeviceBase* {return new name(tok);})
#define DEVICE_SIG_SENTINEL() DeviceSignature()

extern const DeviceSignature BOO_DEVICE_SIGS[];

}

#endif // SDeviceSignature_HPP


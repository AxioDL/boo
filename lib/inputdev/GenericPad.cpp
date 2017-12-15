#include "boo/inputdev/GenericPad.hpp"
#include "boo/inputdev/DeviceToken.hpp"

namespace boo
{

GenericPad::GenericPad(DeviceToken* token)
: TDeviceBase<IGenericPadCallback>(token)
{

}

GenericPad::~GenericPad() {}

void GenericPad::deviceDisconnected()
{
    std::lock_guard<std::mutex> lk(m_callbackLock);
    if (m_callback)
        m_callback->controllerDisconnected();
}

void GenericPad::initialCycle()
{
#if _WIN32
#if !WINDOWS_STORE
    const PHIDP_PREPARSED_DATA reportDesc = getReportDescriptor();
    m_parser.Parse(reportDesc);
#endif
#else
    std::vector<uint8_t> reportDesc = getReportDescriptor();
    m_parser.Parse(reportDesc.data(), reportDesc.size());
#endif
    std::lock_guard<std::mutex> lk(m_callbackLock);
    if (m_callback)
        m_callback->controllerConnected();
}

void GenericPad::receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
{
    std::lock_guard<std::mutex> lk(m_callbackLock);
    if (length == 0 || tp != HIDReportType::Input || !m_callback)
        return;
    std::function<bool(const HIDMainItem&, int32_t)> func =
    [this](const HIDMainItem& item, int32_t value)
    {
        m_callback->valueUpdate(item, value);
        return true;
    };
    m_parser.ScanValues(func, data, length);
}

void GenericPad::enumerateValues(const std::function<bool(const HIDMainItem& item)>& valueCB) const
{
    m_parser.EnumerateValues(valueCB);
}

}

#include "boo/inputdev/GenericPad.hpp"
#include "boo/inputdev/DeviceToken.hpp"

namespace boo
{

GenericPad::GenericPad(DeviceToken* token)
: DeviceBase(token)
{

}

GenericPad::~GenericPad() {}

void GenericPad::deviceDisconnected()
{
    if (m_cb)
        m_cb->controllerDisconnected();
}

void GenericPad::initialCycle()
{
#if _WIN32
    const PHIDP_PREPARSED_DATA reportDesc = getReportDescriptor();
    m_parser.Parse(reportDesc);
#else
    std::vector<uint8_t> reportDesc = getReportDescriptor();
    m_parser.Parse(reportDesc.data(), reportDesc.size());
#endif
    if (m_cb)
        m_cb->controllerConnected();
}

void GenericPad::receivedHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
{
    if (length == 0 || tp != HIDReportType::Input || !m_cb)
        return;
    std::function<bool(const HIDMainItem&, int32_t)> func =
    [this](const HIDMainItem& item, int32_t value)
    {
        m_cb->valueUpdate(item, value);
        return true;
    };
    m_parser.ScanValues(func, data, length);
}

void GenericPad::enumerateValues(const std::function<bool(const HIDMainItem& item)>& valueCB) const
{
    m_parser.EnumerateValues(valueCB);
}

}

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
    std::vector<uint8_t> reportDesc = getReportDescriptor();
    m_parser.Parse(reportDesc.data(), reportDesc.size());
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

void GenericPad::enumerateValues(std::function<bool(const HIDMainItem& item)>& valueCB) const
{
    std::function<bool(uint32_t, const HIDMainItem&)> func =
        [&](uint32_t rep, const HIDMainItem& item) { return valueCB(item); };
    m_parser.EnumerateValues(func);
}

}

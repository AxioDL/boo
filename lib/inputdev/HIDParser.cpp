#include "boo/inputdev/HIDParser.hpp"
#include <map>
#include <algorithm>

#undef min
#undef max

namespace boo
{

/* Based on "Device Class Definition for Human Interface Devices (HID)"
 * http://www.usb.org/developers/hidpage/HID1_11.pdf
 */

static const char* UsagePageNames[] =
{
    "Undefined",
    "Generic Desktop",
    "Simulation",
    "VR",
    "Sport",
    "Game Controls",
    "Generic Device",
    "Keyboard",
    "LEDs",
    "Button",
    "Ordinal",
    "Telephony",
    "Consumer",
    "Digitizer"
};

static const char* GenericDesktopUsages[] =
{
    "Undefined",
    "Pointer",
    "Mouse",
    "Reserved",
    "Joystick",
    "Game Pad",
    "Keyboard",
    "Keypad",
    "Multi-axis Controller",
    "Tablet PC System Controls",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "X",
    "Y",
    "Z",
    "Rx",
    "Ry",
    "Rz",
    "Slider",
    "Dial",
    "Wheel",
    "Hat Switch",
    "Counted Buffer",
    "Byte Count",
    "Motion Wakeup",
    "Start",
    "Select",
    "Reserved",
    "Vx",
    "Vy",
    "Vz",
    "Vbrx",
    "Vbry",
    "Vbrz",
    "Vno",
    "Feature Notification",
    "Resolution Multiplier",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "System Control",
    "System Power Down",
    "System Sleep",
    "System Wake Up",
    "System Context Menu",
    "System Main Menu",
    "System App Menu",
    "System Menu Help",
    "System Menu Exit",
    "System Menu Select",
    "System Menu Right",
    "System Menu Left",
    "System Menu Up",
    "System Menu Down",
    "System Cold Restart",
    "System Warm Restart",
    "D-pad Up",
    "D-pad Down",
    "D-pad Right",
    "D-pad Left",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "System Dock",
    "System Undock",
    "System Setup",
    "System Break",
    "System Debugger Break",
    "Application Break",
    "Application Debugger Break",
    "System Speaker Mute",
    "System Hibernate",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "System Display Invert",
    "System Display Internal",
    "System Display External",
    "System Display Both",
    "System Display Dual",
    "System Display Toggle Int/Ext"
};

static const char* GameUsages[] =
{
    "Undefined",
    "3D Game Controller",
    "Pinball Device",
    "Gun Device",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "Point of View",
    "Turn Right/Left",
    "Pitch Forward/Backward",
    "Roll Right/Left",
    "Move Right/Left",
    "Move Forward/Backward",
    "Move Up/Down",
    "Lean Right/Left",
    "Lean Forward/Backward",
    "Height of POV",
    "Flipper",
    "Secondary Flipper",
    "Bump",
    "New Game",
    "Shoot Ball",
    "Player",
    "Gun Bolt",
    "Gun Clip",
    "Gun Selector",
    "Gun Single Shot",
    "Gun Burst",
    "Gun Automatic",
    "Gun Safety",
    "Gemepad Fire/Jump",
    nullptr,
    "Gamepad Trigger"
};

enum class HIDCollectionType : uint8_t
{
    Physical,
    Application,
    Logical,
    Report,
    NamedArray,
    UsageSwitch,
    UsageModifier
};

enum class HIDItemType : uint8_t
{
    Main,
    Global,
    Local,
    Reserved
};

enum class HIDItemTag : uint8_t
{
    /* [6.2.2.4] Main Items */
    Input = 0b1000,
    Output = 0b1001,
    Feature = 0b1011,
    Collection = 0b1010,
    EndCollection = 0b1100,

    /* [6.2.2.7] Global Items */
    UsagePage = 0b0000,
    LogicalMinimum = 0b0001,
    LogicalMaximum = 0b0010,
    PhysicalMinimum = 0b0011,
    PhysicalMaximum = 0b0100,
    UnitExponent = 0b0101,
    Unit = 0b0110,
    ReportSize = 0b0111,
    ReportID = 0b1000,
    ReportCount = 0b1001,
    Push = 0b1010,
    Pop = 0b1011,

    /* [6.2.2.8] Local Items */
    Usage = 0b0000,
    UsageMinimum = 0b0001,
    UsageMaximum = 0b0010,
    DesignatorIndex = 0b0011,
    DesignatorMinimum = 0b0100,
    DesignatorMaximum = 0b0101,
    StringIndex = 0b0111,
    StringMinimum = 0b1000,
    StringMaximum = 0b1001,
    Delimiter = 0b1010,
};

struct HIDItemState
{
    /* [6.2.2.7] Global items */
    HIDUsagePage m_usagePage = HIDUsagePage::Undefined;
    HIDRange m_logicalRange = {};
    HIDRange m_physicalRange = {};
    int32_t m_unitExponent = 0;
    uint32_t m_unit = 0;
    uint32_t m_reportSize = 0; // In bits
    uint32_t m_reportID = 0;
    uint32_t m_reportCount = 0;

    /* [6.2.2.8] Local Items */
    std::vector<HIDUsage> m_usage;
    HIDRange m_usageRange = {};
#if 0
    std::vector<int32_t> m_designatorIndex;
    std::vector<HIDRange> m_designatorRange;
    std::vector<int32_t> m_stringIndex;
    std::vector<HIDRange> m_stringRange;
    std::vector<int32_t> m_delimiter;
#endif

    void ResetLocalItems()
    {
        m_usage.clear();
        m_usageRange = HIDRange();
#if 0
        m_designatorIndex.clear();
        m_designatorRange.clear();
        m_stringIndex.clear();
        m_stringRange.clear();
        m_delimiter.clear();
#endif
    }

    template <typename T>
    static T _GetLocal(const std::vector<T>& v, uint32_t idx)
    {
        if (v.empty())
            return {};
        if (idx >= v.size())
            return v[0];
        return v[idx];
    }

    HIDUsage GetUsage(uint32_t idx) const
    {
        if (m_usageRange.second - m_usageRange.first != 0)
            return HIDUsage(m_usageRange.first + idx);
        return _GetLocal(m_usage, idx);
    }
};

struct HIDCollectionItem
{
    /* [6.2.2.6] Collection, End Collection Items */
    HIDCollectionType m_type;
    HIDUsagePage m_usagePage;
    HIDUsage m_usage;

    HIDCollectionItem(HIDCollectionType type, const HIDItemState& state)
    : m_type(type), m_usagePage(state.m_usagePage), m_usage(state.GetUsage(0))
    {}
};

HIDMainItem::HIDMainItem(uint32_t flags, const HIDItemState& state, uint32_t reportIdx)
: m_flags(uint16_t(flags))
{
    m_usagePage = state.m_usagePage;
    m_usage = state.GetUsage(reportIdx);
    m_logicalRange = state.m_logicalRange;
    m_reportSize = state.m_reportSize;
}

HIDMainItem::HIDMainItem(uint32_t flags, HIDUsagePage usagePage, HIDUsage usage,
                         HIDRange logicalRange, int32_t reportSize)
: m_flags(uint16_t(flags)), m_usagePage(usagePage), m_usage(usage),
  m_logicalRange(logicalRange), m_reportSize(reportSize)
{}

const char* HIDMainItem::GetUsagePageName() const
{
    if (int(m_usagePage) >= std::extent<decltype(UsagePageNames)>::value)
        return nullptr;
    return UsagePageNames[int(m_usagePage)];
}

const char* HIDMainItem::GetUsageName() const
{
    switch (m_usagePage)
    {
    case HIDUsagePage::GenericDesktop:
        if (int(m_usage) >= std::extent<decltype(GenericDesktopUsages)>::value)
            return nullptr;
        return GenericDesktopUsages[int(m_usage)];
    case HIDUsagePage::Game:
        if (int(m_usage) >= std::extent<decltype(GameUsages)>::value)
            return nullptr;
        return GameUsages[int(m_usage)];
    default:
        return nullptr;
    }
}

static HIDParser::ParserStatus
AdvanceIt(const uint8_t*& it, const uint8_t* end, size_t adv)
{
    it += adv;
    if (it > end)
    {
        it = end;
        return HIDParser::ParserStatus::Error;
    }
    else if (it == end)
    {
        return HIDParser::ParserStatus::Done;
    }
    return HIDParser::ParserStatus::OK;
}

static uint8_t
GetByteValue(const uint8_t*& it, const uint8_t* end, HIDParser::ParserStatus& status)
{
    const uint8_t* oldIt = it;
    status = AdvanceIt(it, end, 1);
    if (status == HIDParser::ParserStatus::Error)
        return 0;
    return *oldIt;
}

static uint32_t
GetShortValue(const uint8_t*& it, const uint8_t* end, int adv, HIDParser::ParserStatus& status)
{
    const uint8_t* oldIt = it;
    switch (adv)
    {
    case 1:
        status = AdvanceIt(it, end, 1);
        if (status == HIDParser::ParserStatus::Error)
            return 0;
        return *oldIt;
    case 2:
        status = AdvanceIt(it, end, 2);
        if (status == HIDParser::ParserStatus::Error)
            return 0;
        return *reinterpret_cast<const uint16_t*>(&*oldIt);
    case 3:
        status = AdvanceIt(it, end, 4);
        if (status == HIDParser::ParserStatus::Error)
            return 0;
        return *reinterpret_cast<const uint32_t*>(&*oldIt);
    default:
        break;
    }
    return 0;
}

struct HIDReports
{
    std::map<int32_t, std::vector<HIDMainItem>> m_inputReports;
    std::map<int32_t, std::vector<HIDMainItem>> m_outputReports;
    std::map<int32_t, std::vector<HIDMainItem>> m_featureReports;

    static void _AddItem(std::map<int32_t, std::vector<HIDMainItem>>& m, uint32_t flags, const HIDItemState& state)
    {
        std::vector<HIDMainItem>& report = m[state.m_reportID];
        report.reserve(report.size() + state.m_reportCount);
        for (int i=0 ; i<state.m_reportCount ; ++i)
            report.emplace_back(flags, state, i);
    }

    void AddInputItem(uint32_t flags, const HIDItemState& state) { _AddItem(m_inputReports, flags, state); }
    void AddOutputItem(uint32_t flags, const HIDItemState& state) { _AddItem(m_outputReports, flags, state); }
    void AddFeatureItem(uint32_t flags, const HIDItemState& state) { _AddItem(m_featureReports, flags, state); }
};

#if _WIN32
HIDParser::ParserStatus HIDParser::Parse(const PHIDP_PREPARSED_DATA descriptorData)
{
    /* User mode HID report descriptor isn't available on Win32.
     * Opaque preparsed data must be enumerated and sorted into
     * iterable items.
     *
     * Wine's implementation has a good illustration of what's
     * going on here:
     * https://github.com/wine-mirror/wine/blob/master/dlls/hidclass.sys/descriptor.c
     *
     * (Thanks for this pointless pain-in-the-ass Microsoft)
     */

    m_descriptorData = descriptorData;
    HIDP_CAPS caps;
    HidP_GetCaps(descriptorData, &caps);
    m_dataList.resize(HidP_MaxDataListLength(HidP_Input, descriptorData));

    std::map<uint32_t, HIDMainItem> inputItems;

    {
        /* First enumerate buttons */
        USHORT length = caps.NumberInputButtonCaps;
        std::vector<HIDP_BUTTON_CAPS> bCaps(caps.NumberInputButtonCaps, HIDP_BUTTON_CAPS());
        HidP_GetButtonCaps(HidP_Input, bCaps.data(), &length, descriptorData);
        for (const HIDP_BUTTON_CAPS& caps : bCaps)
        {
            if (caps.IsRange)
            {
                int usage = caps.Range.UsageMin;
                for (int i=caps.Range.DataIndexMin ; i<=caps.Range.DataIndexMax ; ++i, ++usage)
                {
                    inputItems.insert(std::make_pair(i,
                        HIDMainItem(caps.BitField, HIDUsagePage(caps.UsagePage),
                                    HIDUsage(usage), std::make_pair(0, 1), 1)));
                }
            }
            else
            {
                inputItems.insert(std::make_pair(caps.NotRange.DataIndex,
                    HIDMainItem(caps.BitField, HIDUsagePage(caps.UsagePage),
                                HIDUsage(caps.NotRange.Usage), std::make_pair(0, 1), 1)));
            }
        }
    }

    {
        /* Now enumerate values */
        USHORT length = caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> vCaps(caps.NumberInputValueCaps, HIDP_VALUE_CAPS());
        HidP_GetValueCaps(HidP_Input, vCaps.data(), &length, descriptorData);
        for (const HIDP_VALUE_CAPS& caps : vCaps)
        {
            if (caps.IsRange)
            {
                int usage = caps.Range.UsageMin;
                for (int i=caps.Range.DataIndexMin ; i<=caps.Range.DataIndexMax ; ++i, ++usage)
                {
                    inputItems.insert(std::make_pair(i,
                        HIDMainItem(caps.BitField, HIDUsagePage(caps.UsagePage), HIDUsage(usage),
                                    std::make_pair(caps.LogicalMin, caps.LogicalMax), caps.BitSize)));
                }
            }
            else
            {
                inputItems.insert(std::make_pair(caps.NotRange.DataIndex,
                    HIDMainItem(caps.BitField, HIDUsagePage(caps.UsagePage), HIDUsage(caps.NotRange.Usage),
                                HIDRange(caps.LogicalMin, caps.LogicalMax), caps.BitSize)));
            }
        }
    }

    m_itemPool.reserve(inputItems.size());
    for (const auto& item : inputItems)
        m_itemPool.push_back(item.second);

    m_status = ParserStatus::Done;
    return ParserStatus::Done;
}
#else

HIDParser::ParserStatus
HIDParser::ParseItem(HIDReports& reportsOut,
                     std::stack<HIDItemState>& stateStack,
                     std::stack<HIDCollectionItem>& collectionStack,
                     const uint8_t*& it, const uint8_t* end)
{
    ParserStatus status = ParserStatus::OK;
    uint8_t head = *it++;
    if (head == 0b11111110)
    {
        /* Long item */
        uint8_t bDataSize = GetByteValue(it, end, status);
        if (status == ParserStatus::Error)
            return ParserStatus::Error;
        uint8_t bLongItemTag = GetByteValue(it, end, status);
        if (status == ParserStatus::Error)
            return ParserStatus::Error;
        status = AdvanceIt(it, end, bDataSize);
        if (status == ParserStatus::Error)
            return ParserStatus::Error;
    }
    else
    {
        /* Short Item */
        uint32_t data = GetShortValue(it, end, head & 0x3, status);
        if (status == ParserStatus::Error)
            return ParserStatus::Error;

        switch (HIDItemType((head >> 2) & 0x3))
        {
        case HIDItemType::Main:
            switch (HIDItemTag(head >> 4))
            {
            case HIDItemTag::Input:
                reportsOut.AddInputItem(data, stateStack.top());
                break;
            case HIDItemTag::Output:
                reportsOut.AddOutputItem(data, stateStack.top());
                break;
            case HIDItemTag::Feature:
                reportsOut.AddFeatureItem(data, stateStack.top());
                break;
            case HIDItemTag::Collection:
                collectionStack.emplace(HIDCollectionType(data), stateStack.top());
                break;
            case HIDItemTag::EndCollection:
                if (collectionStack.empty())
                    return ParserStatus::Error;
                collectionStack.pop();
                break;
            default:
                return ParserStatus::Error;
            }
            stateStack.top().ResetLocalItems();
            break;
        case HIDItemType::Global:
            switch (HIDItemTag(head >> 4))
            {
            case HIDItemTag::UsagePage:
                stateStack.top().m_usagePage = HIDUsagePage(data);
                break;
            case HIDItemTag::LogicalMinimum:
                stateStack.top().m_logicalRange.first = data;
                break;
            case HIDItemTag::LogicalMaximum:
                stateStack.top().m_logicalRange.second = data;
                break;
            case HIDItemTag::PhysicalMinimum:
                stateStack.top().m_physicalRange.first = data;
                break;
            case HIDItemTag::PhysicalMaximum:
                stateStack.top().m_physicalRange.second = data;
                break;
            case HIDItemTag::UnitExponent:
                stateStack.top().m_unitExponent = data;
                break;
            case HIDItemTag::Unit:
                stateStack.top().m_unit = data;
                break;
            case HIDItemTag::ReportSize:
                stateStack.top().m_reportSize = data;
                break;
            case HIDItemTag::ReportID:
                m_multipleReports = true;
                stateStack.top().m_reportID = data;
                break;
            case HIDItemTag::ReportCount:
                stateStack.top().m_reportCount = data;
                break;
            case HIDItemTag::Push:
                stateStack.push(stateStack.top());
                break;
            case HIDItemTag::Pop:
                if (stateStack.empty())
                    return ParserStatus::Error;
                stateStack.pop();
                break;
            default:
                return ParserStatus::Error;
            }
            break;
        case HIDItemType::Local:
            switch (HIDItemTag(head >> 4))
            {
            case HIDItemTag::Usage:
                stateStack.top().m_usage.push_back(HIDUsage(data));
                break;
            case HIDItemTag::UsageMinimum:
                stateStack.top().m_usageRange.first = data;
                break;
            case HIDItemTag::UsageMaximum:
                stateStack.top().m_usageRange.second = data;
                break;
            case HIDItemTag::DesignatorIndex:
            case HIDItemTag::DesignatorMinimum:
            case HIDItemTag::DesignatorMaximum:
            case HIDItemTag::StringIndex:
            case HIDItemTag::StringMinimum:
            case HIDItemTag::StringMaximum:
            case HIDItemTag::Delimiter:
                break;
            default:
                return ParserStatus::Error;
            }
            break;
        default:
            return ParserStatus::Error;
        }

    }

    return it == end ? ParserStatus::Done : ParserStatus::OK;
}

HIDParser::ParserStatus HIDParser::Parse(const uint8_t* descriptorData, size_t len)
{
    std::stack<HIDItemState> stateStack;
    stateStack.emplace();
    std::stack<HIDCollectionItem> collectionStack;
    HIDReports reports;

    const uint8_t* end = descriptorData + len;
    for (const uint8_t* it = descriptorData; it != end;)
        if ((m_status = ParseItem(reports, stateStack, collectionStack, it, end)) != ParserStatus::OK)
            break;

    if (m_status != ParserStatus::Done)
        return m_status;

    uint32_t itemCount = 0;
    uint32_t reportCount = uint32_t(reports.m_inputReports.size() + reports.m_outputReports.size() +
                                    reports.m_featureReports.size());

    for (const auto& rep : reports.m_inputReports)
        itemCount += rep.second.size();
    for (const auto& rep : reports.m_outputReports)
        itemCount += rep.second.size();
    for (const auto& rep : reports.m_featureReports)
        itemCount += rep.second.size();

    m_itemPool.reset(new HIDMainItem[itemCount]);
    m_reportPool.reset(new Report[reportCount]);

    uint32_t itemIndex = 0;
    uint32_t reportIndex = 0;

    auto func = [&](std::pair<uint32_t, uint32_t>& out, const std::map<int32_t, std::vector<HIDMainItem>>& in)
    {
        out = std::make_pair(reportIndex, reportIndex + in.size());
        for (const auto& rep : in)
        {
            m_reportPool[reportIndex++] =
                std::make_pair(rep.first, std::make_pair(itemIndex, itemIndex + rep.second.size()));
            for (const auto& item : rep.second)
                m_itemPool[itemIndex++] = item;
        }
    };
    func(m_inputReports, reports.m_inputReports);
    func(m_outputReports, reports.m_outputReports);
    func(m_featureReports, reports.m_featureReports);

    return m_status;
}
#endif

#if _WIN32
void HIDParser::EnumerateValues(const std::function<bool(const HIDMainItem& item)>& valueCB) const
{
    if (m_status != ParserStatus::Done)
        return;

    for (const HIDMainItem& item : m_itemPool)
    {
        if (item.IsConstant())
            continue;
        if (!valueCB(item))
            return;
    }
}
#else
void HIDParser::EnumerateValues(const std::function<bool(const HIDMainItem& item)>& valueCB) const
{
    if (m_status != ParserStatus::Done)
        return;

    for (uint32_t i=m_inputReports.first ; i<m_inputReports.second ; ++i)
    {
        const Report& rep = m_reportPool[i];
        for (uint32_t j=rep.second.first ; j<rep.second.second ; ++j)
        {
            const HIDMainItem& item = m_itemPool[j];
            if (item.IsConstant())
                continue;
            if (!valueCB(item))
                return;
        }
    }
}
#endif

#if _WIN32
void HIDParser::ScanValues(const std::function<bool(const HIDMainItem& item, int32_t value)>& valueCB,
                           const uint8_t* data, size_t len) const
{
    if (m_status != ParserStatus::Done)
        return;

    ULONG dataLen = m_dataList.size();
    if (HidP_GetData(HidP_Input, m_dataList.data(), &dataLen,
                     m_descriptorData, PCHAR(data), len) != HIDP_STATUS_SUCCESS)
        return;

    int idx = 0;
    auto it = m_dataList.begin();
    auto end = m_dataList.begin() + dataLen;
    for (const HIDMainItem& item : m_itemPool)
    {
        if (item.IsConstant())
            continue;
        int32_t value = 0;
        if (it != end)
        {
            const HIDP_DATA& data = *it;
            if (data.DataIndex == idx)
            {
                value = data.RawValue;
                ++it;
            }
        }
        if (!valueCB(item, value))
            return;
        ++idx;
    }
}
#else

class BitwiseIterator
{
    const uint8_t*& m_it;
    const uint8_t* m_end;
    int m_bit = 0;
public:
    BitwiseIterator(const uint8_t*& it, const uint8_t* end)
    : m_it(it), m_end(end) {}

    uint32_t GetUnsignedValue(int numBits, HIDParser::ParserStatus& status)
    {
        uint32_t val = 0;
        for (int i=0 ; i<numBits ;)
        {
            if (m_it >= m_end)
            {
                status = HIDParser::ParserStatus::Error;
                return 0;
            }
            int remBits = std::min(8 - m_bit, numBits - i);
            val |= uint32_t((*m_it >> m_bit) & ((1 << remBits) - 1)) << i;
            i += remBits;
            m_bit += remBits;
            if (m_bit == 8)
            {
                m_bit = 0;
                AdvanceIt(m_it, m_end, 1);
            }
        }
        return val;
    }
};

void HIDParser::ScanValues(const std::function<bool(const HIDMainItem& item, int32_t value)>& valueCB,
                           const uint8_t* data, size_t len) const
{
    if (m_status != ParserStatus::Done)
        return;

    auto it = data;
    auto end = data + len;
    ParserStatus status = ParserStatus::OK;
    uint8_t reportId = 0;
    if (m_multipleReports)
        reportId = GetByteValue(it, end, status);
    if (status == ParserStatus::Error)
        return;

    BitwiseIterator bitIt(it, end);

    for (uint32_t i=m_inputReports.first ; i<m_inputReports.second ; ++i)
    {
        const Report& rep = m_reportPool[i];
        if (rep.first != reportId)
            continue;
        for (uint32_t j=rep.second.first ; j<rep.second.second ; ++j)
        {
            const HIDMainItem& item = m_itemPool[j];
            int32_t val = bitIt.GetUnsignedValue(item.m_reportSize, status);
            if (status == ParserStatus::Error)
                return;
            if (item.IsConstant())
                continue;
            if (!valueCB(item, val))
                return;
        }
        break;
    }
}
#endif

}

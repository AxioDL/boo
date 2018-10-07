#pragma once

#include "boo/System.hpp"
#include <vector>
#include <stack>
#include <functional>
#include <memory>

#if _WIN32
#include <hidsdi.h>
#endif

namespace boo
{
struct HIDItemState;
struct HIDCollectionItem;
struct HIDReports;

enum class HIDUsagePage : uint8_t
{
    Undefined = 0,
    GenericDesktop = 1,
    Simulation = 2,
    VR = 3,
    Sport = 4,
    Game = 5,
    GenericDevice = 6,
    Keyboard = 7,
    LEDs = 8,
    Button = 9,
    Ordinal = 10,
    Telephony = 11,
    Consumer = 12,
    Digitizer = 13
};

enum class HIDUsage : uint8_t
{
    Undefined = 0,

    /* Generic Desktop */
    Pointer = 1,
    Mouse = 2,
    Reserved = 3,
    Joystick = 4,
    GamePad = 5,
    Keyboard = 6,
    Keypad = 7,
    MultiAxis = 8,
    TabletPC = 9,
    X = 0x30,
    Y = 0x31,
    Z = 0x32,
    Rx = 0x33,
    Ry = 0x34,
    Rz = 0x35,
    Slider = 0x36,
    Dial = 0x37,
    Wheel = 0x38,
    HatSwitch = 0x39,
    CountedBuffer = 0x3a,
    ByteCount = 0x3b,
    MotionWakeup = 0x3c,
    Start = 0x3d,
    Select = 0x3e,
    Vx = 0x40,
    Vy = 0x41,
    Vz = 0x42,
    Vbrx = 0x43,
    Vbry = 0x44,
    Vbrz = 0x45,
    Vno = 0x46,
    FeatureNotification = 0x47,
    ResolutionMultiplier = 0x48,
    SystemControl = 0x80,
    SystemPowerDown = 0x81,
    SystemSleep = 0x82,
    SystemWakeUp = 0x83,
    SystemContextMenu = 0x84,
    SystemMainMenu = 0x85,
    SystemAppMenu = 0x86,
    SystemMenuHelp = 0x87,
    SystemMenuExit = 0x88,
    SystemMenuSelect = 0x89,
    SystemMenuRight = 0x8a,
    SystemMenuLeft = 0x8b,
    SystemMenuUp = 0x8c,
    SystemMenuDown = 0x8d,
    SystemColdRestart = 0x8e,
    SystemWarmRestart = 0x8f,
    DPadUp = 0x90,
    DPadDown = 0x91,
    DPadRight = 0x92,
    DPadLeft = 0x93,
    SystemDock = 0xa0,
    SystemUndock = 0xa1,
    SystemSetup = 0xa2,
    SystemBreak = 0xa3,
    SystemDebuggerBreak = 0xa4,
    ApplicationBreak = 0xa5,
    ApplicationDebuggerBreak = 0xa6,
    SystemSpeakerMute = 0xa7,
    SystemHibernate = 0xa8,
    SystemDisplayInvert = 0xb0,
    SystemDisplayInternal = 0xb1,
    SystemDisplayExternal = 0xb2,
    SystemDisplayBoth = 0xb3,
    SystemDisplayDual = 0xb4,
    SystemDisplayToggleIntExt = 0xb5,

    /* Game Controls */
    _3DGameController = 0x1,
    PinballDevice = 0x2,
    GunDevice = 0x3,
    PointOfView = 0x20,
    TurnLeftRight = 0x21,
    PitchForwardBackward = 0x22,
    RollRightLeft = 0x23,
    MoveRightLeft = 0x24,
    MoveForwardBackward = 0x25,
    MoveUpDown = 0x26,
    LeanLeftRight = 0x27,
    LeanForwardBackward = 0x28,
    HeightOfPOV = 0x29,
    Flipper = 0x2a,
    SecondaryFlipper = 0x2b,
    Bump = 0x2c,
    NewGame = 0x2d,
    ShootBall = 0x2e,
    Player = 0x2f,
    GunBolt = 0x30,
    GunClip = 0x31,
    GunSelector = 0x32,
    GunSingleShot = 0x33,
    GunBurst = 0x34,
    GunAutomatic = 0x35,
    GunSafety = 0x36,
    GamepadFireJump = 0x37,
    GamepadTrigger = 0x39,
};

using HIDRange = std::pair<int32_t, int32_t>;

/* [6.2.2.5] Input, Output, and Feature Items */
struct HIDMainItem
{
    uint16_t m_flags;
    HIDUsagePage m_usagePage;
    HIDUsage m_usage;
    HIDRange m_logicalRange;
    int32_t m_reportSize;
    bool IsConstant() const { return (m_flags & 0x1) != 0; }
    bool IsVariable() const { return (m_flags & 0x2) != 0; }
    bool IsRelative() const { return (m_flags & 0x4) != 0; }
    bool IsWrap() const { return (m_flags & 0x8) != 0; }
    bool IsNonlinear() const { return (m_flags & 0x10) != 0; }
    bool IsNoPreferred() const { return (m_flags & 0x20) != 0; }
    bool IsNullState() const { return (m_flags & 0x40) != 0; }
    bool IsVolatile() const { return (m_flags & 0x80) != 0; }
    bool IsBufferedBytes() const { return (m_flags & 0x100) != 0; }

    HIDMainItem() = default;
    HIDMainItem(uint32_t flags, const HIDItemState& state, uint32_t reportIdx);
    HIDMainItem(uint32_t flags, HIDUsagePage usagePage, HIDUsage usage,
                HIDRange logicalRange, int32_t reportSize);
    const char* GetUsagePageName() const;
    const char* GetUsageName() const;
};

class HIDParser
{
public:
    enum class ParserStatus
    {
        OK,
        Done,
        Error
    };
private:

    ParserStatus m_status = ParserStatus::OK;
#if _WIN32
#if !WINDOWS_STORE
    std::vector<HIDMainItem> m_itemPool;
    mutable std::vector<HIDP_DATA> m_dataList;
    PHIDP_PREPARSED_DATA m_descriptorData = nullptr;
#endif
#else
    std::unique_ptr<HIDMainItem[]> m_itemPool;
    using Report = std::pair<uint32_t, std::pair<uint32_t, uint32_t>>;
    std::unique_ptr<Report[]> m_reportPool;
    std::pair<uint32_t, uint32_t> m_inputReports = {};
    std::pair<uint32_t, uint32_t> m_outputReports = {};
    std::pair<uint32_t, uint32_t> m_featureReports = {};
    bool m_multipleReports = false;
    static ParserStatus ParseItem(HIDReports& reportsOut,
                                  std::stack<HIDItemState>& stateStack,
                                  std::stack<HIDCollectionItem>& collectionStack,
                                  const uint8_t*& it, const uint8_t* end,
                                  bool& multipleReports);
#endif

public:
#if _WIN32
#if !WINDOWS_STORE
    ParserStatus Parse(const PHIDP_PREPARSED_DATA descriptorData);
#endif
#else
    ParserStatus Parse(const uint8_t* descriptorData, size_t len);
    static size_t CalculateMaxInputReportSize(const uint8_t* descriptorData, size_t len);
    static std::pair<HIDUsagePage, HIDUsage> GetApplicationUsage(const uint8_t* descriptorData, size_t len);
#endif
    operator bool() const { return m_status == ParserStatus::Done; }
    void EnumerateValues(const std::function<bool(const HIDMainItem& item)>& valueCB) const;
    void ScanValues(const std::function<bool(const HIDMainItem& item, int32_t value)>& valueCB,
                    const uint8_t* data, size_t len) const;
};

}


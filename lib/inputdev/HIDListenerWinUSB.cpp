#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#include "boo/inputdev/IHIDListener.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include "boo/inputdev/XInputPad.hpp"
#include <cstring>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

#include <initguid.h>
#include <SetupAPI.h>
#include <Cfgmgr32.h>
#include <Usbiodef.h>
#include <Devpkey.h>
#include <hidclass.h>
#include <Xinput.h>

namespace boo
{

class HIDListenerWinUSB final : public IHIDListener
{
    DeviceFinder& m_finder;

    bool m_scanningEnabled;

    /*
     * Reference: https://github.com/pbatard/libwdi/blob/master/libwdi/libwdi.c
     */

    void _enumerate(DeviceType type, CONST GUID* TypeGUID, const char* pathFilter)
    {
        /* Don't ask */
        static const LPCSTR arPrefix[3] = {"VID_", "PID_", "MI_"};
        unsigned i, j;
        CONFIGRET r;
        ULONG devpropType;
        DWORD reg_type;
        HDEVINFO hDevInfo = 0;
        SP_DEVINFO_DATA DeviceInfoData = {0};
        DeviceInfoData.cbSize = sizeof(DeviceInfoData);
        SP_DEVICE_INTERFACE_DATA DeviceInterfaceData = {0};
        DeviceInterfaceData.cbSize = sizeof(DeviceInterfaceData);
        union {
            SP_DEVICE_INTERFACE_DETAIL_DATA_A wtf;
            CHAR alloc[2048];
        } DeviceInterfaceDetailData; /* Stack allocation should be fine for this */
        DeviceInterfaceDetailData.wtf.cbSize = sizeof(DeviceInterfaceDetailData);
        CHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
        LPSTR pszToken, pszNextToken;
        CHAR szVid[MAX_DEVICE_ID_LEN], szPid[MAX_DEVICE_ID_LEN], szMi[MAX_DEVICE_ID_LEN];

        /* List all connected HID devices */
        hDevInfo = SetupDiGetClassDevs(NULL, 0, 0, DIGCF_PRESENT | DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE);
        if (hDevInfo == INVALID_HANDLE_VALUE)
            return;

        for (i=0 ; ; ++i)
        {
            if (!SetupDiEnumDeviceInterfaces(hDevInfo,
                                             NULL,
                                             TypeGUID,
                                             i,
                                             &DeviceInterfaceData))
                break;

            DeviceInterfaceDetailData.wtf.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            if (!SetupDiGetDeviceInterfaceDetailA(hDevInfo,
                                                  &DeviceInterfaceData,
                                                  &DeviceInterfaceDetailData.wtf,
                                                  sizeof(DeviceInterfaceDetailData),
                                                  NULL,
                                                  &DeviceInfoData))
                continue;

            r = CM_Get_Device_IDA(DeviceInfoData.DevInst, szDeviceInstanceID, MAX_PATH, 0);
            if (r != CR_SUCCESS)
                continue;

            /* Retreive the device description as reported by the device itself */
            pszToken = strtok_s(szDeviceInstanceID , "\\#&", &pszNextToken);
            szVid[0] = '\0';
            szPid[0] = '\0';
            szMi[0] = '\0';
            while (pszToken != NULL)
            {
                for (j=0 ; j<3 ; ++j)
                {
                    if (strncmp(pszToken, arPrefix[j], 4) == 0)
                    {
                        switch (j)
                        {
                            case 0:
                                strcpy_s(szVid, MAX_DEVICE_ID_LEN, pszToken);
                                break;
                            case 1:
                                strcpy_s(szPid, MAX_DEVICE_ID_LEN, pszToken);
                                break;
                            case 2:
                                strcpy_s(szMi, MAX_DEVICE_ID_LEN, pszToken);
                                break;
                            default:
                                break;
                        }
                    }
                }
                pszToken = strtok_s(NULL, "\\#&", &pszNextToken);
            }

            if (!szVid[0] || !szPid[0])
                continue;

            unsigned vid = strtol(szVid+4, NULL, 16);
            unsigned pid = strtol(szPid+4, NULL, 16);

            CHAR productW[1024] = {0};
            //CHAR product[1024] = {0};
            DWORD productSz = 0;
            if (!SetupDiGetDevicePropertyW(hDevInfo, &DeviceInfoData, &DEVPKEY_Device_BusReportedDeviceDesc,
                                           &devpropType, (BYTE*)productW, 1024, &productSz, 0)) {
                /* fallback to SPDRP_DEVICEDESC (USB hubs still use it) */
                SetupDiGetDeviceRegistryPropertyA(hDevInfo, &DeviceInfoData, SPDRP_DEVICEDESC,
                                                  &reg_type, (BYTE*)productW, 1024, &productSz);
            }
            /* DAFUQ??? Why isn't this really WCHAR??? */
            //WideCharToMultiByte(CP_UTF8, 0, productW, -1, product, 1024, nullptr, nullptr);

            WCHAR manufW[1024] = L"Someone"; /* Windows Vista and earlier will use this as the vendor */
            CHAR manuf[1024] = {0};
            DWORD manufSz = 0;
            SetupDiGetDevicePropertyW(hDevInfo, &DeviceInfoData, &DEVPKEY_Device_Manufacturer,
                                      &devpropType, (BYTE*)manufW, 1024, &manufSz, 0);
            WideCharToMultiByte(CP_UTF8, 0, manufW, -1, manuf, 1024, nullptr, nullptr);

            if (type == DeviceType::HID)
            {
                HANDLE devHnd = CreateFileA(DeviceInterfaceDetailData.wtf.DevicePath,
                                            GENERIC_WRITE | GENERIC_READ,
                                            FILE_SHARE_WRITE | FILE_SHARE_READ,
                                            NULL,
                                            OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                            NULL);
                if (INVALID_HANDLE_VALUE == devHnd)
                    continue;
                PHIDP_PREPARSED_DATA preparsedData;
                if (!HidD_GetPreparsedData(devHnd, &preparsedData))
                {
                    CloseHandle(devHnd);
                    continue;
                }
                HIDP_CAPS caps;
                HidP_GetCaps(preparsedData, &caps);
                HidD_FreePreparsedData(preparsedData);
                CloseHandle(devHnd);
                /* Filter non joysticks and gamepads */
                if (caps.UsagePage != 1 || (caps.Usage != 4 && caps.Usage != 5))
                    continue;
            }

            /* Store as a shouting string (to keep hash-lookups consistent) */
            CharUpperA(DeviceInterfaceDetailData.wtf.DevicePath);

            /* Filter to specific device (provided by hotplug event) */
            if (pathFilter && strcmp(pathFilter, DeviceInterfaceDetailData.wtf.DevicePath))
                continue;

            if (!m_scanningEnabled || m_finder._hasToken(DeviceInterfaceDetailData.wtf.DevicePath))
                continue;

            /* Whew!! that's a single device enumerated!! */
            m_finder._insertToken(std::make_unique<DeviceToken>(
                                  type, vid, pid, manuf, productW,
                                  DeviceInterfaceDetailData.wtf.DevicePath));
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    void _pollDevices(const char* pathFilter)
    {
        _enumerate(DeviceType::HID, &GUID_DEVINTERFACE_HID, pathFilter);
        _enumerate(DeviceType::USB, &GUID_DEVINTERFACE_USB_DEVICE, pathFilter);
    }

    static XInputPadState ConvertXInputState(const XINPUT_GAMEPAD& pad)
    {
        return {pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger,
                pad.sThumbLX, pad.sThumbLY, pad.sThumbLY, pad.sThumbRY};
    }

    std::thread m_xinputThread;
    bool m_xinputRunning = true;
    DWORD m_xinputPackets[4] = {DWORD(-1), DWORD(-1), DWORD(-1), DWORD(-1)};
    std::vector<DeviceToken> m_xinputTokens;
    void _xinputProc()
    {
        m_xinputTokens.reserve(4);
        for (int i=0 ; i<4 ; ++i)
            m_xinputTokens.emplace_back(DeviceType::XInput, 0, i, "", "", "");

        while (m_xinputRunning)
        {
            for (int i=0 ; i<4 ; ++i)
            {
                DeviceToken& tok = m_xinputTokens[i];
                XINPUT_STATE state;
                if (XInputGetState(i, &state) == ERROR_SUCCESS)
                {
                    if (state.dwPacketNumber != m_xinputPackets[i])
                    {
                        if (m_xinputPackets[i] == -1)
                            m_finder.deviceConnected(tok);
                        m_xinputPackets[i] = state.dwPacketNumber;
                        if (tok.m_connectedDev)
                        {
                            XInputPad& pad = static_cast<XInputPad&>(*tok.m_connectedDev);
                            std::lock_guard<std::mutex> lk(pad.m_callbackLock);
                            if (pad.m_callback)
                                pad.m_callback->controllerUpdate(pad, ConvertXInputState(state.Gamepad));
                        }
                    }
                    if (tok.m_connectedDev)
                    {
                        XInputPad& pad = static_cast<XInputPad&>(*tok.m_connectedDev);
                        if (pad.m_rumbleRequest[0] != pad.m_rumbleState[0] ||
                            pad.m_rumbleRequest[1] != pad.m_rumbleState[1])
                        {
                            pad.m_rumbleState[0] = pad.m_rumbleRequest[0];
                            pad.m_rumbleState[1] = pad.m_rumbleRequest[1];
                            XINPUT_VIBRATION vibe = {pad.m_rumbleRequest[0], pad.m_rumbleRequest[1]};
                            XInputSetState(i, &vibe);
                        }
                    }
                }
                else if (m_xinputPackets[i] != -1)
                {
                    m_xinputPackets[i] = -1;
                    if (tok.m_connectedDev)
                    {
                        XInputPad& pad = static_cast<XInputPad&>(*tok.m_connectedDev);
                        pad.deviceDisconnected();
                    }
                    m_finder.deviceDisconnected(tok, tok.m_connectedDev.get());
                }
            }
            Sleep(10);
        }
    }

public:
    HIDListenerWinUSB(DeviceFinder& finder)
    : m_finder(finder)
    {
        /* Initial HID Device Add */
        _pollDevices(nullptr);

        /* XInput arbitration thread */
        for (const DeviceSignature* sig : m_finder.getTypes())
        {
            if (sig->m_type == DeviceType::XInput)
            {
                m_xinputThread = std::thread(std::bind(&HIDListenerWinUSB::_xinputProc, this));
                break;
            }
        }
    }

    ~HIDListenerWinUSB()
    {
        m_xinputRunning = false;
        if (m_xinputThread.joinable())
            m_xinputThread.join();
    }

    /* Automatic device scanning */
    bool startScanning()
    {
        m_scanningEnabled = true;
        return true;
    }
    bool stopScanning()
    {
        m_scanningEnabled = false;
        return true;
    }

    /* Manual device scanning */
    bool scanNow()
    {
        _pollDevices(nullptr);
        return true;
    }

    bool _extDevConnect(const char* path)
    {
        char upperPath[1024];
        strcpy_s(upperPath, 1024, path);
        CharUpperA(upperPath);
        if (m_scanningEnabled && !m_finder._hasToken(upperPath))
            _pollDevices(upperPath);
        return true;
    }

    bool _extDevDisconnect(const char* path)
    {
        char upperPath[1024];
        strcpy_s(upperPath, 1024, path);
        CharUpperA(upperPath);
        m_finder._removeToken(upperPath);
        return true;
    }
};

std::unique_ptr<IHIDListener> IHIDListenerNew(DeviceFinder& finder)
{
    return std::make_unique<HIDListenerWinUSB>(finder);
}

}

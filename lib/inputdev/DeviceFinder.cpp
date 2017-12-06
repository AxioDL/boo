#include "boo/inputdev/DeviceFinder.hpp"

#if _WIN32
#include <Dbt.h>
#include <hidclass.h>
#include <usbiodef.h>
#endif

namespace boo
{

DeviceFinder* DeviceFinder::skDevFinder = nullptr;

#if _WIN32 && !WINDOWS_STORE
/* Windows-specific WM_DEVICECHANGED handler */
LRESULT DeviceFinder::winDevChangedHandler(WPARAM wParam, LPARAM lParam)
{
    PDEV_BROADCAST_HDR dbh = (PDEV_BROADCAST_HDR)lParam;
    PDEV_BROADCAST_DEVICEINTERFACE dbhi = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;
    DeviceFinder* finder = instance();
    if (!finder)
        return 0;

    if (wParam == DBT_DEVICEARRIVAL)
    {
        if (dbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
        {
            DeviceType type = DeviceType::None;
            if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_USB_DEVICE)
                type = DeviceType::USB;
            else if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_HID)
                type = DeviceType::HID;

            if (type != DeviceType::None)
            {
#ifdef UNICODE
                char devPath[1024];
                wcstombs(devPath, dbhi->dbcc_name, 1024);
                finder->m_listener->_extDevConnect(devPath);
#else
                finder->m_listener->_extDevConnect(dbhi->dbcc_name);
#endif
            }
        }
    }
    else if (wParam == DBT_DEVICEREMOVECOMPLETE)
    {
        if (dbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
        {
            DeviceType type = DeviceType::None;
            if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_USB_DEVICE)
                type = DeviceType::USB;
            else if (dbhi->dbcc_classguid == GUID_DEVINTERFACE_HID)
                type = DeviceType::HID;

            if (type != DeviceType::None)
            {
#ifdef UNICODE
                char devPath[1024];
                wcstombs(devPath, dbhi->dbcc_name, 1024);
                finder->m_listener->_extDevDisconnect(devPath);
#else
                finder->m_listener->_extDevDisconnect(dbhi->dbcc_name);
#endif
            }
        }
    }

    return 0;
}
#endif

}

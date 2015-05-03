
#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#else
#endif
#include <stdio.h>
#include <boo.hpp>
#if _WIN32
#define _WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <initguid.h>
#include <Usbiodef.h>
#include <Dbt.h>
#else
#include <unistd.h>
#endif

namespace boo
{

class CDolphinSmashAdapterCallback : public IDolphinSmashAdapterCallback
{
    void controllerConnected(unsigned idx, EDolphinControllerType type)
    {
        printf("CONTROLLER %u CONNECTED\n", idx);
    }
    void controllerDisconnected(unsigned idx, EDolphinControllerType type)
    {
        printf("CONTROLLER %u DISCONNECTED\n", idx);
    }
    void controllerUpdate(unsigned idx, EDolphinControllerType type,
                          const SDolphinControllerState& state)
    {
        printf("CONTROLLER %u UPDATE %d %d\n", idx, state.m_leftStick[0], state.m_leftStick[1]);
    }
};

class CTestDeviceFinder : public CDeviceFinder
{
    CDolphinSmashAdapter* smashAdapter = NULL;
    CDolphinSmashAdapterCallback m_cb;
public:
    CTestDeviceFinder()
        : CDeviceFinder({"CDolphinSmashAdapter"})
    {}
    void deviceConnected(CDeviceToken& tok)
    {
        smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(tok.openAndGetDevice());
        smashAdapter->setCallback(&m_cb);
        smashAdapter->startRumble(0);
    }
    void deviceDisconnected(CDeviceToken&, CDeviceBase* device)
    {
        if (smashAdapter == device)
        {
            delete smashAdapter;
            smashAdapter = NULL;
        }
    }
};

}

int main(int argc, char** argv)
{

    boo::CTestDeviceFinder finder;
    finder.startScanning();
    
#if 0
    boo::IGraphicsContext* ctx = new boo::CGraphicsContext;

    if (ctx->create())
    {
    }
#endif
    
#if __APPLE__
    CFRunLoopRun();
#elif _WIN32

    /* Register hotplug notification with windows */
    DEV_BROADCAST_DEVICEINTERFACE_A hotplugConf =
    {
        sizeof(DEV_BROADCAST_DEVICEINTERFACE_A),
        DBT_DEVTYP_DEVICEINTERFACE,
        0,
        GUID_DEVINTERFACE_USB_DEVICE
    };
    HWND consoleWnd = GetConsoleWindow();
    HDEVNOTIFY notHandle = RegisterDeviceNotificationA(consoleWnd, &hotplugConf, DEVICE_NOTIFY_WINDOW_HANDLE);

    MSG recvMsg;
    while (GetMessage(&recvMsg, consoleWnd, 0, 0))
    {
        printf("MSG: %d\n", recvMsg.message);
        switch (recvMsg.message)
        {
        case WM_DEVICECHANGE:
            printf("DEVICECHANGE!!\n");
            break;

        default:
            TranslateMessage(&recvMsg);
            DispatchMessage(&recvMsg);
            break;
        }
    }

#else
    while (true) {sleep(1);}
#endif

    //delete ctx;
    return 0;
}

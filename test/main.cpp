#define _CRT_SECURE_NO_WARNINGS 1

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
#else
#include <unistd.h>
#endif

namespace boo
{

class CDolphinSmashAdapterCallback : public IDolphinSmashAdapterCallback
{
    void controllerConnected(unsigned idx, EDolphinControllerType)
    {
        printf("CONTROLLER %u CONNECTED\n", idx);
    }
    void controllerDisconnected(unsigned idx, EDolphinControllerType)
    {
        printf("CONTROLLER %u DISCONNECTED\n", idx);
    }
    void controllerUpdate(unsigned idx, EDolphinControllerType,
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
        : CDeviceFinder({typeid(CDolphinSmashAdapter)})
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

#if _WIN32

/* This simple 'test' console app needs a full windows
 * message loop for receiving device connection events
 */
static const DEV_BROADCAST_DEVICEINTERFACE_A HOTPLUG_CONF =
{
    sizeof(DEV_BROADCAST_DEVICEINTERFACE_A),
    DBT_DEVTYP_DEVICEINTERFACE,
    0,
    GUID_DEVINTERFACE_USB_DEVICE
};

LRESULT CALLBACK WindowProc(
  _In_ HWND   hwnd,
  _In_ UINT   uMsg,
  _In_ WPARAM wParam,
  _In_ LPARAM lParam
)
{
    switch (uMsg)
    {
    case WM_CREATE:
        /* Register hotplug notification with windows */
        RegisterDeviceNotificationA(hwnd, (LPVOID)&HOTPLUG_CONF, DEVICE_NOTIFY_WINDOW_HANDLE);
        return 0;

    case WM_DEVICECHANGE:
        return boo::CDeviceFinder::winDevChangedHandler(wParam, lParam);

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int APIENTRY wWinMain(
  _In_ HINSTANCE hInstance,
  _In_ HINSTANCE,
  _In_ LPTSTR,
  _In_ int
)
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);

    WNDCLASS wndClass =
    {
        0,
        WindowProc,
        0,
        0,
        hInstance,
        0,
        0,
        0,
        0,
        L"BooTestWindow"
    };

    RegisterClassW(&wndClass);

    boo::CTestDeviceFinder finder;
    finder.startScanning();

    HWND hwnd = CreateWindowW(L"BooTestWindow", L"BooTest", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              NULL, NULL, hInstance, NULL);

    /* Pump messages */
    MSG msg = {0};
    while (GetMessage(&msg, hwnd, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

}

#else

int main(int, char**)
{

    boo::CTestDeviceFinder finder;
    finder.startScanning();
    
    boo::IWindow* window = boo::IWindowNew();
    (void)window;
    
#if __APPLE__
    CFRunLoopRun();
#endif

    //delete ctx;
    return 0;
}

#endif

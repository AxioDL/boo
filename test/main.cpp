#define _CRT_SECURE_NO_WARNINGS 1

#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#else
#endif
#include <stdio.h>
#include <iostream>
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
        printf("                     %d %d\n", state.m_rightStick[0], state.m_rightStick[1]);
    }
};

class CDualshockControllerCallback : public IDualshockControllerCallback
{
    void controllerDisconnected()
    {
        printf("CONTROLLER DISCONNECTED\n");
    }
    void controllerUpdate(const SDualshockControllerState& state)
    {
        static time_t timeTotal;
        static time_t lastTime = 0;
        timeTotal = time(NULL);
        time_t timeDif = timeTotal - lastTime;
        /*
        if (timeDif >= .15)
        {
            uint8_t led = ctrl->getLED();
            led *= 2;
            if (led > 0x10)
                led = 2;
            ctrl->setRawLED(led);
            lastTime = timeTotal;
        }
        */
        if (state.m_psButtonState)
        {
            if (timeDif >= 1) // wait 30 seconds before issuing another rumble event
            {
                std::cout << "RUMBLE" << std::endl;
                ctrl->startRumble(DS3_MOTOR_LEFT);
                ctrl->startRumble(DS3_MOTOR_RIGHT, 100);
                lastTime = timeTotal;
            }
        }
        /*
        else
            ctrl->stopRumble(DS3_MOTOR_RIGHT | DS3_MOTOR_LEFT);*/

        printf("CONTROLLER UPDATE %d %d\n", state.m_leftStick[0], state.m_leftStick[1]);
        printf("                  %d %d\n", state.m_rightStick[0], state.m_rightStick[1]);
        printf("                  %f %f %f\n", state.accPitch, state.accYaw, state.gyroZ);
    }
};

class CTestDeviceFinder : public CDeviceFinder
{
    CDolphinSmashAdapter* smashAdapter = NULL;
    CDualshockController* ds3 = nullptr;
    CDolphinSmashAdapterCallback m_cb;
    CDualshockControllerCallback m_ds3CB;
public:
    CTestDeviceFinder()
        : CDeviceFinder({"CDolphinSmashAdapter",
                        "CDualshockController"})
    {}
    void deviceConnected(CDeviceToken& tok)
    {
        smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(tok.openAndGetDevice());
        if (smashAdapter)
        {
            smashAdapter->setCallback(&m_cb);
            smashAdapter->startRumble(0);
            return;
        }
        ds3 = dynamic_cast<CDualshockController*>(tok.openAndGetDevice());
        if (ds3)
        {
            ds3->setCallback(&m_ds3CB);
            ds3->setLED(DS3_LED_1);
        }
    }
    void deviceDisconnected(CDeviceToken&, CDeviceBase* device)
    {
        if (smashAdapter == device)
        {
            delete smashAdapter;
            smashAdapter = NULL;
        }
        if (ds3 == device)
        {
            delete ds3;
            ds3 = nullptr;
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
#endif

    while(1)
    {
    }
    //delete ctx;
    return 0;
}

#endif

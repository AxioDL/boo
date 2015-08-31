#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <shellapi.h>
#include <initguid.h>
#include <Usbiodef.h>

#include <unordered_map>

#include "boo/IApplication.hpp"
#include "boo/inputdev/DeviceFinder.hpp"

namespace boo
{
    
IWindow* _WindowWin32New(const SystemString& title);

class ApplicationWin32 final : public IApplication
{
    const IApplicationCallback& m_callback;
    const SystemString m_uniqueName;
    const SystemString m_friendlyName;
    const SystemString m_pname;
    const std::vector<SystemString> m_args;
    std::unordered_map<HWND, IWindow*> m_allWindows;
    bool m_singleInstance;
    
    void _deletedWindow(IWindow* window)
    {
        m_allWindows.erase(HWND(window->getPlatformHandle()));
    }
    
public:
    
    ApplicationWin32(const IApplicationCallback& callback,
                     const SystemString& uniqueName,
                     const SystemString& friendlyName,
                     const SystemString& pname,
                     const std::vector<SystemString>& args,
                     bool singleInstance)
    : m_callback(callback),
      m_uniqueName(uniqueName),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args),
      m_singleInstance(singleInstance)
    {}
    
    EPlatformType getPlatformType() const
    {
        return PLAT_WIN32;
    }
    
    LRESULT winHwndHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        /* Lookup boo window instance */
        IWindow* window = m_allWindows[hwnd];
        switch (uMsg)
        {
            case WM_CREATE:
                return 0;
                
            case WM_DEVICECHANGE:
                return DeviceFinder::winDevChangedHandler(wParam, lParam);
                
            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
    
    void pump()
    {
        /* Pump messages */
        MSG msg = {0};
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    const SystemString& getUniqueName() const
    {
        return m_uniqueName;
    }

    const SystemString& getFriendlyName() const
    {
        return m_friendlyName;
    }

    const SystemString& getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<SystemString>& getArgs() const
    {
        return m_args;
    }
    
    IWindow* newWindow(const SystemString& title)
    {
        IWindow* window = _WindowWin32New(title);
        HWND hwnd = HWND(window->getPlatformHandle());
        m_allWindows[hwnd] = window;
        return window;
    }
};
    
IApplication* APP = NULL;
std::unique_ptr<IApplication>
ApplicationBootstrap(IApplication::EPlatformType platform,
                     IApplicationCallback& cb,
                     const SystemString& uniqueName,
                     const SystemString& friendlyName,
                     const SystemString& pname,
                     const std::vector<SystemString>& args,
                     bool singleInstance)
{
    if (!APP)
    {
        if (platform != IApplication::PLAT_WIN32 &&
            platform != IApplication::PLAT_AUTO)
            return NULL;
        APP = new ApplicationWin32(cb, uniqueName, friendlyName, pname, args, singleInstance);
    }
    return std::unique_ptr<IApplication>(APP);
}

}

static const DEV_BROADCAST_DEVICEINTERFACE_A HOTPLUG_CONF =
{
    sizeof(DEV_BROADCAST_DEVICEINTERFACE_A),
    DBT_DEVTYP_DEVICEINTERFACE,
    0,
    GUID_DEVINTERFACE_USB_DEVICE
};
static bool HOTPLUG_REGISTERED = false;
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!HOTPLUG_REGISTERED && uMsg == WM_CREATE)
    {
        /* Register hotplug notification with windows */
        RegisterDeviceNotificationA(hwnd, (LPVOID)&HOTPLUG_CONF, DEVICE_NOTIFY_WINDOW_HANDLE);
        HOTPLUG_REGISTERED = true;
    }
    return static_cast<boo::ApplicationWin32*>(boo::APP)->winHwndHandler(hwnd, uMsg, wParam, lParam);
}

int wmain(int argc, wchar_t** argv);
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int)
{
#if DEBUG
    /* Debug console */
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
#endif
    
    /* One class for *all* boo windows */
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
        L"BooWindow"
    };
    
    RegisterClassW(&wndClass);
    
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    
    /* Call into the 'proper' entry point */
    return wmain(argc, argv);
    
}

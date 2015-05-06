#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#define _WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <initguid.h>
#include <Usbiodef.h>

#include <unordered_map>

#include "IRunLoop.hpp"
#include "inputdev/CDeviceFinder.hpp"

namespace boo
{
    
IWindow* _CWindowWin32New(const std::string& title);

class CApplicationWin32 final : public IApplication
{
    const IApplicationCallback& m_callback;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;
    std::unordered_map<HWND, IWindow*> m_allWindows;
    
    void _deletedWindow(IWindow* window)
    {
        m_allWindows.erase(window);
    }
    
public:
    
    CApplicationWin32(const IApplicationCallback& callback,
                      const std::string& friendlyName,
                      const std::string& pname,
                      const std::vector<std::string>& args)
    : m_callback(callback),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args)
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
                return CDeviceFinder::winDevChangedHandler(wParam, lParam);
                
            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
    
    void run()
    {
        /* Pump messages */
        MSG msg = {0};
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    const std::string& getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<std::string>& getArgs() const
    {
        return m_args;
    }
    
    IWindow* newWindow(const std::string& title)
    {
        IWindow* window = _CWindowWin32New(title);
        HWND hwnd = window->getPlatformHandle();
        m_allWindows[hwnd] = window;
    }
};
    
IApplication* APP = NULL;
IApplication* IApplicationBootstrap(IApplication::EPlatformType platform,
                                    IApplicationCallback& cb,
                                    const std::string& friendlyName,
                                    const std::string& pname,
                                    const std::vector<std::string>& args)
{
    if (!APP)
    {
        if (platform != IApplication::PLAT_WIN32 &&
            platform != IApplication::PLAT_AUTO)
            return NULL;
        APP = new CApplicationWin32(cb, friendlyName, pname, args);
    }
    return APP;
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
    if (!HOTPLUG_REGISTERED && hwnd == WM_CREATE)
    {
        /* Register hotplug notification with windows */
        RegisterDeviceNotificationA(hwnd, (LPVOID)&HOTPLUG_CONF, DEVICE_NOTIFY_WINDOW_HANDLE);
        HOTPLUG_REGISTERED = true;
    }
    return IRunLoopInstance()->winHwndHandler(hwnd, uMsg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPCWSTR lpCmdLine, int)
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
    return main(argc, argv);
    
}

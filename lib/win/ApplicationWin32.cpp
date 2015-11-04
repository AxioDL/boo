#include "Win32Common.hpp"
#include <shellapi.h>
#include <initguid.h>
#include <Usbiodef.h>

#if _DEBUG
#define D3D11_CREATE_DEVICE_FLAGS D3D11_CREATE_DEVICE_DEBUG
#else
#define D3D11_CREATE_DEVICE_FLAGS 0
#endif

#include <unordered_map>

#include "boo/System.hpp"
#include "boo/IApplication.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include <LogVisor/LogVisor.hpp>

namespace boo
{
static LogVisor::LogModule Log("ApplicationWin32");
    
IWindow* _WindowWin32New(const SystemString& title, D3DAppContext& d3dCtx);

class ApplicationWin32 final : public IApplication
{
    IApplicationCallback& m_callback;
    const SystemString m_uniqueName;
    const SystemString m_friendlyName;
    const SystemString m_pname;
    const std::vector<SystemString> m_args;
    std::unordered_map<HWND, IWindow*> m_allWindows;
    bool m_singleInstance;

    D3DAppContext m_d3dCtx;

    void _deletedWindow(IWindow* window)
    {
        m_allWindows.erase(HWND(window->getPlatformHandle()));
    }
    
public:
    
    ApplicationWin32(IApplicationCallback& callback,
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
    {
        HMODULE dxgilib = LoadLibraryW(L"dxgi.dll");
        if (!dxgilib)
            Log.report(LogVisor::FatalError, "unable to load dxgi.dll");

        typedef HRESULT(WINAPI*CreateDXGIFactory2PROC)(UINT Flags, REFIID riid, _COM_Outptr_ void **ppFactory);
        CreateDXGIFactory2PROC MyCreateDXGIFactory2 = (CreateDXGIFactory2PROC)GetProcAddress(dxgilib, "CreateDXGIFactory2");
        if (!MyCreateDXGIFactory2)
            Log.report(LogVisor::FatalError, "unable to find CreateDXGIFactory2 in DXGI.dll\n"
                                             "Windows 7 users should install \"Platform Update for Windows 7\" from Microsoft");

#if WINVER >= _WIN32_WINNT_WIN10
        HMODULE d3d12lib = LoadLibraryW(L"D3D12.dll");
        if (d3d12lib)
        {
            /* Create device */
            PFN_D3D12_CREATE_DEVICE MyD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12lib, "D3D12CreateDevice");
            if (!MyD3D12CreateDevice)
                Log.report(LogVisor::FatalError, "unable to find D3D12CreateDevice in D3D12.dll");

            /* Create device */
            if (FAILED(MyD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), &m_d3dCtx.m_ctx12.m_dev)))
                Log.report(LogVisor::FatalError, "unable to create D3D12 device");

            /* Obtain DXGI Factory */
            ComPtr<IDXGIDevice2> device;
            ComPtr<IDXGIAdapter> adapter;
            m_d3dCtx.m_ctx12.m_dev.As<IDXGIDevice2>(&device);
            device->GetParent(__uuidof(IDXGIAdapter), &adapter);
            adapter->GetParent(__uuidof(IDXGIFactory2), &m_d3dCtx.m_dxFactory);
            
            return;
        }
#endif
        HMODULE d3d11lib = LoadLibraryW(L"D3D11.dll");
        if (d3d11lib)
        {
            /* Create device proc */
            PFN_D3D11_CREATE_DEVICE MyD3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11lib, "D3D11CreateDevice");
            if (!MyD3D11CreateDevice)
                Log.report(LogVisor::FatalError, "unable to find D3D11CreateDevice in D3D11.dll");

            /* Create device */
            D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_1;
            ComPtr<ID3D11Device> tempDev;
            ComPtr<ID3D11DeviceContext> tempCtx;
            if (FAILED(MyD3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_FLAGS, &level, 
                                           1, D3D11_SDK_VERSION, &tempDev, nullptr, &tempCtx)))
                Log.report(LogVisor::FatalError, "unable to create D3D11.1 device");
            tempDev.As<ID3D11Device1>(&m_d3dCtx.m_ctx11.m_dev);
            tempCtx.As<ID3D11DeviceContext1>(&m_d3dCtx.m_ctx11.m_devCtx);

            /* Obtain DXGI Factory */
            ComPtr<IDXGIDevice2> device;
            ComPtr<IDXGIAdapter> adapter;
            m_d3dCtx.m_ctx11.m_dev.As<IDXGIDevice2>(&device);
            device->GetParent(__uuidof(IDXGIAdapter), &adapter);
            adapter->GetParent(__uuidof(IDXGIFactory2), &m_d3dCtx.m_dxFactory);

            return;
        }

        Log.report(LogVisor::FatalError, "system doesn't support D3D11.1 or D3D12");
    }
    
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
                
            case WM_SIZE:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_MOUSEMOVE:
                window->_incomingEvent(&HWNDEvent(uMsg, wParam, lParam));

            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
    
    int run()
    {
        /* Spawn client thread */
        int clientReturn = 0;
        std::thread clientThread([&]()
        {clientReturn = m_callback.appMain(this);});
        
        /* Pump messages */
        MSG msg = {0};
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        m_callback.appQuitting(this);
        clientThread.join();
        return clientReturn;
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
        IWindow* window = _WindowWin32New(title, m_d3dCtx);
        HWND hwnd = HWND(window->getPlatformHandle());
        m_allWindows[hwnd] = window;
        return window;
    }
};
    
IApplication* APP = NULL;
int ApplicationRun(IApplication::EPlatformType platform,
                   IApplicationCallback& cb,
                   const SystemString& uniqueName,
                   const SystemString& friendlyName,
                   const SystemString& pname,
                   const std::vector<SystemString>& args,
                   bool singleInstance)
{
    if (APP)
        return 1;
    if (platform != IApplication::PLAT_WIN32 &&
        platform != IApplication::PLAT_AUTO)
        return 1;
    APP = new ApplicationWin32(cb, uniqueName, friendlyName, pname, args, singleInstance);
    return APP->run();
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
#if _DEBUG
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

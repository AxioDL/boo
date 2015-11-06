#include "Win32Common.hpp"
#include <shellapi.h>
#include <initguid.h>
#include <Usbiodef.h>

#if _DEBUG
#define DXGI_CREATE_FLAGS DXGI_CREATE_FACTORY_DEBUG
#define D3D11_CREATE_DEVICE_FLAGS D3D11_CREATE_DEVICE_DEBUG
#else
#define DXGI_CREATE_FLAGS 0
#define D3D11_CREATE_DEVICE_FLAGS 0
#endif

#include <unordered_map>

#include "boo/System.hpp"
#include "boo/IApplication.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include <LogVisor/LogVisor.hpp>

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#if _WIN32_WINNT_WIN10
PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignaturePROC = nullptr;
#endif

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

#if _WIN32_WINNT_WIN10
        HMODULE d3d12lib = LoadLibraryW(L"D3D12.dll");
        if (d3d12lib)
        {
#if _DEBUG
            {
                PFN_D3D12_GET_DEBUG_INTERFACE MyD3D12GetDebugInterface = 
                (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(d3d12lib, "D3D12GetDebugInterface");
                ComPtr<ID3D12Debug> debugController;
                if (SUCCEEDED(MyD3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
                {
                    debugController->EnableDebugLayer();
                }
            }
#endif
            
            D3D12SerializeRootSignaturePROC = 
            (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(d3d12lib, "D3D12SerializeRootSignature");
            
            /* Create device */
            PFN_D3D12_CREATE_DEVICE MyD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12lib, "D3D12CreateDevice");
            if (!MyD3D12CreateDevice)
                Log.report(LogVisor::FatalError, "unable to find D3D12CreateDevice in D3D12.dll");

            /* Create device */
            HRESULT hr = MyD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), &m_d3dCtx.m_ctx12.m_dev);
            if (FAILED(hr))
                Log.report(LogVisor::FatalError, "unable to create D3D12 device");

            /* Obtain DXGI Factory */
            hr = MyCreateDXGIFactory2(DXGI_CREATE_FLAGS, __uuidof(IDXGIFactory4), &m_d3dCtx.m_ctx12.m_dxFactory);
            if (FAILED(hr))
                Log.report(LogVisor::FatalError, "unable to create DXGI factory");

            /* Establish loader objects */
            if (FAILED(m_d3dCtx.m_ctx12.m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
                __uuidof(ID3D12CommandAllocator), &m_d3dCtx.m_ctx12.m_loadqalloc)))
                Log.report(LogVisor::FatalError, "unable to create loader allocator");

            D3D12_COMMAND_QUEUE_DESC desc = 
            {
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                D3D12_COMMAND_QUEUE_FLAG_NONE
            };
            if (FAILED(m_d3dCtx.m_ctx12.m_dev->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), &m_d3dCtx.m_ctx12.m_loadq)))
                Log.report(LogVisor::FatalError, "unable to create loader queue");

            if (FAILED(m_d3dCtx.m_ctx12.m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), &m_d3dCtx.m_ctx12.m_loadfence)))
                Log.report(LogVisor::FatalError, "unable to create loader fence");

            m_d3dCtx.m_ctx12.m_loadfencehandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

            if (FAILED(m_d3dCtx.m_ctx12.m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_d3dCtx.m_ctx12.m_loadqalloc.Get(), 
                nullptr, __uuidof(ID3D12GraphicsCommandList), &m_d3dCtx.m_ctx12.m_loadlist)))
                Log.report(LogVisor::FatalError, "unable to create loader list");

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
            adapter->GetParent(__uuidof(IDXGIFactory2), &m_d3dCtx.m_ctx11.m_dxFactory);

            /* Build default sampler here */
            m_d3dCtx.m_ctx11.m_dev->CreateSamplerState(&CD3D11_SAMPLER_DESC(D3D11_DEFAULT), &m_d3dCtx.m_ctx11.m_ss);

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
            case WM_SYSKEYDOWN:
            case WM_KEYDOWN:
            case WM_SYSKEYUP:
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
    
    DWORD m_mainThreadId = 0;
    int run()
    {
        m_mainThreadId = GetCurrentThreadId();

        /* Spawn client thread */
        int clientReturn = 0;
        std::thread clientThread([&]()
        {clientReturn = m_callback.appMain(this);});
        
        /* Pump messages */
        MSG msg = {0};
        while (GetMessage(&msg, NULL, 0, 0))
        {
            if (msg.message == WM_USER)
            {
                /* New-window message (coalesced onto main thread) */
                std::unique_lock<std::mutex> lk(m_nwmt);
                const SystemString* title = reinterpret_cast<const SystemString*>(msg.wParam);
                m_mwret = newWindow(*title);
                lk.unlock();
                m_nwcv.notify_one();
                continue;
            }
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
    
    std::mutex m_nwmt;
    std::condition_variable m_nwcv;
    IWindow* m_mwret = nullptr;
    IWindow* newWindow(const SystemString& title)
    {
        if (GetCurrentThreadId() != m_mainThreadId)
        {
            std::unique_lock<std::mutex> lk(m_nwmt);
            if (!PostThreadMessage(m_mainThreadId, WM_USER, WPARAM(&title), 0))
                Log.report(LogVisor::FatalError, "PostThreadMessage error");
            m_nwcv.wait(lk);
            return m_mwret;
        }
        
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

    /* One class for *all* boo windows */
    WNDCLASS wndClass =
    {
        0,
        WindowProc,
        0,
        0,
        GetModuleHandle(nullptr),
        0,
        0,
        0,
        0,
        L"BooWindow"
    };

    RegisterClassW(&wndClass);

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


#include "Win32Common.hpp"
#include <shellapi.h>
#include <initguid.h>
#include <Usbiodef.h>
#include <winver.h>

#if _DEBUG
#define D3D11_CREATE_DEVICE_FLAGS D3D11_CREATE_DEVICE_DEBUG
#else
#define D3D11_CREATE_DEVICE_FLAGS 0
#endif

#include <unordered_map>

#include "boo/System.hpp"
#include "boo/IApplication.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include "boo/graphicsdev/D3D.hpp"
#include "logvisor/logvisor.hpp"

DWORD g_mainThreadId = 0;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#if _WIN32_WINNT_WIN10
PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignaturePROC = nullptr;
#endif
pD3DCompile D3DCompilePROC = nullptr;
pD3DCreateBlob D3DCreateBlobPROC = nullptr;

static bool FindBestD3DCompile()
{
    HMODULE d3dCompilelib = LoadLibraryW(L"D3DCompiler_47.dll");
    if (!d3dCompilelib)
    {
        d3dCompilelib = LoadLibraryW(L"D3DCompiler_46.dll");
        if (!d3dCompilelib)
        {
            d3dCompilelib = LoadLibraryW(L"D3DCompiler_45.dll");
            if (!d3dCompilelib)
            {
                d3dCompilelib = LoadLibraryW(L"D3DCompiler_44.dll");
                if (!d3dCompilelib)
                {
                    d3dCompilelib = LoadLibraryW(L"D3DCompiler_43.dll");
                }
            }
        }
    }
    if (d3dCompilelib)
    {
        D3DCompilePROC = (pD3DCompile)GetProcAddress(d3dCompilelib, "D3DCompile");
        D3DCreateBlobPROC = (pD3DCreateBlob)GetProcAddress(d3dCompilelib, "D3DCreateBlob");
        return D3DCompilePROC != nullptr && D3DCreateBlobPROC != nullptr;
    }
    return false;
}

namespace boo
{
static logvisor::Module Log("boo::ApplicationWin32");
Win32Cursors WIN32_CURSORS;

IWindow* _WindowWin32New(const SystemString& title, Boo3DAppContext& d3dCtx, uint32_t sampleCount);

class ApplicationWin32 final : public IApplication
{
    IApplicationCallback& m_callback;
    const SystemString m_uniqueName;
    const SystemString m_friendlyName;
    const SystemString m_pname;
    const std::vector<SystemString> m_args;
    std::unordered_map<HWND, IWindow*> m_allWindows;
    bool m_singleInstance;

    Boo3DAppContext m_3dCtx;

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
            Log.report(logvisor::Fatal, "unable to load dxgi.dll");

        typedef HRESULT(WINAPI*CreateDXGIFactory1PROC)(REFIID riid, _COM_Outptr_ void **ppFactory);
        CreateDXGIFactory1PROC MyCreateDXGIFactory1 = (CreateDXGIFactory1PROC)GetProcAddress(dxgilib, "CreateDXGIFactory1");
        if (!MyCreateDXGIFactory1)
            Log.report(logvisor::Fatal, "unable to find CreateDXGIFactory1 in DXGI.dll\n");

        bool no12 = false;
        bool noD3d = false;
        for (const SystemString& arg : args)
        {
            if (!arg.compare(L"--d3d11"))
                no12 = true;
            if (!arg.compare(L"--gl"))
                noD3d = true;
        }

#if _WIN32_WINNT_WIN10
        HMODULE d3d12lib = LoadLibraryW(L"D3D12.dll");
        if (!no12 && !noD3d && d3d12lib)
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
            if (!FindBestD3DCompile())
                Log.report(logvisor::Fatal, "unable to find D3DCompile_[43-47].dll");

            D3D12SerializeRootSignaturePROC =
            (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(d3d12lib, "D3D12SerializeRootSignature");

            /* Create device */
            PFN_D3D12_CREATE_DEVICE MyD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12lib, "D3D12CreateDevice");
            if (!MyD3D12CreateDevice)
                Log.report(logvisor::Fatal, "unable to find D3D12CreateDevice in D3D12.dll");

            /* Create device */
            HRESULT hr = MyD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), &m_3dCtx.m_ctx12.m_dev);
            if (FAILED(hr))
                Log.report(logvisor::Fatal, "unable to create D3D12 device");

            /* Obtain DXGI Factory */
            hr = MyCreateDXGIFactory1(__uuidof(IDXGIFactory2), &m_3dCtx.m_ctx12.m_dxFactory);
            if (FAILED(hr))
                Log.report(logvisor::Fatal, "unable to create DXGI factory");

            /* Establish loader objects */
            if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator), &m_3dCtx.m_ctx12.m_loadqalloc)))
                Log.report(logvisor::Fatal, "unable to create loader allocator");

            D3D12_COMMAND_QUEUE_DESC desc =
            {
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                D3D12_COMMAND_QUEUE_FLAG_NONE
            };
            if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), &m_3dCtx.m_ctx12.m_loadq)))
                Log.report(logvisor::Fatal, "unable to create loader queue");

            if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), &m_3dCtx.m_ctx12.m_loadfence)))
                Log.report(logvisor::Fatal, "unable to create loader fence");

            m_3dCtx.m_ctx12.m_loadfencehandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

            if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_3dCtx.m_ctx12.m_loadqalloc.Get(),
                nullptr, __uuidof(ID3D12GraphicsCommandList), &m_3dCtx.m_ctx12.m_loadlist)))
                Log.report(logvisor::Fatal, "unable to create loader list");

            Log.report(logvisor::Info, "initialized D3D12 renderer");
            return;
        }
#endif
        HMODULE d3d11lib = LoadLibraryW(L"D3D11.dll");
        if (d3d11lib && !noD3d)
        {
            if (!FindBestD3DCompile())
                Log.report(logvisor::Fatal, "unable to find D3DCompile_[43-47].dll");

            /* Create device proc */
            PFN_D3D11_CREATE_DEVICE MyD3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11lib, "D3D11CreateDevice");
            if (!MyD3D11CreateDevice)
                Log.report(logvisor::Fatal, "unable to find D3D11CreateDevice in D3D11.dll");

            /* Create device */
            D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
            ComPtr<ID3D11Device> tempDev;
            ComPtr<ID3D11DeviceContext> tempCtx;
            if (FAILED(MyD3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_FLAGS, &level,
                                           1, D3D11_SDK_VERSION, &tempDev, nullptr, &tempCtx)))
                Log.report(logvisor::Fatal, "unable to create D3D11 device");
            tempDev.As<ID3D11Device1>(&m_3dCtx.m_ctx11.m_dev);
            tempCtx.As<ID3D11DeviceContext1>(&m_3dCtx.m_ctx11.m_devCtx);

            /* Obtain DXGI Factory */
            ComPtr<IDXGIDevice2> device;
            ComPtr<IDXGIAdapter> adapter;
            m_3dCtx.m_ctx11.m_dev.As<IDXGIDevice2>(&device);
            device->GetParent(__uuidof(IDXGIAdapter), &adapter);
            adapter->GetParent(__uuidof(IDXGIFactory2), &m_3dCtx.m_ctx11.m_dxFactory);

            /* Build default sampler here */
            CD3D11_SAMPLER_DESC sampDesc(D3D11_DEFAULT);
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss);

            Log.report(logvisor::Info, "initialized D3D11 renderer");
            return;
        }

        /* Finally try OpenGL */
        {
            /* Obtain DXGI Factory */
            HRESULT hr = MyCreateDXGIFactory1(__uuidof(IDXGIFactory1), &m_3dCtx.m_ctxOgl.m_dxFactory);
            if (FAILED(hr))
                Log.report(logvisor::Fatal, "unable to create DXGI factory");

            Log.report(logvisor::Info, "initialized OpenGL renderer");
            return;
        }

        Log.report(logvisor::Fatal, "system doesn't support OGL, D3D11 or D3D12");
    }

    EPlatformType getPlatformType() const
    {
        return EPlatformType::Win32;
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

            case WM_CLOSE:
            case WM_SIZE:
            case WM_MOVING:
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
            case WM_MOUSELEAVE:
            case WM_NCMOUSELEAVE:
            case WM_MOUSEHOVER:
            case WM_NCMOUSEHOVER:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_CHAR:
            case WM_UNICHAR:
                window->_incomingEvent(&HWNDEvent(uMsg, wParam, lParam));

            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }

    int run()
    {
        g_mainThreadId = GetCurrentThreadId();

        /* Spawn client thread */
        int clientReturn = 0;
        std::thread clientThread([&]()
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            clientReturn = m_callback.appMain(this);
            PostThreadMessage(g_mainThreadId, WM_USER+1, 0, 0);
        });

        /* Pump messages */
        MSG msg = {0};
        while (GetMessage(&msg, NULL, 0, 0))
        {
            if (!msg.hwnd)
            {
                /* PostThreadMessage events */
                switch (msg.message)
                {
                case WM_USER:
                {
                    /* New-window message (coalesced onto main thread) */
                    std::unique_lock<std::mutex> lk(m_nwmt);
                    const SystemString* title = reinterpret_cast<const SystemString*>(msg.wParam);
                    m_mwret = newWindow(*title, 1);
                    lk.unlock();
                    m_nwcv.notify_one();
                    continue;
                }
                case WM_USER+1:
                    /* Quit message from client thread */
                    PostQuitMessage(0);
                    continue;
                case WM_USER+2:
                    /* SetCursor call from client thread */
                    SetCursor(HCURSOR(msg.wParam));
                    continue;
                case WM_USER+3:
                    /* ImmSetOpenStatus call from client thread */
                    ImmSetOpenStatus(HIMC(msg.wParam), BOOL(msg.lParam));
                    continue;
                case WM_USER+4:
                    /* ImmSetCompositionWindow call from client thread */
                    ImmSetCompositionWindow(HIMC(msg.wParam), LPCOMPOSITIONFORM(msg.lParam));
                    continue;
                default: break;
                }
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
    IWindow* newWindow(const SystemString& title, uint32_t sampleCount)
    {
        if (GetCurrentThreadId() != g_mainThreadId)
        {
            std::unique_lock<std::mutex> lk(m_nwmt);
            if (!PostThreadMessage(g_mainThreadId, WM_USER, WPARAM(&title), 0))
                Log.report(logvisor::Fatal, "PostThreadMessage error");
            m_nwcv.wait(lk);
            return m_mwret;
        }

        IWindow* window = _WindowWin32New(title, m_3dCtx, sampleCount);
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
    if (platform != IApplication::EPlatformType::Win32 &&
        platform != IApplication::EPlatformType::Auto)
        return 1;

    WIN32_CURSORS.m_arrow = LoadCursor(nullptr, IDC_ARROW);
    WIN32_CURSORS.m_hResize = LoadCursor(nullptr, IDC_SIZEWE);
    WIN32_CURSORS.m_vResize = LoadCursor(nullptr, IDC_SIZENS);
    WIN32_CURSORS.m_ibeam = LoadCursor(nullptr, IDC_IBEAM);
    WIN32_CURSORS.m_crosshairs = LoadCursor(nullptr, IDC_CROSS);
    WIN32_CURSORS.m_wait = LoadCursor(nullptr, IDC_WAIT);

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
    wndClass.hIcon = LoadIconW(wndClass.hInstance, MAKEINTRESOURCEW(101));
    wndClass.hCursor = WIN32_CURSORS.m_arrow;
    RegisterClassW(&wndClass);

    APP = new ApplicationWin32(cb, uniqueName, friendlyName, pname, args, singleInstance);
    return APP->run();
}

}

static const DEV_BROADCAST_DEVICEINTERFACE HOTPLUG_CONF =
{
    sizeof(DEV_BROADCAST_DEVICEINTERFACE),
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
        RegisterDeviceNotification(hwnd, (LPVOID)&HOTPLUG_CONF, DEVICE_NOTIFY_WINDOW_HANDLE);
        HOTPLUG_REGISTERED = true;
    }
    return static_cast<boo::ApplicationWin32*>(boo::APP)->winHwndHandler(hwnd, uMsg, wParam, lParam);
}


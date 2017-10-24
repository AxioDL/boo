#include "UWPCommon.hpp"

using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Platform;

#if _DEBUG
#define D3D11_CREATE_DEVICE_FLAGS D3D11_CREATE_DEVICE_DEBUG
#else
#define D3D11_CREATE_DEVICE_FLAGS 0
#endif

#include "boo/System.hpp"
#include "boo/IApplication.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include "boo/graphicsdev/D3D.hpp"
#include "logvisor/logvisor.hpp"

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
static logvisor::Module Log("boo::ApplicationUWP");

std::shared_ptr<IWindow> _WindowUWPNew(const SystemString& title, Boo3DAppContextUWP& d3dCtx,
                                       uint32_t sampleCount);

class ApplicationUWP final : public IApplication
{
    friend ref class AppView;
    IApplicationCallback& m_callback;
    const SystemString m_uniqueName;
    const SystemString m_friendlyName;
    const SystemString m_pname;
    const std::vector<SystemString> m_args;
    std::shared_ptr<IWindow> m_window;
    bool m_singleInstance;
    bool m_issuedWindow = false;

    Boo3DAppContextUWP m_3dCtx;

    void _deletedWindow(IWindow* window)
    {
    }

public:

    ApplicationUWP(IApplicationCallback& callback,
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
        for (const SystemString& arg : args)
            if (!arg.compare(L"--d3d11"))
                no12 = true;

#if _WIN32_WINNT_WIN10
        HMODULE d3d12lib = LoadLibraryW(L"D3D12.dll");
        if (!no12 && d3d12lib)
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

            /* Obtain DXGI Factory */
            HRESULT hr = MyCreateDXGIFactory1(__uuidof(IDXGIFactory2), &m_3dCtx.m_ctx12.m_dxFactory);
            if (FAILED(hr))
                Log.report(logvisor::Fatal, "unable to create DXGI factory");

            /* Adapter */
            ComPtr<IDXGIAdapter1> ppAdapter;
            for (UINT adapterIndex = 0; ; ++adapterIndex)
            {
                ComPtr<IDXGIAdapter1> pAdapter;
                if (DXGI_ERROR_NOT_FOUND == m_3dCtx.m_ctx12.m_dxFactory->EnumAdapters1(adapterIndex, &pAdapter))
                    break;

                // Check to see if the adapter supports Direct3D 12, but don't create the
                // actual device yet.
                if (SUCCEEDED(MyD3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                {
                    ppAdapter = std::move(pAdapter);
                    break;
                }
            }

            /* Create device */
            hr = ppAdapter ? MyD3D12CreateDevice(ppAdapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), &m_3dCtx.m_ctx12.m_dev) : E_FAIL;
            if (!FAILED(hr))
            {
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
            else
            {
                /* Some Win10 client HW doesn't support D3D12 (despite being supposedly HW-agnostic) */
                m_3dCtx.m_ctx12.m_dev.Reset();
                m_3dCtx.m_ctx12.m_dxFactory.Reset();
            }
        }
#endif
        HMODULE d3d11lib = LoadLibraryW(L"D3D11.dll");
        if (d3d11lib)
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

            ComPtr<IDXGIDevice2> device;
            if (FAILED(tempDev.As<ID3D11Device1>(&m_3dCtx.m_ctx11.m_dev)) || !m_3dCtx.m_ctx11.m_dev ||
                FAILED(tempCtx.As<ID3D11DeviceContext1>(&m_3dCtx.m_ctx11.m_devCtx)) || !m_3dCtx.m_ctx11.m_devCtx ||
                FAILED(m_3dCtx.m_ctx11.m_dev.As<IDXGIDevice2>(&device)) || !device)
            {
                MessageBoxW(nullptr, L"Windows 7 users should install 'Platform Update for Windows 7':\n"
                                     L"https://www.microsoft.com/en-us/download/details.aspx?id=36805",
                                     L"IDXGIDevice2 interface error", MB_OK | MB_ICONERROR);
                exit(1);
            }

            /* Obtain DXGI Factory */
            ComPtr<IDXGIAdapter> adapter;
            device->GetParent(__uuidof(IDXGIAdapter), &adapter);
            adapter->GetParent(__uuidof(IDXGIFactory2), &m_3dCtx.m_ctx11.m_dxFactory);

            /* Build default sampler here */
            CD3D11_SAMPLER_DESC sampDesc(D3D11_DEFAULT);
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[0]);

            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[1]);

            Log.report(logvisor::Info, "initialized D3D11 renderer");
            return;
        }

        Log.report(logvisor::Fatal, "system doesn't support D3D11 or D3D12");
    }

    EPlatformType getPlatformType() const
    {
        return EPlatformType::UWP;
    }

    int run()
    {
        /* Spawn client thread */
        int clientReturn = 0;
        std::thread clientThread([&]()
        {
            logvisor::RegisterThreadName("Boo Client Thread");
            clientReturn = m_callback.appMain(this);
        });

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

    std::shared_ptr<IWindow> newWindow(const SystemString& title, uint32_t sampleCount)
    {
        if (!m_issuedWindow)
        {
            m_issuedWindow = true;
            return m_window;
        }
        return {};
    }

    void _setWindow(CoreWindow^ window)
    {
        m_window = _WindowUWPNew(m_friendlyName, m_3dCtx, 1);
    }
};

IApplication* APP = NULL;
ref class AppView sealed : public IFrameworkView
{
    ApplicationUWP m_app;

internal:
    AppView(IApplicationCallback& callback,
            const SystemString& uniqueName,
            const SystemString& friendlyName,
            const SystemString& pname,
            const std::vector<SystemString>& args,
            bool singleInstance)
    : m_app(callback, uniqueName, friendlyName, pname, args, singleInstance) { APP = &m_app; }

public:
    virtual void Initialize(CoreApplicationView^ applicationView)
    {
        applicationView->Activated += ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &AppView::OnActivated);
    }

    virtual void SetWindow(CoreWindow^ window)
    {
        m_app._setWindow(window);
    }

    virtual void Load(String^ entryPoint)
    {

    }

    virtual void Run()
    {
        m_app.run();
    }

    virtual void Uninitialize()
    {

    }

    void OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args)
    {
        CoreWindow::GetForCurrentThread()->Activate();
    }
};

IFrameworkView^ ViewProvider::CreateView()
{
    return ref new AppView(m_appCb, m_uniqueName, m_friendlyName, m_pname, m_args, m_singleInstance);
}

}


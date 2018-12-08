#include "UWPCommon.hpp"

using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
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
#include "boo/UWPViewProvider.hpp"

#if _WIN32_WINNT_WIN10
PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignaturePROC = nullptr;
#endif
pD3DCompile D3DCompilePROC = nullptr;
pD3DCreateBlob D3DCreateBlobPROC = nullptr;

static bool FindBestD3DCompile() {
  D3DCompilePROC = D3DCompile;
  D3DCreateBlobPROC = D3DCreateBlob;
  return D3DCompilePROC != nullptr && D3DCreateBlobPROC != nullptr;
}

namespace boo {
static logvisor::Module Log("boo::ApplicationUWP");

std::shared_ptr<IWindow> _WindowUWPNew(SystemStringView title, Boo3DAppContextUWP& d3dCtx);

class ApplicationUWP final : public IApplication {
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

  void _deletedWindow(IWindow* window) {}

public:
  ApplicationUWP(IApplicationCallback& callback, SystemStringView uniqueName, SystemStringView friendlyName,
                 SystemStringView pname, const std::vector<SystemString>& args, bool singleInstance)
  : m_callback(callback)
  , m_uniqueName(uniqueName)
  , m_friendlyName(friendlyName)
  , m_pname(pname)
  , m_args(args)
  , m_singleInstance(singleInstance) {
    typedef HRESULT(WINAPI * CreateDXGIFactory1PROC)(REFIID riid, _COM_Outptr_ void** ppFactory);
    CreateDXGIFactory1PROC MyCreateDXGIFactory1 = CreateDXGIFactory1;

    bool no12 = true;
    for (const SystemString& arg : args)
      if (!arg.compare(L"--d3d12"))
        no12 = false;

#if _WIN32_WINNT_WIN10
    if (!no12) {
      if (!FindBestD3DCompile())
        Log.report(logvisor::Fatal, "unable to find D3DCompile_[43-47].dll");

      D3D12SerializeRootSignaturePROC = D3D12SerializeRootSignature;

      /* Create device */
      PFN_D3D12_CREATE_DEVICE MyD3D12CreateDevice = D3D12CreateDevice;

      /* Obtain DXGI Factory */
      HRESULT hr = MyCreateDXGIFactory1(__uuidof(IDXGIFactory2), &m_3dCtx.m_ctx12.m_dxFactory);
      if (FAILED(hr))
        Log.report(logvisor::Fatal, "unable to create DXGI factory");

      /* Adapter */
      ComPtr<IDXGIAdapter1> ppAdapter;
      for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> pAdapter;
        if (DXGI_ERROR_NOT_FOUND == m_3dCtx.m_ctx12.m_dxFactory->EnumAdapters1(adapterIndex, &pAdapter))
          break;

        // Check to see if the adapter supports Direct3D 12, but don't create the
        // actual device yet.
        if (SUCCEEDED(MyD3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
          ppAdapter = std::move(pAdapter);
          break;
        }
      }

      /* Create device */
      hr = ppAdapter ? MyD3D12CreateDevice(ppAdapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                           &m_3dCtx.m_ctx12.m_dev)
                     : E_FAIL;
      if (!FAILED(hr)) {
        /* Establish loader objects */
        if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), &m_3dCtx.m_ctx12.m_loadqalloc)))
          Log.report(logvisor::Fatal, "unable to create loader allocator");

        D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                         D3D12_COMMAND_QUEUE_FLAG_NONE};
        if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue),
                                                             &m_3dCtx.m_ctx12.m_loadq)))
          Log.report(logvisor::Fatal, "unable to create loader queue");

        if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                                      &m_3dCtx.m_ctx12.m_loadfence)))
          Log.report(logvisor::Fatal, "unable to create loader fence");

        m_3dCtx.m_ctx12.m_loadfencehandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (FAILED(m_3dCtx.m_ctx12.m_dev->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_3dCtx.m_ctx12.m_loadqalloc.Get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList), &m_3dCtx.m_ctx12.m_loadlist)))
          Log.report(logvisor::Fatal, "unable to create loader list");

        Log.report(logvisor::Info, "initialized D3D12 renderer");
        return;
      } else {
        /* Some Win10 client HW doesn't support D3D12 (despite being supposedly HW-agnostic) */
        m_3dCtx.m_ctx12.m_dev.Reset();
        m_3dCtx.m_ctx12.m_dxFactory.Reset();
      }
    }
#endif
    {
      if (!FindBestD3DCompile())
        Log.report(logvisor::Fatal, "unable to find D3DCompile_[43-47].dll");

      /* Create device proc */
      PFN_D3D11_CREATE_DEVICE MyD3D11CreateDevice = D3D11CreateDevice;

      /* Create device */
      D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
      ComPtr<ID3D11Device> tempDev;
      ComPtr<ID3D11DeviceContext> tempCtx;
      if (FAILED(MyD3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_FLAGS, &level, 1,
                                     D3D11_SDK_VERSION, &tempDev, nullptr, &tempCtx)))
        Log.report(logvisor::Fatal, "unable to create D3D11 device");

      ComPtr<IDXGIDevice2> device;
      if (FAILED(tempDev.As<ID3D11Device1>(&m_3dCtx.m_ctx11.m_dev)) || !m_3dCtx.m_ctx11.m_dev ||
          FAILED(tempCtx.As<ID3D11DeviceContext1>(&m_3dCtx.m_ctx11.m_devCtx)) || !m_3dCtx.m_ctx11.m_devCtx ||
          FAILED(m_3dCtx.m_ctx11.m_dev.As<IDXGIDevice2>(&device)) || !device) {
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

  EPlatformType getPlatformType() const { return EPlatformType::UWP; }

  std::thread m_clientThread;
  int run() {
    /* Spawn client thread */
    int clientReturn = 0;
    m_clientThread = std::thread([&]() {
      std::string thrName = WCSTMBS(getFriendlyName().data()) + " Client Thread";
      logvisor::RegisterThreadName(thrName.c_str());
      clientReturn = m_callback.appMain(this);
    });

    CoreWindow::GetForCurrentThread()->Activate();
    CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    return 0;
  }

  void quit() {
    m_callback.appQuitting(this);
    if (m_clientThread.joinable())
      m_clientThread.join();
  }

  SystemStringView getUniqueName() const { return m_uniqueName; }

  SystemStringView getFriendlyName() const { return m_friendlyName; }

  SystemStringView getProcessName() const { return m_pname; }

  const std::vector<SystemString>& getArgs() const { return m_args; }

  std::shared_ptr<IWindow> newWindow(SystemStringView title, uint32_t sampleCount) {
    if (!m_issuedWindow) {
      m_issuedWindow = true;
      return m_window;
    }
    return {};
  }

  void _setWindow(CoreWindow ^ window) { m_window = _WindowUWPNew(m_friendlyName, m_3dCtx); }
};

IApplication* APP = NULL;
ref class AppView sealed : public IFrameworkView {
  ApplicationUWP m_app;

  internal : AppView(IApplicationCallback& callback, SystemStringView uniqueName, SystemStringView friendlyName,
                     SystemStringView pname, const std::vector<SystemString>& args, bool singleInstance)
  : m_app(callback, uniqueName, friendlyName, pname, args, singleInstance) {
    APP = &m_app;
  }

public:
  virtual void Initialize(CoreApplicationView ^ applicationView) {
    applicationView->Activated +=
        ref new TypedEventHandler<CoreApplicationView ^, IActivatedEventArgs ^>(this, &AppView::OnActivated);
  }

  virtual void SetWindow(CoreWindow ^ window) { m_app._setWindow(window); }

  virtual void Load(String ^ entryPoint) {}

  virtual void Run() { m_app.run(); }

  virtual void Uninitialize() { m_app.quit(); }

  void OnActivated(CoreApplicationView ^ applicationView, IActivatedEventArgs ^ args) {
    CoreWindow::GetForCurrentThread()->Activate();
  }
};

IFrameworkView ^ ViewProvider::CreateView() {
  return ref new AppView(m_appCb, m_uniqueName, m_friendlyName, m_pname, m_args, m_singleInstance);
}

} // namespace boo

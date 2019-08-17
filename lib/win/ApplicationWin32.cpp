#include "Win32Common.hpp"
#include <shellapi.h>
#include <initguid.h>
#include <Usbiodef.h>
#include <winver.h>
#include <Dbt.h>

#if _WIN32_WINNT_WINBLUE
PFN_GetScaleFactorForMonitor MyGetScaleFactorForMonitor = nullptr;
#endif

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

#if BOO_HAS_VULKAN
#include "boo/graphicsdev/Vulkan.hpp"
#endif

#include <condition_variable>
#include <mutex>

DWORD g_mainThreadId = 0;
std::mutex g_nwmt;
std::condition_variable g_nwcv;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
pD3DCompile D3DCompilePROC = nullptr;
pD3DCreateBlob D3DCreateBlobPROC = nullptr;
pD3DPERF_BeginEvent D3DPERF_BeginEventPROC = nullptr;
pD3DPERF_EndEvent D3DPERF_EndEventPROC = nullptr;

static bool FindBestD3DCompile() {
  HMODULE d3dCompilelib = LoadLibraryW(L"D3DCompiler_47.dll");
  if (!d3dCompilelib) {
    d3dCompilelib = LoadLibraryW(L"D3DCompiler_46.dll");
    if (!d3dCompilelib) {
      d3dCompilelib = LoadLibraryW(L"D3DCompiler_45.dll");
      if (!d3dCompilelib) {
        d3dCompilelib = LoadLibraryW(L"D3DCompiler_44.dll");
        if (!d3dCompilelib) {
          d3dCompilelib = LoadLibraryW(L"D3DCompiler_43.dll");
        }
      }
    }
  }
  if (d3dCompilelib) {
    D3DCompilePROC = (pD3DCompile)GetProcAddress(d3dCompilelib, "D3DCompile");
    D3DCreateBlobPROC = (pD3DCreateBlob)GetProcAddress(d3dCompilelib, "D3DCreateBlob");
    return D3DCompilePROC != nullptr && D3DCreateBlobPROC != nullptr;
  }
  return false;
}

static bool FindD3DPERF() {
  HMODULE d3d9lib = LoadLibraryW(L"d3d9.dll");
  if (d3d9lib) {
    D3DPERF_BeginEventPROC = (pD3DPERF_BeginEvent)GetProcAddress(d3d9lib, "D3DPERF_BeginEvent");
    D3DPERF_EndEventPROC = (pD3DPERF_EndEvent)GetProcAddress(d3d9lib, "D3DPERF_EndEvent");
    return D3DPERF_BeginEventPROC != nullptr && D3DPERF_EndEventPROC != nullptr;
  }
  return false;
}

namespace boo {
static logvisor::Module Log("boo::ApplicationWin32");
Win32Cursors WIN32_CURSORS;

std::shared_ptr<IWindow> _WindowWin32New(SystemStringView title, Boo3DAppContextWin32& d3dCtx);

class ApplicationWin32 final : public IApplication {
  IApplicationCallback& m_callback;
  const SystemString m_uniqueName;
  const SystemString m_friendlyName;
  const SystemString m_pname;
  const std::vector<SystemString> m_args;
  std::unordered_map<HWND, std::weak_ptr<IWindow>> m_allWindows;

  Boo3DAppContextWin32 m_3dCtx;
#if BOO_HAS_VULKAN
  PFN_vkGetInstanceProcAddr m_getVkProc = nullptr;
#endif

  void _deletedWindow(IWindow* window) override { m_allWindows.erase(HWND(window->getPlatformHandle())); }

public:
  ApplicationWin32(IApplicationCallback& callback, SystemStringView uniqueName, SystemStringView friendlyName,
                   SystemStringView pname, const std::vector<SystemString>& args, std::string_view gfxApi,
                   uint32_t samples, uint32_t anisotropy, bool deepColor, bool singleInstance)
  : m_callback(callback), m_uniqueName(uniqueName), m_friendlyName(friendlyName), m_pname(pname), m_args(args) {
    m_3dCtx.m_ctx11.m_sampleCount = samples;
    m_3dCtx.m_ctx11.m_anisotropy = anisotropy;
    m_3dCtx.m_ctx11.m_fbFormat = deepColor ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    m_3dCtx.m_ctxOgl.m_glCtx.m_sampleCount = samples;
    m_3dCtx.m_ctxOgl.m_glCtx.m_anisotropy = anisotropy;
    m_3dCtx.m_ctxOgl.m_glCtx.m_deepColor = deepColor;
#if BOO_HAS_VULKAN
    g_VulkanContext.m_sampleCountColor = samples;
    g_VulkanContext.m_sampleCountDepth = samples;
    g_VulkanContext.m_anisotropy = anisotropy;
    g_VulkanContext.m_deepColor = deepColor;
#endif

    HMODULE dxgilib = LoadLibraryW(L"dxgi.dll");
    if (!dxgilib)
      Log.report(logvisor::Fatal, fmt("unable to load dxgi.dll"));

    using CreateDXGIFactory1PROC =  HRESULT(WINAPI*)(REFIID riid, _COM_Outptr_ void** ppFactory);
    auto MyCreateDXGIFactory1 = (CreateDXGIFactory1PROC)GetProcAddress(dxgilib, "CreateDXGIFactory1");
    if (!MyCreateDXGIFactory1)
      Log.report(logvisor::Fatal, fmt("unable to find CreateDXGIFactory1 in DXGI.dll\n"));

    bool noD3d = false;
#if BOO_HAS_VULKAN
    bool useVulkan = false;
#endif
    if (!gfxApi.empty()) {
#if BOO_HAS_VULKAN
      if (!gfxApi.compare("Vulkan")) {
        noD3d = true;
        useVulkan = true;
      }
      if (!gfxApi.compare("OpenGL")) {
        noD3d = true;
        useVulkan = false;
      }
#else
      if (!gfxApi.compare("OpenGL"))
        noD3d = true;
#endif
    }
    for (const SystemString& arg : args) {
#if BOO_HAS_VULKAN
      if (!arg.compare(L"--d3d11")) {
        useVulkan = false;
        noD3d = false;
      }
      if (!arg.compare(L"--vulkan")) {
        noD3d = true;
        useVulkan = true;
      }
      if (!arg.compare(L"--gl")) {
        noD3d = true;
        useVulkan = false;
      }
#else
      if (!arg.compare(L"--d3d11"))
        noD3d = false;
      if (!arg.compare(L"--gl"))
        noD3d = true;
#endif
    }

    HMODULE d3d11lib = nullptr;
    if (!noD3d)
      d3d11lib = LoadLibraryW(L"D3D11.dll");
    if (d3d11lib) {
      if (!FindBestD3DCompile())
        Log.report(logvisor::Fatal, fmt("unable to find D3DCompile_[43-47].dll"));
      if (!FindD3DPERF())
        Log.report(logvisor::Fatal, fmt("unable to find d3d9.dll"));

      /* Create device proc */
      PFN_D3D11_CREATE_DEVICE MyD3D11CreateDevice =
          (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11lib, "D3D11CreateDevice");
      if (!MyD3D11CreateDevice)
        Log.report(logvisor::Fatal, fmt("unable to find D3D11CreateDevice in D3D11.dll"));

      /* Create device */
      D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
      ComPtr<ID3D11Device> tempDev;
      ComPtr<ID3D11DeviceContext> tempCtx;
      if (FAILED(MyD3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_FLAGS, &level, 1,
                                     D3D11_SDK_VERSION, &tempDev, nullptr, &tempCtx)))
        Log.report(logvisor::Fatal, fmt("unable to create D3D11 device"));

      ComPtr<IDXGIDevice2> device;
      if (FAILED(tempDev.As<ID3D11Device1>(&m_3dCtx.m_ctx11.m_dev)) || !m_3dCtx.m_ctx11.m_dev ||
          FAILED(tempCtx.As<ID3D11DeviceContext1>(&m_3dCtx.m_ctx11.m_devCtx)) || !m_3dCtx.m_ctx11.m_devCtx ||
          FAILED(m_3dCtx.m_ctx11.m_dev.As<IDXGIDevice2>(&device)) || !device) {
        MessageBoxW(nullptr,
                    L"Windows 7 users should install 'Platform Update for Windows 7':\n"
                    L"https://www.microsoft.com/en-us/download/details.aspx?id=36805",
                    L"IDXGIDevice2 interface error", MB_OK | MB_ICONERROR);
        exit(1);
      }

      /* Obtain DXGI Factory */
      ComPtr<IDXGIAdapter> adapter;
      device->GetParent(__uuidof(IDXGIAdapter), &adapter);
      adapter->GetParent(__uuidof(IDXGIFactory2), &m_3dCtx.m_ctx11.m_dxFactory);

      m_3dCtx.m_ctx11.m_anisotropy = std::min(m_3dCtx.m_ctx11.m_anisotropy, uint32_t(16));

      /* Build default sampler here */
      CD3D11_SAMPLER_DESC sampDesc(D3D11_DEFAULT);
      sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
      sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
      sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
      sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
      sampDesc.MaxAnisotropy = m_3dCtx.m_ctx11.m_anisotropy;
      m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[0]);

      sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
      sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
      m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[1]);

      sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
      sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
      std::fill(std::begin(sampDesc.BorderColor), std::end(sampDesc.BorderColor), 0.f);
      m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[2]);

      sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[3]);

      sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
      m_3dCtx.m_ctx11.m_dev->CreateSamplerState(&sampDesc, &m_3dCtx.m_ctx11.m_ss[4]);

      Log.report(logvisor::Info, fmt("initialized D3D11 renderer"));
      return;
    }

#if BOO_HAS_VULKAN
    if (useVulkan) {
      HMODULE vulkanLib = LoadLibraryW(L"vulkan-1.dll");
      if (vulkanLib) {
        m_getVkProc = (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkanLib, "vkGetInstanceProcAddr");
        if (m_getVkProc) {
          /* Check device support for vulkan */
          if (g_VulkanContext.m_instance == VK_NULL_HANDLE) {
            auto appName = getUniqueName();
            if (g_VulkanContext.initVulkan(WCSTMBS(appName.data()).c_str(), m_getVkProc)) {
              if (g_VulkanContext.enumerateDevices()) {
                /* Obtain DXGI Factory */
                HRESULT hr = MyCreateDXGIFactory1(__uuidof(IDXGIFactory1), &m_3dCtx.m_vulkanDxFactory);
                if (FAILED(hr))
                  Log.report(logvisor::Fatal, fmt("unable to create DXGI factory"));

                Log.report(logvisor::Info, fmt("initialized Vulkan renderer"));
                return;
              }
            }
          }
        }
      }
    }
#endif

    /* Finally try OpenGL */
    {
      /* Obtain DXGI Factory */
      HRESULT hr = MyCreateDXGIFactory1(__uuidof(IDXGIFactory1), &m_3dCtx.m_ctxOgl.m_dxFactory);
      if (FAILED(hr))
        Log.report(logvisor::Fatal, fmt("unable to create DXGI factory"));

      Log.report(logvisor::Info, fmt("initialized OpenGL renderer"));
      return;
    }

    Log.report(logvisor::Fatal, fmt("system doesn't support Vulkan, D3D11, or OpenGL"));
  }

  EPlatformType getPlatformType() const override { return EPlatformType::Win32; }

  LRESULT winHwndHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    /* Lookup boo window instance */
    auto search = m_allWindows.find(hwnd);
    if (search == m_allWindows.end())
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    ;

    std::shared_ptr<IWindow> window = search->second.lock();
    if (!window)
      return DefWindowProc(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
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
    case WM_UNICHAR: {
      HWNDEvent eventData(uMsg, wParam, lParam);
      window->_incomingEvent(&eventData);
    }

    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
  }

  template <class W>
  static void DoSetFullscreen(W& win, bool fs) {
    std::lock_guard<std::mutex> lk(g_nwmt);
    if (fs) {
      win.m_fsStyle = GetWindowLong(win.m_hwnd, GWL_STYLE);
      win.m_fsExStyle = GetWindowLong(win.m_hwnd, GWL_EXSTYLE);
      GetWindowRect(win.m_hwnd, &win.m_fsRect);

      SetWindowLong(win.m_hwnd, GWL_STYLE, win.m_fsStyle & ~(WS_CAPTION | WS_THICKFRAME));
      SetWindowLong(win.m_hwnd, GWL_EXSTYLE,
                    win.m_fsExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

      MONITORINFO monitor_info;
      monitor_info.cbSize = sizeof(monitor_info);
      GetMonitorInfo(MonitorFromWindow(win.m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor_info);
      SetWindowPos(win.m_hwnd, nullptr, monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                   monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                   monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

      win.m_fs = true;
    } else {
      SetWindowLong(win.m_hwnd, GWL_STYLE, win.m_fsStyle);
      SetWindowLong(win.m_hwnd, GWL_EXSTYLE, win.m_fsExStyle);

      SetWindowPos(win.m_hwnd, nullptr, win.m_fsRect.left, win.m_fsRect.top, win.m_fsRect.right - win.m_fsRect.left,
                   win.m_fsRect.bottom - win.m_fsRect.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

      win.m_fs = false;
    }
    g_nwcv.notify_one();
  }

  int run() override {
    g_mainThreadId = GetCurrentThreadId();

    /* Spawn client thread */
    int clientReturn = 0;
    std::thread clientThread([&]() {
      std::string thrName = WCSTMBS(getFriendlyName().data()) + " Client Thread";
      logvisor::RegisterThreadName(thrName.c_str());
      CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
      clientReturn = m_callback.appMain(this);
      PostThreadMessageW(g_mainThreadId, WM_USER + 1, 0, 0);
    });

    /* Pump messages */
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
      if (!msg.hwnd) {
        /* PostThreadMessage events */
        switch (msg.message) {
        case WM_USER: {
          /* New-window message (coalesced onto main thread) */
          std::lock_guard<std::mutex> lk(g_nwmt);
          SystemStringView* title = reinterpret_cast<SystemStringView*>(msg.wParam);
          m_mwret = newWindow(*title);
          g_nwcv.notify_one();
          continue;
        }
        case WM_USER + 1:
          /* Quit message from client thread */
          PostQuitMessage(0);
          continue;
        case WM_USER + 2:
          /* SetCursor call from client thread */
          SetCursor(HCURSOR(msg.wParam));
          continue;
        case WM_USER + 3:
          /* ImmSetOpenStatus call from client thread */
          ImmSetOpenStatus(HIMC(msg.wParam), BOOL(msg.lParam));
          continue;
        case WM_USER + 4:
          /* ImmSetCompositionWindow call from client thread */
          ImmSetCompositionWindow(HIMC(msg.wParam), LPCOMPOSITIONFORM(msg.lParam));
          continue;
        case WM_USER + 5:
          /* SetFullscreen call for OpenGL window */
          DoSetFullscreen(*reinterpret_cast<OGLContext::Window*>(msg.wParam), msg.lParam);
          continue;
#if BOO_HAS_VULKAN
        case WM_USER + 6:
          /* SetFullscreen call for Vulkan window */
          DoSetFullscreen(*reinterpret_cast<boo::VulkanContext::Window*>(msg.wParam), msg.lParam);
          continue;
#endif
        default:
          break;
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    m_callback.appQuitting(this);
    clientThread.join();
    return clientReturn;
  }

  ~ApplicationWin32() override {
    for (auto& p : m_allWindows)
      if (auto w = p.second.lock())
        w->_cleanup();
  }

  SystemStringView getUniqueName() const override { return m_uniqueName; }

  SystemStringView getFriendlyName() const override { return m_friendlyName; }

  SystemStringView getProcessName() const override { return m_pname; }

  const std::vector<SystemString>& getArgs() const override { return m_args; }

  std::shared_ptr<IWindow> m_mwret;
  std::shared_ptr<IWindow> newWindow(SystemStringView title) override {
    if (GetCurrentThreadId() != g_mainThreadId) {
      std::unique_lock<std::mutex> lk(g_nwmt);
      if (!PostThreadMessageW(g_mainThreadId, WM_USER, WPARAM(&title), 0))
        Log.report(logvisor::Fatal, fmt("PostThreadMessage error"));
      g_nwcv.wait(lk);
      std::shared_ptr<IWindow> ret = std::move(m_mwret);
      m_mwret.reset();
      return ret;
    }

    std::shared_ptr<IWindow> window = _WindowWin32New(title, m_3dCtx);
    HWND hwnd = HWND(window->getPlatformHandle());
    m_allWindows[hwnd] = window;
    return window;
  }
};

IApplication* APP = nullptr;
int ApplicationRun(IApplication::EPlatformType platform, IApplicationCallback& cb, SystemStringView uniqueName,
                   SystemStringView friendlyName, SystemStringView pname, const std::vector<SystemString>& args,
                   std::string_view gfxApi, uint32_t samples, uint32_t anisotropy, bool deepColor,
                   bool singleInstance) {
  std::string thrName = WCSTMBS(friendlyName.data()) + " Main Thread";
  logvisor::RegisterThreadName(thrName.c_str());
  if (APP)
    return 1;
  if (platform != IApplication::EPlatformType::Win32 && platform != IApplication::EPlatformType::Auto)
    return 1;

#if _WIN32_WINNT_WINBLUE
  /* HI-DPI support */
  HMODULE shcoreLib = LoadLibraryW(L"Shcore.dll");
  if (shcoreLib)
    MyGetScaleFactorForMonitor = (PFN_GetScaleFactorForMonitor)GetProcAddress(shcoreLib, "GetScaleFactorForMonitor");
#endif

  WIN32_CURSORS.m_arrow = LoadCursor(nullptr, IDC_ARROW);
  WIN32_CURSORS.m_hResize = LoadCursor(nullptr, IDC_SIZEWE);
  WIN32_CURSORS.m_vResize = LoadCursor(nullptr, IDC_SIZENS);
  WIN32_CURSORS.m_ibeam = LoadCursor(nullptr, IDC_IBEAM);
  WIN32_CURSORS.m_crosshairs = LoadCursor(nullptr, IDC_CROSS);
  WIN32_CURSORS.m_wait = LoadCursor(nullptr, IDC_WAIT);

  /* One class for *all* boo windows */
  WNDCLASS wndClass = {0, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"BooWindow"};
  wndClass.hIcon = LoadIconW(wndClass.hInstance, MAKEINTRESOURCEW(101));
  wndClass.hCursor = WIN32_CURSORS.m_arrow;
  RegisterClassW(&wndClass);

  APP = new ApplicationWin32(cb, uniqueName, friendlyName, pname, args, gfxApi, samples, anisotropy, deepColor,
                             singleInstance);
  int ret = APP->run();
  delete APP;
  APP = nullptr;
  return ret;
}

} // namespace boo

static const DEV_BROADCAST_DEVICEINTERFACE HOTPLUG_CONF = {sizeof(DEV_BROADCAST_DEVICEINTERFACE),
                                                           DBT_DEVTYP_DEVICEINTERFACE};
static bool HOTPLUG_REGISTERED = false;
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (!HOTPLUG_REGISTERED && uMsg == WM_CREATE) {
    /* Register hotplug notification with windows */
    RegisterDeviceNotification(hwnd, (LPVOID)&HOTPLUG_CONF,
                               DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    HOTPLUG_REGISTERED = true;
  }
  return static_cast<boo::ApplicationWin32*>(boo::APP)->winHwndHandler(hwnd, uMsg, wParam, lParam);
}

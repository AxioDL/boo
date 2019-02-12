#include "UWPCommon.hpp"
#include "boo/IApplication.hpp"
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "logvisor/logvisor.hpp"

#include "boo/graphicsdev/D3D.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::ViewManagement;
using namespace Windows::System;
using namespace Windows::Graphics::Display;
using namespace Windows::Foundation;
using namespace Platform;

#include <agile.h>

namespace boo {
static logvisor::Module Log("boo::WindowWin32");
#if _WIN32_WINNT_WIN10
IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx,
                                             IGraphicsContext* parent, ID3D12CommandQueue** cmdQueueOut);
IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent, uint32_t sampleCount);
#endif
IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx,
                                             IGraphicsContext* parent);
IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent, uint32_t sampleCount);

struct GraphicsContextUWP : IGraphicsContext {
  EGraphicsAPI m_api;
  EPixelFormat m_pf;
  IWindow* m_parentWindow;
  Boo3DAppContextUWP& m_3dCtx;
  ComPtr<IDXGIOutput> m_output;
  GraphicsContextUWP(EGraphicsAPI api, IWindow* parentWindow, Boo3DAppContextUWP& b3dCtx)
  : m_api(api), m_pf(EPixelFormat::RGBA8), m_parentWindow(parentWindow), m_3dCtx(b3dCtx) {}

  virtual void resized(const SWindowRect& rect) { m_3dCtx.resize(m_parentWindow, rect.size[0], rect.size[1]); }
};

struct GraphicsContextUWPD3D : GraphicsContextUWP {
  ComPtr<IDXGISwapChain1> m_swapChain;

  IGraphicsCommandQueue* m_commandQueue = nullptr;
  IGraphicsDataFactory* m_dataFactory = nullptr;

public:
  IWindowCallback* m_callback;

  GraphicsContextUWPD3D(EGraphicsAPI api, IWindow* parentWindow, Agile<CoreWindow>& coreWindow,
                        Boo3DAppContextUWP& b3dCtx)
  : GraphicsContextUWP(api, parentWindow, b3dCtx) {
    /* Create Swap Chain */
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
#if !WINDOWS_STORE
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
#else
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
#endif
    scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    IUnknown* cw = reinterpret_cast<IUnknown*>(coreWindow.Get());

#if _WIN32_WINNT_WIN10
    if (b3dCtx.m_ctx12.m_dev) {
      auto insIt = b3dCtx.m_ctx12.m_windows.emplace(std::make_pair(parentWindow, D3D12Context::Window()));
      D3D12Context::Window& w = insIt.first->second;

      ID3D12CommandQueue* cmdQueue;
      m_dataFactory = _NewD3D12DataFactory(&b3dCtx.m_ctx12, this);
      m_commandQueue = _NewD3D12CommandQueue(&b3dCtx.m_ctx12, &w, this, &cmdQueue);

      scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      HRESULT hr =
          b3dCtx.m_ctx12.m_dxFactory->CreateSwapChainForCoreWindow(cmdQueue, cw, &scDesc, nullptr, &m_swapChain);
      if (FAILED(hr))
        Log.report(logvisor::Fatal, "unable to create swap chain");

      m_swapChain.As<IDXGISwapChain3>(&w.m_swapChain);
      ComPtr<ID3D12Resource> fb;
      m_swapChain->GetBuffer(0, __uuidof(ID3D12Resource), &fb);
      w.m_backBuf = w.m_swapChain->GetCurrentBackBufferIndex();
      D3D12_RESOURCE_DESC resDesc = fb->GetDesc();
      w.width = resDesc.Width;
      w.height = resDesc.Height;

      if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
        Log.report(logvisor::Fatal, "unable to get DXGI output");
    } else
#endif
    {
      if (FAILED(b3dCtx.m_ctx11.m_dxFactory->CreateSwapChainForCoreWindow(b3dCtx.m_ctx11.m_dev.Get(), cw, &scDesc,
                                                                          nullptr, &m_swapChain)))
        Log.report(logvisor::Fatal, "unable to create swap chain");

      auto insIt = b3dCtx.m_ctx11.m_windows.emplace(std::make_pair(parentWindow, D3D11Context::Window()));
      D3D11Context::Window& w = insIt.first->second;

      m_swapChain.As<IDXGISwapChain1>(&w.m_swapChain);
      ComPtr<ID3D11Texture2D> fbRes;
      m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &fbRes);
      D3D11_TEXTURE2D_DESC resDesc;
      fbRes->GetDesc(&resDesc);
      w.width = resDesc.Width;
      w.height = resDesc.Height;
      m_dataFactory = _NewD3D11DataFactory(&b3dCtx.m_ctx11, this);
      m_commandQueue = _NewD3D11CommandQueue(&b3dCtx.m_ctx11, &insIt.first->second, this);

      if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
        Log.report(logvisor::Fatal, "unable to get DXGI output");
    }
  }

  ~GraphicsContextUWPD3D() {
#if _WIN32_WINNT_WIN10
    if (m_3dCtx.m_ctx12.m_dev)
      m_3dCtx.m_ctx12.m_windows.erase(m_parentWindow);
    else
#endif
      m_3dCtx.m_ctx11.m_windows.erase(m_parentWindow);
  }

  void _setCallback(IWindowCallback* cb) { m_callback = cb; }

  EGraphicsAPI getAPI() const { return m_api; }

  EPixelFormat getPixelFormat() const { return m_pf; }

  void setPixelFormat(EPixelFormat pf) {
    if (pf > EPixelFormat::RGBAF32_Z24)
      return;
    m_pf = pf;
  }

  bool initializeContext(void*) { return true; }

  void makeCurrent() {}

  void postInit() {}

  void present() {}

  IGraphicsCommandQueue* getCommandQueue() { return m_commandQueue; }

  IGraphicsDataFactory* getDataFactory() { return m_dataFactory; }

  IGraphicsDataFactory* getMainContextDataFactory() { return m_dataFactory; }

  IGraphicsDataFactory* getLoadContextDataFactory() { return m_dataFactory; }
};

static uint32_t translateKeysym(CoreWindow ^ window, VirtualKey sym, ESpecialKey& specialSym,
                                EModifierKey& modifierSym) {
  specialSym = ESpecialKey::None;
  modifierSym = EModifierKey::None;
  if (sym >= VirtualKey::F1 && sym <= VirtualKey::F12)
    specialSym = ESpecialKey(uint32_t(ESpecialKey::F1) + uint32_t(sym - VirtualKey::F1));
  else if (sym == VirtualKey::Escape)
    specialSym = ESpecialKey::Esc;
  else if (sym == VirtualKey::Enter)
    specialSym = ESpecialKey::Enter;
  else if (sym == VirtualKey::Back)
    specialSym = ESpecialKey::Backspace;
  else if (sym == VirtualKey::Insert)
    specialSym = ESpecialKey::Insert;
  else if (sym == VirtualKey::Delete)
    specialSym = ESpecialKey::Delete;
  else if (sym == VirtualKey::Home)
    specialSym = ESpecialKey::Home;
  else if (sym == VirtualKey::End)
    specialSym = ESpecialKey::End;
  else if (sym == VirtualKey::PageUp)
    specialSym = ESpecialKey::PgUp;
  else if (sym == VirtualKey::PageDown)
    specialSym = ESpecialKey::PgDown;
  else if (sym == VirtualKey::Left)
    specialSym = ESpecialKey::Left;
  else if (sym == VirtualKey::Right)
    specialSym = ESpecialKey::Right;
  else if (sym == VirtualKey::Up)
    specialSym = ESpecialKey::Up;
  else if (sym == VirtualKey::Down)
    specialSym = ESpecialKey::Down;
  else if (sym == VirtualKey::Shift)
    modifierSym = EModifierKey::Shift;
  else if (sym == VirtualKey::Control)
    modifierSym = EModifierKey::Ctrl;
  else if (sym == VirtualKey::Menu)
    modifierSym = EModifierKey::Alt;
  else if (sym >= VirtualKey::A && sym <= VirtualKey::Z)
    return uint32_t(sym - VirtualKey::A) + (window->GetKeyState(VirtualKey::Shift) != CoreVirtualKeyStates::None) ? 'A'
                                                                                                                  : 'a';
  return 0;
}

static EModifierKey translateModifiers(CoreWindow ^ window) {
  EModifierKey retval = EModifierKey::None;
  if (window->GetKeyState(VirtualKey::Shift) != CoreVirtualKeyStates::None)
    retval |= EModifierKey::Shift;
  if (window->GetKeyState(VirtualKey::Control) != CoreVirtualKeyStates::None)
    retval |= EModifierKey::Ctrl;
  if (window->GetKeyState(VirtualKey::Menu) != CoreVirtualKeyStates::None)
    retval |= EModifierKey::Alt;
  return retval;
}

class WindowUWP : public IWindow {
  friend struct GraphicsContextUWP;
  ApplicationView ^ m_appView = ApplicationView::GetForCurrentView();
  Platform::Agile<CoreWindow> m_coreWindow;
  Rect m_bounds;
  float m_dispInfoDpiFactor = 1.f;
  std::unique_ptr<GraphicsContextUWP> m_gfxCtx;
  IWindowCallback* m_callback = nullptr;

public:
  ref struct EventReceiver sealed {
    void OnKeyDown(CoreWindow ^ window, KeyEventArgs ^ keyEventArgs) { w.OnKeyDown(window, keyEventArgs); }

    void OnKeyUp(CoreWindow ^ window, KeyEventArgs ^ keyEventArgs) { w.OnKeyUp(window, keyEventArgs); }

    void OnPointerEntered(CoreWindow ^ window, PointerEventArgs ^ args) { w.OnPointerEntered(window, args); }

    void OnPointerExited(CoreWindow ^ window, PointerEventArgs ^ args) { w.OnPointerExited(window, args); }

    void OnPointerMoved(CoreWindow ^ window, PointerEventArgs ^ args) { w.OnPointerMoved(window, args); }

    void OnPointerPressed(CoreWindow ^ window, PointerEventArgs ^ args) { w.OnPointerPressed(window, args); }

    void OnPointerReleased(CoreWindow ^ window, PointerEventArgs ^ args) { w.OnPointerReleased(window, args); }

    void OnPointerWheelChanged(CoreWindow ^ window, PointerEventArgs ^ args) { w.OnPointerWheelChanged(window, args); }

    void OnClosed(CoreWindow ^ sender, CoreWindowEventArgs ^ args) { w.OnClosed(sender, args); }

    void SizeChanged(CoreWindow ^ window, WindowSizeChangedEventArgs ^) {
      w.m_bounds = window->Bounds;
      w._resized();
    }

    void DisplayInfoChanged(DisplayInformation ^ di, Object ^) {
      w.m_dispInfoDpiFactor = di->LogicalDpi / 96.f;
      w._resized();
    }

    internal : WindowUWP& w;
    EventReceiver(WindowUWP& w) : w(w) {
      w.m_coreWindow->KeyDown +=
          ref new TypedEventHandler<CoreWindow ^, KeyEventArgs ^>(this, &EventReceiver::OnKeyDown);
      w.m_coreWindow->KeyUp += ref new TypedEventHandler<CoreWindow ^, KeyEventArgs ^>(this, &EventReceiver::OnKeyUp);
      w.m_coreWindow->PointerEntered +=
          ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &EventReceiver::OnPointerEntered);
      w.m_coreWindow->PointerExited +=
          ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &EventReceiver::OnPointerExited);
      w.m_coreWindow->PointerMoved +=
          ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &EventReceiver::OnPointerMoved);
      w.m_coreWindow->PointerPressed +=
          ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &EventReceiver::OnPointerPressed);
      w.m_coreWindow->PointerReleased +=
          ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &EventReceiver::OnPointerReleased);
      w.m_coreWindow->PointerWheelChanged +=
          ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &EventReceiver::OnPointerWheelChanged);
      w.m_coreWindow->Closed +=
          ref new TypedEventHandler<CoreWindow ^, CoreWindowEventArgs ^>(this, &EventReceiver::OnClosed);
      w.m_coreWindow->SizeChanged +=
          ref new TypedEventHandler<CoreWindow ^, WindowSizeChangedEventArgs ^>(this, &EventReceiver::SizeChanged);
      DisplayInformation::GetForCurrentView()->DpiChanged +=
          ref new TypedEventHandler<DisplayInformation ^, Object ^>(this, &EventReceiver::DisplayInfoChanged);
    }
  };
  EventReceiver ^ m_eventReceiver;

  WindowUWP(SystemStringView title, Boo3DAppContextUWP& b3dCtx)
  : m_coreWindow(CoreWindow::GetForCurrentThread()), m_eventReceiver(ref new EventReceiver(*this)) {
    IGraphicsContext::EGraphicsAPI api = IGraphicsContext::EGraphicsAPI::D3D11;
#if _WIN32_WINNT_WIN10
    if (b3dCtx.m_ctx12.m_dev)
      api = IGraphicsContext::EGraphicsAPI::D3D12;
#endif
    m_gfxCtx.reset(new GraphicsContextUWPD3D(api, this, m_coreWindow, b3dCtx));

    setTitle(title);
    m_bounds = m_coreWindow->Bounds;
    m_dispInfoDpiFactor = DisplayInformation::GetForCurrentView()->LogicalDpi / 96.f;
    if (auto titleBar = ApplicationView::GetForCurrentView()->TitleBar) {
      Color grey = {0xFF, 0x33, 0x33, 0x33};
      Color transWhite = {0xFF, 0x88, 0x88, 0x88};

      titleBar->ButtonBackgroundColor = grey;
      titleBar->ButtonForegroundColor = Colors::White;
      titleBar->BackgroundColor = grey;
      titleBar->ForegroundColor = Colors::White;

      titleBar->ButtonInactiveBackgroundColor = grey;
      titleBar->ButtonInactiveForegroundColor = transWhite;
      titleBar->InactiveBackgroundColor = grey;
      titleBar->InactiveForegroundColor = transWhite;
    }
  }

  ~WindowUWP() {}

  void setCallback(IWindowCallback* cb) { m_callback = cb; }

  void closeWindow() { m_coreWindow->Close(); }

  void showWindow() {}

  void hideWindow() {}

  SystemString getTitle() { return SystemString(m_appView->Title->Data()); }

  void setTitle(SystemStringView title) { m_appView->Title = ref new Platform::String(title.data()); }

  void setCursor(EMouseCursor cursor) {}

  void setWaitCursor(bool wait) {}

  double getWindowRefreshRate() const {
    /* TODO: Actually get refresh rate */
    return 60.0;
  }

  void setWindowFrameDefault() {}

  void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const {
    xOut = m_bounds.X * m_dispInfoDpiFactor;
    yOut = m_bounds.Y * m_dispInfoDpiFactor;
    wOut = m_bounds.Width * m_dispInfoDpiFactor;
    hOut = m_bounds.Height * m_dispInfoDpiFactor;
  }

  void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const {
    xOut = m_bounds.X * m_dispInfoDpiFactor;
    yOut = m_bounds.Y * m_dispInfoDpiFactor;
    wOut = m_bounds.Width * m_dispInfoDpiFactor;
    hOut = m_bounds.Height * m_dispInfoDpiFactor;
  }

  void setWindowFrame(float x, float y, float w, float h) {}

  void setWindowFrame(int x, int y, int w, int h) {}

  float getVirtualPixelFactor() const { return m_dispInfoDpiFactor; }

  bool isFullscreen() const { return ApplicationView::GetForCurrentView()->IsFullScreenMode; }

  void setFullscreen(bool fs) {
    if (fs)
      ApplicationView::GetForCurrentView()->TryEnterFullScreenMode();
    else
      ApplicationView::GetForCurrentView()->ExitFullScreenMode();
  }

  void claimKeyboardFocus(const int coord[2]) {}

  bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) { return false; }

  std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) { return std::unique_ptr<uint8_t[]>(); }

  int waitForRetrace(IAudioVoiceEngine* engine) {
    if (engine)
      engine->pumpAndMixVoices();
    m_gfxCtx->m_output->WaitForVBlank();
    return 1;
  }

  uintptr_t getPlatformHandle() const { return 0; }

  bool _incomingEvent(void* ev) { return false; }

  void OnKeyDown(CoreWindow ^ window, KeyEventArgs ^ keyEventArgs) {
    ESpecialKey specialKey;
    EModifierKey modifierKey;
    uint32_t charCode = translateKeysym(m_coreWindow.Get(), keyEventArgs->VirtualKey, specialKey, modifierKey);
    EModifierKey modifierMask = translateModifiers(window);
    bool repeat = keyEventArgs->KeyStatus.RepeatCount > 1;
    if (charCode)
      m_callback->charKeyDown(charCode, modifierMask, repeat);
    else if (specialKey != ESpecialKey::None)
      m_callback->specialKeyDown(specialKey, modifierMask, repeat);
    else if (modifierKey != EModifierKey::None)
      m_callback->modKeyDown(modifierKey, repeat);
  }

  void OnKeyUp(CoreWindow ^ window, KeyEventArgs ^ keyEventArgs) {
    ESpecialKey specialKey;
    EModifierKey modifierKey;
    uint32_t charCode = translateKeysym(m_coreWindow.Get(), keyEventArgs->VirtualKey, specialKey, modifierKey);
    EModifierKey modifierMask = translateModifiers(window);
    if (charCode)
      m_callback->charKeyUp(charCode, modifierMask);
    else if (specialKey != ESpecialKey::None)
      m_callback->specialKeyUp(specialKey, modifierMask);
    else if (modifierKey != EModifierKey::None)
      m_callback->modKeyUp(modifierKey);
  }

  SWindowCoord GetCursorCoords(const Point& point) {
    SWindowCoord coord = {point.X * m_dispInfoDpiFactor,
                          (m_bounds.Height - point.Y) * m_dispInfoDpiFactor,
                          point.X,
                          m_bounds.Height - point.Y,
                          point.X / m_bounds.Width,
                          (m_bounds.Height - point.Y) / m_bounds.Height};
    return coord;
  }

  void OnPointerEntered(CoreWindow ^ window, PointerEventArgs ^ args) {
    m_callback->mouseEnter(GetCursorCoords(args->CurrentPoint->Position));
  }

  void OnPointerExited(CoreWindow ^ window, PointerEventArgs ^ args) {
    m_callback->mouseLeave(GetCursorCoords(args->CurrentPoint->Position));
  }

  void OnPointerMoved(CoreWindow ^ window, PointerEventArgs ^ args) {
    m_callback->mouseMove(GetCursorCoords(args->CurrentPoint->Position));
  }

  boo::EMouseButton m_pressedButton = boo::EMouseButton::None;
  void OnPointerPressed(CoreWindow ^ window, PointerEventArgs ^ args) {
    auto properties = args->CurrentPoint->Properties;
    boo::EMouseButton button = boo::EMouseButton::None;
    if (properties->IsLeftButtonPressed)
      button = boo::EMouseButton::Primary;
    else if (properties->IsMiddleButtonPressed)
      button = boo::EMouseButton::Middle;
    else if (properties->IsRightButtonPressed)
      button = boo::EMouseButton::Secondary;
    else if (properties->IsXButton1Pressed)
      button = boo::EMouseButton::Aux1;
    else if (properties->IsXButton2Pressed)
      button = boo::EMouseButton::Aux2;
    m_callback->mouseDown(GetCursorCoords(args->CurrentPoint->Position), button,
                          translateModifiers(m_coreWindow.Get()));
    m_pressedButton = button;
  }

  void OnPointerReleased(CoreWindow ^ window, PointerEventArgs ^ args) {
    auto properties = args->CurrentPoint->Properties;
    m_callback->mouseUp(GetCursorCoords(args->CurrentPoint->Position), m_pressedButton,
                        translateModifiers(m_coreWindow.Get()));
  }

  void OnPointerWheelChanged(CoreWindow ^ window, PointerEventArgs ^ args) {
    auto properties = args->CurrentPoint->Properties;
    SScrollDelta scroll = {};
    scroll.delta[1] = properties->MouseWheelDelta / double(WHEEL_DELTA);
    m_callback->scroll(GetCursorCoords(args->CurrentPoint->Position), scroll);
  }

  void OnClosed(CoreWindow ^ sender, CoreWindowEventArgs ^ args) {
    if (m_callback)
      m_callback->destroyed();
  }

  void _resized() {
    boo::SWindowRect rect(m_bounds.X * m_dispInfoDpiFactor, m_bounds.Y * m_dispInfoDpiFactor,
                          m_bounds.Width * m_dispInfoDpiFactor, m_bounds.Height * m_dispInfoDpiFactor);
    m_gfxCtx->resized(rect);
    if (m_callback)
      m_callback->resized(rect, false);
  }

  ETouchType getTouchType() const { return ETouchType::None; }

  void setStyle(EWindowStyle style) {}

  EWindowStyle getStyle() const {
    EWindowStyle retval = EWindowStyle::None;
    return retval;
  }

  IGraphicsCommandQueue* getCommandQueue() { return m_gfxCtx->getCommandQueue(); }
  IGraphicsDataFactory* getDataFactory() { return m_gfxCtx->getDataFactory(); }

  /* Creates a new context on current thread!! Call from main client thread */
  IGraphicsDataFactory* getMainContextDataFactory() { return m_gfxCtx->getMainContextDataFactory(); }

  /* Creates a new context on current thread!! Call from client loading thread */
  IGraphicsDataFactory* getLoadContextDataFactory() { return m_gfxCtx->getLoadContextDataFactory(); }
};

std::shared_ptr<IWindow> _WindowUWPNew(SystemStringView title, Boo3DAppContextUWP& d3dCtx) {
  return std::make_shared<WindowUWP>(title, d3dCtx);
}

} // namespace boo

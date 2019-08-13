#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

#include <X11/Xlib.h>
#undef None

namespace boo {

struct GraphicsContextWayland : IGraphicsContext {

  EGraphicsAPI m_api;
  EPixelFormat m_pf;
  IWindow* m_parentWindow;

public:
  IWindowCallback* m_callback;

  GraphicsContextWayland(EGraphicsAPI api, IWindow* parentWindow)
  : m_api(api), m_pf(EPixelFormat::RGBA8), m_parentWindow(parentWindow) {}

  ~GraphicsContextWayland() override = default;

  void _setCallback(IWindowCallback* cb) override { m_callback = cb; }

  EGraphicsAPI getAPI() const override { return m_api; }

  EPixelFormat getPixelFormat() const override { return m_pf; }

  void setPixelFormat(EPixelFormat pf) override {
    if (pf > EPixelFormat::RGBAF32_Z24)
      return;
    m_pf = pf;
  }

  bool initializeContext(void*) override { return false; }

  void makeCurrent() override {}

  void postInit() override {}

  IGraphicsCommandQueue* getCommandQueue() override { return nullptr; }

  IGraphicsDataFactory* getDataFactory() override { return nullptr; }

  IGraphicsDataFactory* getMainContextDataFactory() override { return nullptr; }

  IGraphicsDataFactory* getLoadContextDataFactory() override { return nullptr; }

  void present() override {}
};

struct WindowWayland : IWindow {
  GraphicsContextWayland m_gfxCtx;

  WindowWayland(std::string_view title) : m_gfxCtx(IGraphicsContext::EGraphicsAPI::OpenGL3_3, this) {}

  ~WindowWayland() override = default;

  void setCallback(IWindowCallback* cb) override {}

  void closeWindow() override {}

  void showWindow() override {}

  void hideWindow() override {}

  std::string getTitle() override { return ""; }

  void setTitle(std::string_view title) override {}

  void setCursor(EMouseCursor cursor) override {}

  void setWaitCursor(bool wait) override {}

  double getWindowRefreshRate() const override { return 60.0; }

  void setWindowFrameDefault() override {}

  void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const override {}

  void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const override {}

  void setWindowFrame(float x, float y, float w, float h) override {}

  void setWindowFrame(int x, int y, int w, int h) override {}

  float getVirtualPixelFactor() const override { return 0; }

  void setStyle(EWindowStyle /*style*/) override {}

  EWindowStyle getStyle() const override { return EWindowStyle::None; }

  bool isFullscreen() const override { return false; }

  void setFullscreen(bool fs) override {}

  void claimKeyboardFocus(const int coord[2]) override {}

  bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) override { return false; }

  std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) override { return std::unique_ptr<uint8_t[]>(); }

  int waitForRetrace() override { return 1; }

  uintptr_t getPlatformHandle() const override { return 0; }

  ETouchType getTouchType() const override { return ETouchType::None; }

  IGraphicsCommandQueue* getCommandQueue() override { return m_gfxCtx.getCommandQueue(); }

  IGraphicsDataFactory* getDataFactory() override { return m_gfxCtx.getDataFactory(); }

  IGraphicsDataFactory* getMainContextDataFactory() override { return m_gfxCtx.getMainContextDataFactory(); }

  IGraphicsDataFactory* getLoadContextDataFactory() override { return m_gfxCtx.getLoadContextDataFactory(); }
};

std::shared_ptr<IWindow> _WindowWaylandNew(std::string_view title) { return std::make_shared<WindowWayland>(title); }

} // namespace boo

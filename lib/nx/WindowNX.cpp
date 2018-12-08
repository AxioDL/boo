#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "logvisor/logvisor.hpp"
#include "boo/graphicsdev/NX.hpp"

#include <switch.h>

namespace boo {

std::unique_ptr<IGraphicsCommandQueue> _NewNXCommandQueue(NXContext* ctx, IGraphicsContext* parent);
std::unique_ptr<IGraphicsDataFactory> _NewNXDataFactory(IGraphicsContext* parent, NXContext* ctx);

struct GraphicsContextNX : IGraphicsContext {
  NXContext* m_nxCtx;
  std::unique_ptr<IGraphicsDataFactory> m_dataFactory;
  std::unique_ptr<IGraphicsCommandQueue> m_commandQueue;

public:
  explicit GraphicsContextNX(NXContext* nxCtx) : m_nxCtx(nxCtx) {
    m_dataFactory = _NewNXDataFactory(this, nxCtx);
    m_commandQueue = _NewNXCommandQueue(nxCtx, this);
  }

  EGraphicsAPI getAPI() const { return EGraphicsAPI::NX; }
  EPixelFormat getPixelFormat() const { return EPixelFormat::RGBA8; }
  void setPixelFormat(EPixelFormat pf) {}
  bool initializeContext(void* handle) { return m_nxCtx->initialize(); }
  void makeCurrent() {}
  void postInit() {}
  void present() {}

  IGraphicsCommandQueue* getCommandQueue() { return m_commandQueue.get(); }
  IGraphicsDataFactory* getDataFactory() { return m_dataFactory.get(); }
  IGraphicsDataFactory* getMainContextDataFactory() { return m_dataFactory.get(); }
  IGraphicsDataFactory* getLoadContextDataFactory() { return m_dataFactory.get(); }
};

class WindowNX : public IWindow {
  std::string m_title;
  std::unique_ptr<GraphicsContextNX> m_gfxCtx;
  IWindowCallback* m_callback = nullptr;

public:
  WindowNX(std::string_view title, NXContext* nxCtx) : m_title(title), m_gfxCtx(new GraphicsContextNX(nxCtx)) {
    m_gfxCtx->initializeContext(nullptr);
  }

  void setCallback(IWindowCallback* cb) { m_callback = cb; }

  void closeWindow() {}
  void showWindow() {}
  void hideWindow() {}

  SystemString getTitle() { return m_title; }
  void setTitle(SystemStringView title) { m_title = title; }

  void setCursor(EMouseCursor cursor) {}
  void setWaitCursor(bool wait) {}

  void setWindowFrameDefault() {}
  void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const {
    u32 width, height;
    gfxGetFramebufferResolution(&width, &height);
    xOut = 0;
    yOut = 0;
    wOut = width;
    hOut = height;
  }
  void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const {
    u32 width, height;
    gfxGetFramebufferResolution(&width, &height);
    xOut = 0;
    yOut = 0;
    wOut = width;
    hOut = height;
  }
  void setWindowFrame(float x, float y, float w, float h) {}
  void setWindowFrame(int x, int y, int w, int h) {}
  float getVirtualPixelFactor() const { return 1.f; }

  bool isFullscreen() const { return true; }
  void setFullscreen(bool fs) {}

  void claimKeyboardFocus(const int coord[2]) {}
  bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) { return false; }
  std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) { return {}; }

  void waitForRetrace() {}

  uintptr_t getPlatformHandle() const { return 0; }
  bool _incomingEvent(void* event) {
    (void)event;
    return false;
  }
  void _cleanup() {}

  ETouchType getTouchType() const { return ETouchType::Display; }

  void setStyle(EWindowStyle style) {}
  EWindowStyle getStyle() const { return EWindowStyle::None; }

  void setTouchBarProvider(void*) {}

  IGraphicsCommandQueue* getCommandQueue() { return m_gfxCtx->getCommandQueue(); }
  IGraphicsDataFactory* getDataFactory() { return m_gfxCtx->getDataFactory(); }
  IGraphicsDataFactory* getMainContextDataFactory() { return m_gfxCtx->getMainContextDataFactory(); }
  IGraphicsDataFactory* getLoadContextDataFactory() { return m_gfxCtx->getLoadContextDataFactory(); }
};

std::shared_ptr<IWindow> _WindowNXNew(std::string_view title, NXContext* nxCtx) {
  std::shared_ptr<IWindow> ret = std::make_shared<WindowNX>(title, nxCtx);
  return ret;
}

} // namespace boo

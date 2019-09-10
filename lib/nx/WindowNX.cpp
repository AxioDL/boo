#include "boo/IWindow.hpp"

#include "boo/IGraphicsContext.hpp"
#include "boo/graphicsdev/NX.hpp"

#include <logvisor/logvisor.hpp>
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

  EGraphicsAPI getAPI() const override { return EGraphicsAPI::NX; }
  EPixelFormat getPixelFormat() const override { return EPixelFormat::RGBA8; }
  void setPixelFormat(EPixelFormat pf) override {}
  bool initializeContext(void* handle) override { return m_nxCtx->initialize(); }
  void makeCurrent() override {}
  void postInit() override {}
  void present() override {}

  IGraphicsCommandQueue* getCommandQueue() override { return m_commandQueue.get(); }
  IGraphicsDataFactory* getDataFactory() override { return m_dataFactory.get(); }
  IGraphicsDataFactory* getMainContextDataFactory() override { return m_dataFactory.get(); }
  IGraphicsDataFactory* getLoadContextDataFactory() override { return m_dataFactory.get(); }
};

class WindowNX : public IWindow {
  std::string m_title;
  std::unique_ptr<GraphicsContextNX> m_gfxCtx;
  IWindowCallback* m_callback = nullptr;

public:
  WindowNX(std::string_view title, NXContext* nxCtx) : m_title(title), m_gfxCtx(new GraphicsContextNX(nxCtx)) {
    m_gfxCtx->initializeContext(nullptr);
  }

  void setCallback(IWindowCallback* cb) override { m_callback = cb; }

  void closeWindow() override {}
  void showWindow() override {}
  void hideWindow() override {}

  SystemString getTitle() override { return m_title; }
  void setTitle(SystemStringView title) override { m_title = title; }

  void setCursor(EMouseCursor cursor) override {}
  void setWaitCursor(bool wait) override {}

  void setWindowFrameDefault() override {}
  void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const override {
    u32 width, height;
    gfxGetFramebufferResolution(&width, &height);
    xOut = 0;
    yOut = 0;
    wOut = width;
    hOut = height;
  }
  void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const override {
    u32 width, height;
    gfxGetFramebufferResolution(&width, &height);
    xOut = 0;
    yOut = 0;
    wOut = width;
    hOut = height;
  }
  void setWindowFrame(float x, float y, float w, float h) override {}
  void setWindowFrame(int x, int y, int w, int h) override {}
  float getVirtualPixelFactor() const override { return 1.f; }

  bool isFullscreen() const override { return true; }
  void setFullscreen(bool fs) override {}

  void claimKeyboardFocus(const int coord[2]) override {}
  bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) override { return false; }
  std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) override { return {}; }

  void waitForRetrace() override {}

  uintptr_t getPlatformHandle() const override { return 0; }
  bool _incomingEvent([[maybe_unused]] void* event) override { return false; }
  void _cleanup() override {}

  ETouchType getTouchType() const override { return ETouchType::Display; }

  void setStyle(EWindowStyle style) override {}
  EWindowStyle getStyle() const override { return EWindowStyle::None; }

  void setTouchBarProvider(void*) override {}

  IGraphicsCommandQueue* getCommandQueue() override { return m_gfxCtx->getCommandQueue(); }
  IGraphicsDataFactory* getDataFactory() override { return m_gfxCtx->getDataFactory(); }
  IGraphicsDataFactory* getMainContextDataFactory() override { return m_gfxCtx->getMainContextDataFactory(); }
  IGraphicsDataFactory* getLoadContextDataFactory() override { return m_gfxCtx->getLoadContextDataFactory(); }
};

std::shared_ptr<IWindow> _WindowNXNew(std::string_view title, NXContext* nxCtx) {
  std::shared_ptr<IWindow> ret = std::make_shared<WindowNX>(title, nxCtx);
  return ret;
}

} // namespace boo

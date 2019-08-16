#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/IApplication.hpp"
#include "boo/graphicsdev/GL.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"
#include "boo/graphicsdev/glew.h"
#include "../Common.hpp"

#if BOO_HAS_VULKAN
#include "boo/graphicsdev/Vulkan.hpp"
#include <X11/Xlib-xcb.h>
#endif

#include <limits.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <thread>
#include <mutex>
#include <condition_variable>

#include <GL/glx.h>

#define XK_MISCELLANY
#define XK_XKB_KEYS
#define XK_LATIN1
#include <X11/keysymdef.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include "logvisor/logvisor.hpp"

#include "XlibCommon.hpp"

#define REF_DPMM 3.78138
#define FS_ATOM "_NET_WM_STATE_FULLSCREEN"

#define MWM_HINTS_FUNCTIONS (1L << 0)
#define MWM_HINTS_DECORATIONS (1L << 1)

#define MWM_DECOR_BORDER (1L << 1)
#define MWM_DECOR_RESIZEH (1L << 2)
#define MWM_DECOR_TITLE (1L << 3)
#define MWM_DECOR_MENU (1L << 4)
#define MWM_DECOR_MINIMIZE (1L << 5)
#define MWM_DECOR_MAXIMIZE (1L << 6)

#define MWM_FUNC_RESIZE (1L << 1)
#define MWM_FUNC_MOVE (1L << 2)
#define MWM_FUNC_MINIMIZE (1L << 3)
#define MWM_FUNC_MAXIMIZE (1L << 4)
#define MWM_FUNC_CLOSE (1L << 5)

#undef None
#undef False
#undef True

using glXCreateContextAttribsARBProc = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
static bool s_glxError;
static int ctxErrorHandler(Display* dpy, XErrorEvent* ev) {
  s_glxError = true;
  return 0;
}

static const int ContextAttribList[7][7] = {
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 5, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 4, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 3, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 2, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 1, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 0, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
    {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, GLX_CONTEXT_PROFILE_MASK_ARB,
     GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0},
};

extern "C" const uint8_t MAINICON_NETWM[];
extern "C" const size_t MAINICON_NETWM_SZ;

namespace boo {
static logvisor::Module Log("boo::WindowXlib");
std::unique_ptr<IGraphicsCommandQueue> _NewGLCommandQueue(IGraphicsContext* parent, GLContext* glCtx);
std::unique_ptr<IGraphicsDataFactory> _NewGLDataFactory(IGraphicsContext* parent, GLContext* glCtx);
#if BOO_HAS_VULKAN
std::unique_ptr<IGraphicsCommandQueue> _NewVulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx,
                                                              IGraphicsContext* parent);
std::unique_ptr<IGraphicsDataFactory> _NewVulkanDataFactory(IGraphicsContext* parent, VulkanContext* ctx);
#endif
void _XlibUpdateLastGlxCtx(GLXContext lastGlxCtx);
void GLXExtensionCheck();
void GLXEnableVSync(Display* disp, GLXWindow drawable);

extern int XINPUT_OPCODE;

static std::string translateUTF8(XKeyEvent* ev, XIC xIC) {
  char chs[512];
  KeySym ks;
  Status stat;
  int len = Xutf8LookupString(xIC, ev, chs, 512, &ks, &stat);
  if (len > 1 && (stat == XLookupChars || stat == XLookupBoth))
    return std::string(chs, len);
  return std::string();
}

static char translateKeysym(XKeyEvent* ev, ESpecialKey& specialSym, EModifierKey& modifierSym) {
  KeySym sym = XLookupKeysym(ev, 0);

  specialSym = ESpecialKey::None;
  modifierSym = EModifierKey::None;
  if (sym >= XK_F1 && sym <= XK_F12)
    specialSym = ESpecialKey(int(ESpecialKey::F1) + sym - XK_F1);
  else if (sym == XK_Escape)
    specialSym = ESpecialKey::Esc;
  else if (sym == XK_Return)
    specialSym = ESpecialKey::Enter;
  else if (sym == XK_BackSpace)
    specialSym = ESpecialKey::Backspace;
  else if (sym == XK_Insert)
    specialSym = ESpecialKey::Insert;
  else if (sym == XK_Delete)
    specialSym = ESpecialKey::Delete;
  else if (sym == XK_Home)
    specialSym = ESpecialKey::Home;
  else if (sym == XK_End)
    specialSym = ESpecialKey::End;
  else if (sym == XK_Page_Up)
    specialSym = ESpecialKey::PgUp;
  else if (sym == XK_Page_Down)
    specialSym = ESpecialKey::PgDown;
  else if (sym == XK_Left)
    specialSym = ESpecialKey::Left;
  else if (sym == XK_Right)
    specialSym = ESpecialKey::Right;
  else if (sym == XK_Up)
    specialSym = ESpecialKey::Up;
  else if (sym == XK_Down)
    specialSym = ESpecialKey::Down;
  else if (sym == XK_Shift_L || sym == XK_Shift_R)
    modifierSym = EModifierKey::Shift;
  else if (sym == XK_Control_L || sym == XK_Control_R)
    modifierSym = EModifierKey::Ctrl;
  else if (sym == XK_Alt_L || sym == XK_Alt_R)
    modifierSym = EModifierKey::Alt;
  else {
    char ch = 0;
    KeySym ks;
    XLookupString(ev, (char*)&ch, 1, &ks, nullptr);
    return ch;
  }
  return 0;
}

static EModifierKey translateModifiers(unsigned state) {
  EModifierKey retval = EModifierKey::None;
  if (state & ShiftMask)
    retval |= EModifierKey::Shift;
  if (state & ControlMask)
    retval |= EModifierKey::Ctrl;
  if (state & Mod1Mask)
    retval |= EModifierKey::Alt;
  return retval;
}

static EMouseButton translateButton(unsigned detail) {
  switch (detail) {
  case 1:
    return EMouseButton::Primary;
  case 3:
    return EMouseButton::Secondary;
  case 2:
    return EMouseButton::Middle;
  case 8:
    return EMouseButton::Aux1;
  case 9:
    return EMouseButton::Aux2;
  default:
    break;
  }
  return EMouseButton::None;
}

struct XlibAtoms {
  Atom m_wmProtocols = 0;
  Atom m_wmDeleteWindow = 0;
  Atom m_netSupported = 0;
  Atom m_netwmName = 0;
  Atom m_netwmPid = 0;
  Atom m_netwmIcon = 0;
  Atom m_netwmIconName = 0;
  Atom m_netwmState = 0;
  Atom m_netwmStateFullscreen = 0;
  Atom m_netwmStateAdd = 0;
  Atom m_netwmStateRemove = 0;
  Atom m_motifWmHints = 0;
  Atom m_targets = 0;
  Atom m_clipboard = 0;
  Atom m_clipdata = 0;
  Atom m_utf8String = 0;
  Atom m_imagePng = 0;
  XlibAtoms(Display* disp) {
    m_wmProtocols = XInternAtom(disp, "WM_PROTOCOLS", true);
    m_wmDeleteWindow = XInternAtom(disp, "WM_DELETE_WINDOW", true);
    m_netSupported = XInternAtom(disp, "_NET_SUPPORTED", true);
    m_netwmName = XInternAtom(disp, "_NET_WM_NAME", false);
    m_netwmPid = XInternAtom(disp, "_NET_WM_PID", false);
    m_netwmIcon = XInternAtom(disp, "_NET_WM_ICON", false);
    m_netwmIconName = XInternAtom(disp, "_NET_WM_ICON_NAME", false);
    m_netwmState = XInternAtom(disp, "_NET_WM_STATE", false);
    m_netwmStateFullscreen = XInternAtom(disp, "_NET_WM_STATE_FULLSCREEN", false);
    m_netwmStateAdd = XInternAtom(disp, "_NET_WM_STATE_ADD", false);
    m_netwmStateRemove = XInternAtom(disp, "_NET_WM_STATE_REMOVE", false);
    m_motifWmHints = XInternAtom(disp, "_MOTIF_WM_HINTS", true);
    m_targets = XInternAtom(disp, "TARGETS", false);
    m_clipboard = XInternAtom(disp, "CLIPBOARD", false);
    m_clipdata = XInternAtom(disp, "CLIPDATA", false);
    m_utf8String = XInternAtom(disp, "UTF8_STRING", false);
    m_imagePng = XInternAtom(disp, "image/png", false);
  }
};
static XlibAtoms* S_ATOMS = nullptr;

static Atom GetClipboardTypeAtom(EClipboardType t) {
  switch (t) {
  case EClipboardType::String:
    return XA_STRING;
  case EClipboardType::UTF8String:
    return S_ATOMS->m_utf8String;
  case EClipboardType::PNGImage:
    return S_ATOMS->m_imagePng;
  default:
    return 0;
  }
}

static void genFrameDefault(Screen* screen, int& xOut, int& yOut, int& wOut, int& hOut) {
  float width = screen->width * 2.0 / 3.0;
  float height = screen->height * 2.0 / 3.0;
  xOut = (screen->width - width) / 2.0;
  yOut = (screen->height - height) / 2.0;
  wOut = width;
  hOut = height;
}

static void genFrameDefault(XRRMonitorInfo* screen, int& xOut, int& yOut, int& wOut, int& hOut) {
  float width = screen->width * 2.0 / 3.0;
  float height = screen->height * 2.0 / 3.0;
  xOut = (screen->width - width) / 2.0 + screen->x;
  yOut = (screen->height - height) / 2.0 + screen->y;
  wOut = width;
  hOut = height;
}

struct GraphicsContextXlib : IGraphicsContext {
  EGraphicsAPI m_api;
  EPixelFormat m_pf;
  uint32_t m_drawSamples;
  IWindow* m_parentWindow;
  GLContext* m_glCtx;
  Display* m_xDisp;

  GraphicsContextXlib(EGraphicsAPI api, EPixelFormat pf, IWindow* parentWindow, Display* disp, GLContext* glCtx)
  : m_api(api), m_pf(pf), m_parentWindow(parentWindow), m_glCtx(glCtx), m_xDisp(disp) {}
  virtual void destroy() = 0;
  virtual void resized(const SWindowRect& rect) = 0;
};

struct GraphicsContextXlibGLX : GraphicsContextXlib {
  GLXContext m_lastCtx = 0;

  GLXFBConfig m_fbconfig = 0;
  int m_visualid = 0;
  int m_attribIdx = 0;
  GLXWindow m_glxWindow = 0;
  GLXContext m_glxCtx = 0;

  std::unique_ptr<IGraphicsDataFactory> m_dataFactory;
  std::unique_ptr<IGraphicsCommandQueue> m_commandQueue;
  GLXContext m_mainCtx = 0;
  GLXContext m_loadCtx = 0;

public:
  IWindowCallback* m_callback;

  GraphicsContextXlibGLX(EGraphicsAPI api, IWindow* parentWindow, Display* display, int defaultScreen,
                         GLXContext lastCtx, uint32_t& visualIdOut, GLContext* glCtx)
  : GraphicsContextXlib(api, glCtx->m_deepColor ? EPixelFormat::RGBA16 : EPixelFormat::RGBA8, parentWindow, display,
                        glCtx)
  , m_lastCtx(lastCtx) {
    m_dataFactory = _NewGLDataFactory(this, m_glCtx);

    /* Query framebuffer configurations */
    GLXFBConfig* fbConfigs = nullptr;
    int numFBConfigs = 0;
    fbConfigs = glXGetFBConfigs(display, defaultScreen, &numFBConfigs);
    if (!fbConfigs || numFBConfigs == 0) {
      Log.report(logvisor::Fatal, fmt("glXGetFBConfigs failed"));
      return;
    }

    for (int i = 0; i < numFBConfigs; ++i) {
      GLXFBConfig config = fbConfigs[i];
      int visualId, depthSize, colorSize, doubleBuffer;
      glXGetFBConfigAttrib(display, config, GLX_VISUAL_ID, &visualId);
      glXGetFBConfigAttrib(display, config, GLX_DEPTH_SIZE, &depthSize);
      glXGetFBConfigAttrib(display, config, GLX_BUFFER_SIZE, &colorSize);
      glXGetFBConfigAttrib(display, config, GLX_DOUBLEBUFFER, &doubleBuffer);

      /* Double-buffer only */
      if (!doubleBuffer || !visualId)
        continue;

      if (m_pf == EPixelFormat::RGBA8 && colorSize >= 32) {
        m_fbconfig = config;
        m_visualid = visualId;
        break;
      } else if (m_pf == EPixelFormat::RGBA16) {
        if (colorSize >= 64) {
          m_fbconfig = config;
          m_visualid = visualId;
          break;
        } else if (!m_visualid && colorSize >= 32) {
          m_fbconfig = config;
          m_visualid = visualId;
        }
      } else if (m_pf == EPixelFormat::RGBA8_Z24 && colorSize >= 32 && depthSize >= 24) {
        m_fbconfig = config;
        m_visualid = visualId;
        break;
      } else if (m_pf == EPixelFormat::RGBAF32 && colorSize >= 128) {
        m_fbconfig = config;
        m_visualid = visualId;
        break;
      } else if (m_pf == EPixelFormat::RGBAF32_Z24 && colorSize >= 128 && depthSize >= 24) {
        m_fbconfig = config;
        m_visualid = visualId;
        break;
      }
    }
    XFree(fbConfigs);

    if (!m_fbconfig) {
      Log.report(logvisor::Fatal, fmt("unable to find suitable pixel format"));
      return;
    }

    visualIdOut = m_visualid;
  }

  void destroy() override {
    if (m_glxCtx) {
      glXDestroyContext(m_xDisp, m_glxCtx);
      m_glxCtx = nullptr;
    }
    if (m_glxWindow) {
      glXDestroyWindow(m_xDisp, m_glxWindow);
      m_glxWindow = 0;
    }
    if (m_loadCtx) {
      glXDestroyContext(m_xDisp, m_loadCtx);
      m_loadCtx = nullptr;
    }
  }

  ~GraphicsContextXlibGLX() override { destroy(); }

  void resized(const SWindowRect& rect) override {}

  void _setCallback(IWindowCallback* cb) override { m_callback = cb; }

  EGraphicsAPI getAPI() const override { return m_api; }

  EPixelFormat getPixelFormat() const override { return m_pf; }

  void setPixelFormat(EPixelFormat pf) override {
    if (pf > EPixelFormat::RGBAF32_Z24)
      return;
    m_pf = pf;
  }

  bool initializeContext(void*) override {
    if (!glXCreateContextAttribsARB) {
      glXCreateContextAttribsARB =
          (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
      if (!glXCreateContextAttribsARB)
        Log.report(logvisor::Fatal, fmt("unable to resolve glXCreateContextAttribsARB"));
    }

    s_glxError = false;
    XErrorHandler oldHandler = XSetErrorHandler(ctxErrorHandler);
    for (m_attribIdx = 0; m_attribIdx < int(std::extent_v<decltype(ContextAttribList)>); ++m_attribIdx) {
      m_glxCtx = glXCreateContextAttribsARB(m_xDisp, m_fbconfig, m_lastCtx, true, ContextAttribList[m_attribIdx]);
      if (m_glxCtx)
        break;
    }
    XSetErrorHandler(oldHandler);
    if (!m_glxCtx)
      Log.report(logvisor::Fatal, fmt("unable to make new GLX context"));
    m_glxWindow = glXCreateWindow(m_xDisp, m_fbconfig, m_parentWindow->getPlatformHandle(), nullptr);
    if (!m_glxWindow)
      Log.report(logvisor::Fatal, fmt("unable to make new GLX window"));
    _XlibUpdateLastGlxCtx(m_glxCtx);

    if (!glXMakeCurrent(m_xDisp, DefaultRootWindow(m_xDisp), m_glxCtx))
      Log.report(logvisor::Fatal, fmt("unable to make GLX context current"));
    if (glewInit() != GLEW_OK)
      Log.report(logvisor::Fatal, fmt("glewInit failed"));
    glXMakeCurrent(m_xDisp, 0, 0);

    XUnlockDisplay(m_xDisp);
    m_commandQueue = _NewGLCommandQueue(this, m_glCtx);
    m_commandQueue->startRenderer();
    XLockDisplay(m_xDisp);

    return true;
  }

  void makeCurrent() override {
    XLockDisplay(m_xDisp);
    if (!glXMakeContextCurrent(m_xDisp, m_glxWindow, m_glxWindow, m_glxCtx))
      Log.report(logvisor::Fatal, fmt("unable to make GLX context current"));
    XUnlockDisplay(m_xDisp);
  }

  void postInit() override {
    GLXExtensionCheck();
    XLockDisplay(m_xDisp);
    GLXEnableVSync(m_xDisp, m_glxWindow);
    XUnlockDisplay(m_xDisp);
  }

  IGraphicsCommandQueue* getCommandQueue() override { return m_commandQueue.get(); }

  IGraphicsDataFactory* getDataFactory() override { return m_dataFactory.get(); }

  IGraphicsDataFactory* getMainContextDataFactory() override {
    XLockDisplay(m_xDisp);
    if (!m_mainCtx) {
      s_glxError = false;
      XErrorHandler oldHandler = XSetErrorHandler(ctxErrorHandler);
      m_mainCtx = glXCreateContextAttribsARB(m_xDisp, m_fbconfig, m_glxCtx, true, ContextAttribList[m_attribIdx]);
      XSetErrorHandler(oldHandler);
      if (!m_mainCtx)
        Log.report(logvisor::Fatal, fmt("unable to make main GLX context"));
    }
    if (!glXMakeContextCurrent(m_xDisp, m_glxWindow, m_glxWindow, m_mainCtx))
      Log.report(logvisor::Fatal, fmt("unable to make main GLX context current"));
    XUnlockDisplay(m_xDisp);
    return getDataFactory();
  }

  IGraphicsDataFactory* getLoadContextDataFactory() override {
    XLockDisplay(m_xDisp);
    if (!m_loadCtx) {
      s_glxError = false;
      XErrorHandler oldHandler = XSetErrorHandler(ctxErrorHandler);
      m_loadCtx = glXCreateContextAttribsARB(m_xDisp, m_fbconfig, m_glxCtx, true, ContextAttribList[m_attribIdx]);
      XSetErrorHandler(oldHandler);
      if (!m_loadCtx)
        Log.report(logvisor::Fatal, fmt("unable to make load GLX context"));
    }
    if (!glXMakeContextCurrent(m_xDisp, m_glxWindow, m_glxWindow, m_loadCtx))
      Log.report(logvisor::Fatal, fmt("unable to make load GLX context current"));
    XUnlockDisplay(m_xDisp);
    return getDataFactory();
  }

  void present() override { glXSwapBuffers(m_xDisp, m_glxWindow); }
};

#if BOO_HAS_VULKAN
struct GraphicsContextXlibVulkan : GraphicsContextXlib {
  xcb_connection_t* m_xcbConn;
  VulkanContext* m_ctx;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  VkColorSpaceKHR m_colorspace;

  GLXFBConfig m_fbconfig = 0;
  int m_visualid = 0;

  std::unique_ptr<IGraphicsDataFactory> m_dataFactory;
  std::unique_ptr<IGraphicsCommandQueue> m_commandQueue;

  static void ThrowIfFailed(VkResult res) {
    if (res != VK_SUCCESS)
      Log.report(logvisor::Fatal, fmt("{}\n"), res);
  }

public:
  IWindowCallback* m_callback;

  GraphicsContextXlibVulkan(IWindow* parentWindow, Display* display, xcb_connection_t* xcbConn, int defaultScreen,
                            VulkanContext* ctx, uint32_t& visualIdOut, GLContext* glCtx)
  : GraphicsContextXlib(EGraphicsAPI::Vulkan, ctx->m_deepColor ? EPixelFormat::RGBA16 : EPixelFormat::RGBA8,
                        parentWindow, display, glCtx)
  , m_xcbConn(xcbConn)
  , m_ctx(ctx) {
    Screen* screen = ScreenOfDisplay(display, defaultScreen);
    m_visualid = screen->root_visual->visualid;
    visualIdOut = screen->root_visual->visualid;
  }

  void destroy() override {
    VulkanContext::Window& m_windowCtx = *m_ctx->m_windows[m_parentWindow];
    m_windowCtx.m_swapChains[0].destroy(m_ctx->m_dev);
    m_windowCtx.m_swapChains[1].destroy(m_ctx->m_dev);
    if (m_surface) {
      vk::DestroySurfaceKHR(m_ctx->m_instance, m_surface, nullptr);
      m_surface = VK_NULL_HANDLE;
    }
  }

  ~GraphicsContextXlibVulkan() override { destroy(); }

  VulkanContext::Window* m_windowCtx = nullptr;

  void resized(const SWindowRect& rect) override {
    if (m_windowCtx)
      m_ctx->resizeSwapChain(*m_windowCtx, m_surface, m_format, m_colorspace, rect);
  }

  void _setCallback(IWindowCallback* cb) override { m_callback = cb; }

  EGraphicsAPI getAPI() const override { return m_api; }

  EPixelFormat getPixelFormat() const override { return m_pf; }

  void setPixelFormat(EPixelFormat pf) override {
    if (pf > EPixelFormat::RGBAF32_Z24)
      return;
    m_pf = pf;
  }

  bool initializeContext(void* getVkProc) override {
    if (m_ctx->m_instance == VK_NULL_HANDLE)
      m_ctx->initVulkan(APP->getUniqueName(), PFN_vkGetInstanceProcAddr(getVkProc));

    if (!m_ctx->enumerateDevices())
      return false;

    m_windowCtx = m_ctx->m_windows.emplace(std::make_pair(m_parentWindow, std::make_unique<VulkanContext::Window>()))
                      .first->second.get();

    VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.connection = m_xcbConn;
    surfaceInfo.window = m_parentWindow->getPlatformHandle();
    ThrowIfFailed(vk::CreateXcbSurfaceKHR(m_ctx->m_instance, &surfaceInfo, nullptr, &m_surface));

    /* Iterate over each queue to learn whether it supports presenting */
    VkBool32* supportsPresent = (VkBool32*)malloc(m_ctx->m_queueCount * sizeof(VkBool32));
    for (uint32_t i = 0; i < m_ctx->m_queueCount; ++i)
      vk::GetPhysicalDeviceSurfaceSupportKHR(m_ctx->m_gpus[0], i, m_surface, &supportsPresent[i]);

    /* Search for a graphics queue and a present queue in the array of queue
     * families, try to find one that supports both */
    if (m_ctx->m_graphicsQueueFamilyIndex == UINT32_MAX) {
      /* First window, init device */
      for (uint32_t i = 0; i < m_ctx->m_queueCount; ++i) {
        if ((m_ctx->m_queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
          if (supportsPresent[i] == VK_TRUE) {
            m_ctx->m_graphicsQueueFamilyIndex = i;
          }
        }
      }

      /* Generate error if could not find a queue that supports both a graphics
       * and present */
      if (m_ctx->m_graphicsQueueFamilyIndex == UINT32_MAX)
        Log.report(logvisor::Fatal, fmt("Could not find a queue that supports both graphics and present"));

      m_ctx->initDevice();
    } else {
      /* Subsequent window, verify present */
      if (supportsPresent[m_ctx->m_graphicsQueueFamilyIndex] == VK_FALSE)
        Log.report(logvisor::Fatal, fmt("subsequent surface doesn't support present"));
    }
    free(supportsPresent);

    if (!vk::GetPhysicalDeviceXcbPresentationSupportKHR(m_ctx->m_gpus[0], m_ctx->m_graphicsQueueFamilyIndex, m_xcbConn,
                                                        m_visualid)) {
      Log.report(logvisor::Fatal, fmt("XCB visual doesn't support vulkan present"));
      return false;
    }

    /* Get the list of VkFormats that are supported */
    uint32_t formatCount;
    ThrowIfFailed(vk::GetPhysicalDeviceSurfaceFormatsKHR(m_ctx->m_gpus[0], m_surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> surfFormats(formatCount);
    ThrowIfFailed(
        vk::GetPhysicalDeviceSurfaceFormatsKHR(m_ctx->m_gpus[0], m_surface, &formatCount, surfFormats.data()));

    /* If the format list includes just one entry of VK_FORMAT_UNDEFINED,
     * the surface has no preferred format.  Otherwise, at least one
     * supported format will be returned. */
    if (formatCount >= 1) {
      if (m_ctx->m_deepColor) {
        for (uint32_t i = 0; i < formatCount; ++i) {
          if (surfFormats[i].format == VK_FORMAT_R16G16B16A16_UNORM) {
            m_format = surfFormats[i].format;
            m_colorspace = surfFormats[i].colorSpace;
            break;
          }
        }
      }
      if (m_format == VK_FORMAT_UNDEFINED) {
        for (uint32_t i = 0; i < formatCount; ++i) {
          if (surfFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM || surfFormats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            m_format = surfFormats[i].format;
            m_colorspace = surfFormats[i].colorSpace;
            break;
          }
        }
      }
    } else
      Log.report(logvisor::Fatal, fmt("no surface formats available for Vulkan swapchain"));

    if (m_format == VK_FORMAT_UNDEFINED)
      Log.report(logvisor::Fatal, fmt("no UNORM formats available for Vulkan swapchain"));

    m_ctx->initSwapChain(*m_windowCtx, m_surface, m_format, m_colorspace);

    m_dataFactory = _NewVulkanDataFactory(this, m_ctx);
    m_commandQueue = _NewVulkanCommandQueue(m_ctx, m_ctx->m_windows[m_parentWindow].get(), this);
    m_commandQueue->startRenderer();

    return true;
  }

  void makeCurrent() override {}

  void postInit() override {}

  IGraphicsCommandQueue* getCommandQueue() override { return m_commandQueue.get(); }

  IGraphicsDataFactory* getDataFactory() override { return m_dataFactory.get(); }

  IGraphicsDataFactory* getMainContextDataFactory() override { return getDataFactory(); }

  IGraphicsDataFactory* getLoadContextDataFactory() override { return getDataFactory(); }

  void present() override {}
};
#endif

class WindowXlib final : public IWindow {
  Display* m_xDisp;
  IWindowCallback* m_callback;
  Colormap m_colormapId;
  Window m_windowId;
  XIMStyle m_bestStyle;
  XIC m_xIC = nullptr;
  std::unique_ptr<GraphicsContextXlib> m_gfxCtx;
  uint32_t m_visualId;

  struct timespec m_waitPeriod = {0, static_cast<long int>(1000000000.0/60.0)};
  struct timespec m_lastWaitTime = {};

  /* Key state trackers (for auto-repeat detection) */
  std::unordered_set<unsigned long> m_charKeys;
  std::unordered_set<unsigned long> m_specialKeys;
  std::unordered_set<unsigned long> m_modKeys;

  /* Last known input device id (0xffff if not yet set) */
  int m_lastInputID = 0xffff;
  ETouchType m_touchType = ETouchType::None;

  /* Scroll valuators */
  int m_hScrollValuator = -1;
  int m_vScrollValuator = -1;
  double m_hScrollLast = 0.0;
  double m_vScrollLast = 0.0;

  /* Cached window rectangle (to avoid repeated X queries) */
  boo::SWindowRect m_wrect;
  float m_pixelFactor;
  bool m_inFs = false;

  /* Cached window style */
  EWindowStyle m_styleFlags;

  /* Current cursor enum */
  EMouseCursor m_cursor = EMouseCursor::None;
  bool m_cursorWait = false;
  static Cursor GetXCursor(EMouseCursor cur) {
    switch (cur) {
    case EMouseCursor::Pointer:
      return X_CURSORS.m_pointer;
    case EMouseCursor::HorizontalArrow:
      return X_CURSORS.m_hArrow;
    case EMouseCursor::VerticalArrow:
      return X_CURSORS.m_vArrow;
    case EMouseCursor::IBeam:
      return X_CURSORS.m_ibeam;
    case EMouseCursor::Crosshairs:
      return X_CURSORS.m_crosshairs;
    default:
      break;
    }
    return X_CURSORS.m_pointer;
  }

  bool m_openGL = false;

public:
  WindowXlib(std::string_view title, Display* display, void* xcbConn, int defaultScreen, XIM xIM,
             XIMStyle bestInputStyle, XFontSet fontset, GLXContext lastCtx, void* vulkanHandle, GLContext* glCtx)
  : m_xDisp(display), m_callback(nullptr), m_bestStyle(bestInputStyle) {
    BOO_MSAN_NO_INTERCEPT
    if (!S_ATOMS)
      S_ATOMS = new XlibAtoms(display);

    for (int i = 1; i >= 0; --i) {
#if BOO_HAS_VULKAN
      if (vulkanHandle && i == 1) {
        m_gfxCtx.reset(new GraphicsContextXlibVulkan(this, display, (xcb_connection_t*)xcbConn, defaultScreen,
                                                     &g_VulkanContext, m_visualId, glCtx));
      } else
#endif
      {
        i = 0;
        m_gfxCtx.reset(new GraphicsContextXlibGLX(IGraphicsContext::EGraphicsAPI::OpenGL3_3, this, display,
                                                  defaultScreen, lastCtx, m_visualId, glCtx));
        m_openGL = true;
      }

      XVisualInfo visTemplate;
      visTemplate.screen = defaultScreen;
      int numVisuals;
      XVisualInfo* visualList = XGetVisualInfo(display, VisualScreenMask, &visTemplate, &numVisuals);
      Visual* selectedVisual = nullptr;
      for (int i = 0; i < numVisuals; ++i) {
        if (visualList[i].visualid == m_visualId) {
          selectedVisual = visualList[i].visual;
          break;
        }
      }
      XFree(visualList);

      /* Create colormap */
      Screen* screen = ScreenOfDisplay(display, defaultScreen);
      m_colormapId = XCreateColormap(m_xDisp, screen->root, selectedVisual, AllocNone);

      /* Create window */
      int x, y, w, h;
      int nmonitors = 0;
      XRRMonitorInfo* mInfo = XRRGetMonitors(m_xDisp, screen->root, true, &nmonitors);
      BOO_MSAN_UNPOISON(mInfo, sizeof(XRRMonitorInfo) * nmonitors);
      if (nmonitors) {
        genFrameDefault(mInfo, x, y, w, h);
        m_pixelFactor = mInfo->width / (float)mInfo->mwidth / REF_DPMM;
      } else {
        genFrameDefault(screen, x, y, w, h);
        m_pixelFactor = screen->width / (float)screen->mwidth / REF_DPMM;
      }
      XRRFreeMonitors(mInfo);
      XSetWindowAttributes swa;
      swa.colormap = m_colormapId;
      swa.border_pixmap = 0;
      swa.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | ExposureMask | StructureNotifyMask | LeaveWindowMask | EnterWindowMask;

      m_windowId = XCreateWindow(display, screen->root, x, y, w, h, 10, CopyFromParent, CopyFromParent, selectedVisual,
                                 CWBorderPixel | CWEventMask | CWColormap, &swa);

      /*
       * Now go create an IC using the style we chose.
       * Also set the window and fontset attributes now.
       */
      if (xIM) {
        XPoint pt = {0, 0};
        XVaNestedList nlist;
        m_xIC = XCreateIC(xIM, XNInputStyle, bestInputStyle, XNClientWindow, m_windowId, XNFocusWindow, m_windowId,
                          XNPreeditAttributes,
                          nlist = XVaCreateNestedList(0, XNSpotLocation, &pt, XNFontSet, fontset, nullptr), nullptr);
        XFree(nlist);
        long im_event_mask;
        XGetICValues(m_xIC, XNFilterEvents, &im_event_mask, nullptr);
        XSelectInput(display, m_windowId, swa.event_mask | im_event_mask);
        XSetICFocus(m_xIC);
      }

      /* The XInput 2.1 extension enables per-pixel smooth scrolling trackpads */
      XIEventMask mask = {XIAllMasterDevices, XIMaskLen(XI_LASTEVENT)};
      mask.mask = (unsigned char*)malloc(mask.mask_len);
      memset(mask.mask, 0, mask.mask_len);
      /* XISetMask(mask.mask, XI_Motion); Can't do this without losing mouse move events :( */
      XISetMask(mask.mask, XI_TouchBegin);
      XISetMask(mask.mask, XI_TouchUpdate);
      XISetMask(mask.mask, XI_TouchEnd);
      XISelectEvents(m_xDisp, m_windowId, &mask, 1);
      free(mask.mask);

      /* Register netwm extension atom for window closing */
      XSetWMProtocols(m_xDisp, m_windowId, &S_ATOMS->m_wmDeleteWindow, 1);

      /* Set the title of the window */
      if (S_ATOMS->m_netwmName) {
        XChangeProperty(m_xDisp, m_windowId, S_ATOMS->m_netwmName, S_ATOMS->m_utf8String, 8,
                        PropModeReplace, (unsigned char*)title.data(), title.length());
      }
      XStoreName(m_xDisp, m_windowId, title.data());

      /* Set the title of the window icon */
      XChangeProperty(m_xDisp, m_windowId, XA_WM_ICON_NAME, XA_STRING, 8, PropModeReplace,
                      (unsigned char*)title.data(), title.length());

      /* Add window icon */
      if (MAINICON_NETWM_SZ && S_ATOMS->m_netwmIcon) {
        XChangeProperty(m_xDisp, m_windowId, S_ATOMS->m_netwmIcon, XA_CARDINAL, 32, PropModeReplace, MAINICON_NETWM,
                        MAINICON_NETWM_SZ / sizeof(unsigned long));
      }

      /* Set the pid of the window */
      if (S_ATOMS->m_netwmPid) {
        pid_t pid = getpid();
        XChangeProperty(m_xDisp, m_windowId, S_ATOMS->m_netwmPid, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char*)&pid, 1);
      }

      /* Initialize context */
      XMapWindow(m_xDisp, m_windowId);
      setStyle(EWindowStyle::Default);
      setCursor(EMouseCursor::Pointer);
      setWindowFrameDefault();
      double hz = getWindowRefreshRate();
      uint64_t nanos = uint64_t(1000000000.0 / hz);
      m_waitPeriod = {nanos / 1000000000, nanos % 1000000000};
      XFlush(m_xDisp);

      if (!m_gfxCtx->initializeContext(vulkanHandle)) {
        XUnmapWindow(m_xDisp, m_windowId);
        XDestroyWindow(m_xDisp, m_windowId);
        XFreeColormap(m_xDisp, m_colormapId);
        continue;
      }
      break;
    }
  }

  ~WindowXlib() override {
    _cleanup();
    if (APP)
      APP->_deletedWindow(this);
  }

  void setCallback(IWindowCallback* cb) override {
    XLockDisplay(m_xDisp);
    m_callback = cb;
    XUnlockDisplay(m_xDisp);
  }

  void closeWindow() override {
    // TODO: Free window resources and prevent further access
    XLockDisplay(m_xDisp);
    XUnmapWindow(m_xDisp, m_windowId);
    XUnlockDisplay(m_xDisp);
  }

  void showWindow() override {
    XLockDisplay(m_xDisp);
    XMapWindow(m_xDisp, m_windowId);
    XUnlockDisplay(m_xDisp);
  }

  void hideWindow() override {
    XLockDisplay(m_xDisp);
    XUnmapWindow(m_xDisp, m_windowId);
    XUnlockDisplay(m_xDisp);
  }

  std::string getTitle() override {
    unsigned long nitems;
    Atom actualType;
    int actualFormat;
    unsigned long bytes;
    unsigned char* string = nullptr;
    XLockDisplay(m_xDisp);
    int ret = XGetWindowProperty(m_xDisp, m_windowId, XA_WM_NAME, 0, ~0l, false, XA_STRING, &actualType, &actualFormat,
                                 &nitems, &bytes, &string);
    XUnlockDisplay(m_xDisp);
    if (ret == Success) {
      std::string retval((const char*)string);
      XFree(string);
      return retval;
    }
    return std::string();
  }

  void setTitle(std::string_view title) override {
    XLockDisplay(m_xDisp);

    /* Set the title of the window */
    if (S_ATOMS->m_netwmName) {
      XChangeProperty(m_xDisp, m_windowId, S_ATOMS->m_netwmName, S_ATOMS->m_utf8String, 8,
                      PropModeReplace, (unsigned char*)title.data(), title.length());
    }
    XStoreName(m_xDisp, m_windowId, title.data());

    /* Set the title of the window icon */
    XChangeProperty(m_xDisp, m_windowId, XA_WM_ICON_NAME, XA_STRING, 8, PropModeReplace,
                    (unsigned char*)title.data(), title.length());

    XUnlockDisplay(m_xDisp);
  }

  void setCursor(EMouseCursor cursor) override {
    if (cursor == m_cursor && !m_cursorWait)
      return;
    m_cursor = cursor;
    XLockDisplay(m_xDisp);
    XDefineCursor(m_xDisp, m_windowId, GetXCursor(cursor));
    XUnlockDisplay(m_xDisp);
  }

  void setWaitCursor(bool wait) override {
    if (wait && !m_cursorWait) {
      XLockDisplay(m_xDisp);
      XDefineCursor(m_xDisp, m_windowId, X_CURSORS.m_wait);
      XUnlockDisplay(m_xDisp);
      m_cursorWait = true;
    } else if (!wait && m_cursorWait) {
      setCursor(m_cursor);
      m_cursorWait = false;
    }
  }

  static double calculateRefreshRate(const XRRModeInfo& mi) {
    if (mi.hTotal && mi.vTotal)
      return double(mi.dotClock) / (double(mi.hTotal) * double(mi.vTotal));
    else
      return 60.0;
  }

  double getWindowRefreshRate() const override {
    BOO_MSAN_NO_INTERCEPT
    double ret = 60.0;
    int nmonitors;
    Screen* screen = DefaultScreenOfDisplay(m_xDisp);
    XRRMonitorInfo* mInfo = XRRGetMonitors(m_xDisp, screen->root, true, &nmonitors);
    BOO_MSAN_UNPOISON(mInfo, sizeof(XRRMonitorInfo) * nmonitors);
    if (nmonitors) {
      XRRScreenResources* res = XRRGetScreenResourcesCurrent(m_xDisp, screen->root);
      XRROutputInfo* oinfo = XRRGetOutputInfo(m_xDisp, res, *mInfo->outputs);
      XRRCrtcInfo* ci = XRRGetCrtcInfo(m_xDisp, res, oinfo->crtc);
      for (int i = 0; i < res->nmode; ++i) {
        const XRRModeInfo& mode = res->modes[i];
        BOO_MSAN_UNPOISON(&mode, sizeof(XRRModeInfo));
        if (mode.id == ci->mode) {
          ret = calculateRefreshRate(mode);
          break;
        }
      }
      XRRFreeCrtcInfo(ci);
      XRRFreeOutputInfo(oinfo);
      XRRFreeScreenResources(res);
    }
    XRRFreeMonitors(mInfo);
    return ret;
  }

  void setWindowFrameDefault() override {
    BOO_MSAN_NO_INTERCEPT
    int x, y, w, h, nmonitors;
    Screen* screen = DefaultScreenOfDisplay(m_xDisp);
    XRRMonitorInfo* mInfo = XRRGetMonitors(m_xDisp, screen->root, true, &nmonitors);
    BOO_MSAN_UNPOISON(mInfo, sizeof(XRRMonitorInfo) * nmonitors);
    if (nmonitors)
      genFrameDefault(mInfo, x, y, w, h);
    else
      genFrameDefault(screen, x, y, w, h);
    XRRFreeMonitors(mInfo);
    XWindowChanges values = {(int)x, (int)y, (int)w, (int)h};
    XLockDisplay(m_xDisp);
    XConfigureWindow(m_xDisp, m_windowId, CWX | CWY | CWWidth | CWHeight, &values);
    XUnlockDisplay(m_xDisp);
  }

  void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const override {
    BOO_MSAN_NO_INTERCEPT
    XWindowAttributes attrs = {};
    XLockDisplay(m_xDisp);
    XGetWindowAttributes(m_xDisp, m_windowId, &attrs);
    XUnlockDisplay(m_xDisp);
    xOut = attrs.x;
    yOut = attrs.y;
    wOut = attrs.width;
    hOut = attrs.height;
  }

  void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const override {
    BOO_MSAN_NO_INTERCEPT
    XWindowAttributes attrs = {};
    XLockDisplay(m_xDisp);
    XGetWindowAttributes(m_xDisp, m_windowId, &attrs);
    XUnlockDisplay(m_xDisp);
    xOut = attrs.x;
    yOut = attrs.y;
    wOut = attrs.width;
    hOut = attrs.height;
  }

  void setWindowFrame(float x, float y, float w, float h) override {
    BOO_MSAN_NO_INTERCEPT
    XWindowChanges values = {(int)x, (int)y, (int)w, (int)h};
    XLockDisplay(m_xDisp);
    XConfigureWindow(m_xDisp, m_windowId, CWX | CWY | CWWidth | CWHeight, &values);
    XUnlockDisplay(m_xDisp);
  }

  void setWindowFrame(int x, int y, int w, int h) override {
    BOO_MSAN_NO_INTERCEPT
    XWindowChanges values = {x, y, w, h};
    XLockDisplay(m_xDisp);
    XConfigureWindow(m_xDisp, m_windowId, CWX | CWY | CWWidth | CWHeight, &values);
    XUnlockDisplay(m_xDisp);
  }

  float getVirtualPixelFactor() const override { return m_pixelFactor; }

  bool isFullscreen() const override {
    return m_inFs;
    unsigned long nitems;
    Atom actualType;
    int actualFormat;
    unsigned long bytes;
    Atom* vals = nullptr;
    bool fullscreen = false;
    XLockDisplay(m_xDisp);
    int ret = XGetWindowProperty(m_xDisp, m_windowId, S_ATOMS->m_netwmState, 0, ~0l, false, XA_ATOM, &actualType,
                                 &actualFormat, &nitems, &bytes, (unsigned char**)&vals);
    XUnlockDisplay(m_xDisp);
    if (ret == Success) {
      for (unsigned long i = 0; i < nitems; ++i) {
        if (vals[i] == S_ATOMS->m_netwmStateFullscreen) {
          fullscreen = true;
          break;
        }
      }
      XFree(vals);
      return fullscreen;
    }

    return false;
  }

  void setStyle(EWindowStyle style) override {
    struct {
      unsigned long flags;
      unsigned long functions;
      unsigned long decorations;
      long inputMode;
      unsigned long status;
    } wmHints = {0};

    if (S_ATOMS->m_motifWmHints) {
      wmHints.flags = MWM_HINTS_DECORATIONS | MWM_HINTS_FUNCTIONS;
      if (True(style & EWindowStyle::Titlebar)) {
        wmHints.decorations |= MWM_DECOR_BORDER | MWM_DECOR_TITLE | MWM_DECOR_MINIMIZE | MWM_DECOR_MENU;
        wmHints.functions |= MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE;
      }
      if (True(style & EWindowStyle::Resize)) {
        wmHints.decorations |= MWM_DECOR_MAXIMIZE | MWM_DECOR_RESIZEH;
        wmHints.functions |= MWM_FUNC_RESIZE | MWM_FUNC_MAXIMIZE;
      }

      if (True(style & EWindowStyle::Close))
        wmHints.functions |= MWM_FUNC_CLOSE;

      XLockDisplay(m_xDisp);
      XChangeProperty(m_xDisp, m_windowId, S_ATOMS->m_motifWmHints, S_ATOMS->m_motifWmHints, 32, PropModeReplace,
                      (unsigned char*)&wmHints, 5);
      XUnlockDisplay(m_xDisp);
    }

    m_styleFlags = style;
  }

  EWindowStyle getStyle() const override { return m_styleFlags; }

  void setFullscreen(bool fs) override {
    if (fs == m_inFs)
      return;

    XEvent fsEvent = {0};
    fsEvent.xclient.type = ClientMessage;
    fsEvent.xclient.serial = 0;
    fsEvent.xclient.send_event = true;
    fsEvent.xclient.window = m_windowId;
    fsEvent.xclient.message_type = S_ATOMS->m_netwmState;
    fsEvent.xclient.format = 32;
    fsEvent.xclient.data.l[0] = fs;
    fsEvent.xclient.data.l[1] = S_ATOMS->m_netwmStateFullscreen;
    fsEvent.xclient.data.l[2] = 0;
    XLockDisplay(m_xDisp);
    XSendEvent(m_xDisp, DefaultRootWindow(m_xDisp), false, StructureNotifyMask | SubstructureRedirectMask,
               (XEvent*)&fsEvent);
    XUnlockDisplay(m_xDisp);

    m_inFs = fs;
  }

  struct ClipData {
    EClipboardType m_type = EClipboardType::None;
    std::unique_ptr<uint8_t[]> m_data;
    size_t m_sz = 0;
    void clear() {
      m_type = EClipboardType::None;
      m_data.reset();
      m_sz = 0;
    }
  } m_clipData;

  void claimKeyboardFocus(const int coord[2]) override {
    if (m_xIC) {
      XLockDisplay(m_xDisp);
      if (!coord) {
        XUnsetICFocus(m_xIC);
        XUnlockDisplay(m_xDisp);
        return;
      }
      getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
      XPoint pt = {short(coord[0]), short(m_wrect.size[1] - coord[1])};
      XVaNestedList list = XVaCreateNestedList(0, XNSpotLocation, &pt, nullptr);
      XSetICValues(m_xIC, XNPreeditAttributes, list, nullptr);
      XFree(list);
      XSetICFocus(m_xIC);
      XUnlockDisplay(m_xDisp);
    }
  }

  bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) override {
    Atom xType = GetClipboardTypeAtom(type);
    if (!xType)
      return false;

    XLockDisplay(m_xDisp);
    m_clipData.m_type = type;
    m_clipData.m_data.reset(new uint8_t[sz]);
    m_clipData.m_sz = sz;
    memcpy(m_clipData.m_data.get(), data, sz);
    XSetSelectionOwner(m_xDisp, S_ATOMS->m_clipboard, m_windowId, CurrentTime);
    XUnlockDisplay(m_xDisp);

    return true;
  }

  std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) override {
    Atom xType = GetClipboardTypeAtom(type);
    if (!xType)
      return {};

    XLockDisplay(m_xDisp);
    XConvertSelection(m_xDisp, S_ATOMS->m_clipboard, xType, S_ATOMS->m_clipdata, m_windowId, CurrentTime);
    XFlush(m_xDisp);
    XEvent event;
    for (int i = 0; i < 20; ++i) {
      if (XCheckTypedWindowEvent(m_xDisp, m_windowId, SelectionNotify, &event)) {
        if (event.xselection.property != 0) {
          XSync(m_xDisp, false);

          unsigned long nitems, rem;
          int format;
          unsigned char* data;
          Atom type;

          // Atom t1 = S_ATOMS->m_clipboard;
          // Atom t2 = S_ATOMS->m_clipdata;

          if (XGetWindowProperty(m_xDisp, m_windowId, S_ATOMS->m_clipdata, 0, 32, false, AnyPropertyType, &type,
                                 &format, &nitems, &rem, &data)) {
            Log.report(logvisor::Fatal, fmt("Clipboard allocation failed"));
            XUnlockDisplay(m_xDisp);
            return {};
          }

          if (rem != 0) {
            Log.report(logvisor::Fatal, fmt("partial clipboard read"));
            XUnlockDisplay(m_xDisp);
            return {};
          }

          sz = nitems * format / 8;
          std::unique_ptr<uint8_t[]> ret(new uint8_t[sz]);
          memcpy(ret.get(), data, sz);
          XFree(data);
          XUnlockDisplay(m_xDisp);
          return ret;
        }
        XUnlockDisplay(m_xDisp);
        return {};
      }
      if (XCheckTypedWindowEvent(m_xDisp, m_windowId, SelectionRequest, &event) &&
          event.xselectionrequest.owner == m_windowId)
        handleSelectionRequest(&event.xselectionrequest);
      if (XCheckTypedWindowEvent(m_xDisp, m_windowId, SelectionClear, &event) &&
          event.xselectionclear.window == m_windowId)
        m_clipData.clear();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    XUnlockDisplay(m_xDisp);
    return {};
  }

  void handleSelectionRequest(XSelectionRequestEvent* se) {
    XEvent reply;
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = m_xDisp;
    reply.xselection.requestor = se->requestor;
    reply.xselection.selection = se->selection;
    reply.xselection.target = se->target;
    reply.xselection.time = se->time;
    reply.xselection.property = se->property;
    if (se->target == S_ATOMS->m_targets) {
      Atom ValidTargets[] = {GetClipboardTypeAtom(m_clipData.m_type)};
      XChangeProperty(m_xDisp, se->requestor, se->property, XA_ATOM, 32, 0, (unsigned char*)ValidTargets,
                      m_clipData.m_type != EClipboardType::None);
    } else {
      if (se->target == GetClipboardTypeAtom(m_clipData.m_type)) {
        XChangeProperty(m_xDisp, se->requestor, se->property, se->target, 8, PropModeReplace, m_clipData.m_data.get(),
                        m_clipData.m_sz);
      } else
        reply.xselection.property = 0;
    }
    XSendEvent(m_xDisp, se->requestor, false, 0, &reply);
  }

#define NSEC_PER_SEC 1000000000

  static void set_normalized_timespec(struct timespec& ts, time_t sec, int64_t nsec) {
    while (nsec >= NSEC_PER_SEC) {
      nsec -= NSEC_PER_SEC;
      ++sec;
    }
    while (nsec < 0) {
      nsec += NSEC_PER_SEC;
      --sec;
    }
    ts.tv_sec = sec;
    ts.tv_nsec = nsec;
  }

  static struct timespec timespec_add(const struct timespec& lhs, const struct timespec& rhs) {
    struct timespec ts_delta;
    set_normalized_timespec(ts_delta, lhs.tv_sec + rhs.tv_sec,
                            lhs.tv_nsec + rhs.tv_nsec);
    return ts_delta;
  }

  static struct timespec timespec_sub(const struct timespec& lhs, const struct timespec& rhs) {
    struct timespec ts_delta;
    set_normalized_timespec(ts_delta, lhs.tv_sec - rhs.tv_sec,
                            lhs.tv_nsec - rhs.tv_nsec);
    return ts_delta;
  }

  static inline long int timespec_compare(const struct timespec& lhs, const struct timespec& rhs)
  {
    if (lhs.tv_sec < rhs.tv_sec)
      return -1;
    if (lhs.tv_sec > rhs.tv_sec)
      return 1;
    return lhs.tv_nsec - rhs.tv_nsec;
  }

  int waitForRetrace() override {
    BOO_MSAN_NO_INTERCEPT
    struct timespec tp = {};
    clock_gettime(CLOCK_REALTIME, &tp);
    if (!m_lastWaitTime.tv_sec) {
      /* Initialize reference point */
      sched_param prio = {75};
      sched_setscheduler(0, SCHED_RR, &prio);
      m_lastWaitTime = tp;
      return 0;
    }

    m_lastWaitTime = timespec_add(m_lastWaitTime, m_waitPeriod);
    long int comp = timespec_compare(m_lastWaitTime, tp);
    if (comp == 0) {
      /* Exactly at the due date */
      return 1;
    } else if (comp > 0) {
      /* Not at due date yet, sleep here */
      struct timespec wait_time = timespec_sub(m_lastWaitTime, tp);
      while (nanosleep(&wait_time, &wait_time)) {}
      return 1;
    } else {
      /* Missed due date, assign next one and return passed cycle count */
      int cycles = 0;
      do {
        m_lastWaitTime = timespec_add(m_lastWaitTime, m_waitPeriod);
        ++cycles;
      } while (timespec_compare(m_lastWaitTime, tp) < 0);
      return cycles;
    }
  }

  uintptr_t getPlatformHandle() const override { return (uintptr_t)m_windowId; }

  void _pointingDeviceChanged(int deviceId) {
    int nDevices;
    XIDeviceInfo* devices = XIQueryDevice(m_xDisp, deviceId, &nDevices);

    for (int i = 0; i < nDevices; ++i) {
      XIDeviceInfo* device = &devices[i];

      /* First iterate classes for scrollables */
      int hScroll = -1;
      int vScroll = -1;
      m_hScrollLast = 0.0;
      m_vScrollLast = 0.0;
      m_hScrollValuator = -1;
      m_vScrollValuator = -1;
      for (int j = 0; j < device->num_classes; ++j) {
        XIAnyClassInfo* dclass = device->classes[j];
        if (dclass->type == XIScrollClass) {
          XIScrollClassInfo* scrollClass = (XIScrollClassInfo*)dclass;
          if (scrollClass->scroll_type == XIScrollTypeVertical)
            vScroll = scrollClass->number;
          else if (scrollClass->scroll_type == XIScrollTypeHorizontal)
            hScroll = scrollClass->number;
        }
      }

      /* Next iterate for touch and scroll valuators */
      for (int j = 0; j < device->num_classes; ++j) {
        XIAnyClassInfo* dclass = device->classes[j];
        if (dclass->type == XIValuatorClass) {
          XIValuatorClassInfo* valClass = (XIValuatorClassInfo*)dclass;
          if (valClass->number == vScroll) {
            m_vScrollLast = valClass->value;
            m_vScrollValuator = vScroll;
          } else if (valClass->number == hScroll) {
            m_hScrollLast = valClass->value;
            m_hScrollValuator = hScroll;
          }
        } else if (dclass->type == XITouchClass) {
          XITouchClassInfo* touchClass = (XITouchClassInfo*)dclass;
          if (touchClass->mode == XIDirectTouch)
            m_touchType = ETouchType::Display;
          else if (touchClass->mode == XIDependentTouch)
            m_touchType = ETouchType::Trackpad;
          else
            m_touchType = ETouchType::None;
        }
      }
    }

    XIFreeDeviceInfo(devices);
    m_lastInputID = deviceId;
  }

  SWindowCoord MakeButtonEventCoord(XEvent* event) const {
    int x = event->xbutton.x;
    int y = m_wrect.size[1] - event->xbutton.y;
    return {{x, y},
            {int(x / m_pixelFactor), int(y / m_pixelFactor)},
            {x / float(m_wrect.size[0]), y / float(m_wrect.size[1])}};
  }

  SWindowCoord MakeMotionEventCoord(XEvent* event) const {
    int x = event->xmotion.x;
    int y = m_wrect.size[1] - event->xmotion.y;
    return {{x, y},
            {int(x / m_pixelFactor), int(y / m_pixelFactor)},
            {x / float(m_wrect.size[0]), y / float(m_wrect.size[1])}};
  }

  SWindowCoord MakeCrossingEventCoord(XEvent* event) const {
    int x = event->xcrossing.x;
    int y = m_wrect.size[1] - event->xcrossing.y;
    return {{x, y},
            {int(x / m_pixelFactor), int(y / m_pixelFactor)},
            {x / float(m_wrect.size[0]), y / float(m_wrect.size[1])}};
  }

#if 0
    /* This procedure sets the application's size constraints and returns
     * the IM's preferred size for either the Preedit or Status areas,
     * depending on the value of the name argument.  The area argument is
     * used to pass the constraints and to return the preferred size.
     */
    void GetPreferredGeometry(const char* name, XRectangle* area)
    {
        XVaNestedList list;
        list = XVaCreateNestedList(0, XNAreaNeeded, area, nullptr);
        /* set the constraints */
        XSetICValues(m_xIC, name, list, nullptr);
        /* query the preferred size */
        XGetICValues(m_xIC, name, list, nullptr);
        XFree(list);
    }

    /* This procedure sets the geometry of either the Preedit or Status
     * Areas, depending on the value of the name argument.
     */
    void SetGeometry(const char* name, XRectangle* area)
    {
        XVaNestedList list;
        list = XVaCreateNestedList(0, XNArea, area, nullptr);
        XSetICValues(m_xIC, name, list, nullptr);
        XFree(list);
    }
#endif

  bool _incomingEvent(void* e) override {
    XEvent* event = (XEvent*)e;
    switch (event->type) {
    case SelectionRequest: {
      handleSelectionRequest(&event->xselectionrequest);
      return false;
    }
    case ClientMessage: {
      if (Atom(event->xclient.data.l[0]) == S_ATOMS->m_wmDeleteWindow && m_callback) {
        m_callback->destroyed();
        m_callback = nullptr;
        return true;
      }
      return false;
    }
    case Expose: {
      Window nw = 0;
      XWindowAttributes wxa = {};
      int x = 0, y = 0;
      XTranslateCoordinates(m_xDisp, m_windowId, DefaultRootWindow(m_xDisp), event->xexpose.x, event->xexpose.y, &x, &y,
                            &nw);
      XGetWindowAttributes(m_xDisp, m_windowId, &wxa);
      m_wrect.location[0] = x - wxa.x;
      m_wrect.location[1] = y - wxa.y;
#if 0
      /* This breaks with GNOME, why? */
      m_wrect.size[0] = event->xexpose.width;
      m_wrect.size[1] = event->xexpose.height;
#else
      m_wrect.size[0] = wxa.width;
      m_wrect.size[1] = wxa.height;
#endif
      if (m_callback) {
        XUnlockDisplay(m_xDisp);
        m_gfxCtx->resized(m_wrect);
        m_callback->resized(m_wrect, m_openGL);
        XLockDisplay(m_xDisp);
      }
      return false;
    }
    case ConfigureNotify: {
      Window nw = 0;
      XWindowAttributes wxa = {};
      int x = 0, y = 0;
      XTranslateCoordinates(m_xDisp, m_windowId, DefaultRootWindow(m_xDisp), event->xconfigure.x, event->xconfigure.y,
                            &x, &y, &nw);
      XGetWindowAttributes(m_xDisp, m_windowId, &wxa);
      m_wrect.location[0] = x - wxa.x;
      m_wrect.location[1] = y - wxa.y;
      m_wrect.size[0] = event->xconfigure.width;
      m_wrect.size[1] = event->xconfigure.height;

      if (m_callback)
        m_callback->windowMoved(m_wrect);
      return false;
    }
    case KeyPress: {
      if (m_callback) {
        ESpecialKey specialKey;
        EModifierKey modifierKey;
        unsigned int state = event->xkey.state;
        event->xkey.state &= ~ControlMask;
        ITextInputCallback* inputCb = m_callback->getTextInputCallback();
        if (m_xIC) {
          std::string utf8Frag = translateUTF8(&event->xkey, m_xIC);
          if (utf8Frag.size()) {
            if (inputCb)
              inputCb->insertText(utf8Frag);
            return false;
          }
        }
        char charCode = translateKeysym(&event->xkey, specialKey, modifierKey);
        EModifierKey modifierMask = translateModifiers(state);
        if (charCode) {
          if (inputCb && False(modifierMask & (EModifierKey::Ctrl | EModifierKey::Command)))
            inputCb->insertText(std::string(1, charCode));

          bool isRepeat = m_charKeys.find(charCode) != m_charKeys.cend();
          m_callback->charKeyDown(charCode, modifierMask, isRepeat);
          if (!isRepeat)
            m_charKeys.insert(charCode);
        } else if (specialKey != ESpecialKey::None) {
          bool isRepeat = m_specialKeys.find((unsigned long)specialKey) != m_specialKeys.cend();
          m_callback->specialKeyDown(specialKey, modifierMask, isRepeat);
          if (!isRepeat)
            m_specialKeys.insert((unsigned long)specialKey);
        } else if (True(modifierKey)) {
          bool isRepeat = m_modKeys.find((unsigned long)modifierKey) != m_modKeys.cend();
          m_callback->modKeyDown(modifierKey, isRepeat);
          if (!isRepeat)
            m_modKeys.insert((unsigned long)modifierKey);
        }
      }
      return false;
    }
    case KeyRelease: {
      if (m_callback) {
        ESpecialKey specialKey;
        EModifierKey modifierKey;
        unsigned int state = event->xkey.state;
        event->xkey.state &= ~ControlMask;
        char charCode = translateKeysym(&event->xkey, specialKey, modifierKey);
        EModifierKey modifierMask = translateModifiers(state);
        if (charCode) {
          m_charKeys.erase(charCode);
          m_callback->charKeyUp(charCode, modifierMask);
        } else if (specialKey != ESpecialKey::None) {
          m_specialKeys.erase((unsigned long)specialKey);
          m_callback->specialKeyUp(specialKey, modifierMask);
        } else if (True(modifierKey)) {
          m_modKeys.erase((unsigned long)modifierKey);
          m_callback->modKeyUp(modifierKey);
        }
      }
      return false;
    }
    case ButtonPress: {
      if (m_callback) {
        getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
        EMouseButton button = translateButton(event->xbutton.button);
        if (button != EMouseButton::None) {
          EModifierKey modifierMask = translateModifiers(event->xbutton.state);
          m_callback->mouseDown(MakeButtonEventCoord(event), (EMouseButton)button, (EModifierKey)modifierMask);
        }

        /* Also handle legacy scroll events here */
        if (event->xbutton.button >= 4 && event->xbutton.button <= 7 && m_hScrollValuator == -1 &&
            m_vScrollValuator == -1) {
          SScrollDelta scrollDelta = {{0.0, 0.0}, false};
          if (event->xbutton.button == 4)
            scrollDelta.delta[1] = 1.0;
          else if (event->xbutton.button == 5)
            scrollDelta.delta[1] = -1.0;
          else if (event->xbutton.button == 6)
            scrollDelta.delta[0] = 1.0;
          else if (event->xbutton.button == 7)
            scrollDelta.delta[0] = -1.0;
          m_callback->scroll(MakeButtonEventCoord(event), scrollDelta);
        }
      }
      return false;
    }
    case ButtonRelease: {
      if (m_callback) {
        getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
        EMouseButton button = translateButton(event->xbutton.button);
        if (button != EMouseButton::None) {
          EModifierKey modifierMask = translateModifiers(event->xbutton.state);
          m_callback->mouseUp(MakeButtonEventCoord(event), (EMouseButton)button, (EModifierKey)modifierMask);
        }
      }
      return false;
    }
    case FocusIn: {
      if (m_callback)
        m_callback->focusGained();
      return false;
    }
    case FocusOut: {
      if (m_callback)
        m_callback->focusLost();
      return false;
    }
    case MotionNotify: {
      if (m_callback) {
        getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
        m_callback->mouseMove(MakeMotionEventCoord(event));
      }
      return false;
    }
    case EnterNotify: {
      if (m_callback) {
        getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
        m_callback->mouseEnter(MakeCrossingEventCoord(event));
      }
      return false;
    }
    case LeaveNotify: {
      if (m_callback) {
        getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
        m_callback->mouseLeave(MakeCrossingEventCoord(event));
      }
      return false;
    }
    case GenericEvent: {
      if (event->xgeneric.extension == XINPUT_OPCODE) {
        getWindowFrame(m_wrect.location[0], m_wrect.location[1], m_wrect.size[0], m_wrect.size[1]);
        switch (event->xgeneric.evtype) {
        case XI_Motion: {
          fmt::print(stderr, fmt("motion\n"));

          XIDeviceEvent* ev = (XIDeviceEvent*)event;
          if (m_lastInputID != ev->deviceid)
            _pointingDeviceChanged(ev->deviceid);

          int cv = 0;
          double newScroll[2] = {m_hScrollLast, m_vScrollLast};
          bool didScroll = false;
          for (int i = 0; i < ev->valuators.mask_len * 8; ++i) {
            if (XIMaskIsSet(ev->valuators.mask, i)) {
              if (i == m_hScrollValuator) {
                newScroll[0] = ev->valuators.values[cv];
                didScroll = true;
              } else if (i == m_vScrollValuator) {
                newScroll[1] = ev->valuators.values[cv];
                didScroll = true;
              }
              ++cv;
            }
          }

          SScrollDelta scrollDelta = {{newScroll[0] - m_hScrollLast, newScroll[1] - m_vScrollLast}, true};

          m_hScrollLast = newScroll[0];
          m_vScrollLast = newScroll[1];

          if (m_callback && didScroll) {
            int event_x = int(ev->event_x) >> 16;
            int event_y = m_wrect.size[1] - (int(ev->event_y) >> 16);
            SWindowCoord coord = {{event_x, event_y},
                                  {int(event_x / m_pixelFactor), int(event_y / m_pixelFactor)},
                                  {event_x / float(m_wrect.size[0]), event_y / float(m_wrect.size[1])}};
            m_callback->scroll(coord, scrollDelta);
          }
          return false;
        }
        case XI_TouchBegin: {
          XIDeviceEvent* ev = (XIDeviceEvent*)event;
          if (m_lastInputID != ev->deviceid)
            _pointingDeviceChanged(ev->deviceid);

          int cv = 0;
          double vals[32] = {};
          for (int i = 0; i < ev->valuators.mask_len * 8 && i < 32; ++i) {
            if (XIMaskIsSet(ev->valuators.mask, i)) {
              vals[i] = ev->valuators.values[cv];
              ++cv;
            }
          }

          STouchCoord coord = {{vals[0], vals[1]}};

          if (m_callback)
            m_callback->touchDown(coord, ev->detail);
          return false;
        }
        case XI_TouchUpdate: {
          XIDeviceEvent* ev = (XIDeviceEvent*)event;
          if (m_lastInputID != ev->deviceid)
            _pointingDeviceChanged(ev->deviceid);

          int cv = 0;
          double vals[32] = {};
          for (int i = 0; i < ev->valuators.mask_len * 8 && i < 32; ++i) {
            if (XIMaskIsSet(ev->valuators.mask, i)) {
              vals[i] = ev->valuators.values[cv];
              ++cv;
            }
          }

          STouchCoord coord = {{vals[0], vals[1]}};

          if (m_callback)
            m_callback->touchMove(coord, ev->detail);
          return false;
        }
        case XI_TouchEnd: {
          XIDeviceEvent* ev = (XIDeviceEvent*)event;
          if (m_lastInputID != ev->deviceid)
            _pointingDeviceChanged(ev->deviceid);

          int cv = 0;
          double vals[32] = {};
          for (int i = 0; i < ev->valuators.mask_len * 8 && i < 32; ++i) {
            if (XIMaskIsSet(ev->valuators.mask, i)) {
              vals[i] = ev->valuators.values[cv];
              ++cv;
            }
          }

          STouchCoord coord = {{vals[0], vals[1]}};

          if (m_callback)
            m_callback->touchUp(coord, ev->detail);
          return false;
        }
        }
      }
    }
    }

    return false;
  }

  void _cleanup() override {
    if (m_gfxCtx) {
      XLockDisplay(m_xDisp);
      m_gfxCtx->destroy();
      m_gfxCtx.reset();
      XUnmapWindow(m_xDisp, m_windowId);
      XDestroyWindow(m_xDisp, m_windowId);
      XFreeColormap(m_xDisp, m_colormapId);
      XUnlockDisplay(m_xDisp);
    }
  }

  ETouchType getTouchType() const override { return m_touchType; }

  IGraphicsCommandQueue* getCommandQueue() override { return m_gfxCtx->getCommandQueue(); }

  IGraphicsDataFactory* getDataFactory() override { return m_gfxCtx->getDataFactory(); }

  IGraphicsDataFactory* getMainContextDataFactory() override { return m_gfxCtx->getMainContextDataFactory(); }

  IGraphicsDataFactory* getLoadContextDataFactory() override { return m_gfxCtx->getLoadContextDataFactory(); }

  bool _isWindowMapped() {
    XWindowAttributes attr;
    XLockDisplay(m_xDisp);
    XGetWindowAttributes(m_xDisp, m_windowId, &attr);
    XUnlockDisplay(m_xDisp);
    return attr.map_state != IsUnmapped;
  }
};

std::shared_ptr<IWindow> _WindowXlibNew(std::string_view title, Display* display, void* xcbConn, int defaultScreen,
                                        XIM xIM, XIMStyle bestInputStyle, XFontSet fontset, GLXContext lastCtx,
                                        void* vulkanHandle, GLContext* glCtx) {
  std::shared_ptr<IWindow> ret = std::make_shared<WindowXlib>(title, display, xcbConn, defaultScreen, xIM,
                                                              bestInputStyle, fontset, lastCtx, vulkanHandle, glCtx);
  return ret;
}

} // namespace boo

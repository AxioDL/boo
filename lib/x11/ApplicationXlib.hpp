#ifndef APPLICATION_UNIX_CPP
#error This file may only be included from ApplicationUnix.cpp
#endif

#include "boo/IApplication.hpp"
#include "boo/graphicsdev/GL.hpp"
#include "lib/Common.hpp"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include <dbus/dbus.h>
DBusConnection* RegisterDBus(const char* appName, bool& isFirst);

#include <clocale>
#include <condition_variable>
#include <csignal>
#include <locale>
#include <mutex>
#include <sys/param.h>
#include <thread>

#include "XlibCommon.hpp"
#include <X11/cursorfont.h>

#if BOO_HAS_VULKAN
#include <X11/Xlib-xcb.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include "boo/graphicsdev/Vulkan.hpp"
#endif

namespace boo {
XlibCursors X_CURSORS;

int XINPUT_OPCODE = 0;

static Window GetWindowOfEvent(XEvent* event, bool& windowEvent) {
  switch (event->type) {
  case SelectionRequest: {
    windowEvent = true;
    return event->xselectionrequest.owner;
  }
  case ClientMessage: {
    windowEvent = true;
    return event->xclient.window;
  }
  case Expose: {
    windowEvent = true;
    return event->xexpose.window;
  }
  case ConfigureNotify: {
    windowEvent = true;
    return event->xconfigure.window;
  }
  case KeyPress:
  case KeyRelease: {
    windowEvent = true;
    return event->xkey.window;
  }
  case ButtonPress:
  case ButtonRelease: {
    windowEvent = true;
    return event->xbutton.window;
  }
  case MotionNotify: {
    windowEvent = true;
    return event->xmotion.window;
  }
  case EnterNotify:
  case LeaveNotify: {
    windowEvent = true;
    return event->xcrossing.window;
  }
  case FocusIn:
  case FocusOut: {
    windowEvent = true;
    return event->xfocus.window;
  }
  case GenericEvent: {
    if (event->xgeneric.extension == XINPUT_OPCODE) {
      switch (event->xgeneric.evtype) {
      case XI_Motion:
      case XI_TouchBegin:
      case XI_TouchUpdate:
      case XI_TouchEnd: {
        XIDeviceEvent* ev = (XIDeviceEvent*)event;
        windowEvent = true;
        return ev->event;
      }
      }
    }
  }
  }
  windowEvent = false;
  return 0;
}

std::shared_ptr<IWindow> _WindowXlibNew(std::string_view title, Display* display, void* xcbConn, int defaultScreen,
                                        XIM xIM, XIMStyle bestInputStyle, XFontSet fontset, GLXContext lastCtx,
                                        void* vulkanHandle, GLContext* glCtx);

static XIMStyle ChooseBetterStyle(XIMStyle style1, XIMStyle style2) {
  XIMStyle s, t;
  XIMStyle preedit = XIMPreeditArea | XIMPreeditCallbacks | XIMPreeditPosition | XIMPreeditNothing | XIMPreeditNone;
  XIMStyle status = XIMStatusArea | XIMStatusCallbacks | XIMStatusNothing | XIMStatusNone;
  if (style1 == 0)
    return style2;
  if (style2 == 0)
    return style1;
  if ((style1 & (preedit | status)) == (style2 & (preedit | status)))
    return style1;
  s = style1 & preedit;
  t = style2 & preedit;
  if (s != t) {
    if (s | t | XIMPreeditCallbacks)
      return (s == XIMPreeditCallbacks) ? style1 : style2;
    else if (s | t | XIMPreeditPosition)
      return (s == XIMPreeditPosition) ? style1 : style2;
    else if (s | t | XIMPreeditArea)
      return (s == XIMPreeditArea) ? style1 : style2;
    else if (s | t | XIMPreeditNothing)
      return (s == XIMPreeditNothing) ? style1 : style2;
  } else { /* if preedit flags are the same, compare status flags */
    s = style1 & status;
    t = style2 & status;
    if (s | t | XIMStatusCallbacks)
      return (s == XIMStatusCallbacks) ? style1 : style2;
    else if (s | t | XIMStatusArea)
      return (s == XIMStatusArea) ? style1 : style2;
    else if (s | t | XIMStatusNothing)
      return (s == XIMStatusNothing) ? style1 : style2;
  }
  return 0;
}

class ApplicationXlib final : public IApplication {
  friend class ScreenSaverInhibitor;
  IApplicationCallback& m_callback;
  const std::string m_uniqueName;
  const std::string m_friendlyName;
  const std::string m_pname;
  const std::vector<std::string> m_args;
  GLContext m_glContext;

  /* DBus single-instance */
  bool m_singleInstance;
#ifndef BOO_MSAN
  DBusConnection* m_dbus = nullptr;
#endif

  /* All windows */
  std::unordered_map<Window, std::weak_ptr<IWindow>> m_windows;

  Display* m_xDisp = nullptr;
  XIM m_xIM = nullptr;
  XFontSet m_fontset = nullptr;
  XIMStyle m_bestStyle = 0;
  int m_xDefaultScreen = 0;
  int m_x11Fd, m_dbusFd, m_maxFd;

#if BOO_HAS_VULKAN
  /* Vulkan enable */
  xcb_connection_t* m_xcbConn;

  void* m_vkHandle = nullptr;
  PFN_vkGetInstanceProcAddr m_getVkProc = 0;
  bool loadVk() {
    const char filename[] = "libvulkan.so";
    void *handle, *symbol;

#ifdef UNINSTALLED_LOADER
    handle = dlopen(UNINSTALLED_LOADER, RTLD_LAZY);
    if (!handle)
      handle = dlopen(filename, RTLD_LAZY);
#else
    handle = dlopen(filename, RTLD_LAZY);
#endif

    if (handle)
      symbol = dlsym(handle, "vkGetInstanceProcAddr");

    if (!handle || !symbol) {

      if (handle)
        dlclose(handle);
      return false;
    }

    m_vkHandle = handle;
    m_getVkProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol);
    return true;
  }
#endif

  void _deletedWindow(IWindow* window) override { m_windows.erase((Window)window->getPlatformHandle()); }

public:
  ApplicationXlib(IApplicationCallback& callback, std::string_view uniqueName, std::string_view friendlyName,
                  std::string_view pname, const std::vector<std::string>& args, std::string_view gfxApi,
                  uint32_t samples, uint32_t anisotropy, bool deepColor, bool singleInstance)
  : m_callback(callback)
  , m_uniqueName(uniqueName)
  , m_friendlyName(friendlyName)
  , m_pname(pname)
  , m_args(args)
  , m_singleInstance(singleInstance) {
    m_glContext.m_sampleCount = samples;
    m_glContext.m_anisotropy = anisotropy;
    m_glContext.m_deepColor = deepColor;
#if BOO_HAS_VULKAN
    g_VulkanContext.m_sampleCountColor = samples;
    g_VulkanContext.m_sampleCountDepth = samples;
    g_VulkanContext.m_anisotropy = anisotropy;
    g_VulkanContext.m_deepColor = deepColor;

    /* Check for Vulkan presence and preference */
    bool tryVulkan = true;
    if (!gfxApi.compare("OpenGL"))
      tryVulkan = false;
    for (const std::string& arg : args) {
      if (!arg.compare("--gl")) {
        tryVulkan = false;
        break;
      }
      if (!arg.compare("--vulkan")) {
        tryVulkan = true;
        break;
      }
    }

    if (tryVulkan)
      loadVk();

    if (m_getVkProc)
      Log.report(logvisor::Info, fmt("using Vulkan renderer"));
    else
#endif
      Log.report(logvisor::Info, fmt("using OpenGL renderer"));

#ifndef BOO_MSAN
    /* DBus single instance registration */
    bool isFirst;
    m_dbus = RegisterDBus(uniqueName.data(), isFirst);
    if (m_singleInstance) {
      if (!isFirst) {
        /* This is a duplicate instance, send signal and return */
        if (args.size()) {
          /* create a signal & check for errors */
          DBusMessage* msg = dbus_message_new_signal("/boo/signal/FileHandler", "boo.signal.FileHandling", "Open");

          /* append arguments onto signal */
          DBusMessageIter argsIter;
          dbus_message_iter_init_append(msg, &argsIter);
          for (const std::string& arg : args) {
            const char* sigvalue = arg.c_str();
            dbus_message_iter_append_basic(&argsIter, DBUS_TYPE_STRING, &sigvalue);
          }

          /* send the message and flush the connection */
          dbus_uint32_t serial;
          dbus_connection_send(m_dbus, msg, &serial);
          dbus_connection_flush(m_dbus);
          dbus_message_unref(msg);
        }
        return;
      } else {
        /* This is the first instance, register for signal */
        // add a rule for which messages we want to see
        DBusError err = {};
        dbus_bus_add_match(m_dbus, "type='signal',interface='boo.signal.FileHandling'", &err);
        dbus_connection_add_filter(m_dbus, DBusHandleMessageFunction(DBusCallback), this, nullptr);
        dbus_connection_flush(m_dbus);
      }
    }
#endif

    if (!XInitThreads()) {
      Log.report(logvisor::Fatal, fmt("X doesn't support multithreading"));
      return;
    }

    /* Program is portable to all locales */
    setlocale(LC_ALL, "");

    /* Open Xlib Display */
    m_xDisp = XOpenDisplay(0);
    if (!m_xDisp) {
      Log.report(logvisor::Fatal, fmt("Can't open X display"));
      return;
    }

#if BOO_HAS_VULKAN
    /* Cast Display to XCB connection for vulkan */
    m_xcbConn = XGetXCBConnection(m_xDisp);
    if (!m_xcbConn) {
      Log.report(logvisor::Fatal, fmt("Can't cast Display to XCB connection for Vulkan"));
      return;
    }
#endif

    /* Configure locale */
    if (!XSupportsLocale()) {
      Log.report(logvisor::Fatal, fmt("X does not support locale {}."), setlocale(LC_ALL, nullptr));
      return;
    }
    if (XSetLocaleModifiers("") == nullptr)
      Log.report(logvisor::Warning, fmt("Cannot set locale modifiers."));

    if ((m_xIM = XOpenIM(m_xDisp, nullptr, nullptr, nullptr))) {
      char** missing_charsets;
      int num_missing_charsets = 0;
      char* default_string;
      m_fontset = XCreateFontSet(m_xDisp,
                                 "-adobe-helvetica-*-r-*-*-*-120-*-*-*-*-*-*,\
                                        -misc-fixed-*-r-*-*-*-130-*-*-*-*-*-*",
                                 &missing_charsets, &num_missing_charsets, &default_string);

      /* figure out which styles the IM can support */
      XIMStyles* im_supported_styles;
      XIMStyle app_supported_styles;
      XGetIMValues(m_xIM, XNQueryInputStyle, &im_supported_styles, nullptr);
      /* set flags for the styles our application can support */
      app_supported_styles = XIMPreeditNone | XIMPreeditNothing | XIMPreeditPosition;
      app_supported_styles |= XIMStatusNone | XIMStatusNothing;
      /*
       * now look at each of the IM supported styles, and
       * chose the "best" one that we can support.
       */
      for (int i = 0; i < im_supported_styles->count_styles; ++i) {
        XIMStyle style = im_supported_styles->supported_styles[i];
        if ((style & app_supported_styles) == style) /* if we can handle it */
          m_bestStyle = ChooseBetterStyle(style, m_bestStyle);
      }
      /* if we couldn't support any of them, print an error and exit */
      if (m_bestStyle == 0) {
        Log.report(logvisor::Fatal, fmt("interaction style not supported."));
        return;
      }
      XFree(im_supported_styles);
    }

    m_xDefaultScreen = DefaultScreen(m_xDisp);
    X_CURSORS.m_pointer = XCreateFontCursor(m_xDisp, XC_left_ptr);
    X_CURSORS.m_hArrow = XCreateFontCursor(m_xDisp, XC_sb_h_double_arrow);
    X_CURSORS.m_vArrow = XCreateFontCursor(m_xDisp, XC_sb_v_double_arrow);
    X_CURSORS.m_ibeam = XCreateFontCursor(m_xDisp, XC_xterm);
    X_CURSORS.m_crosshairs = XCreateFontCursor(m_xDisp, XC_cross);
    X_CURSORS.m_wait = XCreateFontCursor(m_xDisp, XC_watch);

    /* The xkb extension requests that the X server does not
     * send repeated keydown events when a key is held */
    XkbQueryExtension(m_xDisp, &XINPUT_OPCODE, nullptr, nullptr, nullptr, nullptr);
    XkbSetDetectableAutoRepeat(m_xDisp, True, nullptr);

    /* Get file descriptors of xcb and dbus interfaces */
    m_x11Fd = ConnectionNumber(m_xDisp);
#ifndef BOO_MSAN
    dbus_connection_get_unix_fd(m_dbus, &m_dbusFd);
#else
    m_dbusFd = 0;
#endif
    m_maxFd = MAX(m_x11Fd, m_dbusFd);

    XFlush(m_xDisp);
  }

  ~ApplicationXlib() override {
    for (auto& p : m_windows)
      if (auto w = p.second.lock())
        w->_cleanup();

#if BOO_HAS_VULKAN
    g_VulkanContext.destroyDevice();
#endif

    if (m_fontset)
      XFreeFontSet(m_xDisp, m_fontset);
    if (m_xIM)
      XCloseIM(m_xIM);
    if (m_xDisp)
      XCloseDisplay(m_xDisp);
  }

  EPlatformType getPlatformType() const override { return EPlatformType::Xlib; }

  /* Empty handler for SIGINT */
  static void _sigint(int) {}

  static DBusHandlerResult DBusCallback(DBusConnection* connection,
                                        DBusMessage* message,
                                        ApplicationXlib* user_data) {
    return user_data->HandleDBusMessage(message);
  }

  DBusHandlerResult HandleDBusMessage(DBusMessage* message) {
    if (dbus_message_is_signal(message, "boo.signal.FileHandling", "Open")) {
      /* read the parameters */
      std::vector<std::string> paths;
      DBusMessageIter iter;
      dbus_message_iter_init(message, &iter);
      while (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INVALID) {
        const char* argVal;
        dbus_message_iter_get_basic(&iter, &argVal);
        paths.push_back(argVal);
        dbus_message_iter_next(&iter);
      }
      m_callback.appFilesOpen(this, paths);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  int run() override {
    if (!m_xDisp)
      return 1;

    /* SIGINT will be used to cancel main thread when client thread ends
     * (also enables graceful quitting via ctrl-c) */
    pthread_t mainThread = pthread_self();
    struct sigaction s;
    s.sa_handler = _sigint;
    sigemptyset(&s.sa_mask);
    s.sa_flags = 0;
    sigaction(SIGINT, &s, nullptr);
    sigaction(SIGUSR2, &s, nullptr);

    sigset_t waitmask, origmask;
    sigemptyset(&waitmask);
    sigaddset(&waitmask, SIGINT);
    sigaddset(&waitmask, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &waitmask, &origmask);

    /* Screen Saver inhibitor */
    std::optional<ScreenSaverInhibitor> inhibitor;

    /* Spawn client thread */
    int clientReturn = INT_MIN;
    std::mutex initmt;
    std::condition_variable initcv;
    std::unique_lock<std::mutex> outerLk(initmt);
    std::thread clientThread([&]() {
      std::unique_lock<std::mutex> innerLk(initmt);
      innerLk.unlock();
      initcv.notify_one();
      std::string thrName = std::string(getFriendlyName()) + " Client";
      logvisor::RegisterThreadName(thrName.c_str());
      clientReturn = m_callback.appMain(this);
      pthread_kill(mainThread, SIGUSR2);
    });
    initcv.wait(outerLk);

    /* Begin application event loop */
    while (clientReturn == INT_MIN) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(m_x11Fd, &fds);
      FD_SET(m_dbusFd, &fds);
      if (pselect(m_maxFd + 1, &fds, nullptr, nullptr, nullptr, &origmask) < 0) {
        /* SIGINT/SIGUSR2 handled here */
        if (errno == EINTR)
          break;
      }

      if (FD_ISSET(m_x11Fd, &fds)) {
        bool needsQuit = false;

        XLockDisplay(m_xDisp);
        while (XPending(m_xDisp)) {
          XEvent event = {};
          XNextEvent(m_xDisp, &event);
          if (XFilterEvent(&event, None))
            continue;
          bool windowEvent;
          Window evWindow = GetWindowOfEvent(&event, windowEvent);
          if (windowEvent) {
            if (event.type == ClientMessage && event.xclient.data.l[0] == 'NWID') {
              if (!inhibitor) /* First window created, use to inhibit screensaver */
                inhibitor.emplace(m_dbus, evWindow);
            } else {
              auto window = m_windows.find(evWindow);
              if (window != m_windows.end())
                if (std::shared_ptr<IWindow> w = window->second.lock())
                  if (w->_incomingEvent(&event) && m_windows.size() == 1) {
                    needsQuit = true;
                    break;
                  }
            }
          }
        }
        XUnlockDisplay(m_xDisp);

        if (needsQuit)
          break;
      }

#ifndef BOO_MSAN
      if (FD_ISSET(m_dbusFd, &fds)) {
        dbus_connection_read_write(m_dbus, 0);
        while (dbus_connection_dispatch(m_dbus) == DBUS_DISPATCH_DATA_REMAINS) {}
      }
#endif
    }

    m_callback.appQuitting(this);
    if (clientThread.joinable())
      clientThread.join();

    return clientReturn;
  }

  std::string_view getUniqueName() const override { return m_uniqueName; }

  std::string_view getFriendlyName() const override { return m_friendlyName; }

  std::string_view getProcessName() const override { return m_pname; }

  const std::vector<std::string>& getArgs() const override { return m_args; }

  std::shared_ptr<IWindow> newWindow(std::string_view title) override {
    XLockDisplay(m_xDisp);
#if BOO_HAS_VULKAN
    std::shared_ptr<IWindow> newWindow = _WindowXlibNew(title, m_xDisp, m_xcbConn, m_xDefaultScreen, m_xIM, m_bestStyle,
                                                        m_fontset, m_lastGlxCtx, (void*)m_getVkProc, &m_glContext);
#else
    std::shared_ptr<IWindow> newWindow = _WindowXlibNew(title, m_xDisp, nullptr, m_xDefaultScreen, m_xIM, m_bestStyle,
                                                        m_fontset, m_lastGlxCtx, nullptr, &m_glContext);
#endif
    Window wid = (Window)newWindow->getPlatformHandle();
    m_windows[wid] = newWindow;
    XEvent reply = {};
    reply.xclient.type = ClientMessage;
    reply.xclient.window = wid;
    reply.xclient.message_type = XA_INTEGER;
    reply.xclient.format = 32;
    reply.xclient.data.l[0] = 'NWID';
    XSendEvent(m_xDisp, wid, false, 0, &reply);
    XUnlockDisplay(m_xDisp);
    return newWindow;
  }

  /* Last GLX context */
  GLXContext m_lastGlxCtx = nullptr;
};

void _XlibUpdateLastGlxCtx(GLXContext lastGlxCtx) { static_cast<ApplicationXlib*>(APP)->m_lastGlxCtx = lastGlxCtx; }

} // namespace boo

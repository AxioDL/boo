#ifndef APPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "boo/IApplication.hpp"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include <dbus/dbus.h>
DBusConnection* RegisterDBus(const char* appName, bool& isFirst);

#include <LogVisor/LogVisor.hpp>

#include <sys/param.h>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace boo
{
static LogVisor::LogModule Log("boo::ApplicationXCB");

int XCB_GLX_EVENT_BASE = 0;
int XINPUT_OPCODE = 0;

static Window GetWindowOfEvent(XEvent* event, bool& windowEvent)
{
    switch (event->type)
    {
    case ClientMessage:
    {
        windowEvent = true;
        return event->xclient.window;
    }
    case Expose:
    {
        windowEvent = true;
        return event->xexpose.window;
    }
    case ConfigureNotify:
    {
        windowEvent = true;
        return event->xconfigure.window;
    }
    case KeyPress:
    case KeyRelease:
    {
        windowEvent = true;
        return event->xkey.window;
    }
    case ButtonPress:
    case ButtonRelease:
    {
        windowEvent = true;
        return event->xbutton.window;;
    }
    case MotionNotify:
    {
        windowEvent = true;
        return event->xmotion.window;
    }
    case EnterNotify:
    case LeaveNotify:
    {
        windowEvent = true;
        return event->xcrossing.window;
    }
    case FocusIn:
    case FocusOut:
    {
        windowEvent = true;
        return event->xfocus.window;
    }
    case GenericEvent:
    {
        if (event->xgeneric.extension == XINPUT_OPCODE)
        {
            switch (event->xgeneric.evtype)
            {
            case XI_Motion:
            case XI_TouchBegin:
            case XI_TouchUpdate:
            case XI_TouchEnd:
            {
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
    
IWindow* _WindowXlibNew(const std::string& title,
                       Display* display, int defaultScreen,
                       GLXContext lastCtx);
    
class ApplicationXlib final : public IApplication
{
    IApplicationCallback& m_callback;
    const std::string m_uniqueName;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;

    /* DBus single-instance */
    bool m_singleInstance;
    DBusConnection* m_dbus = nullptr;

    /* All windows */
    std::unordered_map<Window, IWindow*> m_windows;

    Display* m_xDisp = nullptr;
    int m_xDefaultScreen = 0;
    int m_xcbFd, m_dbusFd, m_maxFd;
    
    void _deletedWindow(IWindow* window)
    {
        m_windows.erase((Window)window->getPlatformHandle());
    }
    
public:
    ApplicationXlib(IApplicationCallback& callback,
                    const std::string& uniqueName,
                    const std::string& friendlyName,
                    const std::string& pname,
                    const std::vector<std::string>& args,
                    bool singleInstance)
    : m_callback(callback),
      m_uniqueName(uniqueName),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args),
      m_singleInstance(singleInstance)
    {
        /* DBus single instance registration */
        bool isFirst;
        m_dbus = RegisterDBus(uniqueName.c_str(), isFirst);
        if (m_singleInstance)
        {
            if (!isFirst)
            {
                /* This is a duplicate instance, send signal and return */
                if (args.size())
                {
                    /* create a signal & check for errors */
                    DBusMessage*
                    msg = dbus_message_new_signal("/boo/signal/FileHandler",
                                                  "boo.signal.FileHandling",
                                                  "Open");

                    /* append arguments onto signal */
                    DBusMessageIter argsIter;
                    dbus_message_iter_init_append(msg, &argsIter);
                    for (const std::string& arg : args)
                    {
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
            }
            else
            {
                /* This is the first instance, register for signal */
                // add a rule for which messages we want to see
                DBusError err = {};
                dbus_bus_add_match(m_dbus, "type='signal',interface='boo.signal.FileHandling'", &err);
                dbus_connection_flush(m_dbus);
            }
        }

        if (!XInitThreads())
        {
            Log.report(LogVisor::FatalError, "X doesn't support multithreading");
            return;
        }

        /* Open Xlib Display */
        m_xDisp = XOpenDisplay(0);
        if (!m_xDisp)
        {
            Log.report(LogVisor::FatalError, "Can't open X display");
            return;
        }

        m_xDefaultScreen = DefaultScreen(m_xDisp);


        /* The xkb extension requests that the X server does not
         * send repeated keydown events when a key is held */
        XkbQueryExtension(m_xDisp, &XINPUT_OPCODE, nullptr, nullptr, nullptr, nullptr);
        XkbSetDetectableAutoRepeat(m_xDisp, True, nullptr);

        /* Get file descriptors of xcb and dbus interfaces */
        m_xcbFd = ConnectionNumber(m_xDisp);
        dbus_connection_get_unix_fd(m_dbus, &m_dbusFd);
        m_maxFd = MAX(m_xcbFd, m_dbusFd);

        XFlush(m_xDisp);
    }

    ~ApplicationXlib()
    {
        XCloseDisplay(m_xDisp);
    }
    
    EPlatformType getPlatformType() const
    {
        return PLAT_XLIB;
    }
    
    /* Empty handler for SIGTERM */
    static void _sigterm(int) {}

    int run()
    {
        if (!m_xDisp)
            return 1;

        /* SIGTERM will be used to terminate main thread when client thread ends */
        pthread_t mainThread = pthread_self();
        struct sigaction s;
        s.sa_handler = _sigterm;
        sigemptyset(&s.sa_mask);
        s.sa_flags = 0;
        sigaction(SIGTERM, &s, nullptr);

        /* Spawn client thread */
        int clientReturn = INT_MIN;
        std::mutex initmt;
        std::condition_variable initcv;
        std::unique_lock<std::mutex> outerLk(initmt);
        std::thread clientThread([&]()
        {
            std::unique_lock<std::mutex> innerLk(initmt);
            innerLk.unlock();
            initcv.notify_one();
            clientReturn = m_callback.appMain(this);
            pthread_kill(mainThread, SIGTERM);
        });
        initcv.wait(outerLk);

        /* Begin application event loop */
        while (clientReturn == INT_MIN)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_xcbFd, &fds);
            FD_SET(m_dbusFd, &fds);
            if (select(m_maxFd+1, &fds, NULL, NULL, NULL) < 0)
            {
                /* SIGTERM handled here */
                if (errno == EINTR)
                    break;
            }

            if (FD_ISSET(m_xcbFd, &fds))
            {
                XLockDisplay(m_xDisp);
                while (XPending(m_xDisp))
                {
                    XEvent event;
                    XNextEvent(m_xDisp, &event);
                    bool windowEvent;
                    Window evWindow = GetWindowOfEvent(&event, windowEvent);
                    //fprintf(stderr, "EVENT %d\n", XCB_EVENT_RESPONSE_TYPE(event));
                    if (windowEvent)
                    {
                        auto window = m_windows.find(evWindow);
                        if (window != m_windows.end())
                            window->second->_incomingEvent(&event);
                    }
                }
                XUnlockDisplay(m_xDisp);
            }

            if (FD_ISSET(m_dbusFd, &fds))
            {
                DBusMessage* msg;
                dbus_connection_read_write(m_dbus, 0);
                while ((msg = dbus_connection_pop_message(m_dbus)))
                {
                    /* check if the message is a signal from the correct interface and with the correct name */
                    if (dbus_message_is_signal(msg, "boo.signal.FileHandling", "Open"))
                    {
                        /* read the parameters */
                        std::vector<std::string> paths;
                        DBusMessageIter iter;
                        dbus_message_iter_init(msg, &iter);
                        while (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INVALID)
                        {
                            const char* argVal;
                            dbus_message_iter_get_basic(&iter, &argVal);
                            paths.push_back(argVal);
                            dbus_message_iter_next(&iter);
                        }
                        m_callback.appFilesOpen(this, paths);
                    }
                    dbus_message_unref(msg);
                }
            }
        }

        m_callback.appQuitting(this);
        clientThread.join();
        return clientReturn;
    }

    const std::string& getUniqueName() const
    {
        return m_uniqueName;
    }

    const std::string& getFriendlyName() const
    {
        return m_friendlyName;
    }
    
    const std::string& getProcessName() const
    {
        return m_pname;
    }
    
    const std::vector<std::string>& getArgs() const
    {
        return m_args;
    }
    
    IWindow* newWindow(const std::string& title)
    {
        IWindow* newWindow = _WindowXlibNew(title, m_xDisp, m_xDefaultScreen, m_lastGlxCtx);
        m_windows[(Window)newWindow->getPlatformHandle()] = newWindow;
        return newWindow;
    }

    /* Last GLX context */
    GLXContext m_lastGlxCtx = nullptr;
};

void _XlibUpdateLastGlxCtx(GLXContext lastGlxCtx)
{
    static_cast<ApplicationXlib*>(APP)->m_lastGlxCtx = lastGlxCtx;
}
    
}

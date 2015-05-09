#ifndef CAPPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "IApplication.hpp"

#define explicit explicit_c
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xcb/xkb.h>
#undef explicit

namespace boo
{

static xcb_window_t getWindowOfEvent(xcb_generic_event_t* event, bool& windowEvent)
{
    switch (event->response_type & ~0x80)
    {
    case XCB_EXPOSE:
    {
        xcb_expose_event_t* ev = (xcb_expose_event_t*)event;
        windowEvent = true;
        return ev->window;
    }
    case XCB_CONFIGURE_NOTIFY:
    {
        xcb_configure_notify_event_t* ev = (xcb_configure_notify_event_t*)event;
        windowEvent = true;
        return ev->window;
    }
    case XCB_KEY_PRESS:
    {
        xcb_key_press_event_t* ev = (xcb_key_press_event_t*)event;
        windowEvent = true;
        return ev->root;
    }
    case XCB_KEY_RELEASE:
    {
        xcb_key_release_event_t* ev = (xcb_key_release_event_t*)event;
        windowEvent = true;
        return ev->root;
    }
    case XCB_BUTTON_PRESS:
    {
        xcb_button_press_event_t* ev = (xcb_button_press_event_t*)event;
        windowEvent = true;
        return ev->root;
    }
    case XCB_BUTTON_RELEASE:
    {
        xcb_button_release_event_t* ev = (xcb_button_release_event_t*)event;
        windowEvent = true;
        return ev->root;
    }
    case XCB_MOTION_NOTIFY:
    {
        xcb_motion_notify_event_t* ev = (xcb_motion_notify_event_t*)event;
        windowEvent = true;
        return ev->root;
    }
    default:
        windowEvent = false;
        return 0;
    }
}
    
IWindow* _CWindowXCBNew(const std::string& title, xcb_connection_t* conn);
    
class CApplicationXCB final : public IApplication
{
    const IApplicationCallback& m_callback;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;

    /* All windows */
    std::unordered_map<xcb_window_t, IWindow*> m_windows;

    xcb_connection_t* m_xcbConn;
    bool m_running;
    
    void _deletedWindow(IWindow* window)
    {
        m_windows.erase((xcb_window_t)window->getPlatformHandle());
    }
    
public:
    CApplicationXCB(const IApplicationCallback& callback,
                    const std::string& friendlyName,
                    const std::string& pname,
                    const std::vector<std::string>& args)
    : m_callback(callback),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args)
    {
        m_xcbConn = xcb_connect(NULL, NULL);

        /* The convoluted xkb extension requests that the X server does not
         * send repeated keydown events when a key is held */
        xkb_x11_setup_xkb_extension(m_xcbConn,
                                    XKB_X11_MIN_MAJOR_XKB_VERSION,
                                    XKB_X11_MIN_MINOR_XKB_VERSION,
                                    XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                    NULL, NULL, NULL, NULL);
        xcb_xkb_per_client_flags(m_xcbConn, XCB_XKB_ID_USE_CORE_KBD,
                                 XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                 XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0);
    }

    ~CApplicationXCB()
    {
        xcb_disconnect(m_xcbConn);
    }
    
    EPlatformType getPlatformType() const
    {
        return PLAT_XCB;
    }
    
    void run()
    {
        xcb_generic_event_t* event;
        while (m_running && (event = xcb_wait_for_event(m_xcbConn)))
        {
            bool windowEvent;
            xcb_window_t evWindow = getWindowOfEvent(event, windowEvent);
            if (windowEvent)
            {
                IWindow* window = m_windows[evWindow];

            }
            free(event);
        }
    }

    void quit()
    {
        m_running = false;
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
        IWindow* newWindow = _CWindowXCBNew(title, m_xcbConn);
        m_windows[(xcb_window_t)newWindow->getPlatformHandle()] = newWindow;
        return newWindow;
    }
};
    
}

#ifndef CAPPLICATION_UNIX_CPP
#error This file may only be included from CApplicationUnix.cpp
#endif

#include "IApplication.hpp"

#define explicit explicit_c
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xcb/xkb.h>
#include <xcb/xinput.h>
#undef explicit

namespace boo
{


int XINPUT_OPCODE = 0;

static xcb_window_t getWindowOfEvent(xcb_generic_event_t* event, bool& windowEvent)
{
    switch (XCB_EVENT_RESPONSE_TYPE(event))
    {
    case XCB_CLIENT_MESSAGE:
    {
        xcb_client_message_event_t* ev = (xcb_client_message_event_t*)event;
        windowEvent = true;
        return ev->window;
    }
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
        return ev->event;
    }
    case XCB_KEY_RELEASE:
    {
        xcb_key_release_event_t* ev = (xcb_key_release_event_t*)event;
        windowEvent = true;
        return ev->event;
    }
    case XCB_BUTTON_PRESS:
    {
        xcb_button_press_event_t* ev = (xcb_button_press_event_t*)event;
        windowEvent = true;
        return ev->event;
    }
    case XCB_BUTTON_RELEASE:
    {
        xcb_button_release_event_t* ev = (xcb_button_release_event_t*)event;
        windowEvent = true;
        return ev->event;
    }
    case XCB_MOTION_NOTIFY:
    {
        xcb_motion_notify_event_t* ev = (xcb_motion_notify_event_t*)event;
        windowEvent = true;
        return ev->event;
    }
    case XCB_GE_GENERIC:
    {
        xcb_ge_event_t* gev = (xcb_ge_event_t*)event;
        if (gev->pad0 == XINPUT_OPCODE)
        {
            fprintf(stderr, "INPUTEVENT\n");
            return 0;
            switch (XCB_EVENT_RESPONSE_TYPE(gev))
            {
            case XCB_INPUT_DEVICE_CHANGED:
            {
                xcb_input_device_changed_event_t* ev = (xcb_input_device_changed_event_t*)event;
                return 0;
            }
            case XCB_INPUT_DEVICE_MOTION_NOTIFY:
            {
                xcb_input_device_motion_notify_event_t* ev = (xcb_input_device_motion_notify_event_t*)event;
                windowEvent = true;
                return ev->event;
            }
            default:
                return 0;
            }
        }
    }
    default:
        windowEvent = false;
        return 0;
    }
}
    
IWindow* _CWindowXCBNew(const std::string& title, xcb_connection_t* conn);
    
class CApplicationXCB final : public IApplication
{
    IApplicationCallback& m_callback;
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
    CApplicationXCB(IApplicationCallback& callback,
                    const std::string& friendlyName,
                    const std::string& pname,
                    const std::vector<std::string>& args)
    : m_callback(callback),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args)
    {
        m_xcbConn = xcb_connect(NULL, NULL);

        /* This convoluted xkb extension requests that the X server does not
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
        m_running = true;
        m_callback.appLaunched(this);
        xcb_flush(m_xcbConn);
        while (m_running && (event = xcb_wait_for_event(m_xcbConn)))
        {
            bool windowEvent;
            xcb_window_t evWindow = getWindowOfEvent(event, windowEvent);
            fprintf(stderr, "EVENT %d\n", XCB_EVENT_RESPONSE_TYPE(event));
            if (windowEvent)
            {
                auto window = m_windows.find(evWindow);
                if (window != m_windows.end())
                    window->second->_incomingEvent(event);
            }
            free(event);
        }
        m_callback.appQuitting(this);
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

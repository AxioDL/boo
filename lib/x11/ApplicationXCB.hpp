#ifndef APPLICATION_UNIX_CPP
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

#include <dbus/dbus.h>
DBusConnection* registerDBus(const char* appName, bool& isFirst);

#include <sys/param.h>

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
            switch (gev->event_type)
            {
            case XCB_INPUT_MOTION:
            {
                xcb_input_motion_event_t* ev = (xcb_input_motion_event_t*)event;
                windowEvent = true;
                return ev->event;
            }
            case XCB_INPUT_TOUCH_BEGIN:
            {
                xcb_input_touch_begin_event_t* ev = (xcb_input_touch_begin_event_t*)event;
                windowEvent = true;
                return ev->event;
            }
            case XCB_INPUT_TOUCH_UPDATE:
            {
                xcb_input_touch_update_event_t* ev = (xcb_input_touch_update_event_t*)event;
                windowEvent = true;
                return ev->event;
            }
            case XCB_INPUT_TOUCH_END:
            {
                xcb_input_touch_end_event_t* ev = (xcb_input_touch_end_event_t*)event;
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
    
IWindow* _CWindowXCBNew(const std::string& title, xcb_connection_t* conn);
    
class ApplicationXCB final : public IApplication
{
    IApplicationCallback& m_callback;
    const std::string m_uniqueName;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;

    /* DBus single-instance */
    bool m_singleInstance;
    DBusConnection* m_dbus = NULL;

    /* All windows */
    std::unordered_map<xcb_window_t, IWindow*> m_windows;

    xcb_connection_t* m_xcbConn = NULL;
    bool m_running;
    
    void _deletedWindow(IWindow* window)
    {
        m_windows.erase((xcb_window_t)window->getPlatformHandle());
    }
    
public:
    ApplicationXCB(IApplicationCallback& callback,
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
        m_dbus = registerDBus(uniqueName.c_str(), isFirst);
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

        /* Open X connection */
        m_xcbConn = xcb_connect(NULL, NULL);

        /* The xkb extension requests that the X server does not
         * send repeated keydown events when a key is held */
        xkb_x11_setup_xkb_extension(m_xcbConn,
                                    XKB_X11_MIN_MAJOR_XKB_VERSION,
                                    XKB_X11_MIN_MINOR_XKB_VERSION,
                                    XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                    NULL, NULL, NULL, NULL);
        xcb_xkb_per_client_flags(m_xcbConn, XCB_XKB_ID_USE_CORE_KBD,
                                 XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                 XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0);

        /* Xinput major opcode */
        const xcb_query_extension_reply_t* xiReply =
        xcb_get_extension_data(m_xcbConn, &xcb_input_id);
        if (xiReply)
            XINPUT_OPCODE = xiReply->major_opcode;

    }

    ~ApplicationXCB()
    {
        xcb_disconnect(m_xcbConn);
    }
    
    EPlatformType getPlatformType() const
    {
        return PLAT_XCB;
    }
    
    void run()
    {
        if (!m_xcbConn)
            return;

        xcb_generic_event_t* event;
        m_running = true;
        m_callback.appLaunched(this);
        xcb_flush(m_xcbConn);

        int xcbFd = xcb_get_file_descriptor(m_xcbConn);
        int dbusFd;
        dbus_connection_get_unix_fd(m_dbus, &dbusFd);
        int maxFd = MAX(xcbFd, dbusFd);

        while (m_running)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xcbFd, &fds);
            FD_SET(dbusFd, &fds);
            select(maxFd+1, &fds, NULL, NULL, NULL);

            if (FD_ISSET(xcbFd, &fds))
            {
                event = xcb_poll_for_event(m_xcbConn);
                if (!event)
                    break;

                bool windowEvent;
                xcb_window_t evWindow = getWindowOfEvent(event, windowEvent);
                //fprintf(stderr, "EVENT %d\n", XCB_EVENT_RESPONSE_TYPE(event));
                if (windowEvent)
                {
                    auto window = m_windows.find(evWindow);
                    if (window != m_windows.end())
                        window->second->_incomingEvent(event);
                }
                free(event);
            }

            if (FD_ISSET(dbusFd, &fds))
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
    }

    void quit()
    {
        m_running = false;
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
        IWindow* newWindow = _CWindowXCBNew(title, m_xcbConn);
        m_windows[(xcb_window_t)newWindow->getPlatformHandle()] = newWindow;
        return newWindow;
    }
};
    
}

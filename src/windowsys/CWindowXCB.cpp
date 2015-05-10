#include "windowsys/IWindow.hpp"
#include "windowsys/IGraphicsContext.hpp"
#include "IApplication.hpp"

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define XK_MISCELLANY
#define XK_XKB_KEYS
#define XK_LATIN1
#include <X11/keysymdef.h>

#define REF_DPMM 3.7824 /* 96 DPI */
#define FS_ATOM "_NET_WM_STATE_FULLSCREEN"

namespace boo
{

static uint32_t translateKeysym(xcb_keysym_t sym, int& specialSym, int& modifierSym)
{
    specialSym = IWindowCallback::KEY_NONE;
    modifierSym = IWindowCallback::MKEY_NONE;
    if (sym >= XK_F1 && sym <= XK_F12)
        specialSym = IWindowCallback::KEY_F1 + sym - XK_F1;
    else if (sym == XK_Escape)
        specialSym = IWindowCallback::KEY_ESC;
    else if (sym == XK_Return)
        specialSym = IWindowCallback::KEY_ENTER;
    else if (sym == XK_BackSpace)
        specialSym = IWindowCallback::KEY_BACKSPACE;
    else if (sym == XK_Insert)
        specialSym = IWindowCallback::KEY_INSERT;
    else if (sym == XK_Delete)
        specialSym = IWindowCallback::KEY_DELETE;
    else if (sym == XK_Home)
        specialSym = IWindowCallback::KEY_HOME;
    else if (sym == XK_End)
        specialSym = IWindowCallback::KEY_END;
    else if (sym == XK_Page_Up)
        specialSym = IWindowCallback::KEY_PGUP;
    else if (sym == XK_Page_Down)
        specialSym = IWindowCallback::KEY_PGDOWN;
    else if (sym == XK_Left)
        specialSym = IWindowCallback::KEY_LEFT;
    else if (sym == XK_Right)
        specialSym = IWindowCallback::KEY_RIGHT;
    else if (sym == XK_Up)
        specialSym = IWindowCallback::KEY_UP;
    else if (sym == XK_Down)
        specialSym = IWindowCallback::KEY_DOWN;
    else if (sym == XK_Shift_L || sym == XK_Shift_R)
        modifierSym = IWindowCallback::MKEY_SHIFT;
    else if (sym == XK_Control_L || sym == XK_Control_R)
        modifierSym = IWindowCallback::MKEY_CTRL;
    else if (sym == XK_Alt_L || sym == XK_Alt_R)
        modifierSym = IWindowCallback::MKEY_ALT;
    else
        return xkb_keysym_to_utf32(sym);
    return 0;
}

static int translateModifiers(unsigned state)
{
    int retval = 0;
    if (state & XCB_MOD_MASK_SHIFT)
        retval |= IWindowCallback::MKEY_SHIFT;
    if (state & XCB_MOD_MASK_CONTROL)
        retval |= IWindowCallback::MKEY_CTRL;
    if (state & XCB_MOD_MASK_1)
        retval |= IWindowCallback::MKEY_ALT;
    return retval;
}

static int translateButton(unsigned state)
{
    int retval = 0;
    if (state & XCB_BUTTON_MASK_1)
        retval = IWindowCallback::BUTTON_PRIMARY;
    else if (state & XCB_BUTTON_MASK_2)
        retval = IWindowCallback::BUTTON_SECONDARY;
    else if (state & XCB_BUTTON_MASK_3)
        retval = IWindowCallback::BUTTON_MIDDLE;
    else if (state & XCB_BUTTON_MASK_4)
        retval = IWindowCallback::BUTTON_AUX1;
    else if (state & XCB_BUTTON_MASK_5)
        retval = IWindowCallback::BUTTON_AUX2;
    return retval;
}

#define INTERN_ATOM(var, conn, name) \
do {\
    xcb_intern_atom_cookie_t cookie = \
    xcb_intern_atom(conn, 0, sizeof(#name), #name); \
    xcb_intern_atom_reply_t* reply = \
    xcb_intern_atom_reply(conn, cookie, NULL); \
    var = reply->atom; \
    free(reply); \
} while(0)

struct SXCBAtoms
{
    xcb_atom_t m_netwmState = 0;
    xcb_atom_t m_netwmStateFullscreen = 0;
    xcb_atom_t m_netwmStateAdd = 0;
    xcb_atom_t m_netwmStateRemove = 0;
    xcb_atom_t m_
    xcb_key_symbols_t* m_keySyms = NULL;
    SXCBAtoms(xcb_connection_t* conn)
    {
        INTERN_ATOM(m_netwmState, conn, _NET_WM_STATE);
        INTERN_ATOM(m_netwmStateFullscreen, conn, _NET_WM_STATE_FULLSCREEN);
        INTERN_ATOM(m_netwmStateAdd, conn, _NET_WM_STATE_ADD);
        INTERN_ATOM(m_netwmStateRemove, conn, _NET_WM_STATE_REMOVE);
        m_keySyms = xcb_key_symbols_alloc(conn);
    }
};
static SXCBAtoms* S_ATOMS = NULL;

static void genFrameDefault(xcb_screen_t* screen, int* xOut, int* yOut, int* wOut, int* hOut)
{
    float width = screen->width_in_pixels * 2.0 / 3.0;
    float height = screen->height_in_pixels * 2.0 / 3.0;
    *xOut = (screen->width_in_pixels - width) / 2.0;
    *yOut = (screen->height_in_pixels - height) / 2.0;
    *wOut = width;
    *hOut = height;
}
    
IGraphicsContext* _CGraphicsContextXCBNew(IGraphicsContext::EGraphicsAPI api,
                                          IWindow* parentWindow, xcb_connection_t* conn,
                                          uint32_t& visualIdOut);

class CWindowXCB final : public IWindow
{
    
    xcb_connection_t* m_xcbConn;
    xcb_window_t m_windowId;
    IGraphicsContext* m_gfxCtx;
    IWindowCallback* m_callback;

    /* Cached window rectangle (to avoid repeated X queries) */
    int m_wx, m_wy, m_ww, m_wh;
    float m_pixelFactor;
    
public:
    
    CWindowXCB(const std::string& title, xcb_connection_t* conn)
    : m_xcbConn(conn), m_callback(NULL)
    {
        if (!S_ATOMS)
            S_ATOMS = new SXCBAtoms(conn);

        /* Default screen */
        xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(m_xcbConn)).data;
        m_pixelFactor = screen->width_in_pixels / (float)screen->width_in_millimeters / REF_DPMM;

        /* Construct graphics context */
        uint32_t visualId;
        m_gfxCtx = _CGraphicsContextXCBNew(IGraphicsContext::API_OPENGL_3_3, this, m_xcbConn, visualId);


        /* Create colormap */
        xcb_colormap_t colormap = xcb_generate_id(m_xcbConn);
        xcb_create_colormap(m_xcbConn, XCB_COLORMAP_ALLOC_NONE,
                            colormap, screen->root, visualId);

        /* Create window */
        int x, y, w, h;
        genFrameDefault(screen, &x, &y, &w, &h);
        uint32_t valueMasks[] =
        {
            XCB_NONE,
            XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_STRUCTURE_NOTIFY | 0xFFFFFF,
            colormap,
            XCB_NONE
        };
        m_windowId = xcb_generate_id(conn);
        xcb_create_window(m_xcbConn, XCB_COPY_FROM_PARENT, m_windowId, screen->root,
                          x, y, w, h, 10,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, visualId,
                          XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                          valueMasks);

        /* Set the title of the window */
        const char* c_title = title.c_str();
        xcb_change_property(m_xcbConn, XCB_PROP_MODE_REPLACE, m_windowId,
                            XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                            strlen(c_title), c_title);

        /* Set the title of the window icon */
        xcb_change_property(m_xcbConn, XCB_PROP_MODE_REPLACE, m_windowId,
                            XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8,
                            strlen(c_title), c_title);

        /* Initialize context */
        xcb_map_window(m_xcbConn, m_windowId);
        m_gfxCtx->initializeContext();

    }
    
    ~CWindowXCB()
    {
        IApplicationInstance()->_deletedWindow(this);
    }
    
    void setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }
    
    void showWindow()
    {
        xcb_map_window(m_xcbConn, m_windowId);
        xcb_flush(m_xcbConn);
    }
    
    void hideWindow()
    {
        xcb_unmap_window(m_xcbConn, m_windowId);
        xcb_flush(m_xcbConn);
    }
    
    std::string getTitle()
    {
        xcb_get_property_cookie_t cookie =
        xcb_get_property(m_xcbConn, 0, m_windowId, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 64);
        xcb_get_property_reply_t* reply =
        xcb_get_property_reply(m_xcbConn, cookie, NULL);
        std::string retval((const char*)xcb_get_property_value(reply));
        free(reply);
        return retval;
    }
    
    void setTitle(const std::string& title)
    {
        const char* c_title = title.c_str();
        xcb_change_property(m_xcbConn, XCB_PROP_MODE_REPLACE, m_windowId,
                            XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                            strlen(c_title), c_title);
    }
    
    void setWindowFrameDefault()
    {
        int x, y, w, h;
        xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(m_xcbConn)).data;
        genFrameDefault(screen, &x, &y, &w, &h);
        uint32_t values[] = {(uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h};
        xcb_configure_window(m_xcbConn, m_windowId,
                             XCB_CONFIG_WINDOW_X |
                             XCB_CONFIG_WINDOW_Y |
                             XCB_CONFIG_WINDOW_WIDTH |
                             XCB_CONFIG_WINDOW_HEIGHT,
                             values);
    }
    
    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        xOut = m_wx;
        yOut = m_wy;
        wOut = m_ww;
        hOut = m_wh;
    }
    
    void setWindowFrame(float x, float y, float w, float h)
    {
        uint32_t values[] = {(uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h};
        xcb_configure_window(m_xcbConn, m_windowId,
                             XCB_CONFIG_WINDOW_X |
                             XCB_CONFIG_WINDOW_Y |
                             XCB_CONFIG_WINDOW_WIDTH |
                             XCB_CONFIG_WINDOW_HEIGHT,
                             values);
    }
    
    float getVirtualPixelFactor() const
    {
        return m_pixelFactor;
    }
    
    bool isFullscreen() const
    {
        xcb_get_property_cookie_t cookie =
        xcb_get_property(m_xcbConn, 0, m_windowId, S_ATOMS->m_netwmState, XCB_ATOM_ATOM, 0, 32);
        xcb_get_property_reply_t* reply =
        xcb_get_property_reply(m_xcbConn, cookie, NULL);
        char* props = (char*)xcb_get_property_value(reply);
        char fullscreen = false;
        for (unsigned i=0 ; i<reply->length/4 ; ++i)
        {
            if ((xcb_atom_t)props[i] == S_ATOMS->m_netwmStateFullscreen)
            {
                fullscreen = true;
                break;
            }
        }
        free(reply);
        return fullscreen;
    }
    
    void setFullscreen(bool fs)
    {
        xcb_client_message_event_t fsEvent =
        {
            XCB_CLIENT_MESSAGE,
            32,
            0,
            m_windowId,
            S_ATOMS->m_netwmState,
            {}
        };
        fsEvent.data.data32[0] = fs ? S_ATOMS->m_netwmStateAdd : S_ATOMS->m_netwmStateRemove;
        fsEvent.data.data32[1] = S_ATOMS->m_netwmStateFullscreen;
        xcb_send_event(m_xcbConn, 0, m_windowId,
                       XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       (const char*)&fsEvent);

    }

    uintptr_t getPlatformHandle() const
    {
        return (uintptr_t)m_windowId;
    }

    void _incomingEvent(void* e)
    {
        xcb_generic_event_t* event = (xcb_generic_event_t*)e;
        switch (XCB_EVENT_RESPONSE_TYPE(event))
        {
        case XCB_EXPOSE:
        {
            xcb_expose_event_t* ev = (xcb_expose_event_t*)event;
            m_wx = ev->x;
            m_wy = ev->y;
            m_ww = ev->width;
            m_wh = ev->height;
        }
        case XCB_CONFIGURE_NOTIFY:
        {
            xcb_configure_notify_event_t* ev = (xcb_configure_notify_event_t*)event;
            m_wx = ev->x;
            m_wy = ev->y;
            m_ww = ev->width;
            m_wh = ev->height;
        }
        case XCB_KEY_PRESS:
        {
            xcb_key_press_event_t* ev = (xcb_key_press_event_t*)event;
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                wchar_t charCode = translateKeysym(xcb_key_press_lookup_keysym(S_ATOMS->m_keySyms, ev, 0),
                                                   specialKey, modifierKey);
                int modifierMask = translateModifiers(ev->state);
                if (charCode)
                    m_callback->charKeyDown(charCode,
                                            (IWindowCallback::EModifierKey)modifierMask, false);
                else if (specialKey)
                    m_callback->specialKeyDown((IWindowCallback::ESpecialKey)specialKey,
                                               (IWindowCallback::EModifierKey)modifierMask, false);
                else if (modifierKey)
                    m_callback->modKeyDown((IWindowCallback::EModifierKey)modifierKey, false);
            }
        }
        case XCB_KEY_RELEASE:
        {
            xcb_key_release_event_t* ev = (xcb_key_release_event_t*)event;
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                wchar_t charCode = translateKeysym(xcb_key_release_lookup_keysym(S_ATOMS->m_keySyms, ev, 0),
                                                   specialKey, modifierKey);
                int modifierMask = translateModifiers(ev->state);
                if (charCode)
                    m_callback->charKeyUp(charCode,
                                          (IWindowCallback::EModifierKey)modifierMask);
                else if (specialKey)
                    m_callback->specialKeyUp((IWindowCallback::ESpecialKey)specialKey,
                                             (IWindowCallback::EModifierKey)modifierMask);
                else if (modifierKey)
                    m_callback->modKeyUp((IWindowCallback::EModifierKey)modifierKey);
            }
        }
        case XCB_BUTTON_PRESS:
        {
            xcb_button_press_event_t* ev = (xcb_button_press_event_t*)event;
            int button = translateButton(ev->state);
            if (m_callback && button)
            {
                int modifierMask = translateModifiers(ev->state);
                IWindowCallback::SWindowCoord coord =
                {
                    {(unsigned)ev->root_x, (unsigned)ev->root_y},
                    {(unsigned)(ev->root_x / m_pixelFactor), (unsigned)(ev->root_y / m_pixelFactor)},
                    {ev->root_x / (float)m_ww, ev->root_y / (float)m_wh}
                };
                m_callback->mouseDown(coord, (IWindowCallback::EMouseButton)button,
                                      (IWindowCallback::EModifierKey)modifierMask);
            }
        }
        case XCB_BUTTON_RELEASE:
        {
            xcb_button_release_event_t* ev = (xcb_button_release_event_t*)event;
            int button = translateButton(ev->state);
            if (m_callback && button)
            {
                int modifierMask = translateModifiers(ev->state);
                IWindowCallback::SWindowCoord coord =
                {
                    {(unsigned)ev->root_x, (unsigned)ev->root_y},
                    {(unsigned)(ev->root_x / m_pixelFactor), (unsigned)(ev->root_y / m_pixelFactor)},
                    {ev->root_x / (float)m_ww, ev->root_y / (float)m_wh}
                };
                m_callback->mouseUp(coord, (IWindowCallback::EMouseButton)button,
                                    (IWindowCallback::EModifierKey)modifierMask);
            }
        }
        case XCB_MOTION_NOTIFY:
        {
            xcb_motion_notify_event_t* ev = (xcb_motion_notify_event_t*)event;
            if (m_callback)
            {
                IWindowCallback::SWindowCoord coord =
                {
                    {(unsigned)ev->root_x, (unsigned)ev->root_y},
                    {(unsigned)(ev->root_x / m_pixelFactor), (unsigned)(ev->root_y / m_pixelFactor)},
                    {ev->root_x / (float)m_ww, ev->root_y / (float)m_wh}
                };
                m_callback->mouseMove(coord);
            }
        }
        }
    }
    
    ETouchType getTouchType() const
    {
        return TOUCH_NONE;
    }
    
};

IWindow* _CWindowXCBNew(const std::string& title, xcb_connection_t* conn)
{
    return new CWindowXCB(title, conn);
}
    
}

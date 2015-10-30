#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/IApplication.hpp"
#include "boo/graphicsdev/GLES3.hpp"

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <xcb/xinput.h>
#include <xcb/glx.h>
#include <GL/glx.h>
#include <GL/glcorearb.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define XK_MISCELLANY
#define XK_XKB_KEYS
#define XK_LATIN1
#include <X11/keysymdef.h>

#define REF_DPMM 3.7824 /* 96 DPI */
#define FS_ATOM "_NET_WM_STATE_FULLSCREEN"

namespace boo
{
IGraphicsCommandQueue* _NewGLES3CommandQueue(IGraphicsContext* parent);

extern PFNGLXGETVIDEOSYNCSGIPROC FglXGetVideoSyncSGI;
extern PFNGLXWAITVIDEOSYNCSGIPROC FglXWaitVideoSyncSGI;

extern int XCB_GLX_EVENT_BASE;
extern int XINPUT_OPCODE;

static inline double fp3232val(xcb_input_fp3232_t* val)
{
    return val->integral + val->frac / (double)UINT_MAX;
}

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

static int translateButton(unsigned detail)
{
    int retval = 0;
    if (detail == 1)
        retval = IWindowCallback::BUTTON_PRIMARY;
    else if (detail == 3)
        retval = IWindowCallback::BUTTON_SECONDARY;
    else if (detail == 2)
        retval = IWindowCallback::BUTTON_MIDDLE;
    else if (detail == 8)
        retval = IWindowCallback::BUTTON_AUX1;
    else if (detail == 9)
        retval =
IWindowCallback::BUTTON_AUX2;
    return retval;
}

#define INTERN_ATOM(var, conn, name, if_exists) \
do {\
    xcb_intern_atom_cookie_t cookie = \
    xcb_intern_atom(conn, if_exists, sizeof(#name), #name); \
    xcb_intern_atom_reply_t* reply = \
    xcb_intern_atom_reply(conn, cookie, NULL); \
    var = reply->atom; \
    free(reply); \
} while(0)

struct XCBAtoms
{
    xcb_atom_t m_wmProtocols = 0;
    xcb_atom_t m_wmDeleteWindow = 0;
    xcb_atom_t m_netwmState = 0;
    xcb_atom_t m_netwmStateFullscreen = 0;
    xcb_atom_t m_netwmStateAdd = 0;
    xcb_atom_t m_netwmStateRemove = 0;
    xcb_key_symbols_t* m_keySyms = NULL;
    XCBAtoms(xcb_connection_t* conn)
    {
        INTERN_ATOM(m_wmProtocols, conn, WM_PROTOCOLS, 1);
        INTERN_ATOM(m_wmDeleteWindow, conn, WM_DELETE_WINDOW, 1);
        INTERN_ATOM(m_netwmState, conn, _NET_WM_STATE, 0);
        INTERN_ATOM(m_netwmStateFullscreen, conn, _NET_WM_STATE_FULLSCREEN, 0);
        INTERN_ATOM(m_netwmStateAdd, conn, _NET_WM_STATE_ADD, 0);
        INTERN_ATOM(m_netwmStateRemove, conn, _NET_WM_STATE_REMOVE, 0);
        m_keySyms = xcb_key_symbols_alloc(conn);
    }
};
static XCBAtoms* S_ATOMS = NULL;

static void genFrameDefault(xcb_screen_t* screen, int* xOut, int* yOut, int* wOut, int* hOut)
{
    float width = screen->width_in_pixels * 2.0 / 3.0;
    float height = screen->height_in_pixels * 2.0 / 3.0;
    *xOut = (screen->width_in_pixels - width) / 2.0;
    *yOut = (screen->height_in_pixels - height) / 2.0;
    *wOut = width;
    *hOut = height;
}
    
struct GraphicsContextXCB : IGraphicsContext
{
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    xcb_connection_t* m_xcbConn;

    xcb_glx_fbconfig_t m_fbconfig = 0;
    xcb_visualid_t m_visualid = 0;
    xcb_glx_window_t m_glxWindow = 0;
    xcb_glx_context_t m_glxCtx = 0;
    xcb_glx_context_tag_t m_glxCtxTag = 0;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;
    xcb_glx_context_t m_loadCtx = 0;

public:
    IWindowCallback* m_callback;

    GraphicsContextXCB(EGraphicsAPI api, IWindow* parentWindow,
                       xcb_connection_t* conn, uint32_t& visualIdOut)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(parentWindow),
      m_xcbConn(conn)
    {
        /* WTF freedesktop?? Fix this awful API and your nonexistant docs */
        xcb_glx_get_fb_configs_reply_t* fbconfigs =
        xcb_glx_get_fb_configs_reply(m_xcbConn, xcb_glx_get_fb_configs(m_xcbConn, 0), NULL);
        struct conf_prop
        {
            uint32_t key;
            uint32_t val;
        }* props = (struct conf_prop*)xcb_glx_get_fb_configs_property_list(fbconfigs);

        for (uint32_t i=0 ; i<fbconfigs->num_FB_configs ; ++i)
        {
            struct conf_prop* configProps = &props[fbconfigs->num_properties * i];
            uint32_t fbconfId, visualId, depthSize, colorSize, doubleBuffer;
            for (uint32_t j=0 ; j<fbconfigs->num_properties ; ++j)
            {
                struct conf_prop* prop = &configProps[j];
                if (prop->key == GLX_FBCONFIG_ID)
                    fbconfId = prop->val;
                if (prop->key == GLX_VISUAL_ID)
                    visualId = prop->val;
                else if (prop->key == GLX_DEPTH_SIZE)
                    depthSize = prop->val;
                else if (prop->key == GLX_BUFFER_SIZE)
                    colorSize = prop->val;
                else if (prop->key == GLX_DOUBLEBUFFER)
                    doubleBuffer = prop->val;
            }

            /* Double-buffer only */
            if (!doubleBuffer)
                continue;

            if (m_pf == PF_RGBA8 && colorSize >= 32)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBA8_Z24 && colorSize >= 32 && depthSize >= 24)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBAF32 && colorSize >= 128)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBAF32_Z24 && colorSize >= 128 && depthSize >= 24)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
        }
        free(fbconfigs);

        if (!m_fbconfig)
        {
            fprintf(stderr, "unable to find suitable pixel format");
            return;
        }

        visualIdOut = m_visualid;
    }

    ~GraphicsContextXCB()
    {
        if (m_glxCtx)
            xcb_glx_destroy_context(m_xcbConn, m_glxCtx);
        if (m_glxWindow)
            xcb_glx_delete_window(m_xcbConn, m_glxWindow);
        if (m_loadCtx)
            xcb_glx_destroy_context(m_xcbConn, m_loadCtx);
    }

    void _setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }

    EGraphicsAPI getAPI() const
    {
        return m_api;
    }

    EPixelFormat getPixelFormat() const
    {
        return m_pf;
    }

    void setPixelFormat(EPixelFormat pf)
    {
        if (pf > PF_RGBAF32_Z24)
            return;
        m_pf = pf;
    }

    void initializeContext()
    {
        m_glxWindow = xcb_generate_id(m_xcbConn);
        xcb_glx_create_window(m_xcbConn, 0, m_fbconfig,
                              m_parentWindow->getPlatformHandle(),
                              m_glxWindow, 0, NULL);
        m_glxCtx = xcb_generate_id(m_xcbConn);
        xcb_glx_create_context(m_xcbConn, m_glxCtx, m_visualid, 0, 0, 1);
    }

    void makeCurrent()
    {
        xcb_generic_error_t* err = nullptr;
        xcb_glx_make_context_current_reply_t* reply =
        xcb_glx_make_context_current_reply(m_xcbConn,
        xcb_glx_make_context_current(m_xcbConn, 0, m_glxWindow, m_glxWindow, m_glxCtx), &err);
        free(reply);
    }

    IGraphicsCommandQueue* getCommandQueue()
    {
        if (!m_commandQueue)
            m_commandQueue = _NewGLES3CommandQueue(this);
        return m_commandQueue;
    }

    IGraphicsDataFactory* getDataFactory()
    {
        if (!m_dataFactory)
            m_dataFactory = new struct GLES3DataFactory(this);
        return m_dataFactory;
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        if (!m_loadCtx)
        {
            m_loadCtx = xcb_generate_id(m_xcbConn);
            xcb_glx_create_context(m_xcbConn, m_loadCtx, m_visualid, 0, m_glxCtx, 1);
            xcb_generic_error_t* err = nullptr;
            xcb_glx_make_context_current_reply_t* reply =
            xcb_glx_make_context_current_reply(m_xcbConn,
            xcb_glx_make_context_current(m_xcbConn, 0, m_glxWindow, m_glxWindow, m_loadCtx), &err);
            free(reply);
        }
        return getDataFactory();
    }

};

struct WindowXCB : IWindow
{
    xcb_connection_t* m_xcbConn;
    IWindowCallback* m_callback;
    xcb_colormap_t m_colormapId;
    xcb_window_t m_windowId;
    GraphicsContextXCB m_gfxCtx;
    uint32_t m_visualId;

    /* Last known input device id (0xffff if not yet set) */
    xcb_input_device_id_t m_lastInputID = 0xffff;
    ETouchType m_touchType = TOUCH_NONE;

    /* Scroll valuators */
    int m_hScrollValuator = -1;
    int m_vScrollValuator = -1;
    double m_hScrollLast = 0.0;
    double m_vScrollLast = 0.0;

    /* Cached window rectangle (to avoid repeated X queries) */
    int m_wx, m_wy, m_ww, m_wh;
    float m_pixelFactor;
    
public:
    
    WindowXCB(const std::string& title, xcb_connection_t* conn)
    : m_xcbConn(conn), m_callback(NULL), m_gfxCtx(IGraphicsContext::API_OPENGL_3_3, this, m_xcbConn, m_visualId)
    {
        if (!S_ATOMS)
            S_ATOMS = new XCBAtoms(conn);

        /* Default screen */
        xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(m_xcbConn)).data;
        m_pixelFactor = screen->width_in_pixels / (float)screen->width_in_millimeters / REF_DPMM;

        /* Create colormap */
        m_colormapId = xcb_generate_id(m_xcbConn);
        xcb_create_colormap(m_xcbConn, XCB_COLORMAP_ALLOC_NONE,
                            m_colormapId, screen->root, m_visualId);

        /* Create window */
        int x, y, w, h;
        genFrameDefault(screen, &x, &y, &w, &h);
        uint32_t valueMasks[] =
        {
            XCB_NONE,
            XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_STRUCTURE_NOTIFY,
            m_colormapId,
            XCB_NONE
        };
        m_windowId = xcb_generate_id(conn);
        xcb_create_window(m_xcbConn, XCB_COPY_FROM_PARENT, m_windowId, screen->root,
                          x, y, w, h, 10,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, m_visualId,
                          XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                          valueMasks);
        

        /* The XInput 2.1 extension enables per-pixel smooth scrolling trackpads */
        xcb_generic_error_t* xiErr = NULL;
        xcb_input_xi_query_version_reply_t* xiReply =
        xcb_input_xi_query_version_reply(m_xcbConn,
        xcb_input_xi_query_version(m_xcbConn, 2, 1), &xiErr);
        if (!xiErr)
        {
            struct
            {
                xcb_input_event_mask_t mask;
                uint32_t maskVal;
            } masks =
            {
                {XCB_INPUT_DEVICE_ALL_MASTER, 1},
                XCB_INPUT_XI_EVENT_MASK_MOTION |
                XCB_INPUT_XI_EVENT_MASK_TOUCH_BEGIN |
                XCB_INPUT_XI_EVENT_MASK_TOUCH_UPDATE |
                XCB_INPUT_XI_EVENT_MASK_TOUCH_END
            };
            xcb_input_xi_select_events(m_xcbConn, m_windowId, 1, &masks.mask);
        }
        free(xiReply);        

        /* Register netwm extension atom for window closing */
#if 0
        xcb_change_property(m_xcbConn, XCB_PROP_MODE_REPLACE, m_windowId, S_ATOMS->m_wmProtocols,
                            XCB_ATOM_ATOM, 32, 1, &S_ATOMS->m_wmDeleteWindow);
        const xcb_atom_t wm_protocols[1] = {
            S_ATOMS->m_wmDeleteWindow,
        };
        xcb_change_property(m_xcbConn, XCB_PROP_MODE_REPLACE, m_windowId,
                            S_ATOMS->m_wmProtocols, 4,
                            32, 1, wm_protocols);
#endif

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
        //xcb_map_window(m_xcbConn, m_windowId);
        xcb_flush(m_xcbConn);

        m_gfxCtx.initializeContext();
    }
    
    ~WindowXCB()
    {
        xcb_unmap_window(m_xcbConn, m_windowId);
        xcb_destroy_window(m_xcbConn, m_windowId);
        xcb_free_colormap(m_xcbConn, m_colormapId);
        APP->_deletedWindow(this);
    }
    
    void setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }
    
    void showWindow()
    {
        xcb_map_window(m_xcbConn, m_windowId);
        m_gfxCtx.makeCurrent();
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

    size_t waitForRetrace(size_t count)
    {
        unsigned int sync;
        FglXWaitVideoSyncSGI(1, 0, &sync);
        return 0;
    }

    uintptr_t getPlatformHandle() const
    {
        return (uintptr_t)m_windowId;
    }

    void _pointingDeviceChanged(xcb_input_device_id_t deviceId)
    {
        xcb_input_xi_query_device_reply_t* reply =
        xcb_input_xi_query_device_reply(m_xcbConn, xcb_input_xi_query_device(m_xcbConn, deviceId), NULL);

        xcb_input_xi_device_info_iterator_t infoIter = xcb_input_xi_query_device_infos_iterator(reply);
        while (infoIter.rem)
        {
            /* First iterate classes for scrollables */
            xcb_input_device_class_iterator_t classIter =
            xcb_input_xi_device_info_classes_iterator(infoIter.data);
            int hScroll = -1;
            int vScroll = -1;
            m_hScrollLast = 0.0;
            m_vScrollLast = 0.0;
            m_hScrollValuator = -1;
            m_vScrollValuator = -1;
            while (classIter.rem)
            {
                if (classIter.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_SCROLL)
                {
                    xcb_input_scroll_class_t* scrollClass = (xcb_input_scroll_class_t*)classIter.data;
                    if (scrollClass->scroll_type == XCB_INPUT_SCROLL_TYPE_VERTICAL)
                        vScroll = scrollClass->number;
                    else if (scrollClass->scroll_type == XCB_INPUT_SCROLL_TYPE_HORIZONTAL)
                        hScroll = scrollClass->number;
                }
                xcb_input_device_class_next(&classIter);
            }

            /* Next iterate for touch and scroll valuators */
            classIter = xcb_input_xi_device_info_classes_iterator(infoIter.data);
            while (classIter.rem)
            {
                if (classIter.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR)
                {
                    xcb_input_valuator_class_t* valClass = (xcb_input_valuator_class_t*)classIter.data;
                    if (valClass->number == vScroll)
                    {
                        m_vScrollLast = fp3232val(&valClass->value);
                        m_vScrollValuator = vScroll;
                    }
                    else if (valClass->number == hScroll)
                    {
                        m_hScrollLast = fp3232val(&valClass->value);
                        m_hScrollValuator = hScroll;
                    }
                }
                else if (classIter.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_TOUCH)
                {
                    xcb_input_touch_class_t* touchClass = (xcb_input_touch_class_t*)classIter.data;
                    if (touchClass->mode == XCB_INPUT_TOUCH_MODE_DIRECT)
                        m_touchType = TOUCH_DISPLAY;
                    else if (touchClass->mode == XCB_INPUT_TOUCH_MODE_DEPENDENT)
                        m_touchType = TOUCH_TRACKPAD;
                    else
                        m_touchType = TOUCH_NONE;
                }
                xcb_input_device_class_next(&classIter);
            }
            xcb_input_xi_device_info_next(&infoIter);
        }

        free(reply);
        m_lastInputID = deviceId;
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
            return;
        }
        case XCB_CONFIGURE_NOTIFY:
        {
            xcb_configure_notify_event_t* ev = (xcb_configure_notify_event_t*)event;
            if (ev->width && ev->height)
            {
                m_wx = ev->x;
                m_wy = ev->y;
                m_ww = ev->width;
                m_wh = ev->height;
            }
            return;
        }
        case XCB_KEY_PRESS:
        {
            xcb_key_press_event_t* ev = (xcb_key_press_event_t*)event;
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                uint32_t charCode = translateKeysym(xcb_key_press_lookup_keysym(S_ATOMS->m_keySyms, ev, 0),
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
            return;
        }
        case XCB_KEY_RELEASE:
        {
            xcb_key_release_event_t* ev = (xcb_key_release_event_t*)event;
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                uint32_t charCode = translateKeysym(xcb_key_release_lookup_keysym(S_ATOMS->m_keySyms, ev, 0),
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
            return;
        }
        case XCB_BUTTON_PRESS:
        {
            xcb_button_press_event_t* ev = (xcb_button_press_event_t*)event;
            if (m_callback)
            {
                int button = translateButton(ev->detail);
                if (button)
                {
                    int modifierMask = translateModifiers(ev->state);
                    IWindowCallback::SWindowCoord coord =
                    {
                        {(unsigned)ev->event_x, (unsigned)ev->event_y},
                        {(unsigned)(ev->event_x / m_pixelFactor), (unsigned)(ev->event_y / m_pixelFactor)},
                        {ev->event_x / (float)m_ww, ev->event_y / (float)m_wh}
                    };
                    m_callback->mouseDown(coord, (IWindowCallback::EMouseButton)button,
                                          (IWindowCallback::EModifierKey)modifierMask);
                }

                /* Also handle legacy scroll events here */
                if (ev->detail >= 4 && ev->detail <= 7 &&
                    m_hScrollValuator == -1 && m_vScrollValuator == -1)
                {
                    IWindowCallback::SWindowCoord coord =
                    {
                        {(unsigned)ev->event_x, (unsigned)ev->event_y},
                        {(unsigned)(ev->event_x / m_pixelFactor), (unsigned)(ev->event_y / m_pixelFactor)},
                        {ev->event_x / (float)m_ww, ev->event_y / (float)m_wh}
                    };
                    IWindowCallback::SScrollDelta scrollDelta =
                    {
                        {0.0, 0.0},
                        false
                    };
                    if (ev->detail == 4)
                        scrollDelta.delta[1] = 1.0;
                    else if (ev->detail == 5)
                        scrollDelta.delta[1] = -1.0;
                    else if (ev->detail == 6)
                        scrollDelta.delta[0] = 1.0;
                    else if (ev->detail == 7)
                        scrollDelta.delta[0] = -1.0;
                    m_callback->scroll(coord, scrollDelta);
                }
            }
            return;
        }
        case XCB_BUTTON_RELEASE:
        {
            xcb_button_release_event_t* ev = (xcb_button_release_event_t*)event;
            if (m_callback)
            {
                int button = translateButton(ev->detail);
                if (button)
                {
                    int modifierMask = translateModifiers(ev->state);
                    IWindowCallback::SWindowCoord coord =
                    {
                        {(unsigned)ev->event_x, (unsigned)ev->event_y},
                        {(unsigned)(ev->event_x / m_pixelFactor), (unsigned)(ev->event_y / m_pixelFactor)},
                        {ev->event_x / (float)m_ww, ev->event_y / (float)m_wh}
                    };
                    m_callback->mouseUp(coord, (IWindowCallback::EMouseButton)button,
                                        (IWindowCallback::EModifierKey)modifierMask);
                }
            }
            return;
        }
        case XCB_MOTION_NOTIFY:
        {
            xcb_motion_notify_event_t* ev = (xcb_motion_notify_event_t*)event;
            if (m_callback)
            {
                IWindowCallback::SWindowCoord coord =
                {
                    {(unsigned)ev->event_x, (unsigned)ev->event_y},
                    {(unsigned)(ev->event_x / m_pixelFactor), (unsigned)(ev->event_y / m_pixelFactor)},
                    {ev->event_x / (float)m_ww, ev->event_y / (float)m_wh}
                };
                m_callback->mouseMove(coord);
            }
            return;
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
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    uint32_t* valuators = (uint32_t*)(((char*)ev) + sizeof(xcb_input_motion_event_t) + sizeof(uint32_t) * ev->buttons_len);
                    xcb_input_fp3232_t* valuatorVals = (xcb_input_fp3232_t*)(((char*)valuators) + sizeof(uint32_t) * ev->valuators_len);
                    int cv = 0;
                    double newScroll[2] = {m_hScrollLast, m_vScrollLast};
                    bool didScroll = false;
                    for (int i=0 ; i<32 ; ++i)
                    {
                        if (valuators[0] & (1<<i))
                        {
                            if (i == m_hScrollValuator)
                            {
                                newScroll[0] = fp3232val(&valuatorVals[cv]);
                                didScroll = true;
                            }
                            else if (i == m_vScrollValuator)
                            {
                                newScroll[1] = fp3232val(&valuatorVals[cv]);
                                didScroll = true;
                            }
                            ++cv;
                        }
                    }

                    IWindowCallback::SScrollDelta scrollDelta =
                    {
                        {newScroll[0] - m_hScrollLast, newScroll[1] - m_vScrollLast},
                        true
                    };

                    m_hScrollLast = newScroll[0];
                    m_vScrollLast = newScroll[1];

                    if (m_callback && didScroll)
                    {
                        unsigned event_x = ev->event_x >> 16;
                        unsigned event_y = ev->event_y >> 16;
                        IWindowCallback::SWindowCoord coord =
                        {
                            {event_x, event_y},
                            {(unsigned)(event_x / m_pixelFactor), (unsigned)(event_y / m_pixelFactor)},
                            {event_x / (float)m_ww, event_y / (float)m_wh}
                        };
                        m_callback->scroll(coord, scrollDelta);
                    }
                    return;
                }
                case XCB_INPUT_TOUCH_BEGIN:
                {
                    xcb_input_touch_begin_event_t* ev = (xcb_input_touch_begin_event_t*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    uint32_t* valuators = (uint32_t*)(((char*)ev) + sizeof(xcb_input_motion_event_t) + sizeof(uint32_t) * ev->buttons_len);
                    xcb_input_fp3232_t* valuatorVals = (xcb_input_fp3232_t*)(((char*)valuators) + sizeof(uint32_t) * ev->valuators_len);
                    int cv = 0;
                    double vals[32] = {};
                    for (int i=0 ; i<32 ; ++i)
                    {
                        if (valuators[0] & (1<<i))
                        {
                            vals[i] = fp3232val(&valuatorVals[cv]);
                            ++cv;
                        }
                    }

                    IWindowCallback::STouchCoord coord =
                    {
                        {vals[0], vals[1]}
                    };

                    if (m_callback)
                        m_callback->touchDown(coord, ev->detail);
                    return;
                }
                case XCB_INPUT_TOUCH_UPDATE:
                {
                    xcb_input_touch_update_event_t* ev = (xcb_input_touch_update_event_t*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    uint32_t* valuators = (uint32_t*)(((char*)ev) + sizeof(xcb_input_motion_event_t) + sizeof(uint32_t) * ev->buttons_len);
                    xcb_input_fp3232_t* valuatorVals = (xcb_input_fp3232_t*)(((char*)valuators) + sizeof(uint32_t) * ev->valuators_len);
                    int cv = 0;
                    double vals[32] = {};
                    for (int i=0 ; i<32 ; ++i)
                    {
                        if (valuators[0] & (1<<i))
                        {
                            vals[i] = fp3232val(&valuatorVals[cv]);
                            ++cv;
                        }
                    }

                    IWindowCallback::STouchCoord coord =
                    {
                        {vals[0], vals[1]}
                    };

                    if (m_callback)
                        m_callback->touchMove(coord, ev->detail);
                    return;
                }
                case XCB_INPUT_TOUCH_END:
                {
                    xcb_input_touch_end_event_t* ev = (xcb_input_touch_end_event_t*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    uint32_t* valuators = (uint32_t*)(((char*)ev) + sizeof(xcb_input_motion_event_t) + sizeof(uint32_t) * ev->buttons_len);
                    xcb_input_fp3232_t* valuatorVals = (xcb_input_fp3232_t*)(((char*)valuators) + sizeof(uint32_t) * ev->valuators_len);
                    int cv = 0;
                    double vals[32] = {};
                    for (int i=0 ; i<32 ; ++i)
                    {
                        if (valuators[0] & (1<<i))
                        {
                            vals[i] = fp3232val(&valuatorVals[cv]);
                            ++cv;
                        }
                    }

                    IWindowCallback::STouchCoord coord =
                    {
                        {vals[0], vals[1]}
                    };

                    if (m_callback)
                        m_callback->touchUp(coord, ev->detail);
                    return;
                }
                }
            }
        }
        }
    }
    
    ETouchType getTouchType() const
    {
        return m_touchType;
    }
    
};

IWindow* _WindowXCBNew(const std::string& title, xcb_connection_t* conn)
{
    return new WindowXCB(title, conn);
}
    
}

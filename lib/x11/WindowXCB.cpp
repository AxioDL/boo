#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/IApplication.hpp"
#include "boo/graphicsdev/GL.hpp"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <GL/glx.h>

#define XK_MISCELLANY
#define XK_XKB_KEYS
#define XK_LATIN1
#include <X11/keysymdef.h>
#include <xkbcommon/xkbcommon.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xatom.h>

#define REF_DPMM 3.7824 /* 96 DPI */
#define FS_ATOM "_NET_WM_STATE_FULLSCREEN"

#include <LogVisor/LogVisor.hpp>

namespace boo
{
static LogVisor::LogModule Log("boo::WindowXCB");
IGraphicsCommandQueue* _NewGLES3CommandQueue(IGraphicsContext* parent);
void _XCBUpdateLastGlxCtx(GLXContext lastGlxCtx);
void GLXExtensionCheck();
void GLXWaitForVSync();
void GLXEnableVSync(Display* disp, GLXWindow drawable);

extern int XINPUT_OPCODE;

static uint32_t translateKeysym(KeySym sym, int& specialSym, int& modifierSym)
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
    if (state & ShiftMask)
        retval |= IWindowCallback::MKEY_SHIFT;
    if (state & ControlMask)
        retval |= IWindowCallback::MKEY_CTRL;
    if (state & Mod1Mask)
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

struct XCBAtoms
{
    Atom m_wmProtocols = 0;
    Atom m_wmDeleteWindow = 0;
    Atom m_netwmState = 0;
    Atom m_netwmStateFullscreen = 0;
    Atom m_netwmStateAdd = 0;
    Atom m_netwmStateRemove = 0;
    //xcb_key_symbols_t* m_keySyms = NULL;
    XCBAtoms(Display* disp)
    {
        m_wmProtocols = XInternAtom(disp, "WM_PROTOCOLS", True);
        m_wmDeleteWindow = XInternAtom(disp, "WM_DELETE_WINDOW", True);
        m_netwmState = XInternAtom(disp, "_NET_WM_STATE", False);
        m_netwmStateFullscreen = XInternAtom(disp, "_NET_WM_STATE_FULLSCREEN", False);
        m_netwmStateAdd = XInternAtom(disp, "_NET_WM_STATE_ADD", False);
        m_netwmStateRemove = XInternAtom(disp, "_NET_WM_STATE_REMOVE", False);
        //m_keySyms = xcb_key_symbols_alloc(conn);
    }
};
static XCBAtoms* S_ATOMS = NULL;

static void genFrameDefault(Screen* screen, int* xOut, int* yOut, int* wOut, int* hOut)
{
    float width = screen->width * 2.0 / 3.0;
    float height = screen->height * 2.0 / 3.0;
    *xOut = (screen->width - width) / 2.0;
    *yOut = (screen->height - height) / 2.0;
    *wOut = width;
    *hOut = height;
}
    
struct GraphicsContextXCB : IGraphicsContext
{
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    Display* m_xDisp = nullptr;
    GLXContext m_lastCtx = 0;

    GLXFBConfig m_fbconfig = 0;
    int m_visualid = 0;
    GLXWindow m_glxWindow = 0;
    GLXContext m_glxCtx = 0;
    GLXContext m_timerCtx = 0;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;
    GLXContext m_loadCtx = 0;

public:
    IWindowCallback* m_callback;

    GraphicsContextXCB(EGraphicsAPI api, IWindow* parentWindow,
                       Display* display, int defaultScreen,
                       GLXContext lastCtx, uint32_t& visualIdOut)
    : m_api(api),
      m_pf(PF_RGBA8_Z24),
      m_parentWindow(parentWindow),
      m_xDisp(display),
      m_lastCtx(lastCtx)
    {
        /* Query framebuffer configurations */
        GLXFBConfig* fbConfigs = nullptr;
        int numFBConfigs = 0;
        fbConfigs = glXGetFBConfigs(display, defaultScreen, &numFBConfigs);
        if (!fbConfigs || numFBConfigs == 0)
        {
            Log.report(LogVisor::FatalError, "glXGetFBConfigs failed");
            return;
        }

        for (int i=0 ; i<numFBConfigs ; ++i)
        {
            GLXFBConfig config = fbConfigs[i];
            int visualId, depthSize, colorSize, doubleBuffer;
            glXGetFBConfigAttrib(display, config, GLX_VISUAL_ID, &visualId);
            glXGetFBConfigAttrib(display, config, GLX_DEPTH_SIZE, &depthSize);
            glXGetFBConfigAttrib(display, config, GLX_BUFFER_SIZE, &colorSize);
            glXGetFBConfigAttrib(display, config, GLX_DOUBLEBUFFER, &doubleBuffer);

            /* Double-buffer only */
            if (!doubleBuffer)
                continue;

            if (m_pf == PF_RGBA8 && colorSize >= 32)
            {
                m_fbconfig = config;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBA8_Z24 && colorSize >= 32 && depthSize >= 24)
            {
                m_fbconfig = config;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBAF32 && colorSize >= 128)
            {
                m_fbconfig = config;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBAF32_Z24 && colorSize >= 128 && depthSize >= 24)
            {
                m_fbconfig = config;
                m_visualid = visualId;
                break;
            }
        }
        XFree(fbConfigs);

        if (!m_fbconfig)
        {
            Log.report(LogVisor::FatalError, "unable to find suitable pixel format");
            return;
        }

        visualIdOut = m_visualid;
    }

    ~GraphicsContextXCB()
    {
        if (m_glxCtx)
            glXDestroyContext(m_xDisp, m_glxCtx);
        if (m_glxWindow)
            glXDestroyWindow(m_xDisp, m_glxWindow);
        if (m_loadCtx)
            glXDestroyContext(m_xDisp, m_loadCtx);
        if (m_timerCtx)
            glXDestroyContext(m_xDisp, m_timerCtx);
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
        m_glxCtx = glXCreateNewContext(m_xDisp, m_fbconfig, GLX_RGBA_TYPE, m_lastCtx, True);
        if (!m_glxCtx)
            Log.report(LogVisor::FatalError, "unable to make new GLX context");
        m_glxWindow = glXCreateWindow(m_xDisp, m_fbconfig, m_parentWindow->getPlatformHandle(), nullptr);
        if (!m_glxWindow)
            Log.report(LogVisor::FatalError, "unable to make new GLX window");
        _XCBUpdateLastGlxCtx(m_glxCtx);

        /* Make additional shared context for vsync timing */
        m_timerCtx = glXCreateNewContext(m_xDisp, m_fbconfig, GLX_RGBA_TYPE, m_glxCtx, True);
        if (!m_timerCtx)
            Log.report(LogVisor::FatalError, "unable to make new timer GLX context");
    }

    void makeCurrent()
    {
        if (!glXMakeContextCurrent(m_xDisp, m_glxWindow, m_glxWindow, m_glxCtx))
            Log.report(LogVisor::FatalError, "unable to make GLX context current");
    }

    void postInit()
    {
        GLXExtensionCheck();
        GLXEnableVSync(m_xDisp, m_glxWindow);
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
            m_loadCtx = glXCreateNewContext(m_xDisp, m_fbconfig, GLX_RGBA_TYPE, m_glxCtx, True);
            if (!m_loadCtx)
                Log.report(LogVisor::FatalError, "unable to make load GLX context");
            if (!glXMakeContextCurrent(m_xDisp, m_glxWindow, m_glxWindow, m_loadCtx))
                Log.report(LogVisor::FatalError, "unable to make load GLX context current");
        }
        return getDataFactory();
    }

    void present()
    {
        glXSwapBuffers(m_xDisp, m_glxWindow);
    }

    bool m_timerBound = false;
    void bindTimerContext()
    {
        if (m_timerBound)
            return;
        if (!glXMakeContextCurrent(m_xDisp, m_glxWindow, m_glxWindow, m_timerCtx))
            Log.report(LogVisor::FatalError, "unable to make timer GLX context current");
        m_timerBound = true;
    }

};

struct WindowXCB : IWindow
{
    Display* m_xDisp;
    IWindowCallback* m_callback;
    Colormap m_colormapId;
    Window m_windowId;
    GraphicsContextXCB m_gfxCtx;
    uint32_t m_visualId;

    /* Last known input device id (0xffff if not yet set) */
    int m_lastInputID = 0xffff;
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
    
    WindowXCB(const std::string& title,
              Display* display, int defaultScreen,
              GLXContext lastCtx)
    : m_xDisp(display), m_callback(nullptr),
      m_gfxCtx(IGraphicsContext::API_OPENGL_3_3,
               this, display, defaultScreen,
               lastCtx, m_visualId)
    {
        if (!S_ATOMS)
            S_ATOMS = new XCBAtoms(display);

        /* Default screen */
        Screen* screen = ScreenOfDisplay(display, defaultScreen);
        m_pixelFactor = screen->width / (float)screen->mwidth / REF_DPMM;

        XVisualInfo visTemplate;
        visTemplate.screen = defaultScreen;
        int numVisuals;
        XVisualInfo* visualList = XGetVisualInfo(display, VisualScreenMask, &visTemplate, &numVisuals);
        Visual* selectedVisual = nullptr;
        for (int i=0 ; i<numVisuals ; ++i)
        {
            if (visualList[i].visualid == m_visualId)
            {
                selectedVisual = visualList[i].visual;
                break;
            }
        }
        XFree(visualList);

        /* Create colormap */
        m_colormapId = XCreateColormap(m_xDisp, screen->root, selectedVisual, AllocNone);

        /* Create window */
        int x, y, w, h;
        genFrameDefault(screen, &x, &y, &w, &h);
        XSetWindowAttributes swa;
        swa.colormap = m_colormapId;
        swa.border_pixmap = None;
        swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask | StructureNotifyMask;

        m_windowId = XCreateWindow(display, screen->root, x, y, w, h, 10,
                                   CopyFromParent, CopyFromParent, selectedVisual,
                                   CWBorderPixel | CWEventMask | CWColormap, &swa);


        /* The XInput 2.1 extension enables per-pixel smooth scrolling trackpads */
        XIEventMask mask = {XIAllMasterDevices, XIMaskLen(XI_LASTEVENT)};
        mask.mask = (unsigned char*)malloc(mask.mask_len);
        memset(mask.mask, 0, mask.mask_len);
        XISetMask(mask.mask, XI_Motion);
        XISetMask(mask.mask, XI_TouchBegin);
        XISetMask(mask.mask, XI_TouchUpdate);
        XISetMask(mask.mask, XI_TouchEnd);
        XISelectEvents(m_xDisp, m_windowId, &mask, 1);
        free(mask.mask);


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
        const unsigned char* c_title = (unsigned char*)title.c_str();
        XChangeProperty(m_xDisp, m_windowId, XA_WM_NAME, XA_STRING, 8, PropModeReplace, c_title, title.length());

        /* Set the title of the window icon */
        XChangeProperty(m_xDisp, m_windowId, XA_WM_ICON_NAME, XA_STRING, 8, PropModeReplace, c_title, title.length());

        /* Initialize context */
        XMapWindow(m_xDisp, m_windowId);
        XFlush(m_xDisp);

        m_gfxCtx.initializeContext();
    }
    
    ~WindowXCB()
    {
        XUnmapWindow(m_xDisp, m_windowId);
        XDestroyWindow(m_xDisp, m_windowId);
        XFreeColormap(m_xDisp, m_colormapId);
        APP->_deletedWindow(this);
    }
    
    void setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }
    
    void showWindow()
    {
        XMapWindow(m_xDisp, m_windowId);
        XFlush(m_xDisp);
    }
    
    void hideWindow()
    {
        XUnmapWindow(m_xDisp, m_windowId);
        XFlush(m_xDisp);
    }
    
    std::string getTitle()
    {
        unsigned char* string = nullptr;
        if (XGetWindowProperty(m_xDisp, m_windowId, XA_WM_NAME, 0, 65536, False,
                               XA_STRING, nullptr, nullptr, nullptr, nullptr, &string))
        {
            std::string retval((const char*)string);
            XFree(string);
            return retval;
        }
        return std::string();
    }
    
    void setTitle(const std::string& title)
    {
        const unsigned char* c_title = (unsigned char*)title.c_str();
        XChangeProperty(m_xDisp, m_windowId, XA_WM_NAME, XA_STRING, 8,
                        PropModeReplace, c_title, title.length());
    }
    
    void setWindowFrameDefault()
    {
        int x, y, w, h;
        Screen* screen = DefaultScreenOfDisplay(m_xDisp);
        genFrameDefault(screen, &x, &y, &w, &h);
        XWindowChanges values = {(int)x, (int)y, (int)w, (int)h};
        XConfigureWindow(m_xDisp, m_windowId, CWX|CWY|CWWidth|CWHeight, &values);
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
        XWindowChanges values = {(int)x, (int)y, (int)w, (int)h};
        XConfigureWindow(m_xDisp, m_windowId, CWX|CWY|CWWidth|CWHeight, &values);
    }
    
    float getVirtualPixelFactor() const
    {
        return m_pixelFactor;
    }
    
    bool isFullscreen() const
    {
        unsigned long nitems;
        Atom     actualType;
        int      actualFormat;
        unsigned long     bytes;
        Atom* vals = nullptr;
        bool fullscreen = false;
        if (XGetWindowProperty(m_xDisp, m_windowId, XInternAtom(m_xDisp, "_NET_WM_STATE", True), 0, ~0l, False,
                               XA_ATOM, &actualType, &actualFormat, &nitems, &bytes, (unsigned char**)&vals) == Success)
        {
            for (int i=0 ; i<nitems ; ++i)
            {
                if (vals[i] == S_ATOMS->m_netwmStateFullscreen)
                {
                    fullscreen = true;
                    break;
                }
            }
            XFree(vals);
            return fullscreen;
        }
        return false;
    }
    
    void setFullscreen(bool fs)
    {
        XEvent fsEvent;
        fsEvent.type = ClientMessage;
        fsEvent.xclient.window = m_windowId;
        fsEvent.xclient.message_type = XInternAtom(m_xDisp, "_NET_WM_STATE", False);
        fsEvent.xclient.format = 32;
        fsEvent.xclient.data.l[0] = fs;
        fsEvent.xclient.data.l[1] = XInternAtom(m_xDisp, "_NET_WM_STATE_FULLSCREEN", False);
        fsEvent.xclient.data.l[2] = 0;
        XSendEvent(m_xDisp, m_windowId, False,
                   StructureNotifyMask | SubstructureRedirectMask, (XEvent*)&fsEvent);
    }

    void waitForRetrace()
    {
        m_gfxCtx.bindTimerContext();
        GLXWaitForVSync();
    }

    uintptr_t getPlatformHandle() const
    {
        return (uintptr_t)m_windowId;
    }

    void _pointingDeviceChanged(int deviceId)
    {
        int nDevices;
        XIDeviceInfo* devices = XIQueryDevice(m_xDisp, deviceId, &nDevices);

        for (int i=0 ; i<nDevices ; ++i)
        {
            XIDeviceInfo* device = &devices[i];

            /* First iterate classes for scrollables */
            int hScroll = -1;
            int vScroll = -1;
            m_hScrollLast = 0.0;
            m_vScrollLast = 0.0;
            m_hScrollValuator = -1;
            m_vScrollValuator = -1;
            for (int j=0 ; j<device->num_classes ; ++j)
            {
                XIAnyClassInfo* dclass = device->classes[j];
                if (dclass->type == XIScrollClass)
                {
                    XIScrollClassInfo* scrollClass = (XIScrollClassInfo*)dclass;
                    if (scrollClass->scroll_type == XIScrollTypeVertical)
                        vScroll = scrollClass->number;
                    else if (scrollClass->scroll_type == XIScrollTypeHorizontal)
                        hScroll = scrollClass->number;
                }
            }

            /* Next iterate for touch and scroll valuators */
            for (int j=0 ; j<device->num_classes ; ++j)
            {
                XIAnyClassInfo* dclass = device->classes[j];
                if (dclass->type == XIValuatorClass)
                {
                    XIValuatorClassInfo* valClass = (XIValuatorClassInfo*)dclass;
                    if (valClass->number == vScroll)
                    {
                        m_vScrollLast = valClass->value;
                        m_vScrollValuator = vScroll;
                    }
                    else if (valClass->number == hScroll)
                    {
                        m_hScrollLast = valClass->value;
                        m_hScrollValuator = hScroll;
                    }
                }
                else if (dclass->type == XITouchClass)
                {
                    XITouchClassInfo* touchClass = (XITouchClassInfo*)dclass;
                    if (touchClass->mode == XIDirectTouch)
                        m_touchType = TOUCH_DISPLAY;
                    else if (touchClass->mode == XIDependentTouch)
                        m_touchType = TOUCH_TRACKPAD;
                    else
                        m_touchType = TOUCH_NONE;
                }
            }
        }

        XIFreeDeviceInfo(devices);
        m_lastInputID = deviceId;
    }

    void _incomingEvent(void* e)
    {
        XEvent* event = (XEvent*)e;
        switch (event->type)
        {
        case Expose:
        {
            m_wx = event->xexpose.x;
            m_wy = event->xexpose.y;
            m_ww = event->xexpose.width;
            m_wh = event->xexpose.height;
            return;
        }
        case ConfigureNotify:
        {
            if (event->xconfigure.width && event->xconfigure.height)
            {
                m_wx = event->xconfigure.x;
                m_wy = event->xconfigure.y;
                m_ww = event->xconfigure.width;
                m_wh = event->xconfigure.height;
            }
            return;
        }
        case KeyPress:
        {
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                uint32_t charCode = translateKeysym(XLookupKeysym(&event->xkey, 0),
                                                    specialKey, modifierKey);
                int modifierMask = translateModifiers(event->xkey.state);
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
        case KeyRelease:
        {
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                uint32_t charCode = translateKeysym(XLookupKeysym(&event->xkey, 0),
                                                    specialKey, modifierKey);
                int modifierMask = translateModifiers(event->xkey.state);
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
        case ButtonPress:
        {
            if (m_callback)
            {
                int button = translateButton(event->xbutton.button);
                if (button)
                {
                    int modifierMask = translateModifiers(event->xbutton.state);
                    IWindowCallback::SWindowCoord coord =
                    {
                        {(unsigned)event->xbutton.x, (unsigned)event->xbutton.y},
                        {(unsigned)(event->xbutton.x / m_pixelFactor), (unsigned)(event->xbutton.y / m_pixelFactor)},
                        {event->xbutton.x / (float)m_ww, event->xbutton.y / (float)m_wh}
                    };
                    m_callback->mouseDown(coord, (IWindowCallback::EMouseButton)button,
                                          (IWindowCallback::EModifierKey)modifierMask);
                }

                /* Also handle legacy scroll events here */
                if (event->xbutton.button >= 4 && event->xbutton.button <= 7 &&
                    m_hScrollValuator == -1 && m_vScrollValuator == -1)
                {
                    IWindowCallback::SWindowCoord coord =
                    {
                        {(unsigned)event->xbutton.x, (unsigned)event->xbutton.y},
                        {(unsigned)(event->xbutton.x / m_pixelFactor), (unsigned)(event->xbutton.y / m_pixelFactor)},
                        {event->xbutton.x / (float)m_ww, event->xbutton.y / (float)m_wh}
                    };
                    IWindowCallback::SScrollDelta scrollDelta =
                    {
                        {0.0, 0.0},
                        false
                    };
                    if (event->xbutton.button == 4)
                        scrollDelta.delta[1] = 1.0;
                    else if (event->xbutton.button == 5)
                        scrollDelta.delta[1] = -1.0;
                    else if (event->xbutton.button == 6)
                        scrollDelta.delta[0] = 1.0;
                    else if (event->xbutton.button == 7)
                        scrollDelta.delta[0] = -1.0;
                    m_callback->scroll(coord, scrollDelta);
                }
            }
            return;
        }
        case ButtonRelease:
        {
            if (m_callback)
            {
                int button = translateButton(event->xbutton.button);
                if (button)
                {
                    int modifierMask = translateModifiers(event->xbutton.state);
                    IWindowCallback::SWindowCoord coord =
                    {
                        {(unsigned)event->xbutton.x, (unsigned)event->xbutton.y},
                        {(unsigned)(event->xbutton.x / m_pixelFactor), (unsigned)(event->xbutton.y / m_pixelFactor)},
                        {event->xbutton.x / (float)m_ww, event->xbutton.y / (float)m_wh}
                    };
                    m_callback->mouseUp(coord, (IWindowCallback::EMouseButton)button,
                                        (IWindowCallback::EModifierKey)modifierMask);
                }
            }
            return;
        }
        case MotionNotify:
        {
            if (m_callback)
            {
                IWindowCallback::SWindowCoord coord =
                {
                    {(unsigned)event->xmotion.x, (unsigned)event->xmotion.y},
                    {(unsigned)(event->xmotion.x / m_pixelFactor), (unsigned)(event->xmotion.y / m_pixelFactor)},
                    {event->xmotion.x / (float)m_ww, event->xmotion.y / (float)m_wh}
                };
                m_callback->mouseMove(coord);
            }
            return;
        }
        case GenericEvent:
        {
            if (event->xgeneric.extension == XINPUT_OPCODE)
            {
                switch (event->xgeneric.evtype)
                {
                case XI_Motion:
                {
                    XIDeviceEvent* ev = (XIDeviceEvent*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    int cv = 0;
                    double newScroll[2] = {m_hScrollLast, m_vScrollLast};
                    bool didScroll = false;
                    for (int i=0 ; i<ev->valuators.mask_len*8 ; ++i)
                    {
                        if (XIMaskIsSet(ev->valuators.mask, i))
                        {
                            if (i == m_hScrollValuator)
                            {
                                newScroll[0] = ev->valuators.values[cv];
                                didScroll = true;
                            }
                            else if (i == m_vScrollValuator)
                            {
                                newScroll[1] = ev->valuators.values[cv];
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
                        unsigned event_x = unsigned(ev->event_x) >> 16;
                        unsigned event_y = unsigned(ev->event_y) >> 16;
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
                case XI_TouchBegin:
                {
                    XIDeviceEvent* ev = (XIDeviceEvent*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    int cv = 0;
                    double vals[32] = {};
                    for (int i=0 ; i<ev->valuators.mask_len*8 && i<32 ; ++i)
                    {
                        if (XIMaskIsSet(ev->valuators.mask, i))
                        {
                            vals[i] = ev->valuators.values[cv];
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
                case XI_TouchUpdate:
                {
                    XIDeviceEvent* ev = (XIDeviceEvent*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    int cv = 0;
                    double vals[32] = {};
                    for (int i=0 ; i<ev->valuators.mask_len*8 && i<32 ; ++i)
                    {
                        if (XIMaskIsSet(ev->valuators.mask, i))
                        {
                            vals[i] = ev->valuators.values[cv];
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
                case XI_TouchEnd:
                {
                    XIDeviceEvent* ev = (XIDeviceEvent*)event;
                    if (m_lastInputID != ev->deviceid)
                        _pointingDeviceChanged(ev->deviceid);

                    int cv = 0;
                    double vals[32] = {};
                    for (int i=0 ; i<ev->valuators.mask_len*8 && i<32 ; ++i)
                    {
                        if (XIMaskIsSet(ev->valuators.mask, i))
                        {
                            vals[i] = ev->valuators.values[cv];
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
    
    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_gfxCtx.getCommandQueue();
    }

    IGraphicsDataFactory* getDataFactory()
    {
        return m_gfxCtx.getDataFactory();
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_gfxCtx.getLoadContextDataFactory();
    }

};

IWindow* _WindowXCBNew(const std::string& title,
                       Display* display, int defaultScreen,
                       GLXContext lastCtx)
{
    return new WindowXCB(title, display, defaultScreen, lastCtx);
}
    
}

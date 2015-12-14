#include "Win32Common.hpp"
#include <Windowsx.h>
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include <LogVisor/LogVisor.hpp>

#include "boo/graphicsdev/D3D.hpp"
#include "boo/graphicsdev/GL.hpp"
#include "boo/graphicsdev/glew.h"
#include "boo/graphicsdev/wglew.h"

static const int ContextAttribs[] =
{
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
    //WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
    //WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
    0, 0
};

namespace boo
{
static LogVisor::LogModule Log("boo::WindowWin32");
#if _WIN32_WINNT_WIN10
IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                                             ID3D12CommandQueue** cmdQueueOut);
IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent);
#endif
IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent);
IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent);
IGraphicsCommandQueue* _NewGLCommandQueue(IGraphicsContext* parent);

struct GraphicsContextWin32 : IGraphicsContext
{
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    Boo3DAppContext& m_3dCtx;
    ComPtr<IDXGIOutput> m_output;
    GraphicsContextWin32(EGraphicsAPI api, IWindow* parentWindow, Boo3DAppContext& b3dCtx)
    : m_api(api),
      m_pf(EPixelFormat::RGBA8),
      m_parentWindow(parentWindow),
      m_3dCtx(b3dCtx) {}
};

struct GraphicsContextWin32D3D : GraphicsContextWin32
{
    ComPtr<IDXGISwapChain1> m_swapChain;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32D3D(EGraphicsAPI api, IWindow* parentWindow, HWND hwnd, Boo3DAppContext& b3dCtx)
    : GraphicsContextWin32(api, parentWindow, b3dCtx)
    {
        /* Create Swap Chain */
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = 2;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

#if _WIN32_WINNT_WIN10
        if (b3dCtx.m_ctx12.m_dev)
        {
            auto insIt = b3dCtx.m_ctx12.m_windows.emplace(std::make_pair(parentWindow, D3D12Context::Window()));
            D3D12Context::Window& w = insIt.first->second;

            ID3D12CommandQueue* cmdQueue;
            m_dataFactory = _NewD3D12DataFactory(&b3dCtx.m_ctx12, this);
            m_commandQueue = _NewD3D12CommandQueue(&b3dCtx.m_ctx12, &w, this, &cmdQueue);

            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            HRESULT hr = b3dCtx.m_ctx12.m_dxFactory->CreateSwapChainForHwnd(cmdQueue, 
                hwnd, &scDesc, nullptr, nullptr, &m_swapChain);
            if (FAILED(hr))
                Log.report(LogVisor::FatalError, "unable to create swap chain");
            b3dCtx.m_ctx12.m_dxFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

            m_swapChain.As<IDXGISwapChain3>(&w.m_swapChain);
            ComPtr<ID3D12Resource> fb;
            m_swapChain->GetBuffer(0, __uuidof(ID3D12Resource), &fb);
            w.m_backBuf = w.m_swapChain->GetCurrentBackBufferIndex();
            D3D12_RESOURCE_DESC resDesc = fb->GetDesc();
            w.width = resDesc.Width;
            w.height = resDesc.Height;

            if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
                Log.report(LogVisor::FatalError, "unable to get DXGI output");
        }
        else
#endif
        {
            if (FAILED(b3dCtx.m_ctx11.m_dxFactory->CreateSwapChainForHwnd(b3dCtx.m_ctx11.m_dev.Get(), 
                hwnd, &scDesc, nullptr, nullptr, &m_swapChain)))
                Log.report(LogVisor::FatalError, "unable to create swap chain");
            b3dCtx.m_ctx11.m_dxFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            
            auto insIt = b3dCtx.m_ctx11.m_windows.emplace(std::make_pair(parentWindow, D3D11Context::Window()));
            D3D11Context::Window& w = insIt.first->second;

            m_swapChain.As<IDXGISwapChain1>(&w.m_swapChain);
            ComPtr<ID3D11Texture2D> fbRes;
            m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &fbRes);
            D3D11_TEXTURE2D_DESC resDesc;
            fbRes->GetDesc(&resDesc);
            w.width = resDesc.Width;
            w.height = resDesc.Height;
            m_dataFactory = _NewD3D11DataFactory(&b3dCtx.m_ctx11, this);
            m_commandQueue = _NewD3D11CommandQueue(&b3dCtx.m_ctx11, &insIt.first->second, this);

            if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
                Log.report(LogVisor::FatalError, "unable to get DXGI output");
        }
    }

    ~GraphicsContextWin32D3D()
    {
#if _WIN32_WINNT_WIN10
        if (m_3dCtx.m_ctx12.m_dev)
            m_3dCtx.m_ctx12.m_windows.erase(m_parentWindow);
        else
#endif
            m_3dCtx.m_ctx11.m_windows.erase(m_parentWindow);
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
        if (pf > EPixelFormat::RGBAF32_Z24)
            return;
        m_pf = pf;
    }

    void initializeContext() {}

    void makeCurrent() {}

    void postInit() {}

    void present() {}

    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_commandQueue;
    }

    IGraphicsDataFactory* getDataFactory()
    {
        return m_dataFactory;
    }

    IGraphicsDataFactory* getMainContextDataFactory()
    {
        return m_dataFactory;
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_dataFactory;
    }
};

struct GraphicsContextWin32GL : GraphicsContextWin32
{
    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32GL(EGraphicsAPI api, IWindow* parentWindow, HWND hwnd, Boo3DAppContext& b3dCtx)
    : GraphicsContextWin32(api, parentWindow, b3dCtx)
    {

        HMONITOR testMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> foundOut;
        int i=0;
        while (b3dCtx.m_ctxOgl.m_dxFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND)
        {
            int j=0;
            ComPtr<IDXGIOutput> out;
            while (adapter->EnumOutputs(j, &out) != DXGI_ERROR_NOT_FOUND)
            {
                DXGI_OUTPUT_DESC desc;
                out->GetDesc(&desc);
                if (desc.Monitor == testMon)
                {
                    out.As<IDXGIOutput>(&m_output);
                    break;
                }
                ++j;
            }
            if (m_output)
                break;
            ++i;
        }

        if (!m_output)
            Log.report(LogVisor::FatalError, "unable to find window's IDXGIOutput");

        auto insIt = b3dCtx.m_ctxOgl.m_windows.emplace(std::make_pair(parentWindow, OGLContext::Window()));
        OGLContext::Window& w = insIt.first->second;
        w.m_hwnd = hwnd;
        w.m_deviceContext = GetDC(hwnd);
        if (!w.m_deviceContext)
            Log.report(LogVisor::FatalError, "unable to create window's device context");

        if (!m_3dCtx.m_ctxOgl.m_lastContext)
        {
            PIXELFORMATDESCRIPTOR pfd =
            {
                sizeof(PIXELFORMATDESCRIPTOR),
                1,
                PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    //Flags
                PFD_TYPE_RGBA,            //The kind of framebuffer. RGBA or palette.
                32,                        //Colordepth of the framebuffer.
                0, 0, 0, 0, 0, 0,
                0,
                0,
                0,
                0, 0, 0, 0,
                24,                        //Number of bits for the depthbuffer
                8,                        //Number of bits for the stencilbuffer
                0,                        //Number of Aux buffers in the framebuffer.
                PFD_MAIN_PLANE,
                0,
                0, 0, 0
            };

            int pf = ChoosePixelFormat(w.m_deviceContext, &pfd); 
            SetPixelFormat(w.m_deviceContext, pf, &pfd);
        }

        w.m_mainContext = wglCreateContext(w.m_deviceContext);
        if (!w.m_mainContext)
            Log.report(LogVisor::FatalError, "unable to create window's main context");
        if (m_3dCtx.m_ctxOgl.m_lastContext)
            if (!wglShareLists(w.m_mainContext, m_3dCtx.m_ctxOgl.m_lastContext))
                Log.report(LogVisor::FatalError, "unable to share contexts");
        m_3dCtx.m_ctxOgl.m_lastContext = w.m_mainContext;

        m_dataFactory = new GLDataFactory(this);
        m_commandQueue = _NewGLCommandQueue(this);
    }

    ~GraphicsContextWin32GL()
    {
        m_3dCtx.m_ctxOgl.m_windows.erase(m_parentWindow);
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
        if (pf > EPixelFormat::RGBAF32_Z24)
            return;
        m_pf = pf;
    }

    void initializeContext() {}

    void makeCurrent() 
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];
        if (!wglMakeCurrent(w.m_deviceContext, w.m_mainContext))
            Log.report(LogVisor::FatalError, "unable to make WGL context current");
    }

    void postInit() 
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];

        wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
            wglGetProcAddress("wglCreateContextAttribsARB");
        w.m_renderContext = wglCreateContextAttribsARB(w.m_deviceContext, w.m_mainContext, ContextAttribs);
        if (!w.m_renderContext)
            Log.report(LogVisor::FatalError, "unable to make new WGL context");
        if (!wglMakeCurrent(w.m_deviceContext, w.m_renderContext))
            Log.report(LogVisor::FatalError, "unable to make WGL context current");

        if (!WGLEW_EXT_swap_control)
            Log.report(LogVisor::FatalError, "WGL_EXT_swap_control not available");
        wglSwapIntervalEXT(1);
    }

    void present() 
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];
        SwapBuffers(w.m_deviceContext);
    }

    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_commandQueue;
    }

    IGraphicsDataFactory* getDataFactory()
    {
        return m_dataFactory;
    }

    /* Creates a new context on current thread!! Call from client loading thread */
    HGLRC m_mainCtx = 0;
    IGraphicsDataFactory* getMainContextDataFactory()
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];
        if (!m_mainCtx)
        {
            m_mainCtx = wglCreateContextAttribsARB(w.m_deviceContext, w.m_mainContext, ContextAttribs);
            if (!m_mainCtx)
                Log.report(LogVisor::FatalError, "unable to make main WGL context");
        }
        if (!wglMakeCurrent(w.m_deviceContext, m_mainCtx))
            Log.report(LogVisor::FatalError, "unable to make main WGL context current");
        return m_dataFactory;
    }

    /* Creates a new context on current thread!! Call from client loading thread */
    HGLRC m_loadCtx = 0;
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];
        if (!m_loadCtx)
        {
            m_loadCtx = wglCreateContextAttribsARB(w.m_deviceContext, w.m_mainContext, ContextAttribs);
            if (!m_loadCtx)
                Log.report(LogVisor::FatalError, "unable to make load WGL context");
        }
        if (!wglMakeCurrent(w.m_deviceContext, m_loadCtx))
            Log.report(LogVisor::FatalError, "unable to make load WGL context current");
        return m_dataFactory;
    }
};

static void genFrameDefault(MONITORINFO* screen, int& xOut, int& yOut, int& wOut, int& hOut)
{
    float width = screen->rcMonitor.right * 2.0 / 3.0;
    float height = screen->rcMonitor.bottom * 2.0 / 3.0;
    xOut = (screen->rcMonitor.right - width) / 2.0;
    yOut = (screen->rcMonitor.bottom - height) / 2.0;
    wOut = width;
    hOut = height;
}

static uint32_t translateKeysym(WPARAM sym, ESpecialKey& specialSym, EModifierKey& modifierSym)
{
    specialSym = ESpecialKey::None;
    modifierSym = EModifierKey::None;
    if (sym >= VK_F1 && sym <= VK_F12)
        specialSym = ESpecialKey(uint32_t(ESpecialKey::F1) + sym - VK_F1);
    else if (sym == VK_ESCAPE)
        specialSym = ESpecialKey::Esc;
    else if (sym == VK_RETURN)
        specialSym = ESpecialKey::Enter;
    else if (sym == VK_BACK)
        specialSym = ESpecialKey::Backspace;
    else if (sym == VK_INSERT)
        specialSym = ESpecialKey::Insert;
    else if (sym == VK_DELETE)
        specialSym = ESpecialKey::Delete;
    else if (sym == VK_HOME)
        specialSym = ESpecialKey::Home;
    else if (sym == VK_END)
        specialSym = ESpecialKey::End;
    else if (sym == VK_PRIOR)
        specialSym = ESpecialKey::PgUp;
    else if (sym == VK_NEXT)
        specialSym = ESpecialKey::PgDown;
    else if (sym == VK_LEFT)
        specialSym = ESpecialKey::Left;
    else if (sym == VK_RIGHT)
        specialSym = ESpecialKey::Right;
    else if (sym == VK_UP)
        specialSym = ESpecialKey::Up;
    else if (sym == VK_DOWN)
        specialSym = ESpecialKey::Down;
    else if (sym == VK_LSHIFT || sym == VK_RSHIFT)
        modifierSym = EModifierKey::Shift;
    else if (sym == VK_LCONTROL || sym == VK_RCONTROL)
        modifierSym = EModifierKey::Ctrl;
    else if (sym == VK_MENU)
        modifierSym = EModifierKey::Alt;
    else
        return MapVirtualKey(sym, MAPVK_VK_TO_CHAR);
    return 0;
}

static EModifierKey translateModifiers(UINT msg)
{
    EModifierKey retval = EModifierKey::None;
    if ((GetKeyState(VK_LSHIFT) & 0x8000) != 0 || (GetKeyState(VK_RSHIFT) & 0x8000) != 0)
        retval |= EModifierKey::Shift;
    if ((GetKeyState(VK_LCONTROL) & 0x8000) != 0 || (GetKeyState(VK_RCONTROL) & 0x8000) != 0)
        retval |= EModifierKey::Ctrl;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        retval |= EModifierKey::Alt;
    if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
        retval |= EModifierKey::Alt;
    return retval;
}
    
class WindowWin32 : public IWindow
{
    friend struct GraphicsContextWin32;
    HWND m_hwnd;
    std::unique_ptr<GraphicsContextWin32> m_gfxCtx;
    IWindowCallback* m_callback = nullptr;
    EMouseCursor m_cursor = EMouseCursor::None;
    bool m_cursorWait = false;
    static HCURSOR GetWin32Cursor(EMouseCursor cur)
    {
        switch (cur)
        {
        case EMouseCursor::Pointer:
            return WIN32_CURSORS.m_arrow;
        case EMouseCursor::HorizontalArrow:
            return WIN32_CURSORS.m_hResize;
        case EMouseCursor::VerticalArrow:
            return WIN32_CURSORS.m_vResize;
        default: break;
        }
        return WIN32_CURSORS.m_arrow;
    }

public:

    WindowWin32(const SystemString& title, Boo3DAppContext& b3dCtx)
    {
        m_hwnd = CreateWindowW(L"BooWindow", title.c_str(), WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, NULL, NULL);
        IGraphicsContext::EGraphicsAPI api = IGraphicsContext::EGraphicsAPI::D3D11;
#if _WIN32_WINNT_WIN10
        if (b3dCtx.m_ctx12.m_dev)
            api = IGraphicsContext::EGraphicsAPI::D3D12;
#endif
        if (b3dCtx.m_ctxOgl.m_dxFactory)
        {
            m_gfxCtx.reset(new GraphicsContextWin32GL(IGraphicsContext::EGraphicsAPI::OpenGL3_3, this, m_hwnd, b3dCtx));
            return;
        }
        m_gfxCtx.reset(new GraphicsContextWin32D3D(api, this, m_hwnd, b3dCtx));
    }

    ~WindowWin32()
    {

    }

    void setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }

    void showWindow()
    {
        ShowWindow(m_hwnd, SW_SHOW);
    }

    void hideWindow()
    {
        ShowWindow(m_hwnd, SW_HIDE);
    }

    SystemString getTitle()
    {
        wchar_t title[256];
        int c = GetWindowTextW(m_hwnd, title, 256);
        return SystemString(title, c);
    }

    void setTitle(const SystemString& title)
    {
        SetWindowTextW(m_hwnd, title.c_str());
    }

    static void _setCursor(HCURSOR cur)
    {
        PostThreadMessageW(g_mainThreadId, WM_USER+2, WPARAM(cur), 0);
    }

    void setCursor(EMouseCursor cursor)
    {
        if (cursor == m_cursor && !m_cursorWait)
            return;
        m_cursor = cursor;
        _setCursor(GetWin32Cursor(cursor));
    }

    void setWaitCursor(bool wait)
    {
        if (wait && !m_cursorWait)
        {
            _setCursor(WIN32_CURSORS.m_wait);
            m_cursorWait = true;
        }
        else if (!wait && m_cursorWait)
        {
            setCursor(m_cursor);
            m_cursorWait = false;
        }
    }

    void setWindowFrameDefault()
    {
        MONITORINFO monInfo;
        GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &monInfo);
        int x, y, w, h;
        genFrameDefault(&monInfo, x, y, w, h);
        setWindowFrame(x, y, w, h);
    }

    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        RECT rct;
        GetClientRect(m_hwnd, &rct);
        POINT pt;
        pt.x = rct.left;
        pt.y = rct.top;
        MapWindowPoints(m_hwnd, HWND_DESKTOP, &pt, 1);
        xOut = pt.x;
        yOut = pt.y;
        wOut = rct.right;
        hOut = rct.bottom;
    }

    void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const
    {
        RECT rct;
        GetClientRect(m_hwnd, &rct);
        POINT pt;
        pt.x = rct.left;
        pt.y = rct.top;
        MapWindowPoints(m_hwnd, HWND_DESKTOP, &pt, 1);
        xOut = pt.x;
        yOut = pt.y;
        wOut = rct.right;
        hOut = rct.bottom;
    }

    void setWindowFrame(float x, float y, float w, float h)
    {
        MoveWindow(m_hwnd, x, y, w, h, true);
    }

    void setWindowFrame(int x, int y, int w, int h)
    {
        MoveWindow(m_hwnd, x, y, w, h, true);
    }

    float getVirtualPixelFactor() const
    {
        return 1.0;
    }

    bool isFullscreen() const
    {
        return m_gfxCtx->m_3dCtx.isFullscreen(this);
    }

    void setFullscreen(bool fs)
    {
        m_gfxCtx->m_3dCtx.setFullscreen(this, fs);
    }

    void waitForRetrace()
    {
        m_gfxCtx->m_output->WaitForVBlank();
    }

    uintptr_t getPlatformHandle() const
    {
        return uintptr_t(m_hwnd);
    }

    void buttonDown(HWNDEvent& e, EMouseButton button)
    {
        if (m_callback)
        {
            int x, y, w, h;
            getWindowFrame(x, y, w, h);
            EModifierKey modifierMask = translateModifiers(e.uMsg);
            SWindowCoord coord =
            {
                {GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam)},
                {GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam)},
                {float(GET_X_LPARAM(e.lParam)) / float(w), float(h-GET_Y_LPARAM(e.lParam)) / float(h)}
            };
            m_callback->mouseDown(coord, button, modifierMask);
        }
    }

    void buttonUp(HWNDEvent& e, EMouseButton button)
    {
        if (m_callback)
        {
            int x, y, w, h;
            getWindowFrame(x, y, w, h);
            EModifierKey modifierMask = translateModifiers(e.uMsg);
            SWindowCoord coord =
            {
                {GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam)},
                {GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam)},
                {float(GET_X_LPARAM(e.lParam)) / float(w), float(h-GET_Y_LPARAM(e.lParam)) / float(h)}
            };
            m_callback->mouseUp(coord, button, modifierMask);
        }
    }

    void _trackMouse()
    {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_NONCLIENT | TME_HOVER | TME_LEAVE;
        tme.dwHoverTime = 500;
        tme.hwndTrack = m_hwnd;
    }

    bool mouseTracking = false;
    void _incomingEvent(void* ev)
    {
        HWNDEvent& e = *static_cast<HWNDEvent*>(ev);
        switch (e.uMsg)
        {
        case WM_CLOSE:
            if (m_callback)
                m_callback->destroyed();
            return;
        case WM_SIZE:
        {
            SWindowRect rect;
            getWindowFrame(rect.location[0], rect.location[1], rect.size[0], rect.size[1]);
            if (!rect.size[0] || !rect.size[1])
                return;
            m_gfxCtx->m_3dCtx.resize(this, rect.size[0], rect.size[1]);
            if (m_callback)
                m_callback->resized(rect);
            return;
        }
        case WM_MOVING:
        {
            SWindowRect rect;
            getWindowFrame(rect.location[0], rect.location[1], rect.size[0], rect.size[1]);
            if (!rect.size[0] || !rect.size[1])
                return;

            if (m_callback)
                m_callback->windowMoved(rect);
            return;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            if (m_callback)
            {
                ESpecialKey specialKey;
                EModifierKey modifierKey;
                uint32_t charCode = translateKeysym(e.wParam, specialKey, modifierKey);
                EModifierKey modifierMask = translateModifiers(e.uMsg);
                if (charCode)
                    m_callback->charKeyDown(charCode, modifierMask, (e.lParam & 0xffff) != 0);
                else if (specialKey != ESpecialKey::None)
                    m_callback->specialKeyDown(specialKey, modifierMask, (e.lParam & 0xffff) != 0);
                else if (modifierKey != EModifierKey::None)
                    m_callback->modKeyDown(modifierKey, (e.lParam & 0xffff) != 0);
            }
            return;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            if (m_callback)
            {
                ESpecialKey specialKey;
                EModifierKey modifierKey;
                uint32_t charCode = translateKeysym(e.wParam, specialKey, modifierKey);
                EModifierKey modifierMask = translateModifiers(e.uMsg);
                if (charCode)
                    m_callback->charKeyUp(charCode, modifierMask);
                else if (specialKey != ESpecialKey::None)
                    m_callback->specialKeyUp(specialKey, modifierMask);
                else if (modifierKey != EModifierKey::None)
                    m_callback->modKeyUp(modifierKey);
            }
            return;
        }
        case WM_LBUTTONDOWN:
        {
            buttonDown(e, EMouseButton::Primary);
            return;
        }
        case WM_LBUTTONUP:
        {
            buttonUp(e, EMouseButton::Primary);
            return;
        }
        case WM_RBUTTONDOWN:
        {
            buttonDown(e, EMouseButton::Secondary);
            return;
        }
        case WM_RBUTTONUP:
        {
            buttonUp(e, EMouseButton::Secondary);
            return;
        }
        case WM_MBUTTONDOWN:
        {
            buttonDown(e, EMouseButton::Middle);
            return;
        }
        case WM_MBUTTONUP:
        {
            buttonUp(e, EMouseButton::Middle);
            return;
        }
        case WM_XBUTTONDOWN:
        {
            if (HIWORD(e.wParam) == XBUTTON1)
                buttonDown(e, EMouseButton::Aux1);
            else if (HIWORD(e.wParam) == XBUTTON2)
                buttonDown(e, EMouseButton::Aux2);
            return;
        }
        case WM_XBUTTONUP:
        {
            if (HIWORD(e.wParam) == XBUTTON1)
                buttonUp(e, EMouseButton::Aux1);
            else if (HIWORD(e.wParam) == XBUTTON2)
                buttonUp(e, EMouseButton::Aux2);
            return;
        }
        case WM_MOUSEMOVE:
        {
            if (m_callback)
            {
                int x, y, w, h;
                getWindowFrame(x, y, w, h);
                SWindowCoord coord =
                {
                    {GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam)},
                    {GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam)},
                    {float(GET_X_LPARAM(e.lParam)) / float(w), float(h-GET_Y_LPARAM(e.lParam)) / float(h)}
                };
                if (!mouseTracking)
                {
                    _trackMouse();
                    mouseTracking = true;
                    m_callback->mouseEnter(coord);
                }
                else
                    m_callback->mouseMove(coord);
            }

            return;
        }
        case WM_MOUSELEAVE:
        case WM_NCMOUSELEAVE:
        {
            if (m_callback)
            {
                int x, y, w, h;
                getWindowFrame(x, y, w, h);
                SWindowCoord coord =
                {
                    { GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam) },
                    { GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam) },
                    { float(GET_X_LPARAM(e.lParam)) / float(w), float(h-GET_Y_LPARAM(e.lParam)) / float(h) }
                };
                m_callback->mouseLeave(coord);
                mouseTracking = false;
            }
            return;
        }
        case WM_NCMOUSEHOVER:
        case WM_MOUSEHOVER:
        {
            if (m_callback)
            {
                int x, y, w, h;
                getWindowFrame(x, y, w, h);
                SWindowCoord coord =
                {
                    { GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam) },
                    { GET_X_LPARAM(e.lParam), h-GET_Y_LPARAM(e.lParam) },
                    { float(GET_X_LPARAM(e.lParam)) / float(w), float(h-GET_Y_LPARAM(e.lParam)) / float(h) }
                };
                m_callback->mouseEnter(coord);
            }
            return;
        }
        default: break;
        }
    }

    ETouchType getTouchType() const
    {
        return ETouchType::None;
    }

    void setStyle(EWindowStyle style)
    {
        LONG sty = GetWindowLong(m_hwnd, GWL_STYLE);

        if ((style & EWindowStyle::Titlebar) != EWindowStyle::None)
            sty |= WS_CAPTION;
        else
            sty &= ~WS_CAPTION;

        if ((style & EWindowStyle::Resize) != EWindowStyle::None)
            sty |= WS_THICKFRAME;
        else
            sty &= ~WS_THICKFRAME;

        if ((style & EWindowStyle::Close) != EWindowStyle::None)
            sty |= (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        else
            sty &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

        SetWindowLong(m_hwnd, GWL_STYLE, sty);
    }

    EWindowStyle getStyle() const
    {
        LONG sty = GetWindowLong(m_hwnd, GWL_STYLE);
        EWindowStyle retval = EWindowStyle::None;
        if ((sty & WS_CAPTION) != 0)
            retval |= EWindowStyle::Titlebar;
        if ((sty & WS_THICKFRAME) != 0)
            retval |= EWindowStyle::Resize;
        if ((sty & WS_SYSMENU))
            retval |= EWindowStyle::Close;
        return retval;
    }

    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_gfxCtx->getCommandQueue();
    }
    IGraphicsDataFactory* getDataFactory()
    {
        return m_gfxCtx->getDataFactory();
    }

    /* Creates a new context on current thread!! Call from main client thread */
    IGraphicsDataFactory* getMainContextDataFactory()
    {
        return m_gfxCtx->getMainContextDataFactory();
    }

    /* Creates a new context on current thread!! Call from client loading thread */
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_gfxCtx->getLoadContextDataFactory();
    }

};

IWindow* _WindowWin32New(const SystemString& title, Boo3DAppContext& d3dCtx)
{
    return new WindowWin32(title, d3dCtx);
}
    
}

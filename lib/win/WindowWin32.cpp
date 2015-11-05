#include "Win32Common.hpp"
#include <Windowsx.h>
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include <LogVisor/LogVisor.hpp>

#include "boo/graphicsdev/D3D11.hpp"
#include "boo/graphicsdev/D3D12.hpp"

namespace boo
{
static LogVisor::LogModule Log("WindowWin32");
IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                                             ID3D12CommandQueue** cmdQueueOut);
IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent);

struct GraphicsContextWin32 : IGraphicsContext
{

    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    D3DAppContext& m_d3dCtx;

    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<IDXGIOutput> m_output;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32(EGraphicsAPI api, IWindow* parentWindow, HWND hwnd, D3DAppContext& d3dCtx)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(parentWindow),
      m_d3dCtx(d3dCtx)
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
        if (d3dCtx.m_ctx12.m_dev)
        {
            auto insIt = d3dCtx.m_ctx12.m_windows.emplace(std::make_pair(parentWindow, D3D12Context::Window()));
            D3D12Context::Window& w = insIt.first->second;

            ID3D12CommandQueue* cmdQueue;
            m_dataFactory = new D3D12DataFactory(this, &d3dCtx.m_ctx12);
            m_commandQueue = _NewD3D12CommandQueue(&d3dCtx.m_ctx12, &w, this, &cmdQueue);

            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            HRESULT hr = d3dCtx.m_ctx12.m_dxFactory->CreateSwapChainForHwnd(cmdQueue, 
                hwnd, &scDesc, nullptr, nullptr, &m_swapChain);
            if (FAILED(hr))
                Log.report(LogVisor::FatalError, "unable to create swap chain");

            m_swapChain.As<IDXGISwapChain3>(&w.m_swapChain);
            ComPtr<ID3D12Resource> fb;
            m_swapChain->GetBuffer(0, __uuidof(ID3D12Resource), &fb);
            w.m_backBuf = w.m_swapChain->GetCurrentBackBufferIndex();
            D3D12_RESOURCE_DESC resDesc = fb->GetDesc();
            w.width = resDesc.Width;
            w.height = resDesc.Height;
        }
        else
#endif
        {
#if 0
            if (FAILED(d3dCtx.m_ctx11.m_dxFactory->CreateSwapChainForHwnd(d3dCtx.m_ctx11.m_dev.Get(), 
                hwnd, &scDesc, nullptr, nullptr, &m_swapChain)))
                Log.report(LogVisor::FatalError, "unable to create swap chain");
            
            auto insIt = d3dCtx.m_ctx11.m_windows.emplace(std::make_pair(parentWindow, D3D11Context::Window()));
            D3D11Context::Window& w = insIt.first->second;
            ComPtr<ID3D11Texture2D> fbRes;
            m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &fbRes);
            D3D11_TEXTURE2D_DESC resDesc;
            fbRes->GetDesc(&resDesc);
            w.width = resDesc.Width;
            w.height = resDesc.Height;
            m_dataFactory = new D3D11DataFactory(this, &d3dCtx.m_ctx11);
            m_commandQueue = _NewD3D11CommandQueue(&d3dCtx.m_ctx11, &insIt.first->second, this);
#endif
        }

        if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
            Log.report(LogVisor::FatalError, "unable to get DXGI output");
    }

    ~GraphicsContextWin32()
    {
#if _WIN32_WINNT_WIN10
        if (m_d3dCtx.m_ctx12.m_dev)
        {
            m_d3dCtx.m_ctx12.m_windows.erase(m_parentWindow);
        }
        else
#endif
        {
        }
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

    /* Creates a new context on current thread!! Call from client loading thread */
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
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

static uint32_t translateKeysym(WPARAM sym, int& specialSym, int& modifierSym)
{
    specialSym = KEY_NONE;
    modifierSym = MKEY_NONE;
    if (sym >= VK_F1 && sym <= VK_F12)
        specialSym = KEY_F1 + sym - VK_F1;
    else if (sym == VK_ESCAPE)
        specialSym = KEY_ESC;
    else if (sym == VK_RETURN)
        specialSym = KEY_ENTER;
    else if (sym == VK_BACK)
        specialSym = KEY_BACKSPACE;
    else if (sym == VK_INSERT)
        specialSym = KEY_INSERT;
    else if (sym == VK_DELETE)
        specialSym = KEY_DELETE;
    else if (sym == VK_HOME)
        specialSym = KEY_HOME;
    else if (sym == VK_END)
        specialSym = KEY_END;
    else if (sym == VK_PRIOR)
        specialSym = KEY_PGUP;
    else if (sym == VK_NEXT)
        specialSym = KEY_PGDOWN;
    else if (sym == VK_LEFT)
        specialSym = KEY_LEFT;
    else if (sym == VK_RIGHT)
        specialSym = KEY_RIGHT;
    else if (sym == VK_UP)
        specialSym = KEY_UP;
    else if (sym == VK_DOWN)
        specialSym = KEY_DOWN;
    else if (sym == VK_LSHIFT || sym == VK_RSHIFT)
        modifierSym = MKEY_SHIFT;
    else if (sym == VK_LCONTROL || sym == VK_RCONTROL)
        modifierSym = MKEY_CTRL;
    else if (sym == VK_MENU)
        modifierSym = MKEY_ALT;
    else
        return MapVirtualKey(sym, MAPVK_VK_TO_CHAR);
    return 0;
}

static int translateModifiers()
{
    int retval = 0;
    if (GetKeyState(VK_LSHIFT) & 0x8000 != 0 || GetKeyState(VK_RSHIFT) & 0x8000 != 0)
        retval |= MKEY_SHIFT;
    if (GetKeyState(VK_LCONTROL) & 0x8000 != 0 || GetKeyState(VK_RCONTROL) & 0x8000 != 0)
        retval |= MKEY_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000 != 0)
        retval |= MKEY_ALT;
    return retval;
}
    
class WindowWin32 : public IWindow
{
    friend GraphicsContextWin32;
    HWND m_hwnd;
    std::unique_ptr<GraphicsContextWin32> m_gfxCtx;
    IWindowCallback* m_callback = nullptr;
    
public:
    
    WindowWin32(const SystemString& title, D3DAppContext& d3dCtx)
    {
        m_hwnd = CreateWindowW(L"BooWindow", title.c_str(), WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, NULL, NULL);
        m_gfxCtx.reset(new GraphicsContextWin32(IGraphicsContext::API_D3D11, this, m_hwnd, d3dCtx));
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
        xOut = rct.left;
        yOut = rct.top;
        wOut = rct.right;
        hOut = rct.bottom;
    }

    void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const
    {
        RECT rct;
        GetClientRect(m_hwnd, &rct);
        xOut = rct.left;
        yOut = rct.top;
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
        return false;
    }
    
    void setFullscreen(bool fs)
    {
        
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
            int modifierMask = translateModifiers();
            SWindowCoord coord =
            {
                {(unsigned)GET_X_LPARAM(e.lParam), (unsigned)GET_Y_LPARAM(e.lParam)},
                {(unsigned)GET_X_LPARAM(e.lParam), (unsigned)GET_Y_LPARAM(e.lParam)},
                {float(GET_X_LPARAM(e.lParam)) / float(w), float(GET_Y_LPARAM(e.lParam)) / float(h)}
            };
            m_callback->mouseDown(coord, button, EModifierKey(modifierMask));
        }
    }

    void buttonUp(HWNDEvent& e, EMouseButton button)
    {
        if (m_callback)
        {
            int x, y, w, h;
            getWindowFrame(x, y, w, h);
            int modifierMask = translateModifiers();
            SWindowCoord coord =
            {
                {(unsigned)GET_X_LPARAM(e.lParam), (unsigned)GET_Y_LPARAM(e.lParam)},
                {(unsigned)GET_X_LPARAM(e.lParam), (unsigned)GET_Y_LPARAM(e.lParam)},
                {float(GET_X_LPARAM(e.lParam)) / float(w), float(GET_Y_LPARAM(e.lParam)) / float(h)}
            };
            m_callback->mouseUp(coord, button, EModifierKey(modifierMask));
        }
    }
    
    void _incomingEvent(void* ev)
    {
        HWNDEvent& e = *static_cast<HWNDEvent*>(ev);
        switch (e.uMsg)
        {
        case WM_SIZE:
        {
            SWindowRect rect;
            getWindowFrame(rect.location[0], rect.location[1], rect.size[0], rect.size[1]);
            if (!rect.size[0] || !rect.size[1])
                return;
            m_gfxCtx->m_d3dCtx.resize(this, rect.size[0], rect.size[1]);
            if (m_callback)
                m_callback->resized(rect);
            return;
        }
        case WM_KEYDOWN:
        {
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                uint32_t charCode = translateKeysym(e.wParam, specialKey, modifierKey);
                int modifierMask = translateModifiers();
                if (charCode)
                    m_callback->charKeyDown(charCode, EModifierKey(modifierMask), e.lParam & 0xffff != 0);
                else if (specialKey)
                    m_callback->specialKeyDown(ESpecialKey(specialKey), EModifierKey(modifierMask), e.lParam & 0xffff != 0);
                else if (modifierKey)
                    m_callback->modKeyDown(EModifierKey(modifierKey), e.lParam & 0xffff != 0);
            }
            return;
        }
        case WM_KEYUP:
        {
            if (m_callback)
            {
                int specialKey;
                int modifierKey;
                uint32_t charCode = translateKeysym(e.wParam, specialKey, modifierKey);
                int modifierMask = translateModifiers();
                if (charCode)
                    m_callback->charKeyUp(charCode, EModifierKey(modifierMask));
                else if (specialKey)
                    m_callback->specialKeyUp(ESpecialKey(specialKey), EModifierKey(modifierMask));
                else if (modifierKey)
                    m_callback->modKeyUp(EModifierKey(modifierKey));
            }
            return;
        }
        case WM_LBUTTONDOWN:
        {
            buttonDown(e, BUTTON_PRIMARY);
            return;
        }
        case WM_LBUTTONUP:
        {
            buttonUp(e, BUTTON_PRIMARY);
            return;
        }
        case WM_RBUTTONDOWN:
        {
            buttonDown(e, BUTTON_SECONDARY);
            return;
        }
        case WM_RBUTTONUP:
        {
            buttonUp(e, BUTTON_SECONDARY);
            return;
        }
        case WM_MBUTTONDOWN:
        {
            buttonDown(e, BUTTON_MIDDLE);
            return;
        }
        case WM_MBUTTONUP:
        {
            buttonUp(e, BUTTON_MIDDLE);
            return;
        }
        case WM_XBUTTONDOWN:
        {
            if (HIWORD(e.wParam) == XBUTTON1)
                buttonDown(e, BUTTON_AUX1);
            else if (HIWORD(e.wParam) == XBUTTON2)
                buttonDown(e, BUTTON_AUX2);
            return;
        }
        case WM_XBUTTONUP:
        {
            if (HIWORD(e.wParam) == XBUTTON1)
                buttonUp(e, BUTTON_AUX1);
            else if (HIWORD(e.wParam) == XBUTTON2)
                buttonUp(e, BUTTON_AUX2);
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
                    {(unsigned)GET_X_LPARAM(e.lParam), (unsigned)GET_Y_LPARAM(e.lParam)},
                    {(unsigned)GET_X_LPARAM(e.lParam), (unsigned)GET_Y_LPARAM(e.lParam)},
                    {float(GET_X_LPARAM(e.lParam)) / float(w), float(GET_Y_LPARAM(e.lParam)) / float(h)}
                };
                m_callback->mouseMove(coord);
            }
            return;
        }
        default: break;
        }
    }

    ETouchType getTouchType() const
    {
        return TOUCH_NONE;
    }

    IGraphicsCommandQueue* getCommandQueue()
    {
        return m_gfxCtx->getCommandQueue();
    }
    IGraphicsDataFactory* getDataFactory()
    {
        return m_gfxCtx->getDataFactory();
    }

    /* Creates a new context on current thread!! Call from client loading thread */
    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return m_gfxCtx->getLoadContextDataFactory();
    }
    
};

IWindow* _WindowWin32New(const SystemString& title, D3DAppContext& d3dCtx)
{
    return new WindowWin32(title, d3dCtx);
}
    
}

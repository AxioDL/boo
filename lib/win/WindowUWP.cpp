#include "UWPCommon.hpp"
#include "boo/IApplication.hpp"
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "logvisor/logvisor.hpp"

#include "boo/graphicsdev/D3D.hpp"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

using namespace Windows::UI::Core;
using namespace Windows::System;

namespace boo
{
static logvisor::Module Log("boo::WindowWin32");
#if _WIN32_WINNT_WIN10
IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                                             ID3D12CommandQueue** cmdQueueOut);
IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent, uint32_t sampleCount);
#endif
IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent);
IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent, uint32_t sampleCount);

struct GraphicsContextUWP : IGraphicsContext
{
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    Boo3DAppContextUWP& m_3dCtx;
    ComPtr<IDXGIOutput> m_output;
    GraphicsContextUWP(EGraphicsAPI api, IWindow* parentWindow, Boo3DAppContextUWP& b3dCtx)
    : m_api(api),
      m_pf(EPixelFormat::RGBA8),
      m_parentWindow(parentWindow),
      m_3dCtx(b3dCtx) {}

    virtual void resized(const SWindowRect& rect)
    {
        m_3dCtx.resize(m_parentWindow, rect.size[0], rect.size[1]);
    }
};

struct GraphicsContextUWPD3D : GraphicsContextUWP
{
    ComPtr<IDXGISwapChain1> m_swapChain;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

public:
    IWindowCallback* m_callback;

    GraphicsContextUWPD3D(EGraphicsAPI api, IWindow* parentWindow, CoreWindow^ coreWindow,
                          Boo3DAppContextUWP& b3dCtx, uint32_t sampleCount)
    : GraphicsContextUWP(api, parentWindow, b3dCtx)
    {
        /* Create Swap Chain */
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = 2;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        IUnknown* cw = reinterpret_cast<IUnknown*>(coreWindow);

#if _WIN32_WINNT_WIN10
        if (b3dCtx.m_ctx12.m_dev)
        {
            auto insIt = b3dCtx.m_ctx12.m_windows.emplace(std::make_pair(parentWindow, D3D12Context::Window()));
            D3D12Context::Window& w = insIt.first->second;

            ID3D12CommandQueue* cmdQueue;
            m_dataFactory = _NewD3D12DataFactory(&b3dCtx.m_ctx12, this, sampleCount);
            m_commandQueue = _NewD3D12CommandQueue(&b3dCtx.m_ctx12, &w, this, &cmdQueue);

            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            HRESULT hr = b3dCtx.m_ctx12.m_dxFactory->CreateSwapChainForCoreWindow(cmdQueue,
                cw, &scDesc, nullptr, &m_swapChain);
            if (FAILED(hr))
                Log.report(logvisor::Fatal, "unable to create swap chain");

            m_swapChain.As<IDXGISwapChain3>(&w.m_swapChain);
            ComPtr<ID3D12Resource> fb;
            m_swapChain->GetBuffer(0, __uuidof(ID3D12Resource), &fb);
            w.m_backBuf = w.m_swapChain->GetCurrentBackBufferIndex();
            D3D12_RESOURCE_DESC resDesc = fb->GetDesc();
            w.width = resDesc.Width;
            w.height = resDesc.Height;

            if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
                Log.report(logvisor::Fatal, "unable to get DXGI output");
        }
        else
#endif
        {
            if (FAILED(b3dCtx.m_ctx11.m_dxFactory->CreateSwapChainForCoreWindow(b3dCtx.m_ctx11.m_dev.Get(),
                cw, &scDesc, nullptr, &m_swapChain)))
                Log.report(logvisor::Fatal, "unable to create swap chain");

            auto insIt = b3dCtx.m_ctx11.m_windows.emplace(std::make_pair(parentWindow, D3D11Context::Window()));
            D3D11Context::Window& w = insIt.first->second;

            m_swapChain.As<IDXGISwapChain1>(&w.m_swapChain);
            ComPtr<ID3D11Texture2D> fbRes;
            m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &fbRes);
            D3D11_TEXTURE2D_DESC resDesc;
            fbRes->GetDesc(&resDesc);
            w.width = resDesc.Width;
            w.height = resDesc.Height;
            m_dataFactory = _NewD3D11DataFactory(&b3dCtx.m_ctx11, this, sampleCount);
            m_commandQueue = _NewD3D11CommandQueue(&b3dCtx.m_ctx11, &insIt.first->second, this);

            if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
                Log.report(logvisor::Fatal, "unable to get DXGI output");
        }
    }

    ~GraphicsContextUWPD3D()
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

    bool initializeContext(void*) {return true;}

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

static void genFrameDefault(MONITORINFO* screen, int& xOut, int& yOut, int& wOut, int& hOut)
{
    float width = screen->rcMonitor.right * 2.0 / 3.0;
    float height = screen->rcMonitor.bottom * 2.0 / 3.0;
    xOut = (screen->rcMonitor.right - width) / 2.0;
    yOut = (screen->rcMonitor.bottom - height) / 2.0;
    wOut = width;
    hOut = height;
}

static uint32_t translateKeysym(VirtualKey sym, ESpecialKey& specialSym, EModifierKey& modifierSym)
{
    specialSym = ESpecialKey::None;
    modifierSym = EModifierKey::None;
    if (sym >= VirtualKey_F1 && sym <= VirtualKey_F12)
        specialSym = ESpecialKey(uint32_t(ESpecialKey::F1) + sym - VirtualKey_F1);
    else if (sym == VirtualKey_Escape)
        specialSym = ESpecialKey::Esc;
    else if (sym == VirtualKey_Enter)
        specialSym = ESpecialKey::Enter;
    else if (sym == VirtualKey_Back)
        specialSym = ESpecialKey::Backspace;
    else if (sym == VirtualKey_Insert)
        specialSym = ESpecialKey::Insert;
    else if (sym == VirtualKey_Delete)
        specialSym = ESpecialKey::Delete;
    else if (sym == VirtualKey_Home)
        specialSym = ESpecialKey::Home;
    else if (sym == VirtualKey_End)
        specialSym = ESpecialKey::End;
    else if (sym == VirtualKey_PageUp)
        specialSym = ESpecialKey::PgUp;
    else if (sym == VirtualKey_PageDown)
        specialSym = ESpecialKey::PgDown;
    else if (sym == VirtualKey_Left)
        specialSym = ESpecialKey::Left;
    else if (sym == VirtualKey_Right)
        specialSym = ESpecialKey::Right;
    else if (sym == VirtualKey_Up)
        specialSym = ESpecialKey::Up;
    else if (sym == VirtualKey_Down)
        specialSym = ESpecialKey::Down;
    else if (sym == VirtualKey_Shift)
        modifierSym = EModifierKey::Shift;
    else if (sym == VirtualKey_Control)
        modifierSym = EModifierKey::Ctrl;
    else if (sym == VirtualKey_Menu)
        modifierSym = EModifierKey::Alt;
    else if (sym >= VirtualKey_A && sym <= VirtualKey_Z)
        return sym - VirtualKey_A + window->GetKeyState(VirtualKey_Shift) ? 'A' : 'a'
    return 0;
}

static EModifierKey translateModifiers(CoreWindow^ window)
{
    EModifierKey retval = EModifierKey::None;
    if (window->GetKeyState(VirtualKey_Shift) != None)
        retval |= EModifierKey::Shift;
    if (window->GetKeyState(VirtualKey_Control) != None)
        retval |= EModifierKey::Ctrl;
    if (window->GetKeyState(LefVirtualKey_Menu) != None)
        retval |= EModifierKey::Alt;
    return retval;
}

class WindowUWP : public IWindow
{
    friend struct GraphicsContextUWP;
    ApplicationView^ m_appView = ApplicationView::GetForCurrentView();
    CoreWindow^ m_coreWindow = CoreWindow::GetForCurrentThread();
    std::unique_ptr<GraphicsContextUWP> m_gfxCtx;
    IWindowCallback* m_callback = nullptr;

public:

    WindowUWP(SystemStringView title, Boo3DAppContext& b3dCtx, uint32_t sampleCount)
    {
        IGraphicsContext::EGraphicsAPI api = IGraphicsContext::EGraphicsAPI::D3D11;
#if _WIN32_WINNT_WIN10
        if (b3dCtx.m_ctx12.m_dev)
            api = IGraphicsContext::EGraphicsAPI::D3D12;
#endif
        m_gfxCtx.reset(new GraphicsContextUWPD3D(api, this, m_coreWindow, b3dCtx, sampleCount));

        setTitle(title);
        m_coreWindow->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &WindowUWP::OnKeyDown);
        m_coreWindow->KeyUp += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &WindowUWP::OnKeyUp);
        m_coreWindow->Closed += ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &WindowUWP::OnClosed);
    }

    ~WindowUWP()
    {

    }

    void setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }

    void closeWindow()
    {
        m_coreWindow->Close();
    }

    void showWindow()
    {
    }

    void hideWindow()
    {
    }

    SystemString getTitle()
    {
        return SystemString(m_appView->Title.Data());
    }

    void setTitle(SystemStringView title)
    {
        m_appView->Title = title.data();
    }

    void setCursor(EMouseCursor cursor)
    {
    }

    void setWaitCursor(bool wait)
    {
    }

    void setWindowFrameDefault()
    {
    }

    void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const
    {
        xOut = m_coreWindow->bounds.X;
        yOut = m_coreWindow->bounds.Y;
        wOut = m_coreWindow->bounds.Width;
        hOut = m_coreWindow->bounds.Height;
    }

    void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const
    {
        xOut = m_coreWindow->bounds.X;
        yOut = m_coreWindow->bounds.Y;
        wOut = m_coreWindow->bounds.Width;
        hOut = m_coreWindow->bounds.Height;
    }

    void setWindowFrame(float x, float y, float w, float h)
    {
    }

    void setWindowFrame(int x, int y, int w, int h)
    {
    }

    float getVirtualPixelFactor() const
    {
        DisplayInformation dispInfo = DisplayInformation::GetForCurrentView();
        return dispInfo.LogicalDpi / 96.f;
    }

    bool isFullscreen() const
    {
        return m_gfxCtx->m_3dCtx.isFullscreen(this);
    }

    void setFullscreen(bool fs)
    {
        m_gfxCtx->m_3dCtx.setFullscreen(this, fs);
    }

    void claimKeyboardFocus(const int coord[2])
    {
    }

    bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz)
    {
    }

    std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz)
    {
        return std::unique_ptr<uint8_t[]>();
    }

    void waitForRetrace(IAudioVoiceEngine* engine)
    {
        if (engine)
            engine->pumpAndMixVoices();
        m_gfxCtx->m_output->WaitForVBlank();
    }

    uintptr_t getPlatformHandle() const
    {
        return 0;
    }

    void _incomingEvent(void* ev)
    {
    }

    void OnKeyDown(CoreWindow^ window, KeyEventArgs^ keyEventArgs)
    {
        if (auto w = m_window.lock())
        {
            ESpecialKey specialKey;
            EModifierKey modifierKey;
            uint32_t charCode = translateKeysym(keyEventArgs->VirtualKey, specialKey, modifierKey);
            EModifierKey modifierMask = translateModifiers(window);
            bool repeat = keyEventArgs->KeyStatus.RepeatCount > 1;
            if (charCode)
                m_callback->charKeyDown(charCode, modifierMask, repeat);
            else if (specialKey != ESpecialKey::None)
                m_callback->specialKeyDown(specialKey, modifierMask, repeat);
            else if (modifierKey != EModifierKey::None)
                m_callback->modKeyDown(modifierKey, repeat);
        }
    }

    void OnKeyUp(CoreWindow^ window, KeyEventArgs^ keyEventArgs)
    {
        if (auto w = m_window.lock())
        {
            ESpecialKey specialKey;
            EModifierKey modifierKey;
            uint32_t charCode = translateKeysym(keyEventArgs->VirtualKey, specialKey, modifierKey);
            EModifierKey modifierMask = translateModifiers(window);
            if (charCode)
                m_callback->charKeyDown(charCode, modifierMask);
            else if (specialKey != ESpecialKey::None)
                m_callback->specialKeyDown(specialKey, modifierMask);
            else if (modifierKey != EModifierKey::None)
                m_callback->modKeyDown(modifierKey);
        }
    }

    void OnClosed(CoreWindow ^sender, CoreWindowEventArgs ^args)
    {
    }

    ETouchType getTouchType() const
    {
        return ETouchType::None;
    }

    void setStyle(EWindowStyle style)
    {
    }

    EWindowStyle getStyle() const
    {
        EWindowStyle retval = EWindowStyle::None;
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

std::shared_ptr<IWindow> _WindowUAPNew(SystemStringView title, Boo3DAppContext& d3dCtx,
                                       uint32_t sampleCount)
{
    return std::make_shared<WindowUWP>(title, d3dCtx, sampleCount);
}

}

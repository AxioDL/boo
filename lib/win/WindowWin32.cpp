#include "Win32Common.hpp"
#include <Windowsx.h>
#include "boo/IApplication.hpp"
#include "boo/IWindow.hpp"
#include "boo/IGraphicsContext.hpp"
#include "logvisor/logvisor.hpp"

#include "boo/graphicsdev/D3D.hpp"
#include "boo/graphicsdev/GL.hpp"
#include "boo/graphicsdev/glew.h"
#include "boo/graphicsdev/wglew.h"
#include "boo/audiodev/IAudioVoiceEngine.hpp"

#if BOO_HAS_VULKAN
#include "boo/graphicsdev/Vulkan.hpp"
#endif

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
static logvisor::Module Log("boo::WindowWin32");
#if _WIN32_WINNT_WIN10
IGraphicsCommandQueue* _NewD3D12CommandQueue(D3D12Context* ctx, D3D12Context::Window* windowCtx, IGraphicsContext* parent,
                                             ID3D12CommandQueue** cmdQueueOut);
IGraphicsDataFactory* _NewD3D12DataFactory(D3D12Context* ctx, IGraphicsContext* parent, uint32_t sampleCount);
#endif
IGraphicsCommandQueue* _NewD3D11CommandQueue(D3D11Context* ctx, D3D11Context::Window* windowCtx, IGraphicsContext* parent);
IGraphicsDataFactory* _NewD3D11DataFactory(D3D11Context* ctx, IGraphicsContext* parent, uint32_t sampleCount);
IGraphicsCommandQueue* _NewGLCommandQueue(IGraphicsContext* parent);
IGraphicsDataFactory* _NewGLDataFactory(IGraphicsContext* parent, uint32_t drawSamples);
#if BOO_HAS_VULKAN
IGraphicsCommandQueue* _NewVulkanCommandQueue(VulkanContext* ctx,
                                              VulkanContext::Window* windowCtx,
                                              IGraphicsContext* parent);
IGraphicsDataFactory* _NewVulkanDataFactory(IGraphicsContext* parent, VulkanContext* ctx,
                                            uint32_t drawSamples);
#endif

struct GraphicsContextWin32 : IGraphicsContext
{
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    Boo3DAppContextWin32& m_3dCtx;
    ComPtr<IDXGIOutput> m_output;
    GraphicsContextWin32(EGraphicsAPI api, IWindow* parentWindow, Boo3DAppContextWin32& b3dCtx)
    : m_api(api),
      m_pf(EPixelFormat::RGBA8),
      m_parentWindow(parentWindow),
      m_3dCtx(b3dCtx) {}

    virtual void resized(const SWindowRect& rect)
    {
        m_3dCtx.resize(m_parentWindow, rect.size[0], rect.size[1]);
    }
};

struct GraphicsContextWin32D3D : GraphicsContextWin32
{
    ComPtr<IDXGISwapChain1> m_swapChain;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32D3D(EGraphicsAPI api, IWindow* parentWindow, HWND hwnd,
                            Boo3DAppContextWin32& b3dCtx, uint32_t sampleCount)
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
            m_dataFactory = _NewD3D12DataFactory(&b3dCtx.m_ctx12, this, sampleCount);
            m_commandQueue = _NewD3D12CommandQueue(&b3dCtx.m_ctx12, &w, this, &cmdQueue);

            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            HRESULT hr = b3dCtx.m_ctx12.m_dxFactory->CreateSwapChainForHwnd(cmdQueue,
                hwnd, &scDesc, nullptr, nullptr, &m_swapChain);
            if (FAILED(hr))
                Log.report(logvisor::Fatal, "unable to create swap chain");
            b3dCtx.m_ctx12.m_dxFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

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
            if (FAILED(b3dCtx.m_ctx11.m_dxFactory->CreateSwapChainForHwnd(b3dCtx.m_ctx11.m_dev.Get(),
                hwnd, &scDesc, nullptr, nullptr, &m_swapChain)))
                Log.report(logvisor::Fatal, "unable to create swap chain");
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
            m_dataFactory = _NewD3D11DataFactory(&b3dCtx.m_ctx11, this, sampleCount);
            m_commandQueue = _NewD3D11CommandQueue(&b3dCtx.m_ctx11, &insIt.first->second, this);

            if (FAILED(m_swapChain->GetContainingOutput(&m_output)))
                Log.report(logvisor::Fatal, "unable to get DXGI output");
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

struct GraphicsContextWin32GL : GraphicsContextWin32
{
    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32GL(EGraphicsAPI api, IWindow* parentWindow, HWND hwnd,
                           Boo3DAppContextWin32& b3dCtx, uint32_t sampleCount)
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
            Log.report(logvisor::Fatal, "unable to find window's IDXGIOutput");

        auto insIt = b3dCtx.m_ctxOgl.m_windows.emplace(std::make_pair(parentWindow, OGLContext::Window()));
        OGLContext::Window& w = insIt.first->second;
        w.m_hwnd = hwnd;
        w.m_deviceContext = GetDC(hwnd);
        if (!w.m_deviceContext)
            Log.report(logvisor::Fatal, "unable to create window's device context");

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
            Log.report(logvisor::Fatal, "unable to create window's main context");
        if (m_3dCtx.m_ctxOgl.m_lastContext)
            if (!wglShareLists(w.m_mainContext, m_3dCtx.m_ctxOgl.m_lastContext))
                Log.report(logvisor::Fatal, "unable to share contexts");
        m_3dCtx.m_ctxOgl.m_lastContext = w.m_mainContext;

        m_dataFactory = _NewGLDataFactory(this, sampleCount);
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

    bool initializeContext(void*) {return true;}

    void makeCurrent()
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];
        if (!wglMakeCurrent(w.m_deviceContext, w.m_mainContext))
            Log.report(logvisor::Fatal, "unable to make WGL context current");
    }

    void postInit()
    {
        OGLContext::Window& w = m_3dCtx.m_ctxOgl.m_windows[m_parentWindow];

        wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
            wglGetProcAddress("wglCreateContextAttribsARB");
        w.m_renderContext = wglCreateContextAttribsARB(w.m_deviceContext, w.m_mainContext, ContextAttribs);
        if (!w.m_renderContext)
            Log.report(logvisor::Fatal, "unable to make new WGL context");
        if (!wglMakeCurrent(w.m_deviceContext, w.m_renderContext))
            Log.report(logvisor::Fatal, "unable to make WGL context current");

        if (!WGLEW_EXT_swap_control)
            Log.report(logvisor::Fatal, "WGL_EXT_swap_control not available");
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
                Log.report(logvisor::Fatal, "unable to make main WGL context");
        }
        if (!wglMakeCurrent(w.m_deviceContext, m_mainCtx))
            Log.report(logvisor::Fatal, "unable to make main WGL context current");
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
                Log.report(logvisor::Fatal, "unable to make load WGL context");
        }
        if (!wglMakeCurrent(w.m_deviceContext, m_loadCtx))
            Log.report(logvisor::Fatal, "unable to make load WGL context current");
        return m_dataFactory;
    }
};

#if BOO_HAS_VULKAN
struct GraphicsContextWin32Vulkan : GraphicsContextWin32
{
    HINSTANCE m_appInstance;
    HWND m_hwnd;
    VulkanContext* m_ctx;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR m_colorspace;
    uint32_t m_sampleCount;

    IGraphicsCommandQueue* m_commandQueue = nullptr;
    IGraphicsDataFactory* m_dataFactory = nullptr;

    std::thread m_vsyncThread;
    bool m_vsyncRunning;

    static void ThrowIfFailed(VkResult res)
    {
        if (res != VK_SUCCESS)
            Log.report(logvisor::Fatal, "%d\n", res);
    }

public:
    IWindowCallback* m_callback;

    GraphicsContextWin32Vulkan(IWindow* parentWindow, HINSTANCE appInstance, HWND hwnd,
                               VulkanContext* ctx, Boo3DAppContextWin32& b3dCtx, uint32_t drawSamples)
    : GraphicsContextWin32(EGraphicsAPI::Vulkan, parentWindow, b3dCtx),
      m_appInstance(appInstance), m_hwnd(hwnd), m_ctx(ctx), m_sampleCount(drawSamples)
    {
        HMONITOR testMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> foundOut;
        int i=0;
        while (b3dCtx.m_vulkanDxFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND)
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
            Log.report(logvisor::Fatal, "unable to find window's IDXGIOutput");
    }

    void destroy()
    {
        VulkanContext::Window& m_windowCtx = *m_ctx->m_windows[m_parentWindow];
        m_windowCtx.m_swapChains[0].destroy(m_ctx->m_dev);
        m_windowCtx.m_swapChains[1].destroy(m_ctx->m_dev);
        //vk::DestroySurfaceKHR(m_ctx->m_instance, m_surface, nullptr);

        if (m_vsyncRunning)
        {
            m_vsyncRunning = false;
            if (m_vsyncThread.joinable())
                m_vsyncThread.join();
        }

        m_ctx->m_windows.erase(m_parentWindow);
    }

    ~GraphicsContextWin32Vulkan() {destroy();}

    VulkanContext::Window* m_windowCtx = nullptr;

    void resized(const SWindowRect& rect)
    {
        if (m_windowCtx)
            m_ctx->resizeSwapChain(*m_windowCtx, m_surface, m_format, m_colorspace, rect);
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

    bool initializeContext(void* getVkProc)
    {
        vk::init_dispatch_table_top(PFN_vkGetInstanceProcAddr(getVkProc));
        if (m_ctx->m_instance == VK_NULL_HANDLE)
        {
            const SystemString& appName = APP->getUniqueName();
            m_ctx->initVulkan(WCSTMBS(appName.c_str()).c_str());
        }

        vk::init_dispatch_table_middle(m_ctx->m_instance, false);
        if (!m_ctx->enumerateDevices())
            return false;

        m_windowCtx =
            m_ctx->m_windows.emplace(std::make_pair(m_parentWindow,
            std::make_unique<VulkanContext::Window>())).first->second.get();

        VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = m_appInstance;
        surfaceInfo.hwnd = m_hwnd;
        ThrowIfFailed(vk::CreateWin32SurfaceKHR(m_ctx->m_instance, &surfaceInfo, nullptr, &m_surface));

        /* Iterate over each queue to learn whether it supports presenting */
        VkBool32 *supportsPresent = (VkBool32*)malloc(m_ctx->m_queueCount * sizeof(VkBool32));
        for (uint32_t i=0 ; i<m_ctx->m_queueCount ; ++i)
            vk::GetPhysicalDeviceSurfaceSupportKHR(m_ctx->m_gpus[0], i, m_surface, &supportsPresent[i]);

        /* Search for a graphics queue and a present queue in the array of queue
         * families, try to find one that supports both */
        if (m_ctx->m_graphicsQueueFamilyIndex == UINT32_MAX)
        {
            /* First window, init device */
            for (uint32_t i=0 ; i<m_ctx->m_queueCount; ++i)
            {
                if ((m_ctx->m_queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
                {
                    if (supportsPresent[i] == VK_TRUE)
                    {
                        m_ctx->m_graphicsQueueFamilyIndex = i;
                    }
                }
            }

            /* Generate error if could not find a queue that supports both a graphics
             * and present */
            if (m_ctx->m_graphicsQueueFamilyIndex == UINT32_MAX)
                Log.report(logvisor::Fatal,
                           "Could not find a queue that supports both graphics and present");

            m_ctx->initDevice();
        }
        else
        {
            /* Subsequent window, verify present */
            if (supportsPresent[m_ctx->m_graphicsQueueFamilyIndex] == VK_FALSE)
                Log.report(logvisor::Fatal, "subsequent surface doesn't support present");
        }
        free(supportsPresent);

        vk::init_dispatch_table_bottom(m_ctx->m_instance, m_ctx->m_dev);

        if (!vk::GetPhysicalDeviceWin32PresentationSupportKHR(m_ctx->m_gpus[0], m_ctx->m_graphicsQueueFamilyIndex))
        {
            Log.report(logvisor::Fatal, "Win32 doesn't support vulkan present");
            return false;
        }

        /* Get the list of VkFormats that are supported */
        uint32_t formatCount;
        ThrowIfFailed(vk::GetPhysicalDeviceSurfaceFormatsKHR(m_ctx->m_gpus[0], m_surface, &formatCount, nullptr));
        VkSurfaceFormatKHR* surfFormats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
        ThrowIfFailed(vk::GetPhysicalDeviceSurfaceFormatsKHR(m_ctx->m_gpus[0], m_surface, &formatCount, surfFormats));


        /* If the format list includes just one entry of VK_FORMAT_UNDEFINED,
         * the surface has no preferred format.  Otherwise, at least one
         * supported format will be returned. */
        if (formatCount >= 1)
        {
            for (int i=0 ; i<formatCount ; ++i)
            {
                if (surfFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                    surfFormats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
                {
                    m_format = surfFormats[i].format;
                    m_colorspace = surfFormats[i].colorSpace;
                    break;
                }
            }
        }
        else
            Log.report(logvisor::Fatal, "no surface formats available for Vulkan swapchain");

        if (m_format == VK_FORMAT_UNDEFINED)
            Log.report(logvisor::Fatal, "no UNORM formats available for Vulkan swapchain");

        m_ctx->initSwapChain(*m_windowCtx, m_surface, m_format, m_colorspace);

        m_dataFactory = _NewVulkanDataFactory(this, m_ctx, m_sampleCount);
        m_commandQueue = _NewVulkanCommandQueue(m_ctx, m_ctx->m_windows[m_parentWindow].get(), this);
        return true;
    }

    void makeCurrent() {}

    void postInit() {}

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
        return getDataFactory();
    }

    IGraphicsDataFactory* getLoadContextDataFactory()
    {
        return getDataFactory();
    }

    void present() {}

};
#endif

static void genFrameDefault(MONITORINFO* screen, int& xOut, int& yOut, int& wOut, int& hOut)
{
    float width = screen->rcMonitor.right * 2.0 / 3.0;
    float height = screen->rcMonitor.bottom * 2.0 / 3.0;
    xOut = (screen->rcMonitor.right - width) / 2.0;
    yOut = (screen->rcMonitor.bottom - height) / 2.0;
    wOut = width;
    hOut = height;
}

static uint32_t translateKeysym(WPARAM sym, UINT scancode, ESpecialKey& specialSym, EModifierKey& modifierSym)
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
    else if (sym == VK_SHIFT)
        modifierSym = EModifierKey::Shift;
    else if (sym == VK_CONTROL)
        modifierSym = EModifierKey::Ctrl;
    else if (sym == VK_MENU)
        modifierSym = EModifierKey::Alt;
    else
    {
        BYTE kbState[256];
        GetKeyboardState(kbState);
        kbState[VK_CONTROL] = 0;
        WORD ch = 0;
        ToAscii(sym, scancode, kbState, &ch, 0);
        return ch;
    }
    return 0;
}

static EModifierKey translateModifiers(UINT msg)
{
    EModifierKey retval = EModifierKey::None;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        retval |= EModifierKey::Shift;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        retval |= EModifierKey::Ctrl;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        retval |= EModifierKey::Alt;
    if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
        retval |= EModifierKey::Alt;
    return retval;
}

static HGLOBAL MakeANSICRLF(const char* data, size_t sz)
{
    size_t retSz = 1;
    char lastCh = 0;
    for (size_t i=0 ; i<sz ; ++i)
    {
        char ch = data[i];
        if (ch == '\n' && lastCh != '\r')
            retSz += 2;
        else
            retSz += 1;
        lastCh = ch;
    }
    HGLOBAL ret = GlobalAlloc(GMEM_MOVEABLE, retSz);
    char* retData = reinterpret_cast<char*>(GlobalLock(ret));
    lastCh = 0;
    for (size_t i=0 ; i<sz ; ++i)
    {
        char ch = data[i];
        if (ch == '\n' && lastCh != '\r')
        {
            *retData = '\r';
            ++retData;
            *retData = '\n';
            ++retData;
        }
        else
        {
            *retData = ch;
            ++retData;
        }
        lastCh = ch;
    }
    *retData = '\0';
    GlobalUnlock(ret);
    return ret;
}

static std::unique_ptr<uint8_t[]> MakeANSILF(const char* data, size_t sz, size_t& szOut)
{
    szOut = 0;
    char lastCh = 0;
    for (size_t i=0 ; i<sz ; ++i)
    {
        char ch = data[i];
        if (ch == '\n' && lastCh == '\r')
        {}
        else
            szOut += 1;
        lastCh = ch;
    }
    std::unique_ptr<uint8_t[]> ret(new uint8_t[szOut]);
    uint8_t* retPtr = ret.get();
    lastCh = 0;
    for (size_t i=0 ; i<sz ; ++i)
    {
        char ch = data[i];
        if (ch == '\n' && lastCh == '\r')
            retPtr[-1] = uint8_t('\n');
        else
        {
            *retPtr = uint8_t(ch);
            ++retPtr;
        }
        lastCh = ch;
    }
    return ret;
}

/** Memory could not be allocated. */
#define UTF8PROC_ERROR_NOMEM -1
/** The given string is too long to be processed. */
#define UTF8PROC_ERROR_OVERFLOW -2
/** The given string is not a legal UTF-8 string. */
#define UTF8PROC_ERROR_INVALIDUTF8 -3
/** The @ref UTF8PROC_REJECTNA flag was set and an unassigned codepoint was found. */
#define UTF8PROC_ERROR_NOTASSIGNED -4
/** Invalid options have been used. */
#define UTF8PROC_ERROR_INVALIDOPTS -5

#define UTF8PROC_cont(ch)  (((ch) & 0xc0) == 0x80)

static inline int utf8proc_iterate(const uint8_t *str, int strlen, int32_t *dst) {
  uint32_t uc;
  const uint8_t *end;

  *dst = -1;
  if (!strlen) return 0;
  end = str + ((strlen < 0) ? 4 : strlen);
  uc = *str++;
  if (uc < 0x80) {
    *dst = uc;
    return 1;
  }
  // Must be between 0xc2 and 0xf4 inclusive to be valid
  if ((uc - 0xc2) > (0xf4-0xc2)) return UTF8PROC_ERROR_INVALIDUTF8;
  if (uc < 0xe0) {         // 2-byte sequence
     // Must have valid continuation character
     if (!UTF8PROC_cont(*str)) return UTF8PROC_ERROR_INVALIDUTF8;
     *dst = ((uc & 0x1f)<<6) | (*str & 0x3f);
     return 2;
  }
  if (uc < 0xf0) {        // 3-byte sequence
     if ((str + 1 >= end) || !UTF8PROC_cont(*str) || !UTF8PROC_cont(str[1]))
        return UTF8PROC_ERROR_INVALIDUTF8;
     // Check for surrogate chars
     if (uc == 0xed && *str > 0x9f)
         return UTF8PROC_ERROR_INVALIDUTF8;
     uc = ((uc & 0xf)<<12) | ((*str & 0x3f)<<6) | (str[1] & 0x3f);
     if (uc < 0x800)
         return UTF8PROC_ERROR_INVALIDUTF8;
     *dst = uc;
     return 3;
  }
  // 4-byte sequence
  // Must have 3 valid continuation characters
  if ((str + 2 >= end) || !UTF8PROC_cont(*str) || !UTF8PROC_cont(str[1]) || !UTF8PROC_cont(str[2]))
     return UTF8PROC_ERROR_INVALIDUTF8;
  // Make sure in correct range (0x10000 - 0x10ffff)
  if (uc == 0xf0) {
    if (*str < 0x90) return UTF8PROC_ERROR_INVALIDUTF8;
  } else if (uc == 0xf4) {
    if (*str > 0x8f) return UTF8PROC_ERROR_INVALIDUTF8;
  }
  *dst = ((uc & 7)<<18) | ((*str & 0x3f)<<12) | ((str[1] & 0x3f)<<6) | (str[2] & 0x3f);
  return 4;
}

static HGLOBAL MakeUnicodeCRLF(const char* data, size_t sz)
{
    size_t retSz = 2;
    int32_t lastCh = 0;
    for (size_t i=0 ; i<sz ;)
    {
        int32_t ch;
        int chSz = utf8proc_iterate(reinterpret_cast<const uint8_t*>(data+i), -1, &ch);
        if (chSz < 0)
            Log.report(logvisor::Fatal, "invalid UTF-8 char");
        if (ch <= 0xffff)
        {
            if (ch == '\n' && lastCh != '\r')
                retSz += 4;
            else
                retSz += 2;
            lastCh = ch;
        }
        i += chSz;
    }
    HGLOBAL ret = GlobalAlloc(GMEM_MOVEABLE, retSz);
    wchar_t* retData = reinterpret_cast<wchar_t*>(GlobalLock(ret));
    lastCh = 0;
    for (size_t i=0 ; i<sz ;)
    {
        int32_t ch;
        int chSz = utf8proc_iterate(reinterpret_cast<const uint8_t*>(data+i), -1, &ch);
        if (ch <= 0xffff)
        {
            if (ch == '\n' && lastCh != '\r')
            {
                *retData = L'\r';
                ++retData;
                *retData = L'\n';
                ++retData;
            }
            else
            {
                *retData = wchar_t(ch);
                ++retData;
            }
            lastCh = ch;
        }
        i += chSz;
    }
    *retData = L'\0';
    GlobalUnlock(ret);
    return ret;
}

static inline int utf8proc_encode_char(int32_t uc, uint8_t *dst) {
  if (uc < 0x00) {
    return 0;
  } else if (uc < 0x80) {
    dst[0] = uc;
    return 1;
  } else if (uc < 0x800) {
    dst[0] = 0xC0 + (uc >> 6);
    dst[1] = 0x80 + (uc & 0x3F);
    return 2;
  // Note: we allow encoding 0xd800-0xdfff here, so as not to change
  // the API, however, these are actually invalid in UTF-8
  } else if (uc < 0x10000) {
    dst[0] = 0xE0 + (uc >> 12);
    dst[1] = 0x80 + ((uc >> 6) & 0x3F);
    dst[2] = 0x80 + (uc & 0x3F);
    return 3;
  } else if (uc < 0x110000) {
    dst[0] = 0xF0 + (uc >> 18);
    dst[1] = 0x80 + ((uc >> 12) & 0x3F);
    dst[2] = 0x80 + ((uc >> 6) & 0x3F);
    dst[3] = 0x80 + (uc & 0x3F);
    return 4;
  } else return 0;
}

static std::unique_ptr<uint8_t[]> MakeUnicodeLF(const wchar_t* data, size_t sz, size_t& szOut)
{
    szOut = 0;
    wchar_t lastCh = 0;
    for (size_t i=0 ; i<sz ; ++i)
    {
        wchar_t ch = data[i];
        if (ch == L'\n' && lastCh == L'\r')
        {}
        else
        {
            uint8_t dummy[4];
            szOut += utf8proc_encode_char(ch, dummy);
        }
        lastCh = ch;
    }
    std::unique_ptr<uint8_t[]> ret(new uint8_t[szOut]);
    uint8_t* retPtr = ret.get();
    lastCh = 0;
    for (size_t i=0 ; i<sz ; ++i)
    {
        wchar_t ch = data[i];
        if (ch == L'\n' && lastCh == L'\r')
            retPtr[-1] = uint8_t('\n');
        else
            retPtr += utf8proc_encode_char(ch, retPtr);
        lastCh = ch;
    }
    return ret;
}

class WindowWin32 : public IWindow
{
    friend struct GraphicsContextWin32;
    HWND m_hwnd;
    HIMC m_imc;
    std::unique_ptr<GraphicsContextWin32> m_gfxCtx;
    IWindowCallback* m_callback = nullptr;
    EMouseCursor m_cursor = EMouseCursor::None;
    bool m_cursorWait = false;
    bool m_openGL = false;
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
        case EMouseCursor::IBeam:
            return WIN32_CURSORS.m_ibeam;
        case EMouseCursor::Crosshairs:
            return WIN32_CURSORS.m_crosshairs;
        default: break;
        }
        return WIN32_CURSORS.m_arrow;
    }

public:

    WindowWin32(const SystemString& title, Boo3DAppContextWin32& b3dCtx, void* vulkanHandle, uint32_t sampleCount)
    {
        m_hwnd = CreateWindowW(L"BooWindow", title.c_str(), WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, NULL, NULL);
        HINSTANCE wndInstance = HINSTANCE(GetWindowLongPtr(m_hwnd, GWLP_HINSTANCE));
        m_imc = ImmGetContext(m_hwnd);
        IGraphicsContext::EGraphicsAPI api = IGraphicsContext::EGraphicsAPI::D3D11;
#if _WIN32_WINNT_WIN10
        if (b3dCtx.m_ctx12.m_dev)
            api = IGraphicsContext::EGraphicsAPI::D3D12;
#endif
        if (b3dCtx.m_ctxOgl.m_dxFactory)
        {
            m_gfxCtx.reset(new GraphicsContextWin32GL(IGraphicsContext::EGraphicsAPI::OpenGL3_3,
                                                      this, m_hwnd, b3dCtx, sampleCount));
            m_openGL = true;
            return;
        }
#if BOO_HAS_VULKAN
        else if (b3dCtx.m_vulkanDxFactory)
        {
            m_gfxCtx.reset(new GraphicsContextWin32Vulkan(this, wndInstance, m_hwnd, &g_VulkanContext,
                                                          b3dCtx, sampleCount));
            if (m_gfxCtx->initializeContext(vulkanHandle))
                return;
        }
#endif
        m_gfxCtx.reset(new GraphicsContextWin32D3D(api, this, m_hwnd, b3dCtx, sampleCount));
    }

    ~WindowWin32()
    {

    }

    void setCallback(IWindowCallback* cb)
    {
        m_callback = cb;
    }

    void closeWindow()
    {
        // TODO: Perform thread-coalesced deallocation
        ShowWindow(m_hwnd, SW_HIDE);
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
#if _WIN32_WINNT_WINBLUE
        if (MyGetScaleFactorForMonitor)
        {
            DEVICE_SCALE_FACTOR Factor;
            HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
            MyGetScaleFactorForMonitor(mon, &Factor);
            if (Factor == 0)
                return 1.f;
            return Factor / 100.f;
        }
#endif
        return 1.f;
    }

    bool isFullscreen() const
    {
        return m_gfxCtx->m_3dCtx.isFullscreen(this);
    }

    void setFullscreen(bool fs)
    {
        m_gfxCtx->m_3dCtx.setFullscreen(this, fs);
    }

    void _immSetOpenStatus(bool open)
    {
        if (GetCurrentThreadId() != g_mainThreadId)
        {
            if (!PostThreadMessage(g_mainThreadId, WM_USER+3, WPARAM(m_imc), LPARAM(open)))
                Log.report(logvisor::Fatal, "PostThreadMessage error");
            return;
        }
        ImmSetOpenStatus(m_imc, open);
    }

    COMPOSITIONFORM m_cForm = {CFS_POINT};
    void _immSetCompositionWindow(const int coord[2])
    {
        int x, y, w, h;
        getWindowFrame(x, y, w, h);
        m_cForm.ptCurrentPos.x = coord[0];
        m_cForm.ptCurrentPos.y = h - coord[1];

        if (GetCurrentThreadId() != g_mainThreadId)
        {
            if (!PostThreadMessage(g_mainThreadId, WM_USER+4, WPARAM(m_imc), LPARAM(&m_cForm)))
                Log.report(logvisor::Fatal, "PostThreadMessage error");
            return;
        }
        ImmSetCompositionWindow(m_imc, &m_cForm);
    }

    void claimKeyboardFocus(const int coord[2])
    {
        if (!coord)
        {
            //_immSetOpenStatus(false);
            return;
        }
        _immSetCompositionWindow(coord);
        //_immSetOpenStatus(true);
    }

    bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz)
    {
        switch (type)
        {
        case EClipboardType::String:
        {
            HGLOBAL gStr = MakeANSICRLF(reinterpret_cast<const char*>(data), sz);
            OpenClipboard(m_hwnd);
            EmptyClipboard();
            SetClipboardData(CF_TEXT, gStr);
            CloseClipboard();
            return true;
        }
        case EClipboardType::UTF8String:
        {
            HGLOBAL gStr = MakeUnicodeCRLF(reinterpret_cast<const char*>(data), sz);
            OpenClipboard(m_hwnd);
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, gStr);
            CloseClipboard();
            return true;
        }
        default: break;
        }
        return false;
    }

    std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz)
    {
        switch (type)
        {
        case EClipboardType::String:
        {
            OpenClipboard(m_hwnd);
            HGLOBAL gStr = GetClipboardData(CF_TEXT);
            if (!gStr)
                break;
            const char* str = reinterpret_cast<const char*>(GlobalLock(gStr));
            std::unique_ptr<uint8_t[]> ret = MakeANSILF(str, GlobalSize(gStr), sz);
            GlobalUnlock(gStr);
            CloseClipboard();
            return ret;
        }
        case EClipboardType::UTF8String:
        {
            OpenClipboard(m_hwnd);
            HGLOBAL gStr = GetClipboardData(CF_UNICODETEXT);
            if (!gStr)
                break;
            const wchar_t* str = reinterpret_cast<const wchar_t*>(GlobalLock(gStr));
            std::unique_ptr<uint8_t[]> ret = MakeUnicodeLF(str, GlobalSize(gStr)/2, sz);
            GlobalUnlock(gStr);
            CloseClipboard();
            return ret;
        }
        default: break;
        }
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
            m_gfxCtx->resized(rect);
            if (m_callback)
                m_callback->resized(rect, m_openGL);
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
                uint32_t charCode = translateKeysym(e.wParam, (e.lParam >> 16) & 0xff, specialKey, modifierKey);
                EModifierKey modifierMask = translateModifiers(e.uMsg);
                if (charCode)
                    m_callback->charKeyDown(charCode, modifierMask, (HIWORD(e.lParam) & KF_REPEAT) != 0);
                else if (specialKey != ESpecialKey::None)
                    m_callback->specialKeyDown(specialKey, modifierMask, (HIWORD(e.lParam) & KF_REPEAT) != 0);
                else if (modifierKey != EModifierKey::None)
                    m_callback->modKeyDown(modifierKey, (HIWORD(e.lParam) & KF_REPEAT) != 0);
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
                uint32_t charCode = translateKeysym(e.wParam, (e.lParam >> 16) & 0xff, specialKey, modifierKey);
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
        case WM_MOUSEWHEEL:
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
                SScrollDelta scroll = {};
                scroll.delta[1] = GET_WHEEL_DELTA_WPARAM(e.wParam) / double(WHEEL_DELTA);
                m_callback->scroll(coord, scroll);
            }
            return;
        }
        case WM_MOUSEHWHEEL:
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
                SScrollDelta scroll = {};
                scroll.delta[0] = GET_WHEEL_DELTA_WPARAM(e.wParam) / double(-WHEEL_DELTA);
                m_callback->scroll(coord, scroll);
            }
            return;
        }
        case WM_CHAR:
        case WM_UNICHAR:
        {
            if (m_callback)
            {
                ITextInputCallback* inputCb = m_callback->getTextInputCallback();
                uint8_t utf8ch[4];
                size_t len = utf8proc_encode_char(e.wParam, utf8ch);
                if (inputCb && len)
                    inputCb->insertText(std::string((char*)utf8ch, len));
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

std::shared_ptr<IWindow> _WindowWin32New(const SystemString& title, Boo3DAppContextWin32& d3dCtx,
                                         void* vulkanHandle, uint32_t sampleCount)
{
    return std::make_shared<WindowWin32>(title, d3dCtx, vulkanHandle, sampleCount);
}

}

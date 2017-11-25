#ifndef BOO_WIN32COMMON_HPP
#define BOO_WIN32COMMON_HPP

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include "WinCommon.hpp"
#include <windows.h>

#if BOO_HAS_VULKAN
#include "boo/graphicsdev/Vulkan.hpp"
#endif

extern DWORD g_mainThreadId;

#if _WIN32_WINNT_WINBLUE
#include <ShellScalingApi.h>
typedef HRESULT (WINAPI* PFN_SetProcessDpiAwareness)( _In_ PROCESS_DPI_AWARENESS );
typedef HRESULT (WINAPI* PFN_GetScaleFactorForMonitor)( _In_ HMONITOR, _Out_ DEVICE_SCALE_FACTOR *);
extern PFN_GetScaleFactorForMonitor MyGetScaleFactorForMonitor;
#endif

struct OGLContext
{
    ComPtr<IDXGIFactory1> m_dxFactory;
    HGLRC m_lastContext = 0;
    struct Window
    {
        HWND m_hwnd;
        HDC m_deviceContext;
        HGLRC m_mainContext;
        HGLRC m_renderContext;
        bool m_needsResize = false;
        size_t width, height;

        bool m_fs = false;
        LONG m_fsStyle;
        LONG m_fsExStyle;
        RECT m_fsRect;
        int m_fsCountDown = 0;
    };
    std::unordered_map<const boo::IWindow*, Window> m_windows;
};

template <class W>
static inline void SetFullscreen(W& win, bool fs)
{
    if (fs)
    {
        win.m_fsStyle = GetWindowLong(win.m_hwnd, GWL_STYLE);
        win.m_fsExStyle = GetWindowLong(win.m_hwnd, GWL_EXSTYLE);
        GetWindowRect(win.m_hwnd, &win.m_fsRect);

        SetWindowLong(win.m_hwnd, GWL_STYLE,
            win.m_fsStyle & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLong(win.m_hwnd, GWL_EXSTYLE,
            win.m_fsExStyle & ~(WS_EX_DLGMODALFRAME |
                WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

        MONITORINFO monitor_info;
        monitor_info.cbSize = sizeof(monitor_info);
        GetMonitorInfo(MonitorFromWindow(win.m_hwnd, MONITOR_DEFAULTTONEAREST),
            &monitor_info);
        SetWindowPos(win.m_hwnd, NULL, monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
            monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
            monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);

        win.m_fs = true;
    }
    else
    {
        SetWindowLong(win.m_hwnd, GWL_STYLE, win.m_fsStyle);
        SetWindowLong(win.m_hwnd, GWL_EXSTYLE, win.m_fsExStyle);

        SetWindowPos(win.m_hwnd, NULL, win.m_fsRect.left, win.m_fsRect.top,
            win.m_fsRect.right - win.m_fsRect.left, win.m_fsRect.bottom - win.m_fsRect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);

        win.m_fs = false;
    }
}

struct Boo3DAppContextWin32 : Boo3DAppContext
{
    OGLContext m_ctxOgl;
    ComPtr<IDXGIFactory1> m_vulkanDxFactory;

    bool isFullscreen(const boo::IWindow* window)
    {
#if BOO_HAS_VULKAN
        if (m_vulkanDxFactory)
        {
            boo::VulkanContext::Window& win = *boo::g_VulkanContext.m_windows[window];
            return win.m_fs;
        }
#endif

#if _WIN32_WINNT_WIN10
        if (m_ctx12.m_dev)
        {
            D3D12Context::Window& win = m_ctx12.m_windows[window];
            BOOL isFScr;
            win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
            return isFScr != 0;
        }
#endif
        if (m_ctx11.m_dev)
        {
            D3D11Context::Window& win = m_ctx11.m_windows[window];
            BOOL isFScr;
            win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
            return isFScr != 0;
        }
        OGLContext::Window& win = m_ctxOgl.m_windows[window];
        return win.m_fs;
    }

    bool setFullscreen(boo::IWindow* window, bool fs)
    {
#if BOO_HAS_VULKAN
        if (m_vulkanDxFactory)
        {
            boo::VulkanContext::Window& win = *boo::g_VulkanContext.m_windows[window];
            if (fs && win.m_fs)
                return false;
            else if (!fs && !win.m_fs)
                return false;
            SetFullscreen(win, fs);
            return true;
        }
#endif

#if _WIN32_WINNT_WIN10
        if (m_ctx12.m_dev)
        {
            D3D12Context::Window& win = m_ctx12.m_windows[window];
            BOOL isFScr;
            win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
            if (fs && isFScr)
                return false;
            else if (!fs && !isFScr)
                return false;

            if (fs)
            {
                ComPtr<IDXGIOutput> out;
                win.m_swapChain->GetContainingOutput(&out);
                DXGI_OUTPUT_DESC outDesc;
                out->GetDesc(&outDesc);

                win.m_swapChain->SetFullscreenState(true, nullptr);
                DXGI_MODE_DESC mdesc = {UINT(outDesc.DesktopCoordinates.right - outDesc.DesktopCoordinates.left),
                                        UINT(outDesc.DesktopCoordinates.bottom - outDesc.DesktopCoordinates.top)};
                win.m_swapChain->ResizeTarget(&mdesc);
            }
            else
                win.m_swapChain->SetFullscreenState(false, nullptr);
            return true;
        }
#endif
        if (m_ctx11.m_dev)
        {
            D3D11Context::Window& win = m_ctx11.m_windows[window];
            BOOL isFScr;
            win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
            if (fs && isFScr)
                return false;
            else if (!fs && !isFScr)
                return false;

            if (fs)
            {
                ComPtr<IDXGIOutput> out;
                win.m_swapChain->GetContainingOutput(&out);
                DXGI_OUTPUT_DESC outDesc;
                out->GetDesc(&outDesc);

                win.m_fsdesc.Width = outDesc.DesktopCoordinates.right;
                win.m_fsdesc.Height = outDesc.DesktopCoordinates.bottom;
            }
            win.m_fs = fs;
            win.m_needsFSTransition = true;
            return true;
        }

        OGLContext::Window& win = m_ctxOgl.m_windows[window];
        if (fs && win.m_fs)
            return false;
        else if (!fs && !win.m_fs)
            return false;
        SetFullscreen(win, fs);
        return true;
    }
};

struct HWNDEvent
{
    UINT uMsg;
    WPARAM wParam;
    LPARAM lParam;
    HWNDEvent(UINT m, WPARAM w, LPARAM l)
    : uMsg(m), wParam(w), lParam(l) {}
};

struct Win32Cursors
{
    HCURSOR m_arrow;
    HCURSOR m_hResize;
    HCURSOR m_vResize;
    HCURSOR m_ibeam;
    HCURSOR m_crosshairs;
    HCURSOR m_wait;
};
namespace boo
{
extern Win32Cursors WIN32_CURSORS;
}

#endif // BOO_WIN32COMMON_HPP

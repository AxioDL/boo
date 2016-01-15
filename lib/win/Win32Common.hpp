#ifndef BOO_WIN32COMMON_HPP
#define BOO_WIN32COMMON_HPP

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <unordered_map>

extern DWORD g_mainThreadId;

#include "boo/IWindow.hpp"

namespace boo {class IWindow;}

#if _WIN32_WINNT_WIN10
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wingdi.h>

struct D3D12Context
{
    ComPtr<IDXGIFactory2> m_dxFactory;
    ComPtr<ID3D12Device> m_dev;
    ComPtr<ID3D12CommandAllocator> m_qalloc[2];
    ComPtr<ID3D12CommandQueue> m_q;
    ComPtr<ID3D12CommandAllocator> m_loadqalloc;
    ComPtr<ID3D12CommandQueue> m_loadq;
    ComPtr<ID3D12Fence> m_loadfence;
    UINT64 m_loadfenceval = 0;
    HANDLE m_loadfencehandle;
    ComPtr<ID3D12GraphicsCommandList> m_loadlist;
    ComPtr<ID3D12RootSignature> m_rs;
    struct Window
    {
        ComPtr<IDXGISwapChain3> m_swapChain;
        UINT m_backBuf = 0;
        bool m_needsResize = false;
        size_t width, height;
    };
    std::unordered_map<const boo::IWindow*, Window> m_windows;
};

#elif _WIN32_WINNT_WIN7
#include <dxgi1_2.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wingdi.h>
#else
#error Unsupported Windows target
#endif

struct D3D11Context
{
    ComPtr<IDXGIFactory2> m_dxFactory;
    ComPtr<ID3D11Device1> m_dev;
    ComPtr<ID3D11DeviceContext1> m_devCtx;
    ComPtr<ID3D11SamplerState> m_ss;
    struct Window
    {
        ComPtr<IDXGISwapChain1> m_swapChain;
        bool m_needsResize = false;
        size_t width, height;

        bool m_needsFSTransition = false;
        bool m_fs = false;
        DXGI_MODE_DESC m_fsdesc = {};
    };
    std::unordered_map<const boo::IWindow*, Window> m_windows;
};

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

struct Boo3DAppContext
{
    D3D11Context m_ctx11;
#if _WIN32_WINNT_WIN10
    D3D12Context m_ctx12;
#endif
    OGLContext m_ctxOgl;

    void resize(boo::IWindow* window, size_t width, size_t height)
    {
#if _WIN32_WINNT_WIN10
        if (m_ctx12.m_dev)
        {
            D3D12Context::Window& win = m_ctx12.m_windows[window];
            win.width = width;
            win.height = height;
            win.m_needsResize = true;
        }
        else
#endif
        {
            D3D11Context::Window& win = m_ctx11.m_windows[window];
            win.width = width;
            win.height = height;
            win.m_needsResize = true;
        }
    }

    bool isFullscreen(const boo::IWindow* window)
    {
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

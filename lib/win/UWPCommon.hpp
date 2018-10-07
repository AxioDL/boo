#pragma once

#include "WinCommon.hpp"

struct Boo3DAppContextUWP : Boo3DAppContext
{
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
        return false;
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
        return false;
    }
};


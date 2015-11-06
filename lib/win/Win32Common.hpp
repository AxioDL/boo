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

#include "boo/IWindow.hpp"

namespace boo {class IWindow;}

#if _WIN32_WINNT_WIN10
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d11_1.h>

struct D3D12Context
{
    ComPtr<IDXGIFactory4> m_dxFactory;
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
    };
    std::unordered_map<const boo::IWindow*, Window> m_windows;
};

struct D3DAppContext
{
    D3D11Context m_ctx11;
#if _WIN32_WINNT_WIN10
    D3D12Context m_ctx12;
#endif

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
            return isFScr;
        }
        else
#endif
        {
            D3D11Context::Window& win = m_ctx11.m_windows[window];
            BOOL isFScr;
            win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
            return isFScr;
        }
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
                DXGI_MODE_DESC mdesc = {outDesc.DesktopCoordinates.right, outDesc.DesktopCoordinates.bottom};
                win.m_swapChain->ResizeTarget(&mdesc);
            }
            else
                win.m_swapChain->SetFullscreenState(false, nullptr);
            return true;
        }
        else
#endif
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

                win.m_swapChain->SetFullscreenState(true, nullptr);
                DXGI_MODE_DESC mdesc = {outDesc.DesktopCoordinates.right, outDesc.DesktopCoordinates.bottom};
                win.m_swapChain->ResizeTarget(&mdesc);
            }
            else
                win.m_swapChain->SetFullscreenState(false, nullptr);
            return true;
        }
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

#endif // BOO_WIN32COMMON_HPP

#ifndef BOO_WI2COMMON_HPP
#define BOO_WINCOMMON_HPP

#include <unordered_map>
#include "boo/IWindow.hpp"

namespace boo {class IWindow;}

#if _WIN32_WINNT_WIN10
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wingdi.h>


#elif _WIN32_WINNT_WIN7
#include <dxgi1_2.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wingdi.h>
#else
#error Unsupported Windows target
#endif

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

struct D3D11Context
{
    ComPtr<IDXGIFactory2> m_dxFactory;
    ComPtr<ID3D11Device1> m_dev;
    ComPtr<ID3D11DeviceContext1> m_devCtx;
    ComPtr<ID3D11SamplerState> m_ss[2];
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

struct Boo3DAppContext
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
};

static inline std::string WCSTMBS(const wchar_t* wstr)
{
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr) - 1;
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

#endif // BOO_WINCOMMON_HPP

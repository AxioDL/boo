#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "boo/IWindow.hpp"

namespace boo {
class IWindow;
}

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

#include <d3d9.h>

using pD3DPERF_BeginEvent = int (WINAPI*)(D3DCOLOR col, LPCWSTR wszName);
using pD3DPERF_EndEvent = int (WINAPI*)();

struct D3D12Context {
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
  struct Window {
    ComPtr<IDXGISwapChain3> m_swapChain;
    std::unordered_map<ID3D12Resource*, ComPtr<ID3D12DescriptorHeap>> m_rtvHeaps;
    UINT m_backBuf = 0;
    bool m_needsResize = false;
    size_t width, height;
  };
  std::unordered_map<const boo::IWindow*, Window> m_windows;

  uint32_t m_sampleCount = 1;
  uint32_t m_anisotropy = 1;

  struct RGBATex2DFBViewDesc : D3D12_SHADER_RESOURCE_VIEW_DESC {
    RGBATex2DFBViewDesc() {
      Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      Texture2D = {UINT(0), UINT(1), UINT(0), 0.0f};
    }
  } RGBATex2DFBViewDesc;
};

struct D3D11Context {
  ComPtr<IDXGIFactory2> m_dxFactory;
  ComPtr<ID3D11Device1> m_dev;
  ComPtr<ID3D11DeviceContext1> m_devCtx;
  ComPtr<ID3D11SamplerState> m_ss[5];
  struct Window {
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11Texture2D> m_swapChainTex;
    ComPtr<ID3D11RenderTargetView> m_swapChainRTV;
    bool m_needsResize = false;
    size_t width, height;

    bool m_needsFSTransition = false;
    bool m_fs = false;
    DXGI_MODE_DESC m_fsdesc = {};

    void clearRTV() {
      m_swapChainTex.Reset();
      m_swapChainRTV.Reset();
    }

    void setupRTV(ComPtr<IDXGISwapChain1>& sc, ID3D11Device* dev) {
      m_swapChain = sc;
      m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &m_swapChainTex);
      D3D11_TEXTURE2D_DESC resDesc;
      m_swapChainTex->GetDesc(&resDesc);
      width = resDesc.Width;
      height = resDesc.Height;
      CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(D3D11_RTV_DIMENSION_TEXTURE2D, resDesc.Format);
      dev->CreateRenderTargetView(m_swapChainTex.Get(), &rtvDesc, &m_swapChainRTV);
    }
  };
  std::unordered_map<const boo::IWindow*, Window> m_windows;

  uint32_t m_sampleCount = 1;
  uint32_t m_anisotropy = 1;
  DXGI_FORMAT m_fbFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};

struct Boo3DAppContext {
  D3D11Context m_ctx11;

  void resize(boo::IWindow* window, size_t width, size_t height) {
    D3D11Context::Window& win = m_ctx11.m_windows[window];
    win.width = width;
    win.height = height;
    win.m_needsResize = true;
  }
};

inline std::string WCSTMBS(const wchar_t* wstr) {
  int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr) - 1;
  std::string strTo(sizeNeeded, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], sizeNeeded, nullptr, nullptr);
  return strTo;
}

inline std::wstring MBSTWCS(const char* str) {
  int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0) - 1;
  std::wstring strTo(sizeNeeded, 0);
  MultiByteToWideChar(CP_UTF8, 0, str, -1, &strTo[0], sizeNeeded);
  return strTo;
}

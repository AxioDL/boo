#pragma once

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
#if BOO_HAS_GL
#include "boo/graphicsdev/GL.hpp"
#endif

#include <condition_variable>
#include <mutex>

extern DWORD g_mainThreadId;
extern std::mutex g_nwmt;
extern std::condition_variable g_nwcv;

#if _WIN32_WINNT_WINBLUE && !WINDOWS_STORE
#include <ShellScalingApi.h>
using PFN_GetScaleFactorForMonitor = HRESULT(WINAPI*)(_In_ HMONITOR, _Out_ DEVICE_SCALE_FACTOR*);
extern PFN_GetScaleFactorForMonitor MyGetScaleFactorForMonitor;
#endif

struct OGLContext {
  ComPtr<IDXGIFactory1> m_dxFactory;
  HGLRC m_lastContext = nullptr;
  struct Window {
    HWND m_hwnd;
    HDC m_deviceContext;
    HGLRC m_mainContext;
    HGLRC m_renderContext;
    bool m_needsResize = false;
    size_t width, height;

    bool m_fs = false;
    LONG_PTR m_fsStyle;
    LONG_PTR m_fsExStyle;
    RECT m_fsRect;
    int m_fsCountDown = 0;
  };
  std::unordered_map<const boo::IWindow*, Window> m_windows;
#if BOO_HAS_GL
  boo::GLContext m_glCtx;
#endif
};

#if !WINDOWS_STORE
static inline void SetFullscreen(OGLContext::Window& win, bool fs) {
  std::unique_lock<std::mutex> lk(g_nwmt);
  PostThreadMessageW(g_mainThreadId, WM_USER + 5, WPARAM(&win), LPARAM(fs));
  g_nwcv.wait(lk);
}

#if BOO_HAS_VULKAN
static inline void SetFullscreen(boo::VulkanContext::Window& win, bool fs) {
  std::unique_lock<std::mutex> lk(g_nwmt);
  PostThreadMessageW(g_mainThreadId, WM_USER + 6, WPARAM(&win), LPARAM(fs));
  g_nwcv.wait(lk);
}
#endif
#endif

struct Boo3DAppContextWin32 : Boo3DAppContext {
  OGLContext m_ctxOgl;
  ComPtr<IDXGIFactory1> m_vulkanDxFactory;

  bool isFullscreen(const boo::IWindow* window) {
#if BOO_HAS_VULKAN
    if (m_vulkanDxFactory) {
      boo::VulkanContext::Window& win = *boo::g_VulkanContext.m_windows[window];
      return win.m_fs;
    }
#endif

    if (m_ctx11.m_dev) {
      D3D11Context::Window& win = m_ctx11.m_windows[window];
      BOOL isFScr;
      win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
      return isFScr != 0;
    }
    OGLContext::Window& win = m_ctxOgl.m_windows[window];
    return win.m_fs;
  }

  bool setFullscreen(boo::IWindow* window, bool fs) {
#if BOO_HAS_VULKAN
    if (m_vulkanDxFactory) {
      boo::VulkanContext::Window& win = *boo::g_VulkanContext.m_windows[window];
      if (fs && win.m_fs)
        return false;
      else if (!fs && !win.m_fs)
        return false;
      SetFullscreen(win, fs);
      return true;
    }
#endif

    if (m_ctx11.m_dev) {
      D3D11Context::Window& win = m_ctx11.m_windows[window];
      BOOL isFScr;
      win.m_swapChain->GetFullscreenState(&isFScr, nullptr);
      if (fs && isFScr)
        return false;
      else if (!fs && !isFScr)
        return false;

      if (fs) {
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

#if !WINDOWS_STORE
    OGLContext::Window& win = m_ctxOgl.m_windows[window];
    if (fs && win.m_fs)
      return false;
    else if (!fs && !win.m_fs)
      return false;
    SetFullscreen(win, fs);
#endif
    return true;
  }
};

struct HWNDEvent {
  UINT uMsg;
  WPARAM wParam;
  LPARAM lParam;
  HWNDEvent(UINT m, WPARAM w, LPARAM l) : uMsg(m), wParam(w), lParam(l) {}
};

struct Win32Cursors {
  HCURSOR m_arrow;
  HCURSOR m_weResize;
  HCURSOR m_nsResize;
  HCURSOR m_ibeam;
  HCURSOR m_crosshairs;
  HCURSOR m_wait;
  HCURSOR m_nwseResize;
  HCURSOR m_neswResize;
  HCURSOR m_hand;
  HCURSOR m_notAllowed;
};
namespace boo {
extern Win32Cursors WIN32_CURSORS;
}

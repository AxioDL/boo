#include "windowsys/IGraphicsContext.hpp"
#include "windowsys/IWindow.hpp"

#include <xcb/xcb.h>
#include <xcb/glx.h>
#include <GL/glx.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <stdlib.h>

namespace boo
{

class CGraphicsContextXCB final : public IGraphicsContext
{
    
    EGraphicsAPI m_api;
    EPixelFormat m_pf;
    IWindow* m_parentWindow;
    xcb_connection_t* m_xcbConn;

    xcb_glx_fbconfig_t m_fbconfig = 0;
    xcb_visualid_t m_visualid = 0;
    xcb_glx_window_t m_glxWindow = 0;
    xcb_glx_context_t m_glxCtx = 0;
    xcb_glx_context_tag_t m_glxCtxTag = 0;
    
public:
    IWindowCallback* m_callback;
    
    CGraphicsContextXCB(EGraphicsAPI api, IWindow* parentWindow, xcb_connection_t* conn, uint32_t& visualIdOut)
    : m_api(api),
      m_pf(PF_RGBA8),
      m_parentWindow(parentWindow),
      m_xcbConn(conn)
    {
        /* WTF freedesktop?? Fix this awful API and your nonexistant docs */
        xcb_glx_get_fb_configs_reply_t* fbconfigs =
        xcb_glx_get_fb_configs_reply(m_xcbConn, xcb_glx_get_fb_configs(m_xcbConn, 0), NULL);
        struct conf_prop
        {
            uint32_t key;
            uint32_t val;
        }* props = (struct conf_prop*)xcb_glx_get_fb_configs_property_list(fbconfigs);

        for (uint32_t i=0 ; i<fbconfigs->num_FB_configs ; ++i)
        {
            struct conf_prop* configProps = &props[fbconfigs->num_properties * i];
            uint32_t fbconfId, visualId, depthSize, colorSize, doubleBuffer;
            for (uint32_t j=0 ; j<fbconfigs->num_properties ; ++j)
            {
                struct conf_prop* prop = &configProps[j];
                if (prop->key == GLX_FBCONFIG_ID)
                    fbconfId = prop->val;
                if (prop->key == GLX_VISUAL_ID)
                    visualId = prop->val;
                else if (prop->key == GLX_DEPTH_SIZE)
                    depthSize = prop->val;
                else if (prop->key == GLX_BUFFER_SIZE)
                    colorSize = prop->val;
                else if (prop->key == GLX_DOUBLEBUFFER)
                    doubleBuffer = prop->val;
            }

            /* Double-buffer only */
            if (!doubleBuffer)
                continue;

            if (m_pf == PF_RGBA8 && colorSize >= 32)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBA8_Z24 && colorSize >= 32 && depthSize >= 24)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBAF32 && colorSize >= 128)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
            else if (m_pf == PF_RGBAF32_Z24 && colorSize >= 128 && depthSize >= 24)
            {
                m_fbconfig = fbconfId;
                m_visualid = visualId;
                break;
            }
        }
        free(fbconfigs);

        if (!m_fbconfig)
        {
            throw std::runtime_error("unable to find suitable pixel format");
            return;
        }

        visualIdOut = m_visualid;
    }
    
    ~CGraphicsContextXCB()
    {
        
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
    
    void initializeContext()
    {
        m_glxWindow = xcb_generate_id(m_xcbConn);
        xcb_glx_create_window(m_xcbConn, 0, m_fbconfig,
                              m_parentWindow->getPlatformHandle(),
                              m_glxWindow, 0, NULL);
    }
    
    IGraphicsContext* makeShareContext() const
    {
        return NULL;
    }
    
    void makeCurrent()
    {
        xcb_glx_make_context_current_reply_t* reply =
        xcb_glx_make_context_current_reply(m_xcbConn,
        xcb_glx_make_context_current(m_xcbConn, 0, m_glxWindow, m_glxWindow, m_glxCtx), NULL);
        m_glxCtxTag = reply->context_tag;
        free(reply);
    }
    
    void clearCurrent()
    {
        xcb_glx_make_context_current(m_xcbConn, m_glxCtxTag, m_glxWindow, m_glxWindow, 0);
        m_glxCtxTag = 0;
    }
    
    void swapBuffer()
    {
        xcb_glx_swap_buffers(m_xcbConn, m_glxCtxTag, m_glxWindow);
    }
    
};

IGraphicsContext* _CGraphicsContextXCBNew(IGraphicsContext::EGraphicsAPI api,
                                          IWindow* parentWindow, xcb_connection_t* conn,
                                          uint32_t& visualIdOut)
{
    return new CGraphicsContextXCB(api, parentWindow, conn, visualIdOut);
}
    
}

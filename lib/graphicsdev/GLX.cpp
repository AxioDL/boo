#include "boo/graphicsdev/glxew.h"
#include "logvisor/logvisor.hpp"

namespace boo
{
static logvisor::Module Log("boo::GLX");

void GLXExtensionCheck()
{
    if (!GLXEW_SGI_video_sync)
        Log.report(logvisor::Fatal, "GLX_SGI_video_sync not available");
    if (!GLXEW_EXT_swap_control && !GLXEW_MESA_swap_control && !GLXEW_SGI_swap_control)
        Log.report(logvisor::Fatal, "swap_control not available");
}

void GLXEnableVSync(Display* disp, GLXWindow drawable)
{
    if (GLXEW_EXT_swap_control)
        glXSwapIntervalEXT(disp, drawable, 1);
    else if (GLXEW_MESA_swap_control)
        glXSwapIntervalMESA(1);
    else if (GLXEW_SGI_swap_control)
        glXSwapIntervalSGI(1);
}

}

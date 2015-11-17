#include "boo/graphicsdev/glxew.h"
#include <LogVisor/LogVisor.hpp>

namespace boo
{
static LogVisor::LogModule Log("boo::GLX");

void GLXExtensionCheck()
{
    if (!GLXEW_SGI_video_sync)
        Log.report(LogVisor::FatalError, "GLX_SGI_video_sync not available");
    if (!GLXEW_EXT_swap_control && !GLXEW_MESA_swap_control && !GLXEW_SGI_swap_control)
        Log.report(LogVisor::FatalError, "swap_control not available");
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

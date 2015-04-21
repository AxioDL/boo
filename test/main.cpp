
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <boo.hpp>

int main(int argc, char** argv)
{
    CDeviceFinder finder(DEV_DOL_SMASH_ADAPTER);
    CDeviceToken& smashToken = finder.getTokens().begin()->second;
    CDolphinSmashAdapter* smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(smashToken.openAndGetDevice());
    
    IGraphicsContext* ctx = new CGraphicsContext;

    if (ctx->create())
    {
    }
    
    CFRunLoopRun();

    delete ctx;
    return 0;
}

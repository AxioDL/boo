
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <boo.hpp>

class CTestDeviceFinder : public CDeviceFinder
{
    CDolphinSmashAdapter* smashAdapter = NULL;
public:
    CTestDeviceFinder()
    : CDeviceFinder(DEV_DOL_SMASH_ADAPTER)
    {}
    void deviceConnected(CDeviceToken& tok)
    {
        smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(tok.openAndGetDevice());
    }
    void deviceDisconnected(CDeviceToken&, CDeviceBase* device)
    {
        if (smashAdapter == device)
        {
            delete smashAdapter;
            smashAdapter = NULL;
        }
    }
};

int main(int argc, char** argv)
{
    CTestDeviceFinder finder;
    finder.startScanning();
    
    IGraphicsContext* ctx = new CGraphicsContext;

    if (ctx->create())
    {
    }
    
    CFRunLoopRun();

    delete ctx;
    return 0;
}

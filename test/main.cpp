
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
        printf("CONNECTED %s %s\n", tok.getVendorName().c_str(), tok.getProductName().c_str());
        smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(tok.openAndGetDevice());
    }
    void deviceDisconnected(CDeviceToken& tok)
    {
        printf("DISCONNECTED %s %s\n", tok.getVendorName().c_str(), tok.getProductName().c_str());
        delete smashAdapter;
        smashAdapter = NULL;
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

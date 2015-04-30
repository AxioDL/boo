
#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#else
#endif
#include <stdio.h>
#include <unistd.h>
#include <boo.hpp>

namespace boo
{

class CDolphinSmashAdapterCallback : public IDolphinSmashAdapterCallback
{
    void controllerConnected(unsigned idx, EDolphinControllerType type)
    {
        printf("CONTROLLER %u CONNECTED\n", idx);
    }
    void controllerDisconnected(unsigned idx, EDolphinControllerType type)
    {
        printf("CONTROLLER %u DISCONNECTED\n", idx);
    }
    void controllerUpdate(unsigned idx, EDolphinControllerType type,
                          const SDolphinControllerState& state)
    {
        printf("CONTROLLER %u UPDATE %d %d\n", idx, state.m_leftStick[0], state.m_leftStick[1]);
    }
};

class CTestDeviceFinder : public CDeviceFinder
{
    CDolphinSmashAdapter* smashAdapter = NULL;
    CDolphinSmashAdapterCallback m_cb;
public:
    CTestDeviceFinder()
        : CDeviceFinder({"CDolphinSmashAdapter"})
    {}
    void deviceConnected(CDeviceToken& tok)
    {
        smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(tok.openAndGetDevice());
        smashAdapter->setCallback(&m_cb);
        smashAdapter->startRumble(0);
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

}

int main(int argc, char** argv)
{
    boo::CTestDeviceFinder finder;
    finder.startScanning();
    
    boo::IGraphicsContext* ctx = new boo::CGraphicsContext;

    if (ctx->create())
    {
    }
    
#if __APPLE__
    CFRunLoopRun();
#else
    while (true) {sleep(1);}
#endif

    delete ctx;
    return 0;
}

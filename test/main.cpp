
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <boo.hpp>

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
    : CDeviceFinder(DEV_DOL_SMASH_ADAPTER)
    {}
    void deviceConnected(CDeviceToken& tok)
    {
        smashAdapter = dynamic_cast<CDolphinSmashAdapter*>(tok.openAndGetDevice());
        smashAdapter->setCallback(&m_cb);
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

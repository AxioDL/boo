#include <stdio.h>
#include <boo.hpp>

namespace boo
{

class CDolphinSmashAdapterCallback : public IDolphinSmashAdapterCallback
{
    void controllerConnected(unsigned idx, EDolphinControllerType)
    {
        printf("CONTROLLER %u CONNECTED\n", idx);
    }
    void controllerDisconnected(unsigned idx, EDolphinControllerType)
    {
        printf("CONTROLLER %u DISCONNECTED\n", idx);
    }
    void controllerUpdate(unsigned idx, EDolphinControllerType,
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
        : CDeviceFinder({typeid(CDolphinSmashAdapter)})
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
    
    
struct CTestApplicationCallback : public IApplicationCallback
{
    IWindow* mainWindow = NULL;
    boo::CTestDeviceFinder devFinder;
    void appLaunched(IApplication* app)
    {
        mainWindow = app->newWindow("YAY!");
        mainWindow->showWindow();
        devFinder.startScanning();
    }
    void appQuitting(IApplication*)
    {
        delete mainWindow;
    }
    bool appFileOpen(IApplication*, const std::string& path)
    {
        printf("OPENING: %s\n", path.c_str());
        return true;
    }
};

}

int main(int argc, char** argv)
{
    boo::CTestApplicationCallback appCb;
    boo::IApplication* app = IApplicationBootstrap(boo::IApplication::PLAT_AUTO, appCb, "RWK", argc, argv);
    app->run();
    delete app;
    return 0;
}


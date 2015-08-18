#include <stdio.h>
#include <boo/boo.hpp>

namespace boo
{

class DolphinSmashAdapterCallback : public IDolphinSmashAdapterCallback
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
                          const DolphinControllerState& state)
    {
        printf("CONTROLLER %u UPDATE %d %d\n", idx, state.m_leftStick[0], state.m_leftStick[1]);
    }
};

class TestDeviceFinder : public DeviceFinder
{
    DolphinSmashAdapter* smashAdapter = NULL;
    DolphinSmashAdapterCallback m_cb;
public:
    TestDeviceFinder()
    : DeviceFinder({typeid(DolphinSmashAdapter)})
    {}
    void deviceConnected(DeviceToken& tok)
    {
        smashAdapter = dynamic_cast<DolphinSmashAdapter*>(tok.openAndGetDevice());
        smashAdapter->setCallback(&m_cb);
        smashAdapter->startRumble(0);
    }
    void deviceDisconnected(DeviceToken&, DeviceBase* device)
    {
        if (smashAdapter == device)
        {
            delete smashAdapter;
            smashAdapter = NULL;
        }
    }
};


struct CTestWindowCallback : IWindowCallback
{

    void mouseDown(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {
        fprintf(stderr, "Mouse Down %d (%f,%f)\n", button, coord.norm[0], coord.norm[1]);
    }
    void mouseUp(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {
        fprintf(stderr, "Mouse Up %d (%f,%f)\n", button, coord.norm[0], coord.norm[1]);
    }
    void mouseMove(const SWindowCoord& coord)
    {
        //fprintf(stderr, "Mouse Move (%f,%f)\n", coord.norm[0], coord.norm[1]);
    }
    void scroll(const SWindowCoord& coord, const SScrollDelta& scroll)
    {
        fprintf(stderr, "Mouse Scroll (%f,%f) (%f,%f)\n", coord.norm[0], coord.norm[1], scroll.delta[0], scroll.delta[1]);
    }

    void touchDown(const STouchCoord& coord, uintptr_t tid)
    {
        //fprintf(stderr, "Touch Down %16lX (%f,%f)\n", tid, coord.coord[0], coord.coord[1]);
    }
    void touchUp(const STouchCoord& coord, uintptr_t tid)
    {
        //fprintf(stderr, "Touch Up %16lX (%f,%f)\n", tid, coord.coord[0], coord.coord[1]);
    }
    void touchMove(const STouchCoord& coord, uintptr_t tid)
    {
        //fprintf(stderr, "Touch Move %16lX (%f,%f)\n", tid, coord.coord[0], coord.coord[1]);
    }

    void charKeyDown(unsigned long charCode, EModifierKey mods, bool isRepeat)
    {

    }
    void charKeyUp(unsigned long charCode, EModifierKey mods)
    {

    }
    void specialKeyDown(ESpecialKey key, EModifierKey mods, bool isRepeat)
    {

    }
    void specialKeyUp(ESpecialKey key, EModifierKey mods)
    {

    }
    void modKeyDown(EModifierKey mod, bool isRepeat)
    {

    }
    void modKeyUp(EModifierKey mod)
    {

    }

};

    
struct TestApplicationCallback : IApplicationCallback
{
    IWindow* mainWindow = NULL;
    boo::TestDeviceFinder devFinder;
    CTestWindowCallback windowCallback;
    void appLaunched(IApplication* app)
    {
        mainWindow = app->newWindow("YAY!");
        mainWindow->setCallback(&windowCallback);
        mainWindow->showWindow();
        devFinder.startScanning();
    }
    void appQuitting(IApplication*)
    {
        delete mainWindow;
    }
    void appFilesOpen(IApplication*, const std::vector<std::string>& paths)
    {
        fprintf(stderr, "OPENING: ");
        for (const std::string& path : paths)
            fprintf(stderr, "%s ", path.c_str());
        fprintf(stderr, "\n");
    }
};

}

int main(int argc, const char** argv)
{
    boo::TestApplicationCallback appCb;
    std::shared_ptr<boo::IApplication> app =
            ApplicationBootstrap(boo::IApplication::PLAT_AUTO,
                                 appCb, "rwk", "RWK", argc, argv);
    app->run();
    printf("IM DYING!!\n");
    return 0;
}



#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#else
#endif
#include <stdio.h>
#include <boo.hpp>
#if _WIN32
#define _WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#else
#include <unistd.h>
#endif

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

#if _WIN32
extern "C" int genWin32ShellExecute(const wchar_t* AppFullPath,
                                    const wchar_t* Verb,
                                    const wchar_t* Params,
                                    bool ShowAppWindow,
                                    bool WaitToFinish);
#include <libwdi/libwdi.h>
static void scanWinUSB()
{
    struct wdi_device_info *device, *list;
    struct wdi_options_create_list WDI_LIST_OPTS =
    {
        true, false, true
    };
    int err = wdi_create_list(&list, &WDI_LIST_OPTS);
    if (err == WDI_SUCCESS)
    {
        for (device = list; device != NULL; device = device->next)
        {
            if (device->vid == 0x57E && device->pid == 0x337 &&
                !strcmp(device->driver, "HidUsb"))
            {
                printf("GC adapter detected; installing driver\n");

            }
        }
        wdi_destroy_list(list);
    }
}
#endif

int main(int argc, char** argv)
{
#if _WIN32
    scanWinUSB();
#endif

    boo::CTestDeviceFinder finder;
    finder.startScanning();
    
#if 0
    boo::IGraphicsContext* ctx = new boo::CGraphicsContext;

    if (ctx->create())
    {
    }
#endif
    
#if __APPLE__
    CFRunLoopRun();
#elif _WIN32
    while (true) {Sleep(1000);}
#else
    while (true) {sleep(1);}
#endif

    //delete ctx;
    return 0;
}

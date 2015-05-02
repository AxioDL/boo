
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
static int genWin32ShellExecute(const wchar_t* AppFullPath,
                                const wchar_t* Verb,
                                const wchar_t* Params,
                                bool ShowAppWindow,
                                bool WaitToFinish)
{
    int Result = 0;

    // Setup the required structure
    SHELLEXECUTEINFO ShExecInfo;
    memset(&ShExecInfo, 0, sizeof(SHELLEXECUTEINFO));
    ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShExecInfo.hwnd = NULL;
    ShExecInfo.lpVerb = Verb;
    ShExecInfo.lpFile = AppFullPath;
    ShExecInfo.lpParameters = Params;
    ShExecInfo.lpDirectory = NULL;
    ShExecInfo.nShow = (ShowAppWindow ? SW_SHOW : SW_HIDE);
    ShExecInfo.hInstApp = NULL;

    // Spawn the process
    if (ShellExecuteEx(&ShExecInfo) == FALSE)
    {
        Result = -1; // Failed to execute process
    } else if (WaitToFinish)
    {
        WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
    }

    return Result;
}

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
                genWin32ShellExecute(L"WinUsbInstaller.exe", L"", L"", false, true);
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

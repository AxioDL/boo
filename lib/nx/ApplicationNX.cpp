#include "boo/IApplication.hpp"
#include "logvisor/logvisor.hpp"
#include "nxstl/thread"
#include "nxstl/condition_variable"
#include "boo/graphicsdev/NX.hpp"
#include <limits.h>

#include <switch.h>

namespace boo
{
static logvisor::Module Log("boo::NXApplication");

std::shared_ptr<IWindow> _WindowNXNew(std::string_view title, NXContext* nxCtx);

class ApplicationNX : public IApplication
{
    IApplicationCallback& m_callback;
    const std::string m_uniqueName;
    const std::string m_friendlyName;
    const std::string m_pname;
    const std::vector<std::string> m_args;

    NXContext m_nxCtx;

    void _deletedWindow(IWindow* window) {}

public:
    ApplicationNX(IApplicationCallback& callback,
                  std::string_view uniqueName,
                  std::string_view friendlyName,
                  std::string_view pname,
                  const std::vector<std::string>& args,
                  std::string_view gfxApi,
                  uint32_t samples,
                  uint32_t anisotropy,
                  bool deepColor,
                  bool singleInstance)
    : m_callback(callback),
      m_uniqueName(uniqueName),
      m_friendlyName(friendlyName),
      m_pname(pname),
      m_args(args)
    {}

    EPlatformType getPlatformType() const { return EPlatformType::NX; }

    int run()
    {
        /* Spawn client thread */
        int clientReturn = INT_MIN;
        std::mutex initmt;
        std::condition_variable initcv;
        std::unique_lock<std::mutex> outerLk(initmt);
        std::thread clientThread([&]()
        {
            std::unique_lock<std::mutex> innerLk(initmt);
            innerLk.unlock();
            initcv.notify_one();
            std::string thrName = std::string(getFriendlyName()) + " Client";
            logvisor::RegisterThreadName(thrName.c_str());
            clientReturn = m_callback.appMain(this);
        });
        initcv.wait(outerLk);

        // Main graphics loop
        while (clientReturn == INT_MIN && appletMainLoop())
        {
            // Get and process input
            hidScanInput();
            u32 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
            if (kDown & KEY_PLUS)
                break;
        }

        m_callback.appQuitting(this);
        if (clientThread.joinable())
            clientThread.join();

        return 0;
    }

    std::string_view getUniqueName() const
    {
        return m_uniqueName;
    }

    std::string_view getFriendlyName() const
    {
        return m_friendlyName;
    }

    std::string_view getProcessName() const
    {
        return m_pname;
    }

    const std::vector<std::string>& getArgs() const
    {
        return m_args;
    }

    std::shared_ptr<IWindow> m_window;
    std::shared_ptr<IWindow> newWindow(std::string_view title)
    {
        if (m_window)
            Log.report(logvisor::Fatal, "Only 1 window allowed on NX");
        m_window = _WindowNXNew(title, &m_nxCtx);
        return m_window;
    }
};

IApplication* APP = nullptr;
int ApplicationRun(IApplication::EPlatformType platform,
                   IApplicationCallback& cb,
                   SystemStringView uniqueName,
                   SystemStringView friendlyName,
                   SystemStringView pname,
                   const std::vector<SystemString>& args,
                   std::string_view gfxApi,
                   uint32_t samples,
                   uint32_t anisotropy,
                   bool deepColor,
                   bool singleInstance)
{
    std::string thrName = std::string(friendlyName) + " Main Thread";
    logvisor::RegisterThreadName(thrName.c_str());

    if (APP)
        return 1;
    APP = new ApplicationNX(cb, uniqueName, friendlyName, pname, args, gfxApi,
                            samples, anisotropy, deepColor, singleInstance);
    int ret = APP->run();
    delete APP;
    APP = nullptr;
    return ret;
}

}

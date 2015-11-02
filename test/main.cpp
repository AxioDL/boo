#include <stdio.h>
#include <math.h>
#include <boo/boo.hpp>
#include <boo/graphicsdev/GL.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <LogVisor/LogVisor.hpp>

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
        printf("                     %d %d\n", state.m_rightStick[0], state.m_rightStick[1]);
    }
};

class DualshockPadCallback : public IDualshockPadCallback
{
    void controllerDisconnected()
    {
        printf("CONTROLLER DISCONNECTED\n");
    }
    void controllerUpdate(const DualshockPadState& state)
    {
        static time_t timeTotal;
        static time_t lastTime = 0;
        timeTotal = time(NULL);
        time_t timeDif = timeTotal - lastTime;
        /*
        if (timeDif >= .15)
        {
            uint8_t led = ctrl->getLED();
            led *= 2;
            if (led > 0x10)
                led = 2;
            ctrl->setRawLED(led);
            lastTime = timeTotal;
        }
        */
        if (state.m_psButtonState)
        {
            if (timeDif >= 1) // wait 30 seconds before issuing another rumble event
            {
                ctrl->startRumble(DS3_MOTOR_LEFT);
                ctrl->startRumble(DS3_MOTOR_RIGHT, 100);
                lastTime = timeTotal;
            }
        }
        /*
        else
            ctrl->stopRumble(DS3_MOTOR_RIGHT | DS3_MOTOR_LEFT);*/

        printf("CONTROLLER UPDATE %d %d\n", state.m_leftStick[0], state.m_leftStick[1]);
        printf("                  %d %d\n", state.m_rightStick[0], state.m_rightStick[1]);
        printf("                  %f %f %f\n", state.accPitch, state.accYaw, state.gyroZ);
    }
};

class TestDeviceFinder : public DeviceFinder
{

    DolphinSmashAdapter* smashAdapter = NULL;
    DualshockPad* ds3 = nullptr;
    DolphinSmashAdapterCallback m_cb;
    DualshockPadCallback m_ds3CB;
public:
    TestDeviceFinder()
    : DeviceFinder({typeid(DolphinSmashAdapter)})
    {}
    void deviceConnected(DeviceToken& tok)
    {
        smashAdapter = dynamic_cast<DolphinSmashAdapter*>(tok.openAndGetDevice());
        if (smashAdapter)
        {
            smashAdapter->setCallback(&m_cb);
            smashAdapter->startRumble(0);
            return;
        }
        ds3 = dynamic_cast<DualshockPad*>(tok.openAndGetDevice());
        if (ds3)
        {
            ds3->setCallback(&m_ds3CB);
            ds3->setLED(DS3_LED_1);
        }
    }
    void deviceDisconnected(DeviceToken&, DeviceBase* device)
    {
        if (smashAdapter == device)
        {
            delete smashAdapter;
            smashAdapter = NULL;
        }
        if (ds3 == device)
        {
            delete ds3;
            ds3 = nullptr;
        }
    }
};


struct CTestWindowCallback : IWindowCallback
{
    void resized(const SWindowRect& rect)
    { fprintf(stderr, "Resized %d, %d (%d, %d)\n", rect.size[0], rect.size[1], rect.location[0], rect.location[1]); }

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
        fprintf(stderr, "Mouse Move (%f,%f)\n", coord.norm[0], coord.norm[1]);
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
    IWindow* mainWindow;
    boo::TestDeviceFinder devFinder;
    CTestWindowCallback windowCallback;
    bool running = true;

    const IShaderDataBinding* m_binding = nullptr;
    std::mutex m_mt;
    std::condition_variable m_cv;

    static void LoaderProc(TestApplicationCallback* self)
    {
        GLDataFactory* factory =
        dynamic_cast<GLDataFactory*>(self->mainWindow->getLoadContextDataFactory());

        /* Make Tri-strip VBO */
        struct Vert
        {
            float pos[3];
            float uv[2];
        };
        static const Vert quad[4] =
        {
            {{0.5,0.5},{1.0,1.0}},
            {{-0.5,0.5},{0.0,1.0}},
            {{0.5,-0.5},{1.0,0.0}},
            {{-0.5,-0.5},{0.0,0.0}}
        };
        IGraphicsBuffer* vbo =
        factory->newStaticBuffer(BufferUseVertex, quad, sizeof(quad), 4);

        /* Make vertex format */
        VertexElementDescriptor descs[2] =
        {
            {vbo, nullptr, VertexSemanticPosition},
            {vbo, nullptr, VertexSemanticUV}
        };
        IVertexFormat* vfmt = factory->newVertexFormat(2, descs);

        /* Make ramp texture */
        using Pixel = uint8_t[4];
        static Pixel tex[256][256];
        for (int i=0 ; i<256 ; ++i)
            for (int j=0 ; j<256 ; ++j)
            {
                tex[i][j][0] = i;
                tex[i][j][1] = j;
                tex[i][j][2] = 0;
                tex[i][j][3] = 0xff;
            }
        ITexture* texture =
        factory->newStaticTexture(256, 256, 1, TextureFormatRGBA8, tex, 256*256*4);

        /* Make shader pipeline */
        static const char* VS =
        "#version 330\n"
        "layout(location=0) in vec3 in_pos;\n"
        "layout(location=1) in vec2 in_uv;\n"
        "out vec2 out_uv;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = vec4(in_pos, 1.0);\n"
        "    out_uv = in_uv;\n"
        "}\n";

        static const char* FS =
        "#version 330\n"
        "precision highp float;\n"
        "uniform sampler2D smplr;\n"
        "layout(location=0) out vec4 out_frag;\n"
        "in vec2 out_uv;\n"
        "void main()\n"
        "{\n"
        "    out_frag = texture(smplr, out_uv);\n"
        "}\n";

        static const char* TexNames[] = {"smplr"};

        IShaderPipeline* pipeline =
        factory->newShaderPipeline(VS, FS, 1, TexNames, BlendFactorOne, BlendFactorZero, true, true, false);

        /* Make shader data binding */
        self->m_binding =
        factory->newShaderDataBinding(pipeline, vfmt, vbo, nullptr, 0, nullptr, 1, &texture);

        /* Commit objects */
        IGraphicsData* data = factory->commit();

        /* Wait for exit */
        while (self->running)
        {
            {
                std::unique_lock<std::mutex> lk(self->m_mt);
                self->m_cv.wait(lk);
                if (!self->running)
                    break;
            }
        }
    }

    int appMain(IApplication* app)
    {
        mainWindow = app->newWindow(_S("YAY!"));
        mainWindow->setCallback(&windowCallback);
        mainWindow->showWindow();
        //mainWindow->setFullscreen(true);
        devFinder.startScanning();

        IGraphicsCommandQueue* gfxQ = mainWindow->getCommandQueue();
        std::thread loaderThread(LoaderProc, this);

        size_t frameIdx = 0;
        size_t lastCheck = 0;
        while (running)
        {
            mainWindow->waitForRetrace();
            float rgba[] = {sinf(frameIdx / 60.0), cosf(frameIdx / 60.0), 0.0, 1.0};
            gfxQ->setClearColor(rgba);
            gfxQ->clearTarget();
            if (m_binding)
            {
                gfxQ->setDrawPrimitive(PrimitiveTriStrips);
                gfxQ->setShaderDataBinding(m_binding);
                gfxQ->draw(0, 4);
            }
            gfxQ->present();
            gfxQ->execute();

            //fprintf(stderr, "%zu\n", frameIdx);
            ++frameIdx;

            if ((frameIdx - lastCheck) > 100)
            {
                lastCheck = frameIdx;
                //mainWindow->setFullscreen(!mainWindow->isFullscreen());
            }
        }

        m_cv.notify_one();
        loaderThread.join();
        return 0;
    }
    void appQuitting(IApplication*)
    {
        running = false;
    }
    void appFilesOpen(IApplication*, const std::vector<SystemString>& paths)
    {
        fprintf(stderr, "OPENING: ");
        for (const SystemString& path : paths)
        {
#if _WIN32
            fwprintf(stderr, L"%s ", path.c_str());
#else
            fprintf(stderr, "%s ", path.c_str());
#endif
        }
        fprintf(stderr, "\n");
    }
};

}

#ifdef _WIN32
int wmain(int argc, const wchar_t** argv)
#else
int main(int argc, const char** argv)
#endif
{
    LogVisor::RegisterConsoleLogger();
    boo::TestApplicationCallback appCb;
    int ret = ApplicationRun(boo::IApplication::PLAT_AUTO,
                             appCb, _S("rwk"), _S("RWK"), argc, argv);
    printf("IM DYING!!\n");
    return ret;
}


#include <stdio.h>
#include <math.h>
#include <boo/boo.hpp>
#include <boo/graphicsdev/GL.hpp>
#include <boo/graphicsdev/Vulkan.hpp>
#include <boo/graphicsdev/D3D.hpp>
#include <boo/graphicsdev/Metal.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "logvisor/logvisor.hpp"

namespace boo
{

class DolphinSmashAdapterCallback : public IDolphinSmashAdapterCallback
{
    void controllerConnected(unsigned idx, EDolphinControllerType)
    {
//        printf("CONTROLLER %u CONNECTED\n", idx);
    }
    void controllerDisconnected(unsigned idx, EDolphinControllerType)
    {
//        printf("CONTROLLER %u DISCONNECTED\n", idx);
    }
    void controllerUpdate(unsigned idx, EDolphinControllerType,
                          const DolphinControllerState& state)
    {
//        printf("CONTROLLER %u UPDATE %d %d\n", idx, state.m_leftStick[0], state.m_leftStick[1]);
//        printf("                     %d %d\n", state.m_rightStick[0], state.m_rightStick[1]);
//        printf("                     %d %d\n", state.m_analogTriggers[0], state.m_analogTriggers[1]);
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
                ctrl->startRumble(EDualshockMotor::Left);
                ctrl->startRumble(EDualshockMotor::Right, 100);
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
            ds3->setLED(EDualshockLED::LED_1);
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
    bool m_fullscreenToggleRequested = false;
    SWindowRect m_lastRect;
    bool m_rectDirty = false;
    bool m_windowInvalid = false;

    void resized(const SWindowRect& rect)
    {
        m_lastRect = rect;
        m_rectDirty = true;
        fprintf(stderr, "Resized %d, %d (%d, %d)\n", rect.size[0], rect.size[1], rect.location[0], rect.location[1]);
    }

    void mouseDown(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {
        fprintf(stderr, "Mouse Down %d (%f,%f)\n", int(button), coord.norm[0], coord.norm[1]);
    }
    void mouseUp(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {
        fprintf(stderr, "Mouse Up %d (%f,%f)\n", int(button), coord.norm[0], coord.norm[1]);
    }
    void mouseMove(const SWindowCoord& coord)
    {
        fprintf(stderr, "Mouse Move (%f,%f)\n", coord.norm[0], coord.norm[1]);
    }
    void mouseEnter(const SWindowCoord &coord)
    {
        fprintf(stderr, "Mouse entered (%f,%f)\n", coord.norm[0], coord.norm[1]);
    }
    void mouseLeave(const SWindowCoord &coord)
    {
        fprintf(stderr, "Mouse left (%f,%f)\n", coord.norm[0], coord.norm[1]);
    }
    void scroll(const SWindowCoord& coord, const SScrollDelta& scroll)
    {
        //fprintf(stderr, "Mouse Scroll (%f,%f) (%f,%f)\n", coord.norm[0], coord.norm[1], scroll.delta[0], scroll.delta[1]);
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
        if (key == ESpecialKey::Enter && (mods & EModifierKey::Alt) != EModifierKey::None)
            m_fullscreenToggleRequested = true;
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

    void windowMoved(const SWindowRect& rect)
    {
        fprintf(stderr, "Moved %d, %d (%d, %d)\n", rect.size[0], rect.size[1], rect.location[0], rect.location[1]);
    }

    void destroyed()
    {
        m_windowInvalid = true;
    }

};

struct TestApplicationCallback : IApplicationCallback
{
    IWindow* mainWindow;
    boo::TestDeviceFinder devFinder;
    CTestWindowCallback windowCallback;
    bool running = true;

    IShaderDataBinding* m_binding = nullptr;
    ITextureR* m_renderTarget = nullptr;

    std::mutex m_mt;
    std::condition_variable m_cv;

    std::mutex m_initmt;
    std::condition_variable m_initcv;

    static GraphicsDataToken LoaderProc(TestApplicationCallback* self)
    {
        std::unique_lock<std::mutex> lk(self->m_initmt);

        IGraphicsDataFactory* factory = self->mainWindow->getLoadContextDataFactory();

        GraphicsDataToken data = factory->commitTransaction([&](IGraphicsDataFactory::Context& ctx) -> bool
        {
            /* Create render target */
            int x, y, w, h;
            self->mainWindow->getWindowFrame(x, y, w, h);
            self->m_renderTarget = ctx.newRenderTexture(w, h, false, false);

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
            ctx.newStaticBuffer(BufferUse::Vertex, quad, sizeof(Vert), 4);

            /* Make vertex format */
            VertexElementDescriptor descs[2] =
            {
                {vbo, nullptr, VertexSemantic::Position3},
                {vbo, nullptr, VertexSemantic::UV2}
            };
            IVertexFormat* vfmt = ctx.newVertexFormat(2, descs);

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
            ctx.newStaticTexture(256, 256, 1, TextureFormat::RGBA8, tex, 256*256*4);

            /* Make shader pipeline */
            IShaderPipeline* pipeline = nullptr;
            auto plat = ctx.platform();
            if (plat == IGraphicsDataFactory::Platform::OpenGL)
            {
                GLDataFactory::Context& glF = dynamic_cast<GLDataFactory::Context&>(ctx);

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
                BOO_GLSL_BINDING_HEAD
                "precision highp float;\n"
                "TBINDING0 uniform sampler2D tex;\n"
                "layout(location=0) out vec4 out_frag;\n"
                "in vec2 out_uv;\n"
                "void main()\n"
                "{\n"
                "    out_frag = texture(tex, out_uv);\n"
                "}\n";

                static const char* texName = "tex";

                pipeline = glF.newShaderPipeline(VS, FS, 1, &texName, 0, nullptr,
                                                 BlendFactor::One, BlendFactor::Zero,
                                                 Primitive::TriStrips, true, true, CullMode::None);
            }
#if BOO_HAS_VULKAN
            else if (plat == IGraphicsDataFactory::Platform::Vulkan)
            {
                VulkanDataFactory::Context& vkF = dynamic_cast<VulkanDataFactory::Context&>(ctx);

                static const char* VS =
                "#version 330\n"
                BOO_GLSL_BINDING_HEAD
                "layout(location=0) in vec3 in_pos;\n"
                "layout(location=1) in vec2 in_uv;\n"
                "SBINDING(0) out vec2 out_uv;\n"
                "void main()\n"
                "{\n"
                "    gl_Position = vec4(in_pos, 1.0);\n"
                "    out_uv = in_uv;\n"
                "}\n";

                static const char* FS =
                "#version 330\n"
                BOO_GLSL_BINDING_HEAD
                "precision highp float;\n"
                "TBINDING0 uniform sampler2D texs[1];\n"
                "layout(location=0) out vec4 out_frag;\n"
                "SBINDING(0) in vec2 out_uv;\n"
                "void main()\n"
                "{\n"
                "    out_frag = texture(texs[0], out_uv);\n"
                "}\n";

                pipeline = vkF.newShaderPipeline(VS, FS, vfmt, BlendFactor::One, BlendFactor::Zero,
                                                 Primitive::TriStrips, true, true, CullMode::None);
            }
#endif
#if _WIN32
            else if (plat == IGraphicsDataFactory::Platform::D3D12 ||
                     plat == IGraphicsDataFactory::Platform::D3D11)
            {
                ID3DDataFactory::Context& d3dF = dynamic_cast<ID3DDataFactory::Context&>(ctx);

                static const char* VS =
                    "struct VertData {float3 in_pos : POSITION; float2 in_uv : UV;};\n"
                    "struct VertToFrag {float4 out_pos : SV_Position; float2 out_uv : UV;};\n"
                    "VertToFrag main(in VertData v)\n"
                    "{\n"
                    "    VertToFrag retval;\n"
                    "    retval.out_pos = float4(v.in_pos, 1.0);\n"
                    "    retval.out_uv = v.in_uv;\n"
                    "    return retval;\n"
                    "}\n";

                static const char* PS =
                    "SamplerState samp : register(s0);\n"
                    "Texture2D tex : register(t0);\n"
                    "struct VertToFrag {float4 out_pos : SV_Position; float2 out_uv : UV;};\n"
                    "float4 main(in VertToFrag d) : SV_Target0\n"
                    "{\n"
                    "    return tex.Sample(samp, d.out_uv);\n"
                    "}\n";

                pipeline = d3dF.newShaderPipeline(VS, PS, nullptr, nullptr, nullptr, vfmt,
                                                  BlendFactor::One, BlendFactor::Zero, Primitive::TriStrips,
                                                  true, true, CullMode::None);
            }
#elif BOO_HAS_METAL
            else if (plat == IGraphicsDataFactory::Platform::Metal)
            {
                MetalDataFactory::Context& metalF = dynamic_cast<MetalDataFactory::Context&>(ctx);

                static const char* VS =
                "#include <metal_stdlib>\n"
                "using namespace metal;\n"
                "struct VertData {float3 in_pos [[ attribute(0) ]]; float2 in_uv [[ attribute(1) ]];};\n"
                "struct VertToFrag {float4 out_pos [[ position ]]; float2 out_uv;};\n"
                "vertex VertToFrag vmain(VertData v [[ stage_in ]])\n"
                "{\n"
                "    VertToFrag retval;\n"
                "    retval.out_pos = float4(v.in_pos, 1.0);\n"
                "    retval.out_uv = v.in_uv;\n"
                "    return retval;\n"
                "}\n";

                static const char* FS =
                "#include <metal_stdlib>\n"
                "using namespace metal;\n"
                "constexpr sampler samp(address::repeat);\n"
                "struct VertToFrag {float4 out_pos [[ position ]]; float2 out_uv;};\n"
                "fragment float4 fmain(VertToFrag d [[ stage_in ]], texture2d<float> tex [[ texture(0) ]])\n"
                "{\n"
                "    return tex.sample(samp, d.out_uv);\n"
                "}\n";

                pipeline = metalF.newShaderPipeline(VS, FS, vfmt, 1,
                                                    BlendFactor::One, BlendFactor::Zero, Primitive::TriStrips,
                                                    true, true, boo::CullMode::None);
            }
#endif

            /* Make shader data binding */
            self->m_binding =
            ctx.newShaderDataBinding(pipeline, vfmt, vbo, nullptr, nullptr, 0, nullptr, nullptr, 1, &texture);

            return true;
        });

        /* Return control to client */
        lk.unlock();
        self->m_initcv.notify_one();

        /* Wait for exit */
        while (self->running)
        {
            std::unique_lock<std::mutex> lk(self->m_mt);
            self->m_cv.wait(lk);
            if (!self->running)
                break;
        }
        return data;
    }

    int appMain(IApplication* app)
    {
        mainWindow = app->newWindow(_S("YAY!"), 1);
        mainWindow->setCallback(&windowCallback);
        mainWindow->showWindow();
        windowCallback.m_lastRect = mainWindow->getWindowFrame();
        //mainWindow->setFullscreen(true);
        devFinder.startScanning();

        IGraphicsCommandQueue* gfxQ = mainWindow->getCommandQueue();

        std::unique_lock<std::mutex> lk(m_initmt);
        std::thread loaderThread(LoaderProc, this);
        m_initcv.wait(lk);

        size_t frameIdx = 0;
        size_t lastCheck = 0;
        while (running)
        {
            if (windowCallback.m_windowInvalid)
            {
                running = false;
                break;
            }

            mainWindow->waitForRetrace();

            if (windowCallback.m_rectDirty)
            {
                gfxQ->resizeRenderTexture(m_renderTarget, windowCallback.m_lastRect.size[0], windowCallback.m_lastRect.size[1]);
                windowCallback.m_rectDirty = false;
            }

            if (windowCallback.m_fullscreenToggleRequested)
            {
                mainWindow->setFullscreen(!mainWindow->isFullscreen());
                windowCallback.m_fullscreenToggleRequested = false;
            }

            gfxQ->setRenderTarget(m_renderTarget);
            SWindowRect r = windowCallback.m_lastRect;
            r.location[0] = 0;
            r.location[1] = 0;
            gfxQ->setViewport(r);
            gfxQ->setScissor(r);
            float rgba[] = {std::max(0.f, sinf(frameIdx / 60.0)), std::max(0.f, cosf(frameIdx / 60.0)), 0.0, 1.0};
            gfxQ->setClearColor(rgba);
            gfxQ->clearTarget();

            gfxQ->setShaderDataBinding(m_binding);
            gfxQ->draw(0, 4);
            gfxQ->resolveDisplay(m_renderTarget);
            gfxQ->execute();

            //fprintf(stderr, "%zu\n", frameIdx);
            ++frameIdx;

            if ((frameIdx - lastCheck) > 100)
            {
                lastCheck = frameIdx;
                //mainWindow->setFullscreen(!mainWindow->isFullscreen());
            }
        }

        gfxQ->stopRenderer();
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

#if _WIN32
int wmain(int argc, const boo::SystemChar** argv)
#else
int main(int argc, const boo::SystemChar** argv)
#endif
{
    logvisor::RegisterStandardExceptions();
    logvisor::RegisterConsoleLogger();
    boo::TestApplicationCallback appCb;
    int ret = ApplicationRun(boo::IApplication::EPlatformType::Auto,
        appCb, _S("boo"), _S("boo"), argc, argv);
    printf("IM DYING!!\n");
    return ret;
}

#if _WIN32
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int)
{
    int argc = 0;
    const boo::SystemChar** argv = (const wchar_t**)(CommandLineToArgvW(lpCmdLine, &argc));
    static boo::SystemChar selfPath[1024];
    GetModuleFileNameW(nullptr, selfPath, 1024);
    static const boo::SystemChar* booArgv[32] = {};
    booArgv[0] = selfPath;
    for (int i=0 ; i<argc ; ++i)
        booArgv[i+1] = argv[i];

    logvisor::CreateWin32Console();
    return wmain(argc+1, booArgv);
}
#endif

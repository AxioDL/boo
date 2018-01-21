#include <cstdio>
#include <cmath>
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
    void controllerUpdate(DualshockPad& pad, const DualshockPadState& state)
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
                pad.startRumble(EDualshockMotor::Left);
                pad.startRumble(EDualshockMotor::Right, 100);
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

class GenericPadCallback : public IGenericPadCallback
{
    void controllerConnected()
    {
        printf("CONTROLLER CONNECTED\n");
    }
    void controllerDisconnected()
    {
        printf("CONTROLLER DISCONNECTED\n");
    }
    void valueUpdate(const HIDMainItem& item, int32_t value)
    {
        const char* pageName = item.GetUsagePageName();
        const char* usageName = item.GetUsageName();
        if (pageName)
        {
            if (usageName)
                printf("%s %s %d\n", pageName, usageName, int(value));
            else
                printf("%s %d %d\n", pageName, int(item.m_usage), int(value));
        }
        else
        {
            if (usageName)
                printf("page%d %s %d\n", int(item.m_usagePage), usageName, int(value));
            else
                printf("page%d %d %d\n", int(item.m_usagePage), int(item.m_usage), int(value));
        }
    }
};

class NintendoPowerACallback : public INintendoPowerACallback
{
    void controllerDisconnected()
    {
        fprintf(stderr, "CONTROLLER DISCONNECTED\n");
    }
    void controllerUpdate(const NintendoPowerAState& state)
    {
        fprintf(stderr, "%i %i\n"
                        "%i %i\n",
                state.leftX, state.leftY,
                state.rightX, state.rightY);
    }
};

class TestDeviceFinder : public DeviceFinder
{
    std::shared_ptr<DolphinSmashAdapter> m_smashAdapter;
    std::shared_ptr<NintendoPowerA> m_nintendoPowerA;
    std::shared_ptr<DualshockPad> m_ds3;
    std::shared_ptr<GenericPad> m_generic;
    DolphinSmashAdapterCallback m_cb;
    NintendoPowerACallback m_nintendoPowerACb;
    DualshockPadCallback m_ds3CB;
    GenericPadCallback m_genericCb;
public:
    TestDeviceFinder()
    : DeviceFinder({typeid(DolphinSmashAdapter), typeid(NintendoPowerA), typeid(GenericPad)})
    {}
    void deviceConnected(DeviceToken& tok)
    {
        m_smashAdapter = std::dynamic_pointer_cast<DolphinSmashAdapter>(tok.openAndGetDevice());
        if (m_smashAdapter)
        {
            m_smashAdapter->setCallback(&m_cb);
            m_smashAdapter->startRumble(0);
            return;
        }
        m_nintendoPowerA = std::dynamic_pointer_cast<NintendoPowerA>(tok.openAndGetDevice());
        if (m_nintendoPowerA)
        {
            m_nintendoPowerA->setCallback(&m_nintendoPowerACb);
            return;
        }
        m_ds3 = std::dynamic_pointer_cast<DualshockPad>(tok.openAndGetDevice());
        if (m_ds3)
        {
            m_ds3->setCallback(&m_ds3CB);
            m_ds3->setLED(EDualshockLED::LED_1);
            return;
        }
        m_generic = std::dynamic_pointer_cast<GenericPad>(tok.openAndGetDevice());
        if (m_generic)
        {
            m_generic->setCallback(&m_genericCb);
            return;
        }
    }
    void deviceDisconnected(DeviceToken&, DeviceBase* device)
    {
        if (m_smashAdapter.get() == device)
            m_smashAdapter.reset();
        if (m_ds3.get() == device)
            m_ds3.reset();
        if (m_generic.get() == device)
            m_generic.reset();
        if (m_nintendoPowerA.get() == device)
            m_nintendoPowerA.reset();
    }
};


struct CTestWindowCallback : IWindowCallback
{
    bool m_fullscreenToggleRequested = false;
    SWindowRect m_lastRect;
    bool m_rectDirty = false;
    bool m_windowInvalid = false;

    void resized(const SWindowRect& rect, bool sync)
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
        //fprintf(stderr, "Mouse Move (%f,%f)\n", coord.norm[0], coord.norm[1]);
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
        //fprintf(stderr, "Moved %d, %d (%d, %d)\n", rect.size[0], rect.size[1], rect.location[0], rect.location[1]);
    }

    void destroyed()
    {
        m_windowInvalid = true;
    }

};

struct TestApplicationCallback : IApplicationCallback
{
    std::shared_ptr<IWindow> mainWindow;
    boo::TestDeviceFinder devFinder;
    CTestWindowCallback windowCallback;
    bool running = true;

    boo::ObjToken<IShaderDataBinding> m_binding;
    boo::ObjToken<ITextureR> m_renderTarget;

    static void LoaderProc(TestApplicationCallback* self)
    {
        IGraphicsDataFactory* factory = self->mainWindow->getLoadContextDataFactory();

        factory->commitTransaction([&](IGraphicsDataFactory::Context& ctx) -> bool
        {
            /* Create render target */
            int x, y, w, h;
            self->mainWindow->getWindowFrame(x, y, w, h);
            self->m_renderTarget = ctx.newRenderTexture(w, h, boo::TextureClampMode::ClampToEdge, 1, 0);

            /* Make Tri-strip VBO */
            struct Vert
            {
                float pos[3];
                float uv[2];
            };
            /*
            static const Vert quad[4] =
            {
                {{0.5,0.5},{1.0,1.0}},
                {{-0.5,0.5},{0.0,1.0}},
                {{0.5,-0.5},{1.0,0.0}},
                {{-0.5,-0.5},{0.0,0.0}}
            };
             */
            static const Vert quad[4] =
            {
                {{1.0,1.0},{1.0,1.0}},
                {{-1.0,1.0},{0.0,1.0}},
                {{1.0,-1.0},{1.0,0.0}},
                {{-1.0,-1.0},{0.0,0.0}}
            };
            auto vbo = ctx.newStaticBuffer(BufferUse::Vertex, quad, sizeof(Vert), 4);

            /* Make vertex format */
            VertexElementDescriptor descs[2] =
            {
                {vbo.get(), nullptr, VertexSemantic::Position3},
                {vbo.get(), nullptr, VertexSemantic::UV2}
            };
            auto vfmt = ctx.newVertexFormat(2, descs);

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
            boo::ObjToken<ITexture> texture = ctx.newStaticTexture(256, 256, 1, TextureFormat::RGBA8,
                                              boo::TextureClampMode::ClampToEdge, tex, 256*256*4).get();

            /* Make shader pipeline */
            boo::ObjToken<IShaderPipeline> pipeline;
            auto plat = ctx.platform();
#if BOO_HAS_GL
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
                "    //out_frag = texture(tex, out_uv);\n"
                "    out_frag = vec4(out_uv.xy, 0.0, 1.0);\n"
                "}\n";

                static const char* texName = "tex";

                pipeline = glF.newShaderPipeline(VS, FS, 1, &texName, 0, nullptr,
                                                 BlendFactor::One, BlendFactor::Zero,
                                                 Primitive::TriStrips, boo::ZTest::LEqual,
                                                 true, true, false, CullMode::None);
            } else
#endif
#if BOO_HAS_VULKAN
            if (plat == IGraphicsDataFactory::Platform::Vulkan)
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
                                                 Primitive::TriStrips, boo::ZTest::LEqual,
                                                 true, true, false, CullMode::None);
            } else
#endif
#if _WIN32
            if (plat == IGraphicsDataFactory::Platform::D3D12 ||
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
                    "    //return tex.Sample(samp, d.out_uv);\n"
                    "    return float4(d.out_uv.xy, 0.0, 1.0);\n"
                    "}\n";

                pipeline = d3dF.newShaderPipeline(VS, PS, nullptr, nullptr, nullptr, vfmt,
                                                  BlendFactor::One, BlendFactor::Zero,
                                                  Primitive::TriStrips, boo::ZTest::LEqual,
                                                  true, true, false, CullMode::None);
            } else
#elif BOO_HAS_METAL
            if (plat == IGraphicsDataFactory::Platform::Metal)
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
                "struct VertToFrag {float4 out_pos [[ position ]]; float2 out_uv;};\n"
                "fragment float4 fmain(VertToFrag d [[ stage_in ]],\n"
                "                      sampler samp [[ sampler(2) ]],\n"
                "                      texture2d<float> tex [[ texture(0) ]])\n"
                "{\n"
                "    return tex.sample(samp, d.out_uv);\n"
                "}\n";

                pipeline = metalF.newShaderPipeline(VS, FS, nullptr, nullptr, vfmt,
                                                    BlendFactor::One, BlendFactor::Zero, Primitive::TriStrips,
                                                    boo::ZTest::LEqual, true, true, true, boo::CullMode::None);
            } else
#endif
            {}

            /* Make shader data binding */
            self->m_binding =
            ctx.newShaderDataBinding(pipeline, vfmt, vbo.get(), nullptr, nullptr, 0, nullptr, nullptr,
                                     1, &texture, nullptr, nullptr);

            return true;
        });
    }

    int appMain(IApplication* app)
    {
        mainWindow = app->newWindow(_S("YAY!"));
        mainWindow->setCallback(&windowCallback);
        mainWindow->showWindow();
        windowCallback.m_lastRect = mainWindow->getWindowFrame();
        //mainWindow->setFullscreen(true);
        devFinder.startScanning();

        IGraphicsCommandQueue* gfxQ = mainWindow->getCommandQueue();

        LoaderProc(this);

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
            //float rgba[] = {std::max(0.f, sinf(frameIdx / 60.0)), std::max(0.f, cosf(frameIdx / 60.0)), 0.0, 1.0};
            float gammaT = sinf(frameIdx / 60.0) + 1.f;
            if (gammaT < 1.f)
                gammaT = gammaT * 0.5f + 0.5f;
            //printf("%f\n", gammaT);
            mainWindow->getDataFactory()->setDisplayGamma(gammaT);
            //gfxQ->setClearColor(rgba);
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
        m_renderTarget.reset();
        m_binding.reset();
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

#if !WINDOWS_STORE
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
        appCb, _S("boo"), _S("boo"), argc, argv, {}, 1, 1, true);
    printf("IM DYING!!\n");
    return ret;
}

#else
using namespace Windows::ApplicationModel::Core;

[Platform::MTAThread]
int WINAPIV main(Platform::Array<Platform::String^>^ params)
{
    logvisor::RegisterStandardExceptions();
    logvisor::RegisterConsoleLogger();
    boo::TestApplicationCallback appCb;
    boo::ViewProvider^ viewProvider =
        ref new boo::ViewProvider(appCb, _S("boo"), _S("boo"), _S("boo"), params, false);
    CoreApplication::Run(viewProvider);
    return 0;
}
#endif

#if _WIN32 && !WINDOWS_STORE
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

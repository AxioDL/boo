#include "boo/graphicsdev/Vulkan.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <LogVisor/LogVisor.hpp>

#undef min
#undef max

static const TBuiltInResource DefaultBuiltInResource =
{
    32,
    6,
    32,
    32,
    64,
    4096,
    64,
    32,
    80,
    32,
    4096,
    32,
    128,
    8,
    16,
    16,
    15,
    -8,
    7,
    8,
    65535,
    65535,
    65535,
    1024,
    1024,
    64,
    1024,
    16,
    8,
    8,
    1,
    60,
    64,
    64,
    128,
    128,
    8,
    8,
    8,
    0,
    0,
    0,
    0,
    0,
    8,
    8,
    16,
    256,
    1024,
    1024,
    64,
    128,
    128,
    16,
    1024,
    4096,
    128,
    128,
    16,
    1024,
    120,
    32,
    64,
    16,
    0,
    0,
    0,
    0,
    8,
    8,
    1,
    0,
    0,
    0,
    0,
    1,
    1,
    16384,
    4,
    64,
    8,
    8,
    4,

    {
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1
    }
};

namespace boo
{
static LogVisor::LogModule Log("boo::Vulkan");

IShaderPipeline* VulkanDataFactory::newShaderPipeline
(const char* vertSource, const char* fragSource,
 std::vector<unsigned int>& vertBlobOut, std::vector<unsigned int>& fragBlobOut,
 std::vector<unsigned int>& pipelineBlob,
 size_t texCount, const char* texArrayName,
 size_t uniformBlockCount, const char** uniformBlockNames,
 BlendFactor srcFac, BlendFactor dstFac,
 bool depthTest, bool depthWrite, bool backfaceCulling)
{
    if (vertBlobOut.empty() || fragBlobOut.empty())
    {
        glslang::TShader vs(EShLangVertex);
        vs.setStrings(&vertSource, 1);
        if (!vs.parse(&DefaultBuiltInResource, 110, true, EShMsgDefault))
        {
            Log.report(LogVisor::Error, "unable to compile vertex shader\n%s", vs.getInfoLog());
            return nullptr;
        }

        glslang::TShader fs(EShLangFragment);
        fs.setStrings(&fragSource, 1);
        if (!fs.parse(&DefaultBuiltInResource, 110, true, EShMsgDefault))
        {
            Log.report(LogVisor::Error, "unable to compile fragment shader\n%s", fs.getInfoLog());
            return nullptr;
        }

        glslang::TProgram prog;
        prog.addShader(&vs);
        prog.addShader(&fs);
        if (!prog.link(EShMsgDefault))
        {
            Log.report(LogVisor::Error, "unable to link shader program\n%s", prog.getInfoLog());
            return nullptr;
        }

        glslang::GlslangToSpv(*prog.getIntermediate(EShLangVertex), vertBlobOut);
        glslang::GlslangToSpv(*prog.getIntermediate(EShLangFragment), fragBlobOut);
    }

    /* TODO: The actual Vulkan API stuff */

    return nullptr;
}

}

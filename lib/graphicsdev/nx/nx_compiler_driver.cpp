#include "boo/graphicsdev/nx_compiler.hpp"

int main(int argc, char** argv)
{
    nx_compiler c;
    c.initialize();

    nx_shader_stage_object objs[] =
    {
        c.compile(nx_shader_stage::VERTEX,
                  "#version 330\n"
                  "#extension GL_ARB_separate_shader_objects: enable\n"
                  "#extension GL_ARB_shading_language_420pack: enable\n"
                  "layout(location=0) in vec3 in_pos;\n"
                  "layout(location=1) in vec3 in_norm;\n"
                  "layout(location=2) in vec2 in_uv;\n"
                  "layout(location=0) out vec2 out_uv;\n"
                  "void main()\n"
                  "{\n"
                  "    gl_Position = vec4(in_pos, 1.0).zyxx;\n"
                  "    out_uv = in_uv;\n"
                  "}"),
        c.compile(nx_shader_stage::FRAGMENT,
                  "#version 330\n"
                  "#extension GL_ARB_separate_shader_objects: enable\n"
                  "#extension GL_ARB_shading_language_420pack: enable\n"
                  "layout(binding=8) uniform sampler2D texs[2];\n"
                  "layout(location=0) out vec4 out_frag;\n"
                  "layout(location=0) in vec2 out_uv;\n"
                  "void main()\n"
                  "{\n"
                  "    out_frag = texture(texs[0], out_uv) + texture(texs[1], out_uv);\n"
                  "}")
    };

    std::string log;
    auto linkData = c.link(2, objs, &log);

    return 0;
}

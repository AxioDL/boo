#pragma once

#define BOO_GLSL_MAX_UNIFORM_COUNT 8
#define BOO_GLSL_MAX_TEXTURE_COUNT 12

#define BOO_GLSL_BINDING_HEAD                                                                                          \
  "#ifdef VULKAN\n"                                                                                                    \
  "#define gl_VertexID gl_VertexIndex\n"                                                                               \
  "#extension GL_ARB_separate_shader_objects: enable\n"                                                                \
  "#define SBINDING(idx) layout(location=idx)\n"                                                                       \
  "#else\n"                                                                                                            \
  "#define SBINDING(idx)\n"                                                                                            \
  "#endif\n"                                                                                                           \
  "#extension GL_ARB_shading_language_420pack: enable\n"                                                               \
  "#ifdef GL_ARB_shading_language_420pack\n"                                                                           \
  "#define UBINDING0 layout(binding=0)\n"                                                                              \
  "#define UBINDING1 layout(binding=1)\n"                                                                              \
  "#define UBINDING2 layout(binding=2)\n"                                                                              \
  "#define UBINDING3 layout(binding=3)\n"                                                                              \
  "#define UBINDING4 layout(binding=4)\n"                                                                              \
  "#define UBINDING5 layout(binding=5)\n"                                                                              \
  "#define UBINDING6 layout(binding=6)\n"                                                                              \
  "#define UBINDING7 layout(binding=7)\n"                                                                              \
  "#define TBINDING0 layout(binding=8)\n"                                                                              \
  "#define TBINDING1 layout(binding=9)\n"                                                                              \
  "#define TBINDING2 layout(binding=10)\n"                                                                             \
  "#define TBINDING3 layout(binding=11)\n"                                                                             \
  "#define TBINDING4 layout(binding=12)\n"                                                                             \
  "#define TBINDING5 layout(binding=13)\n"                                                                             \
  "#define TBINDING6 layout(binding=14)\n"                                                                             \
  "#define TBINDING7 layout(binding=15)\n"                                                                             \
  "#define TBINDING8 layout(binding=16)\n"                                                                             \
  "#define TBINDING9 layout(binding=17)\n"                                                                             \
  "#define TBINDING10 layout(binding=18)\n"                                                                            \
  "#define TBINDING11 layout(binding=19)\n"                                                                            \
  "#else\n"                                                                                                            \
  "#define UBINDING0\n"                                                                                                \
  "#define UBINDING1\n"                                                                                                \
  "#define UBINDING2\n"                                                                                                \
  "#define UBINDING3\n"                                                                                                \
  "#define UBINDING4\n"                                                                                                \
  "#define UBINDING5\n"                                                                                                \
  "#define UBINDING6\n"                                                                                                \
  "#define UBINDING7\n"                                                                                                \
  "#define TBINDING0\n"                                                                                                \
  "#define TBINDING1\n"                                                                                                \
  "#define TBINDING2\n"                                                                                                \
  "#define TBINDING3\n"                                                                                                \
  "#define TBINDING4\n"                                                                                                \
  "#define TBINDING5\n"                                                                                                \
  "#define TBINDING6\n"                                                                                                \
  "#define TBINDING7\n"                                                                                                \
  "#define TBINDING8\n"                                                                                                \
  "#define TBINDING9\n"                                                                                                \
  "#define TBINDING10\n"                                                                                               \
  "#define TBINDING11\n"                                                                                               \
  "#endif\n"

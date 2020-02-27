#include "boo/graphicsdev/GL.hpp"
#include "boo/graphicsdev/glew.h"
#include "boo/graphicsdev/GLSLMacros.hpp"

#include "boo/IApplication.hpp"
#include "boo/IGraphicsContext.hpp"
#include "lib/graphicsdev/Common.hpp"

#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Include/Types.h>
#include <StandAlone/ResourceLimits.h>

#include <logvisor/logvisor.hpp>

#if _WIN32
#include "lib/win/WinCommon.hpp"
#endif

#undef min
#undef max

constexpr char GammaVS[] = "#version 330\n" BOO_GLSL_BINDING_HEAD
                           "layout(location=0) in vec4 posIn;\n"
                           "layout(location=1) in vec4 uvIn;\n"
                           "\n"
                           "struct VertToFrag\n"
                           "{\n"
                           "    vec2 uv;\n"
                           "};\n"
                           "\n"
                           "SBINDING(0) out VertToFrag vtf;\n"
                           "void main()\n"
                           "{\n"
                           "    vtf.uv = uvIn.xy;\n"
                           "    gl_Position = posIn;\n"
                           "}\n";

constexpr char GammaFS[] = "#version 330\n" BOO_GLSL_BINDING_HEAD
                           "struct VertToFrag\n"
                           "{\n"
                           "    vec2 uv;\n"
                           "};\n"
                           "\n"
                           "SBINDING(0) in VertToFrag vtf;\n"
                           "layout(location=0) out vec4 colorOut;\n"
                           "TBINDING0 uniform sampler2D screenTex;\n"
                           "TBINDING1 uniform sampler2D gammaLUT;\n"
                           "void main()\n"
                           "{\n"
                           "    ivec4 tex = ivec4(texture(screenTex, vtf.uv) * 65535.0);\n"
                           "    for (int i=0 ; i<3 ; ++i)\n"
                           "        colorOut[i] = texelFetch(gammaLUT, ivec2(tex[i] % 256, tex[i] / 256), 0).r;\n"
                           "}\n";

namespace boo {
static logvisor::Module Log("boo::GL");
class GLDataFactoryImpl;
struct GLCommandQueue;

class GLDataFactoryImpl final : public GLDataFactory, public GraphicsDataFactoryHead {
  friend struct GLCommandQueue;
  friend class GLDataFactory::Context;
  IGraphicsContext* m_parent;
  GLContext* m_glCtx;

  bool m_hasTessellation = false;
  uint32_t m_maxPatchSize = 0;

  float m_gamma = 1.f;
  ObjToken<IShaderPipeline> m_gammaShader;
  ObjToken<ITextureD> m_gammaLUT;
  ObjToken<IGraphicsBufferS> m_gammaVBO;
  ObjToken<IShaderDataBinding> m_gammaBinding;
  void SetupGammaResources() {
    /* Good enough place for this */
    if (!glslang::InitializeProcess())
      Log.report(logvisor::Error, fmt("unable to initialize glslang"));

    if (GLEW_ARB_tessellation_shader) {
      m_hasTessellation = true;
      GLint maxPVerts = 0;
      glGetIntegerv(GL_MAX_PATCH_VERTICES, &maxPVerts);
      m_maxPatchSize = uint32_t(maxPVerts);
    }

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    commitTransaction([this](IGraphicsDataFactory::Context& ctx) {
      auto vertex = ctx.newShaderStage(reinterpret_cast<const uint8_t*>(GammaVS), 0, PipelineStage::Vertex);
      auto fragment = ctx.newShaderStage(reinterpret_cast<const uint8_t*>(GammaFS), 0, PipelineStage::Fragment);
      const AdditionalPipelineInfo info = {
          BlendFactor::One, BlendFactor::Zero, Primitive::TriStrips, ZTest::None, false, true, false, CullMode::None};
      const std::array<VertexElementDescriptor, 2> vfmt{{{VertexSemantic::Position4}, {VertexSemantic::UV4}}};
      m_gammaShader = ctx.newShaderPipeline(std::move(vertex), std::move(fragment), vfmt.data(), info);
      m_gammaLUT = ctx.newDynamicTexture(256, 256, TextureFormat::I16, TextureClampMode::ClampToEdge);
      struct Vert {
        std::array<float, 4> pos;
        std::array<float, 4> uv;
      };
      constexpr std::array<Vert, 4> verts{{
          {{-1.f, -1.f, 0.f, 1.f}, {0.f, 0.f, 0.f, 0.f}},
          {{1.f, -1.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 0.f}},
          {{-1.f, 1.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 0.f}},
          {{1.f, 1.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 0.f}},
      }};
      m_gammaVBO = ctx.newStaticBuffer(BufferUse::Vertex, verts.data(), sizeof(Vert), verts.size());

      const std::array<ObjToken<ITexture>, 2> texs{{
          {},
          m_gammaLUT.get(),
      }};
      m_gammaBinding = ctx.newShaderDataBinding(m_gammaShader, m_gammaVBO.get(), {}, {}, 0, nullptr, nullptr,
                                                texs.size(), texs.data(), nullptr, nullptr);
      return true;
    } BooTrace);
  }
  void DestroyGammaResources() {
    m_gammaBinding.reset();
    m_gammaVBO.reset();
    m_gammaLUT.reset();
    m_gammaShader.reset();
  }

public:
  GLDataFactoryImpl(IGraphicsContext* parent, GLContext* glCtx) : m_parent(parent), m_glCtx(glCtx) {}

  Platform platform() const override { return Platform::OpenGL; }
  const SystemChar* platformName() const override { return _SYS_STR("OpenGL"); }
  void commitTransaction(const FactoryCommitFunc& trans __BooTraceArgs) override;
  ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count __BooTraceArgs) override;

  void setDisplayGamma(float gamma) override {
    m_gamma = gamma;
    if (gamma != 1.f)
      UpdateGammaLUT(m_gammaLUT.get(), gamma);
  }

  bool isTessellationSupported(uint32_t& maxPatchSizeOut) override {
    maxPatchSizeOut = m_maxPatchSize;
    return m_hasTessellation;
  }

  void waitUntilShadersReady() override {}

  bool areShadersReady() override { return true; }
};

constexpr std::array<GLenum, 4> USE_TABLE{
    GL_INVALID_ENUM,
    GL_ARRAY_BUFFER,
    GL_ELEMENT_ARRAY_BUFFER,
    GL_UNIFORM_BUFFER,
};

class GLGraphicsBufferS : public GraphicsDataNode<IGraphicsBufferS> {
  friend class GLDataFactory;
  friend struct GLCommandQueue;
  GLuint m_buf;
  GLenum m_target;
  GLGraphicsBufferS(const ObjToken<BaseGraphicsData>& parent, BufferUse use, const void* data, size_t sz)
  : GraphicsDataNode<IGraphicsBufferS>(parent) {
    m_target = USE_TABLE[int(use)];
    glGenBuffers(1, &m_buf);
    glBindBuffer(m_target, m_buf);
    glBufferData(m_target, sz, data, GL_STATIC_DRAW);
  }

public:
  ~GLGraphicsBufferS() override { glDeleteBuffers(1, &m_buf); }

  void bindVertex() const { glBindBuffer(GL_ARRAY_BUFFER, m_buf); }
  void bindIndex() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buf); }
  void bindUniform(size_t idx) const { glBindBufferBase(GL_UNIFORM_BUFFER, idx, m_buf); }
  void bindUniformRange(size_t idx, GLintptr off, GLsizeiptr size) const {
    glBindBufferRange(GL_UNIFORM_BUFFER, idx, m_buf, off, size);
  }
};

template <class DataCls>
class GLGraphicsBufferD : public GraphicsDataNode<IGraphicsBufferD, DataCls> {
  friend class GLDataFactory;
  friend class GLDataFactoryImpl;
  friend struct GLCommandQueue;
  std::array<GLuint, 3> m_bufs{};
  GLenum m_target;
  std::unique_ptr<uint8_t[]> m_cpuBuf;
  size_t m_cpuSz = 0;
  int m_validMask = 0;

  GLGraphicsBufferD(const ObjToken<DataCls>& parent, BufferUse use, size_t sz)
  : GraphicsDataNode<IGraphicsBufferD, DataCls>(parent)
  , m_target(USE_TABLE[int(use)])
  , m_cpuBuf(new uint8_t[sz])
  , m_cpuSz(sz) {
    glGenBuffers(GLsizei(m_bufs.size()), m_bufs.data());
    for (const GLuint buf : m_bufs) {
      glBindBuffer(m_target, buf);
      glBufferData(m_target, m_cpuSz, nullptr, GL_STREAM_DRAW);
    }
  }

public:
  ~GLGraphicsBufferD() override { glDeleteBuffers(GLsizei(m_bufs.size()), m_bufs.data()); }

  void update(int b) {
    int slot = 1 << b;
    if ((slot & m_validMask) == 0) {
      glBindBuffer(m_target, m_bufs[b]);
      glBufferSubData(m_target, 0, m_cpuSz, m_cpuBuf.get());
      m_validMask |= slot;
    }
  }

  void load(const void* data, size_t sz) override {
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validMask = 0;
  }
  void* map(size_t sz) override {
    if (sz > m_cpuSz)
      return nullptr;
    return m_cpuBuf.get();
  }
  void unmap() override { m_validMask = 0; }
  void bindVertex(int b) { glBindBuffer(GL_ARRAY_BUFFER, m_bufs[b]); }
  void bindIndex(int b) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_bufs[b]); }
  void bindUniform(size_t idx, int b) { glBindBufferBase(GL_UNIFORM_BUFFER, idx, m_bufs[b]); }
  void bindUniformRange(size_t idx, GLintptr off, GLsizeiptr size, int b) {
    glBindBufferRange(GL_UNIFORM_BUFFER, idx, m_bufs[b], off, size);
  }
};

ObjToken<IGraphicsBufferS> GLDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride,
                                                                   size_t count) {
  BOO_MSAN_NO_INTERCEPT
  return {new GLGraphicsBufferS(m_data, use, data, stride * count)};
}

static void SetClampMode(GLenum target, TextureClampMode clampMode) {
  switch (clampMode) {
  case TextureClampMode::Repeat: {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_REPEAT);
    break;
  }
  case TextureClampMode::ClampToWhite: {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    constexpr std::array<GLfloat, 4> color{1.f, 1.f, 1.f, 1.f};
    glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, color.data());
    break;
  }
  case TextureClampMode::ClampToBlack: {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    constexpr std::array<GLfloat, 4> color{0.f, 0.f, 0.f, 1.f};
    glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, color.data());
    break;
  }
  case TextureClampMode::ClampToEdge: {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    break;
  }
  case TextureClampMode::ClampToEdgeNearest: {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    break;
  }
  default:
    break;
  }
}

class GLTextureS : public GraphicsDataNode<ITextureS> {
  friend class GLDataFactory;
  GLuint m_tex;
  TextureClampMode m_clampMode = TextureClampMode::Invalid;
  GLTextureS(const ObjToken<BaseGraphicsData>& parent, size_t width, size_t height, size_t mips, TextureFormat fmt,
             TextureClampMode clampMode, GLint aniso, const void* data, size_t sz)
  : GraphicsDataNode<ITextureS>(parent) {
    const uint8_t* dataIt = static_cast<const uint8_t*>(data);
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips > 1) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mips - 1);
    } else
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    if (GLEW_EXT_texture_filter_anisotropic)
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);

    SetClampMode(GL_TEXTURE_2D, clampMode);

    GLenum intFormat, format;
    int pxPitch;
    bool compressed = false;
    switch (fmt) {
    case TextureFormat::RGBA8:
      intFormat = GL_RGBA8;
      format = GL_RGBA;
      pxPitch = 4;
      break;
    case TextureFormat::I8:
      intFormat = GL_R8;
      format = GL_RED;
      pxPitch = 1;
      break;
    case TextureFormat::I16:
      intFormat = GL_R16;
      format = GL_RED;
      pxPitch = 2;
      break;
    case TextureFormat::DXT1:
      intFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
      compressed = true;
      pxPitch = 2;
      break;
    case TextureFormat::DXT3:
      intFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
      compressed = true;
      pxPitch = 1;
      break;
    case TextureFormat::DXT5:
      intFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
      compressed = true;
      pxPitch = 1;
      break;
    case TextureFormat::BPTC:
      intFormat = GL_COMPRESSED_RGBA_BPTC_UNORM_ARB;
      compressed = true;
      pxPitch = 1;
      break;
    default:
      Log.report(logvisor::Fatal, fmt("unsupported tex format"));
    }

    if (compressed) {
      for (size_t i = 0; i < mips; ++i) {
        size_t dataSz = width * height / pxPitch;
        glCompressedTexImage2D(GL_TEXTURE_2D, i, intFormat, width, height, 0, dataSz, dataIt);
        dataIt += dataSz;
        if (width > 1)
          width /= 2;
        if (height > 1)
          height /= 2;
      }
    } else {
      GLenum compType = intFormat == GL_R16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
      for (size_t i = 0; i < mips; ++i) {
        glTexImage2D(GL_TEXTURE_2D, i, intFormat, width, height, 0, format, compType, dataIt);
        dataIt += width * height * pxPitch;
        if (width > 1)
          width /= 2;
        if (height > 1)
          height /= 2;
      }
    }
  }

public:
  ~GLTextureS() override { glDeleteTextures(1, &m_tex); }

  void setClampMode(TextureClampMode mode) override {
    if (m_clampMode == mode)
      return;
    m_clampMode = mode;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    SetClampMode(GL_TEXTURE_2D, mode);
  }

  void bind(size_t idx) const {
    glActiveTexture(GL_TEXTURE0 + idx);
    glBindTexture(GL_TEXTURE_2D, m_tex);
  }
};

class GLTextureSA : public GraphicsDataNode<ITextureSA> {
  friend class GLDataFactory;
  GLuint m_tex;
  TextureClampMode m_clampMode = TextureClampMode::Invalid;
  GLTextureSA(const ObjToken<BaseGraphicsData>& parent, size_t width, size_t height, size_t layers, size_t mips,
              TextureFormat fmt, TextureClampMode clampMode, GLint aniso, const void* data, size_t sz)
  : GraphicsDataNode<ITextureSA>(parent) {
    const uint8_t* dataIt = static_cast<const uint8_t*>(data);
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips > 1) {
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mips - 1);
    } else
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    if (GLEW_EXT_texture_filter_anisotropic)
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);

    SetClampMode(GL_TEXTURE_2D_ARRAY, clampMode);

    GLenum intFormat = 0, format = 0;
    int pxPitch = 0;
    switch (fmt) {
    case TextureFormat::RGBA8:
      intFormat = GL_RGBA8;
      format = GL_RGBA;
      pxPitch = 4;
      break;
    case TextureFormat::I8:
      intFormat = GL_R8;
      format = GL_RED;
      pxPitch = 1;
      break;
    case TextureFormat::I16:
      intFormat = GL_R16;
      format = GL_RED;
      pxPitch = 2;
      break;
    default:
      Log.report(logvisor::Fatal, fmt("unsupported tex format"));
    }

    GLenum compType = intFormat == GL_R16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    for (size_t i = 0; i < mips; ++i) {
      glTexImage3D(GL_TEXTURE_2D_ARRAY, i, intFormat, width, height, layers, 0, format, compType, dataIt);
      dataIt += width * height * layers * pxPitch;
      if (width > 1)
        width /= 2;
      if (height > 1)
        height /= 2;
    }
  }

public:
  ~GLTextureSA() override { glDeleteTextures(1, &m_tex); }

  void setClampMode(TextureClampMode mode) override {
    if (m_clampMode == mode)
      return;
    m_clampMode = mode;
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
    SetClampMode(GL_TEXTURE_2D_ARRAY, mode);
  }

  void bind(size_t idx) const {
    glActiveTexture(GL_TEXTURE0 + idx);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
  }
};

class GLTextureD : public GraphicsDataNode<ITextureD> {
  friend class GLDataFactory;
  friend struct GLCommandQueue;
  std::array<GLuint, 3> m_texs{};
  std::unique_ptr<uint8_t[]> m_cpuBuf;
  size_t m_cpuSz = 0;
  GLenum m_intFormat, m_format;
  size_t m_width = 0;
  size_t m_height = 0;
  int m_validMask = 0;
  TextureClampMode m_clampMode = TextureClampMode::Invalid;
  GLTextureD(const ObjToken<BaseGraphicsData>& parent, size_t width, size_t height, TextureFormat fmt,
             TextureClampMode clampMode)
  : GraphicsDataNode<ITextureD>(parent), m_width(width), m_height(height) {
    int pxPitch = 4;
    switch (fmt) {
    case TextureFormat::RGBA8:
      m_intFormat = GL_RGBA8;
      m_format = GL_RGBA;
      pxPitch = 4;
      break;
    case TextureFormat::I8:
      m_intFormat = GL_R8;
      m_format = GL_RED;
      pxPitch = 1;
      break;
    case TextureFormat::I16:
      m_intFormat = GL_R16;
      m_format = GL_RED;
      pxPitch = 2;
      break;
    default:
      Log.report(logvisor::Fatal, fmt("unsupported tex format"));
    }
    m_cpuSz = width * height * pxPitch;
    m_cpuBuf.reset(new uint8_t[m_cpuSz]);

    const GLenum compType = m_intFormat == GL_R16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    glGenTextures(GLsizei(m_texs.size()), m_texs.data());
    for (const GLuint tex : m_texs) {
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, m_intFormat, width, height, 0, m_format, compType, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      SetClampMode(GL_TEXTURE_2D, clampMode);
    }
  }

public:
  ~GLTextureD() override { glDeleteTextures(GLsizei(m_texs.size()), m_texs.data()); }

  void update(int b) {
    int slot = 1 << b;
    if ((slot & m_validMask) == 0) {
      glBindTexture(GL_TEXTURE_2D, m_texs[b]);
      GLenum compType = m_intFormat == GL_R16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
      glTexImage2D(GL_TEXTURE_2D, 0, m_intFormat, m_width, m_height, 0, m_format, compType, m_cpuBuf.get());
      m_validMask |= slot;
    }
  }

  void load(const void* data, size_t sz) override {
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validMask = 0;
  }
  void* map(size_t sz) override {
    if (sz > m_cpuSz)
      return nullptr;
    return m_cpuBuf.get();
  }
  void unmap() override { m_validMask = 0; }

  void setClampMode(TextureClampMode mode) override {
    if (m_clampMode == mode) {
      return;
    }

    m_clampMode = mode;
    for (const GLuint tex : m_texs) {
      glBindTexture(GL_TEXTURE_2D, tex);
      SetClampMode(GL_TEXTURE_2D, mode);
    }
  }

  void bind(size_t idx, int b) {
    glActiveTexture(GL_TEXTURE0 + idx);
    glBindTexture(GL_TEXTURE_2D, m_texs[b]);
  }
};

#define MAX_BIND_TEXS 4

class GLTextureR : public GraphicsDataNode<ITextureR> {
  friend class GLDataFactory;
  friend struct GLCommandQueue;
  struct GLCommandQueue* m_q;
  std::array<GLuint, 2> m_texs{};
  std::array<std::array<GLuint, MAX_BIND_TEXS>, 2> m_bindTexs{};
  std::array<std::array<GLuint, MAX_BIND_TEXS>, 2> m_bindFBOs{};
  GLuint m_fbo = 0;
  size_t m_width = 0;
  size_t m_height = 0;
  size_t m_samples = 0;
  GLenum m_colorFormat;
  size_t m_colorBindCount;
  size_t m_depthBindCount;
  GLTextureR(const ObjToken<BaseGraphicsData>& parent, GLCommandQueue* q, size_t width, size_t height, size_t samples,
             GLenum colorFormat, TextureClampMode clampMode, size_t colorBindCount, size_t depthBindCount);

public:
  ~GLTextureR() override {
    glDeleteTextures(GLsizei(m_texs.size()), m_texs.data());
    glDeleteTextures(MAX_BIND_TEXS * 2, m_bindTexs[0].data());
    if (m_samples > 1)
      glDeleteFramebuffers(MAX_BIND_TEXS * 2, m_bindFBOs[0].data());
    glDeleteFramebuffers(1, &m_fbo);
  }

  void setClampMode(TextureClampMode mode) override {
    for (size_t i = 0; i < m_colorBindCount; ++i) {
      glBindTexture(GL_TEXTURE_2D, m_bindTexs[0][i]);
      SetClampMode(GL_TEXTURE_2D, mode);
    }
    for (size_t i = 0; i < m_depthBindCount; ++i) {
      glBindTexture(GL_TEXTURE_2D, m_bindTexs[1][i]);
      SetClampMode(GL_TEXTURE_2D, mode);
    }
  }

  void bind(size_t idx, int bindIdx, bool depth) const {
    glActiveTexture(GL_TEXTURE0 + idx);
    glBindTexture(GL_TEXTURE_2D, m_bindTexs[depth][bindIdx]);
  }

  void resize(size_t width, size_t height) {
    m_width = width;
    m_height = height;

    GLenum compType = m_colorFormat == GL_RGBA16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    if (m_samples > 1) {
      glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[0]);
      glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_samples, m_colorFormat, width, height, GL_FALSE);
      glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[1]);
      glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_samples, GL_DEPTH_COMPONENT32F, width, height, GL_FALSE);
    } else {
      glBindTexture(GL_TEXTURE_2D, m_texs[0]);
      glTexImage2D(GL_TEXTURE_2D, 0, m_colorFormat, width, height, 0, GL_RGBA, compType, nullptr);
      glBindTexture(GL_TEXTURE_2D, m_texs[1]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                   nullptr);

      glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
      glDepthMask(GL_TRUE);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    for (const GLuint bindTex : m_bindTexs[0]) {
      if (bindTex == 0) {
        continue;
      }

      glBindTexture(GL_TEXTURE_2D, bindTex);
      glTexImage2D(GL_TEXTURE_2D, 0, m_colorFormat, width, height, 0, GL_RGBA, compType, nullptr);
    }

    for (const GLuint bindTex : m_bindTexs[1]) {
      if (bindTex == 0) {
        continue;
      }

      glBindTexture(GL_TEXTURE_2D, bindTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                   nullptr);
    }
  }
};

class GLTextureCubeR : public GraphicsDataNode<ITextureCubeR> {
  friend class GLDataFactory;
  friend struct GLCommandQueue;
  struct GLCommandQueue* m_q;
  std::array<GLuint, 2> m_texs{};
  std::array<GLuint, 6> m_fbos{};
  size_t m_width = 0;
  size_t m_mipCount = 0;
  GLenum m_colorFormat;
  GLTextureCubeR(const ObjToken<BaseGraphicsData>& parent, GLCommandQueue* q, size_t width, size_t mips, GLenum colorFormat);

public:
  ~GLTextureCubeR() override {
    glDeleteTextures(GLsizei(m_texs.size()), m_texs.data());
    glDeleteFramebuffers(GLsizei(m_fbos.size()), m_fbos.data());
  }

  void setClampMode(TextureClampMode mode) override {}

  void bind(size_t idx) const {
    glActiveTexture(GL_TEXTURE0 + idx);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_texs[0]);
  }

  void _allocateTextures() {
    GLenum compType = m_colorFormat == GL_RGBA16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

    glBindTexture(GL_TEXTURE_CUBE_MAP, m_texs[0]);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, m_mipCount - 1);
    for (size_t f = 0; f < m_fbos.size(); ++f) {
      size_t tmpWidth = m_width;
      for (size_t m = 0; m < m_mipCount; ++m) {
        glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f), m, m_colorFormat, tmpWidth, tmpWidth,
                     0, GL_RGBA, compType, nullptr);
        tmpWidth >>= 1;
      }
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, m_texs[1]);
    for (size_t f = 0; f < m_fbos.size(); ++f) {
      glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f), 0, GL_DEPTH_COMPONENT32F, m_width, m_width, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    }
  }

  void resize(size_t width, size_t mips) {
    m_width = width;
    m_mipCount = mips;
    _allocateTextures();

    for (const GLuint fbo : m_fbos) {
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glDepthMask(GL_TRUE);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
  }
};

ObjToken<ITextureS> GLDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips,
                                                             TextureFormat fmt, TextureClampMode clampMode,
                                                             const void* data, size_t sz) {
  GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
  BOO_MSAN_NO_INTERCEPT
  return {new GLTextureS(m_data, width, height, mips, fmt, clampMode, factory.m_glCtx->m_anisotropy, data, sz)};
}

ObjToken<ITextureSA> GLDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers,
                                                                   size_t mips, TextureFormat fmt,
                                                                   TextureClampMode clampMode, const void* data,
                                                                   size_t sz) {
  GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
  BOO_MSAN_NO_INTERCEPT
  return {
      new GLTextureSA(m_data, width, height, layers, mips, fmt, clampMode, factory.m_glCtx->m_anisotropy, data, sz)};
}

constexpr std::array<GLenum, 3> PRIMITIVE_TABLE{
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_PATCHES,
};

constexpr std::array<GLenum, 12> BLEND_FACTOR_TABLE{
    GL_ZERO,       GL_ONE,
    GL_SRC_COLOR,  GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,  GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,  GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,  GL_ONE_MINUS_DST_ALPHA,
    GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR,
};

constexpr std::array<GLenum, 6> SHADER_STAGE_TABLE{
    0, GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_GEOMETRY_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER,
};

class GLShaderStage : public GraphicsDataNode<IShaderStage> {
  friend class GLDataFactory;
  GLuint m_shad = 0;
  std::vector<std::pair<std::string, int>> m_texNames;
  std::vector<std::pair<std::string, int>> m_blockNames;

  static constexpr std::array<EShLanguage, 6> ShaderTypes{
      EShLangVertex, EShLangVertex, EShLangFragment, EShLangGeometry, EShLangTessControl, EShLangTessEvaluation,
  };

  /* Use glslang's reflection API to pull out uniform indices from Vulkan
   * version of shader. Aids in glGetUniformBlockIndex and glGetUniformLocation calls */
  void BuildNameLists(const char* source, PipelineStage stage) {
    EShLanguage lang = ShaderTypes[int(stage)];
    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader shader(lang);
    shader.setStrings(&source, 1);
    if (!shader.parse(&glslang::DefaultTBuiltInResource, 110, false, messages)) {
      fmt::print(fmt("{}\n"), source);
      Log.report(logvisor::Fatal, fmt("unable to compile shader\n{}"), shader.getInfoLog());
    }

    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(messages)) {
      fmt::print(fmt("{}\n"), source);
      Log.report(logvisor::Fatal, fmt("unable to link shader program\n{}"), prog.getInfoLog());
    }

    prog.buildReflection();
    int count = prog.getNumLiveUniformVariables();
    for (int i = 0; i < count; ++i) {
      const glslang::TType* tp = prog.getUniformTType(i);
      if (tp->getBasicType() != glslang::TBasicType::EbtSampler)
        continue;
      const auto& qual = tp->getQualifier();
      if (!qual.hasBinding())
        Log.report(logvisor::Fatal, fmt("shader uniform {} does not have layout binding"), prog.getUniformName(i));
      m_texNames.emplace_back(std::make_pair(prog.getUniformName(i), qual.layoutBinding - BOO_GLSL_MAX_UNIFORM_COUNT));
    }
    count = prog.getNumLiveUniformBlocks();
    m_blockNames.reserve(count);
    for (int i = 0; i < count; ++i) {
      const glslang::TType* tp = prog.getUniformBlockTType(i);
      const auto& qual = tp->getQualifier();
      if (!qual.hasBinding())
        Log.report(logvisor::Fatal, fmt("shader uniform {} does not have layout binding"), prog.getUniformBlockName(i));
      m_blockNames.emplace_back(std::make_pair(prog.getUniformBlockName(i), qual.layoutBinding));
    }
  }

  GLShaderStage(const ObjToken<BaseGraphicsData>& parent, const char* source, PipelineStage stage)
  : GraphicsDataNode<IShaderStage>(parent) {
    BuildNameLists(source, stage);

    m_shad = glCreateShader(SHADER_STAGE_TABLE[int(stage)]);
    if (!m_shad) {
      Log.report(logvisor::Fatal, fmt("unable to create shader"));
      return;
    }

    glShaderSource(m_shad, 1, &source, nullptr);
    glCompileShader(m_shad);
    GLint status = GL_FALSE;
    glGetShaderiv(m_shad, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
      GLint logLen;
      glGetShaderiv(m_shad, GL_INFO_LOG_LENGTH, &logLen);
      std::unique_ptr<char[]> log(new char[logLen]);
      glGetShaderInfoLog(m_shad, logLen, nullptr, log.get());
      Log.report(logvisor::Fatal, fmt("unable to compile source\n{}\n{}\n"), log.get(), source);
      return;
    }
  }

public:
  ~GLShaderStage() override {
    if (m_shad)
      glDeleteShader(m_shad);
  }
  GLuint getShader() const { return m_shad; }
  const std::vector<std::pair<std::string, int>>& getTexNames() const { return m_texNames; }
  const std::vector<std::pair<std::string, int>>& getBlockNames() const { return m_blockNames; }
};

class GLShaderPipeline : public GraphicsDataNode<IShaderPipeline> {
protected:
  friend class GLDataFactory;
  friend struct GLCommandQueue;
  friend struct GLShaderDataBinding;
  mutable ObjToken<IShaderStage> m_vertex;
  mutable ObjToken<IShaderStage> m_fragment;
  mutable ObjToken<IShaderStage> m_geometry;
  mutable ObjToken<IShaderStage> m_control;
  mutable ObjToken<IShaderStage> m_evaluation;
  std::vector<VertexElementDescriptor> m_elements;
  size_t baseVert = 0;
  size_t baseInst = 0;
  mutable GLuint m_prog = 0;
  GLenum m_sfactor = GL_ONE;
  GLenum m_dfactor = GL_ZERO;
  GLenum m_drawPrim = GL_TRIANGLES;
  ZTest m_depthTest = ZTest::LEqual;
  bool m_depthWrite = true;
  bool m_colorWrite = true;
  bool m_alphaWrite = true;
  bool m_subtractBlend = false;
  bool m_overwriteAlpha = false;
  CullMode m_culling{};
  uint32_t m_patchSize = 0;
  mutable std::array<GLint, BOO_GLSL_MAX_UNIFORM_COUNT> m_uniLocs{};
  GLShaderPipeline(const ObjToken<BaseGraphicsData>& parent, ObjToken<IShaderStage> vertex,
                   ObjToken<IShaderStage> fragment, ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                   ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                   const AdditionalPipelineInfo& info)
  : GraphicsDataNode<IShaderPipeline>(parent) {
    m_uniLocs.fill(-1);

    if (info.srcFac == BlendFactor::Subtract || info.dstFac == BlendFactor::Subtract) {
      m_sfactor = GL_SRC_ALPHA;
      m_dfactor = GL_ONE;
      m_subtractBlend = true;
    } else {
      m_sfactor = BLEND_FACTOR_TABLE[int(info.srcFac)];
      m_dfactor = BLEND_FACTOR_TABLE[int(info.dstFac)];
      m_subtractBlend = false;
    }

    m_depthTest = info.depthTest;
    m_depthWrite = info.depthWrite;
    m_colorWrite = info.colorWrite;
    m_alphaWrite = info.alphaWrite;
    m_overwriteAlpha = info.overwriteAlpha;
    m_culling = info.culling;
    m_drawPrim = PRIMITIVE_TABLE[int(info.prim)];
    m_patchSize = info.patchSize;

    m_vertex = vertex;
    m_fragment = fragment;
    m_geometry = geometry;
    m_control = control;
    m_evaluation = evaluation;

    if (control && evaluation)
      m_drawPrim = GL_PATCHES;

    m_elements.reserve(vtxFmt.elementCount);
    for (size_t i = 0; i < vtxFmt.elementCount; ++i)
      m_elements.push_back(vtxFmt.elements[i]);
  }

public:
  ~GLShaderPipeline() override {
    if (m_prog)
      glDeleteProgram(m_prog);
  }

  GLuint bind() const {
    if (!m_prog) {
      m_prog = glCreateProgram();
      if (!m_prog) {
        Log.report(logvisor::Error, fmt("unable to create shader program"));
        return 0;
      }

      if (m_vertex)
        glAttachShader(m_prog, m_vertex.cast<GLShaderStage>()->getShader());
      if (m_fragment)
        glAttachShader(m_prog, m_fragment.cast<GLShaderStage>()->getShader());
      if (m_geometry)
        glAttachShader(m_prog, m_geometry.cast<GLShaderStage>()->getShader());
      if (m_control)
        glAttachShader(m_prog, m_control.cast<GLShaderStage>()->getShader());
      if (m_evaluation)
        glAttachShader(m_prog, m_evaluation.cast<GLShaderStage>()->getShader());

      glLinkProgram(m_prog);

      if (m_vertex)
        glDetachShader(m_prog, m_vertex.cast<GLShaderStage>()->getShader());
      if (m_fragment)
        glDetachShader(m_prog, m_fragment.cast<GLShaderStage>()->getShader());
      if (m_geometry)
        glDetachShader(m_prog, m_geometry.cast<GLShaderStage>()->getShader());
      if (m_control)
        glDetachShader(m_prog, m_control.cast<GLShaderStage>()->getShader());
      if (m_evaluation)
        glDetachShader(m_prog, m_evaluation.cast<GLShaderStage>()->getShader());

      GLint status = GL_FALSE;
      glGetProgramiv(m_prog, GL_LINK_STATUS, &status);
      if (status != GL_TRUE) {
        GLint logLen;
        glGetProgramiv(m_prog, GL_INFO_LOG_LENGTH, &logLen);
        std::unique_ptr<char[]> log(new char[logLen]);
        glGetProgramInfoLog(m_prog, logLen, nullptr, log.get());
        Log.report(logvisor::Fatal, fmt("unable to link shader program\n{}\n"), log.get());
        return 0;
      }

      glUseProgram(m_prog);

      for (const auto& shader : {m_vertex, m_fragment, m_geometry, m_control, m_evaluation}) {
        if (const GLShaderStage* stage = shader.cast<GLShaderStage>()) {
          for (const auto& name : stage->getBlockNames()) {
            GLint uniLoc = glGetUniformBlockIndex(m_prog, name.first.c_str());
            // if (uniLoc < 0) {
            //    Log.report(logvisor::Warning, fmt("unable to find uniform block '{}'"), uniformBlockNames[i]);
            // }
            m_uniLocs[name.second] = uniLoc;
          }
          for (const auto& name : stage->getTexNames()) {
            GLint texLoc = glGetUniformLocation(m_prog, name.first.c_str());
            if (texLoc < 0) {
              // Log.report(logvisor::Warning, fmt("unable to find sampler variable '{}'"), texNames[i]);
            } else {
              glUniform1i(texLoc, name.second);
            }
          }
        }
      }

      m_vertex.reset();
      m_fragment.reset();
      m_geometry.reset();
      m_control.reset();
      m_evaluation.reset();
    } else {
      glUseProgram(m_prog);
    }

    if (m_dfactor != GL_ZERO) {
      glEnable(GL_BLEND);
      if (m_overwriteAlpha)
        glBlendFuncSeparate(m_sfactor, m_dfactor, GL_ONE, GL_ZERO);
      else
        glBlendFuncSeparate(m_sfactor, m_dfactor, m_sfactor, m_dfactor);
      if (m_subtractBlend)
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, m_overwriteAlpha ? GL_FUNC_ADD : GL_FUNC_REVERSE_SUBTRACT);
      else
        glBlendEquation(GL_FUNC_ADD);
    } else
      glDisable(GL_BLEND);

    if (m_depthTest != ZTest::None) {
      glEnable(GL_DEPTH_TEST);
      switch (m_depthTest) {
      case ZTest::LEqual:
      default:
        glDepthFunc(GL_LEQUAL);
        break;
      case ZTest::Greater:
        glDepthFunc(GL_GREATER);
        break;
      case ZTest::GEqual:
        glDepthFunc(GL_GEQUAL);
        break;
      case ZTest::Equal:
        glDepthFunc(GL_EQUAL);
        break;
      }
    } else
      glDisable(GL_DEPTH_TEST);
    glDepthMask(m_depthWrite);
    glColorMask(m_colorWrite, m_colorWrite, m_colorWrite, m_alphaWrite);

    if (m_culling != CullMode::None) {
      glEnable(GL_CULL_FACE);
      glCullFace(m_culling == CullMode::Backface ? GL_BACK : GL_FRONT);
    } else
      glDisable(GL_CULL_FACE);

    if (m_patchSize)
      glPatchParameteri(GL_PATCH_VERTICES, m_patchSize);

    return m_prog;
  }

  bool isReady() const override { return true; }
};

ObjToken<IShaderStage> GLDataFactory::Context::newShaderStage(const uint8_t* data, size_t size, PipelineStage stage) {
  const auto& factory = static_cast<GLDataFactoryImpl&>(m_parent);

  if (stage == PipelineStage::Control || stage == PipelineStage::Evaluation) {
    if (!factory.m_hasTessellation)
      Log.report(logvisor::Fatal, fmt("Device does not support tessellation shaders"));
  }

  BOO_MSAN_NO_INTERCEPT
  return {new GLShaderStage(m_data, reinterpret_cast<const char*>(data), stage)};
}

ObjToken<IShaderPipeline> GLDataFactory::Context::newShaderPipeline(
    ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment, ObjToken<IShaderStage> geometry,
    ObjToken<IShaderStage> control, ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
    const AdditionalPipelineInfo& additionalInfo, bool asynchronous) {
  const auto& factory = static_cast<GLDataFactoryImpl&>(m_parent);

  if (control || evaluation) {
    if (!factory.m_hasTessellation)
      Log.report(logvisor::Fatal, fmt("Device does not support tessellation shaders"));
    if (additionalInfo.patchSize > factory.m_maxPatchSize)
      Log.report(logvisor::Fatal, fmt("Device supports {} patch vertices, {} requested"), int(factory.m_maxPatchSize),
                 int(additionalInfo.patchSize));
  }

  BOO_MSAN_NO_INTERCEPT
  return {new GLShaderPipeline(m_data, vertex, fragment, geometry, control, evaluation, vtxFmt, additionalInfo)};
}

struct GLShaderDataBinding : GraphicsDataNode<IShaderDataBinding> {
  ObjToken<IShaderPipeline> m_pipeline;
  ObjToken<IGraphicsBuffer> m_vbo;
  ObjToken<IGraphicsBuffer> m_instVbo;
  ObjToken<IGraphicsBuffer> m_ibo;
  std::vector<ObjToken<IGraphicsBuffer>> m_ubufs;
  std::vector<std::pair<size_t, size_t>> m_ubufOffs;
  struct BoundTex {
    ObjToken<ITexture> tex;
    int idx;
    bool depth;
  };
  std::vector<BoundTex> m_texs;
  size_t m_baseVert;
  size_t m_baseInst;
  std::array<GLuint, 3> m_vao = {};
  GLCommandQueue* m_q;

  GLShaderDataBinding(const ObjToken<BaseGraphicsData>& d, const ObjToken<IShaderPipeline>& pipeline,
                      const ObjToken<IGraphicsBuffer>& vbo, const ObjToken<IGraphicsBuffer>& instVbo,
                      const ObjToken<IGraphicsBuffer>& ibo, size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs,
                      const size_t* ubufOffs, const size_t* ubufSizes, size_t texCount, const ObjToken<ITexture>* texs,
                      const int* bindTexIdx, const bool* depthBind, size_t baseVert, size_t baseInst,
                      GLCommandQueue* q);

  ~GLShaderDataBinding() override;

  void bind(int b) const {
    GLShaderPipeline& pipeline = *m_pipeline.cast<GLShaderPipeline>();
    GLuint prog = pipeline.bind();
    glBindVertexArray(m_vao[b]);
    if (m_ubufOffs.size()) {
      for (size_t i = 0; i < m_ubufs.size(); ++i) {
        GLint loc = pipeline.m_uniLocs[i];
        if (loc < 0)
          continue;
        IGraphicsBuffer* ubuf = m_ubufs[i].get();
        const std::pair<size_t, size_t>& offset = m_ubufOffs[i];
        if (ubuf->dynamic())
          static_cast<GLGraphicsBufferD<BaseGraphicsData>*>(ubuf)->bindUniformRange(i, offset.first, offset.second, b);
        else
          static_cast<GLGraphicsBufferS*>(ubuf)->bindUniformRange(i, offset.first, offset.second);
        glUniformBlockBinding(prog, loc, i);
      }
    } else {
      for (size_t i = 0; i < m_ubufs.size(); ++i) {
        GLint loc = pipeline.m_uniLocs[i];
        if (loc < 0)
          continue;
        IGraphicsBuffer* ubuf = m_ubufs[i].get();
        if (ubuf->dynamic())
          static_cast<GLGraphicsBufferD<BaseGraphicsData>*>(ubuf)->bindUniform(i, b);
        else
          static_cast<GLGraphicsBufferS*>(ubuf)->bindUniform(i);
        glUniformBlockBinding(prog, loc, i);
      }
    }
    for (size_t i = 0; i < m_texs.size(); ++i) {
      const BoundTex& tex = m_texs[i];
      if (tex.tex) {
        switch (tex.tex->type()) {
        case TextureType::Dynamic:
          tex.tex.cast<GLTextureD>()->bind(i, b);
          break;
        case TextureType::Static:
          tex.tex.cast<GLTextureS>()->bind(i);
          break;
        case TextureType::StaticArray:
          tex.tex.cast<GLTextureSA>()->bind(i);
          break;
        case TextureType::Render:
          tex.tex.cast<GLTextureR>()->bind(i, tex.idx, tex.depth);
          break;
        case TextureType::CubeRender:
          tex.tex.cast<GLTextureCubeR>()->bind(i);
          break;
        default:
          break;
        }
      }
    }
  }
};

GLDataFactory::Context::Context(GLDataFactory& parent __BooTraceArgs)
: m_parent(parent), m_data(new BaseGraphicsData(static_cast<GLDataFactoryImpl&>(parent) __BooTraceArgsUse)) {}

GLDataFactory::Context::~Context() {}

void GLDataFactoryImpl::commitTransaction(const FactoryCommitFunc& trans __BooTraceArgs) {
  GLDataFactory::Context ctx(*this __BooTraceArgsUse);
  if (!trans(ctx))
    return;

  /* Let's go ahead and flush to ensure our data gets to the GPU
     While this isn't strictly required, some drivers might behave
     differently */
  // glFlush();
}

ObjToken<IGraphicsBufferD> GLDataFactoryImpl::newPoolBuffer(BufferUse use, size_t stride, size_t count __BooTraceArgs) {
  BOO_MSAN_NO_INTERCEPT
  ObjToken<BaseGraphicsPool> pool(new BaseGraphicsPool(*this __BooTraceArgsUse));
  return {new GLGraphicsBufferD<BaseGraphicsPool>(pool, use, stride * count)};
}

constexpr std::array<GLint, 11> SEMANTIC_COUNT_TABLE{
    0, 3, 4, 3, 4, 4, 4, 2, 4, 4, 4,
};

constexpr std::array<size_t, 11> SEMANTIC_SIZE_TABLE{
    0, 12, 16, 12, 16, 16, 4, 8, 16, 16, 16,
};

constexpr std::array<GLenum, 11> SEMANTIC_TYPE_TABLE{
    GL_INVALID_ENUM,  GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT,
    GL_UNSIGNED_BYTE, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT,
};

struct GLCommandQueue final : IGraphicsCommandQueue {
  Platform platform() const override { return IGraphicsDataFactory::Platform::OpenGL; }
  const SystemChar* platformName() const override { return _SYS_STR("OpenGL"); }
  IGraphicsContext* m_parent = nullptr;
  GLContext* m_glCtx = nullptr;

  std::mutex m_mt;
  std::condition_variable m_cv;
  std::mutex m_initmt;
  std::condition_variable m_initcv;
  std::recursive_mutex m_fmtMt;
  std::thread m_thr;

  struct Command {
    enum class Op {
      SetShaderDataBinding,
      SetRenderTarget,
      SetCubeRenderTarget,
      SetViewport,
      SetScissor,
      SetClearColor,
      ClearTarget,
      Draw,
      DrawIndexed,
      DrawInstances,
      DrawInstancesIndexed,
      ResolveBindTexture,
      GenerateMips,
      Present,
#ifdef BOO_GRAPHICS_DEBUG_GROUPS
      PushDebugGroup,
      PopDebugGroup,
#endif
    } m_op;
    union {
      struct {
        SWindowRect rect;
        float znear, zfar;
      } viewport;
      std::array<float, 4> rgba;
      GLbitfield flags;
      struct {
        size_t start;
        size_t count;
        size_t instCount;
        size_t startInst;
      };
    };
#ifdef BOO_GRAPHICS_DEBUG_GROUPS
    std::string name;
#endif
    ObjToken<IShaderDataBinding> binding;
    ObjToken<ITexture> target;
    ObjToken<ITextureR> source;
    ObjToken<ITextureR> resolveTex;
    int bindIdx;
    bool resolveColor : 1;
    bool resolveDepth : 1;
    bool clearDepth : 1;
    Command(Op op) : m_op(op) {}
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = default;
    Command& operator=(Command&&) = default;
  };
  std::array<std::vector<Command>, 3> m_cmdBufs;
  int m_fillBuf = 0;
  int m_completeBuf = 0;
  int m_drawBuf = 0;
  bool m_running = true;

  struct RenderTextureResize {
    ObjToken<ITextureR> tex;
    size_t width;
    size_t height;
  };

  struct CubeRenderTextureResize {
    ObjToken<ITextureCubeR> tex;
    size_t width, mips;
  };

  /* These members are locked for multithreaded access */
  std::vector<RenderTextureResize> m_pendingResizes;
  std::vector<CubeRenderTextureResize> m_pendingCubeResizes;
  std::vector<std::function<void(void)>> m_pendingPosts1;
  std::vector<std::function<void(void)>> m_pendingPosts2;
  std::vector<ObjToken<IShaderDataBinding>> m_pendingFmtAdds;
  std::vector<std::array<GLuint, 3>> m_pendingFmtDels;
  std::vector<ObjToken<ITextureR>> m_pendingFboAdds;
  std::vector<ObjToken<ITextureCubeR>> m_pendingCubeFboAdds;

  static void ConfigureVertexFormat(GLShaderDataBinding* fmt) {
    glGenVertexArrays(GLsizei(fmt->m_vao.size()), fmt->m_vao.data());

    size_t stride = 0;
    size_t instStride = 0;
    const auto* const pipeline = fmt->m_pipeline.cast<GLShaderPipeline>();
    for (const auto desc : pipeline->m_elements) {
      const size_t size = SEMANTIC_SIZE_TABLE[int(desc.semantic & VertexSemantic::SemanticMask)];

      if (True(desc.semantic & VertexSemantic::Instanced)) {
        instStride += size;
      } else {
        stride += size;
      }
    }

    for (size_t b = 0; b < fmt->m_vao.size(); ++b) {
      size_t offset = fmt->m_baseVert * stride;
      size_t instOffset = fmt->m_baseInst * instStride;
      glBindVertexArray(fmt->m_vao[b]);
      IGraphicsBuffer* lastVBO = nullptr;
      IGraphicsBuffer* lastEBO = nullptr;
      for (size_t i = 0; i < pipeline->m_elements.size(); ++i) {
        const VertexElementDescriptor& desc = pipeline->m_elements[i];
        IGraphicsBuffer* vbo = True(desc.semantic & VertexSemantic::Instanced)
                                   ? fmt->m_instVbo.get()
                                   : fmt->m_vbo.get();
        IGraphicsBuffer* ebo = fmt->m_ibo.get();
        if (vbo != lastVBO) {
          lastVBO = vbo;
          if (lastVBO->dynamic())
            static_cast<GLGraphicsBufferD<BaseGraphicsData>*>(lastVBO)->bindVertex(int(b));
          else
            static_cast<GLGraphicsBufferS*>(lastVBO)->bindVertex();
        }
        if (ebo != lastEBO) {
          lastEBO = ebo;
          if (lastEBO->dynamic())
            static_cast<GLGraphicsBufferD<BaseGraphicsData>*>(lastEBO)->bindIndex(int(b));
          else
            static_cast<GLGraphicsBufferS*>(lastEBO)->bindIndex();
        }

        glEnableVertexAttribArray(i);
        const int maskedSem = int(desc.semantic & VertexSemantic::SemanticMask);
        const auto semanticCount = SEMANTIC_COUNT_TABLE[maskedSem];
        const auto semanticType = SEMANTIC_TYPE_TABLE[maskedSem];
        const auto semanticSize = SEMANTIC_SIZE_TABLE[maskedSem];

        if (True(desc.semantic & VertexSemantic::Instanced)) {
          glVertexAttribPointer(i, semanticCount, semanticType, GL_TRUE, instStride, (void*)instOffset);
          glVertexAttribDivisor(i, 1);
          instOffset += semanticSize;
        } else {
          glVertexAttribPointer(i, semanticCount, semanticType, GL_TRUE, stride, (void*)offset);
          offset += semanticSize;
        }
      }
    }
  }

  static void ConfigureFBO(GLTextureR* tex) {
    glGenFramebuffers(1, &tex->m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tex->m_fbo);
    GLenum target = tex->m_samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, tex->m_texs[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, tex->m_texs[1], 0);

    if (tex->m_samples > 1) {
      if (tex->m_colorBindCount) {
        glGenFramebuffers(tex->m_colorBindCount, tex->m_bindFBOs[0].data());
        for (size_t i = 0; i < tex->m_colorBindCount; ++i) {
          glBindFramebuffer(GL_FRAMEBUFFER, tex->m_bindFBOs[0][i]);
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->m_bindTexs[0][i], 0);
        }
      }
      if (tex->m_depthBindCount) {
        glGenFramebuffers(tex->m_depthBindCount, tex->m_bindFBOs[1].data());
        for (size_t i = 0; i < tex->m_depthBindCount; ++i) {
          glBindFramebuffer(GL_FRAMEBUFFER, tex->m_bindFBOs[1][i]);
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex->m_bindTexs[1][i], 0);
        }
      }
    }
  }

  static void ConfigureFBO(GLTextureCubeR* tex) {
    glGenFramebuffers(GLsizei(tex->m_fbos.size()), tex->m_fbos.data());

    for (size_t i = 0; i < tex->m_fbos.size(); ++i) {
      glBindFramebuffer(GL_FRAMEBUFFER, tex->m_fbos[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i),
                             tex->m_texs[0], 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i),
                             tex->m_texs[1], 0);
    }
  }

  static void RenderingWorker(GLCommandQueue* self) {
    BOO_MSAN_NO_INTERCEPT
#if _WIN32
    std::string thrName = WCSTMBS(APP->getFriendlyName().data()) + " Render";
#else
    std::string thrName = std::string(APP->getFriendlyName()) + " Render";
#endif
    logvisor::RegisterThreadName(thrName.c_str());
    GLDataFactoryImpl* dataFactory = static_cast<GLDataFactoryImpl*>(self->m_parent->getDataFactory());
    {
      std::unique_lock<std::mutex> lk(self->m_initmt);
      self->m_parent->makeCurrent();
      const GLubyte* version = glGetString(GL_VERSION);
      Log.report(logvisor::Info, fmt("OpenGL Version: {}"), version);
      self->m_parent->postInit();
      glClearColor(0.f, 0.f, 0.f, 0.f);
      if (GLEW_EXT_texture_filter_anisotropic) {
        GLint maxAniso = 0;
        glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        self->m_glCtx->m_anisotropy = std::min(uint32_t(maxAniso), self->m_glCtx->m_anisotropy);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
      }
      GLint maxSamples = 0;
      glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
      self->m_glCtx->m_sampleCount =
          flp2(std::min(uint32_t(maxSamples), std::max(uint32_t(1), self->m_glCtx->m_sampleCount)) - 1);

      glEnable(GL_PRIMITIVE_RESTART);
      glPrimitiveRestartIndex(0xffffffff);

      dataFactory->SetupGammaResources();
    }
    self->m_initcv.notify_one();
    while (self->m_running) {
      std::vector<std::function<void(void)>> posts;
      {
        std::unique_lock<std::mutex> lk(self->m_mt);
        self->m_cv.wait(lk);
        if (!self->m_running)
          break;
        self->m_drawBuf = self->m_completeBuf;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (self->m_pendingFboAdds.size()) {
          for (ObjToken<ITextureR>& tex : self->m_pendingFboAdds)
            ConfigureFBO(tex.cast<GLTextureR>());
          self->m_pendingFboAdds.clear();
        }

        if (self->m_pendingCubeFboAdds.size()) {
          for (ObjToken<ITextureCubeR>& tex : self->m_pendingCubeFboAdds)
            ConfigureFBO(tex.cast<GLTextureCubeR>());
          self->m_pendingCubeFboAdds.clear();
        }

        if (self->m_pendingResizes.size()) {
          for (const RenderTextureResize& resize : self->m_pendingResizes)
            resize.tex.cast<GLTextureR>()->resize(resize.width, resize.height);
          self->m_pendingResizes.clear();
        }

        if (self->m_pendingCubeResizes.size()) {
          for (const CubeRenderTextureResize& resize : self->m_pendingCubeResizes)
            resize.tex.cast<GLTextureCubeR>()->resize(resize.width, resize.mips);
          self->m_pendingCubeResizes.clear();
        }

        std::vector<ObjToken<IShaderDataBinding>> pendingFmtAdds;
        std::vector<std::array<GLuint, 3>> pendingFmtDels;
        {
          std::lock_guard<std::recursive_mutex> fmtLk(self->m_fmtMt);
          pendingFmtAdds.swap(self->m_pendingFmtAdds);
          pendingFmtDels.swap(self->m_pendingFmtDels);
        }
        if (pendingFmtAdds.size()) {
          for (ObjToken<IShaderDataBinding>& fmt : pendingFmtAdds)
            ConfigureVertexFormat(fmt.cast<GLShaderDataBinding>());
          pendingFmtAdds.clear();
        }
        if (pendingFmtDels.size()) {
          for (const auto& v : pendingFmtDels)
            glDeleteVertexArrays(3, v.data());
          pendingFmtDels.clear();
        }

        if (self->m_pendingPosts2.size())
          posts.swap(self->m_pendingPosts2);
      }
      std::vector<Command>& cmds = self->m_cmdBufs[self->m_drawBuf];
      GLenum currentPrim = GL_TRIANGLES;
      GLuint curFBO = 0;
      for (const Command& cmd : cmds) {
        switch (cmd.m_op) {
        case Command::Op::SetShaderDataBinding: {
          const GLShaderDataBinding* binding = cmd.binding.cast<GLShaderDataBinding>();
          binding->bind(self->m_drawBuf);
          currentPrim = binding->m_pipeline.cast<GLShaderPipeline>()->m_drawPrim;
          break;
        }
        case Command::Op::SetRenderTarget: {
          const GLTextureR* tex = cmd.target.cast<GLTextureR>();
          curFBO = tex ? tex->m_fbo : 0;
          glBindFramebuffer(GL_FRAMEBUFFER, curFBO);
          break;
        }
        case Command::Op::SetCubeRenderTarget: {
          const GLTextureCubeR* tex = cmd.target.cast<GLTextureCubeR>();
          curFBO = tex ? tex->m_fbos[cmd.bindIdx] : 0;
          glBindFramebuffer(GL_FRAMEBUFFER, curFBO);
          break;
        }
        case Command::Op::SetViewport:
          glViewport(cmd.viewport.rect.location[0], cmd.viewport.rect.location[1], cmd.viewport.rect.size[0],
                     cmd.viewport.rect.size[1]);
          glDepthRange(cmd.viewport.znear, cmd.viewport.zfar);
          break;
        case Command::Op::SetScissor:
          if (cmd.viewport.rect.size[0] == 0 && cmd.viewport.rect.size[1] == 0)
            glDisable(GL_SCISSOR_TEST);
          else {
            glEnable(GL_SCISSOR_TEST);
            glScissor(cmd.viewport.rect.location[0], cmd.viewport.rect.location[1], cmd.viewport.rect.size[0],
                      cmd.viewport.rect.size[1]);
          }
          break;
        case Command::Op::SetClearColor:
          glClearColor(cmd.rgba[0], cmd.rgba[1], cmd.rgba[2], cmd.rgba[3]);
          break;
        case Command::Op::ClearTarget:
          if (cmd.flags & GL_COLOR_BUFFER_BIT)
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
          if (cmd.flags & GL_DEPTH_BUFFER_BIT)
            glDepthMask(GL_TRUE);
          glClear(cmd.flags);
          break;
        case Command::Op::Draw:
          glDrawArrays(currentPrim, cmd.start, cmd.count);
          break;
        case Command::Op::DrawIndexed:
          glDrawElements(currentPrim, cmd.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(cmd.start * 4));
          break;
        case Command::Op::DrawInstances:
          if (cmd.startInst)
            glDrawArraysInstancedBaseInstance(currentPrim, cmd.start, cmd.count, cmd.instCount, cmd.startInst);
          else
            glDrawArraysInstanced(currentPrim, cmd.start, cmd.count, cmd.instCount);
          break;
        case Command::Op::DrawInstancesIndexed:
          if (cmd.startInst)
            glDrawElementsInstancedBaseInstance(currentPrim, cmd.count, GL_UNSIGNED_INT,
                                                reinterpret_cast<void*>(cmd.start * 4), cmd.instCount, cmd.startInst);
          else
            glDrawElementsInstanced(currentPrim, cmd.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(cmd.start * 4),
                                    cmd.instCount);
          break;
        case Command::Op::ResolveBindTexture: {
          const SWindowRect& rect = cmd.viewport.rect;
          const GLTextureR* tex = cmd.resolveTex.cast<GLTextureR>();
          glBindFramebuffer(GL_READ_FRAMEBUFFER, tex->m_fbo);
          if (tex->m_samples <= 1) {
            glActiveTexture(GL_TEXTURE9);
            if (cmd.resolveColor && tex->m_bindTexs[0][cmd.bindIdx]) {
              glBindTexture(GL_TEXTURE_2D, tex->m_bindTexs[0][cmd.bindIdx]);
              glCopyTexSubImage2D(GL_TEXTURE_2D, 0, rect.location[0], rect.location[1], rect.location[0],
                                  rect.location[1], rect.size[0], rect.size[1]);
            }
            if (cmd.resolveDepth && tex->m_bindTexs[1][cmd.bindIdx]) {
              glBindTexture(GL_TEXTURE_2D, tex->m_bindTexs[1][cmd.bindIdx]);
              glCopyTexSubImage2D(GL_TEXTURE_2D, 0, rect.location[0], rect.location[1], rect.location[0],
                                  rect.location[1], rect.size[0], rect.size[1]);
            }
          } else {
            if (cmd.resolveColor && tex->m_bindTexs[0][cmd.bindIdx]) {
              glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tex->m_bindFBOs[0][cmd.bindIdx]);
              glBlitFramebuffer(rect.location[0], rect.location[1], rect.location[0] + rect.size[0],
                                rect.location[1] + rect.size[1], rect.location[0], rect.location[1],
                                rect.location[0] + rect.size[0], rect.location[1] + rect.size[1], GL_COLOR_BUFFER_BIT,
                                GL_NEAREST);
            }
            if (cmd.resolveDepth && tex->m_bindTexs[1][cmd.bindIdx]) {
              glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tex->m_bindFBOs[1][cmd.bindIdx]);
              glBlitFramebuffer(rect.location[0], rect.location[1], rect.location[0] + rect.size[0],
                                rect.location[1] + rect.size[1], rect.location[0], rect.location[1],
                                rect.location[0] + rect.size[0], rect.location[1] + rect.size[1], GL_DEPTH_BUFFER_BIT,
                                GL_NEAREST);
            }
          }
          if (cmd.clearDepth) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tex->m_fbo);
            glDepthMask(GL_TRUE);
            glClear(GL_DEPTH_BUFFER_BIT);
          }
          glBindFramebuffer(GL_FRAMEBUFFER, curFBO);
          break;
        }
        case Command::Op::GenerateMips: {
          if (const GLTextureCubeR* tex = cmd.target.cast<GLTextureCubeR>()) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, tex->m_texs[0]);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
          }
          break;
        }
        case Command::Op::Present: {
          if (const GLTextureR* tex = cmd.source.cast<GLTextureR>()) {
#ifndef NDEBUG
            if (!tex->m_colorBindCount)
              Log.report(logvisor::Fatal, fmt("texture provided to resolveDisplay() must have at least 1 color binding"));
#endif
            if (dataFactory->m_gamma != 1.f) {
              glBindFramebuffer(GL_READ_FRAMEBUFFER, tex->m_fbo);
              if (tex->m_samples <= 1) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex->m_texs[0]);
              } else {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tex->m_bindFBOs[0][0]);
                glBlitFramebuffer(0, 0, tex->m_width, tex->m_height, 0, 0, tex->m_width, tex->m_height,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                tex->bind(0, 0, false);
              }

              glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
              dataFactory->m_gammaBinding.cast<GLShaderDataBinding>()->bind(self->m_drawBuf);
              glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            } else {
              glBindFramebuffer(GL_READ_FRAMEBUFFER, tex->m_fbo);
              glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
              glBlitFramebuffer(0, 0, tex->m_width, tex->m_height, 0, 0, tex->m_width, tex->m_height,
                                GL_COLOR_BUFFER_BIT, GL_NEAREST);
#if 0
              /* First cubemap dump */
              int offset = 0;
              int voffset = 0;
              for (BaseGraphicsData& data : *dataFactory->m_dataHead) {
                if (GLTextureCubeR* cube = static_cast<GLTextureCubeR*>(data.getHead<ITextureCubeR>())) {
                  for (size_t i = 0; i < cube->m_fbos.size(); ++i) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, cube->m_fbos[i]);
                    glBlitFramebuffer(0, 0, cube->m_width, cube->m_width, offset, voffset,
                                      cube->m_width + offset, cube->m_width + voffset,
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    offset += cube->m_width;
                    if (i == 2) {
                      offset = 0;
                      voffset += cube->m_width;
                    }
                  }
                  break;
                }
              }
#endif
            }
          }
          self->m_parent->present();
          break;
        }
#ifdef BOO_GRAPHICS_DEBUG_GROUPS
        case Command::Op::PushDebugGroup: {
          glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 42, -1, cmd.name.c_str());
          break;
        }
        case Command::Op::PopDebugGroup: {
          glPopDebugGroup();
          break;
        }
#endif
        default:
          break;
        }
      }
      for (auto& p : posts)
        p();
      cmds.clear();
    }
    dataFactory->DestroyGammaResources();
    std::lock_guard<std::recursive_mutex> fmtLk(self->m_fmtMt);
    if (self->m_pendingFmtDels.size()) {
      for (const auto& v : self->m_pendingFmtDels) {
        glDeleteVertexArrays(GLsizei(v.size()), v.data());
      }
      self->m_pendingFmtDels.clear();
    }
  }

  GLCommandQueue(IGraphicsContext* parent, GLContext* glCtx) : m_parent(parent), m_glCtx(glCtx) {}

  void startRenderer() override {
    std::unique_lock<std::mutex> lk(m_initmt);
    m_thr = std::thread(RenderingWorker, this);
    m_initcv.wait(lk);
  }

  void stopRenderer() override {
    if (m_running) {
      m_running = false;
      m_cv.notify_one();
      if (m_thr.joinable()) {
        m_thr.join();
      }
      for (auto& cmdBuf : m_cmdBufs) {
        cmdBuf.clear();
      }
    }
  }

  ~GLCommandQueue() override { stopRenderer(); }

  void setShaderDataBinding(const ObjToken<IShaderDataBinding>& binding) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::SetShaderDataBinding);
    cmd.binding = binding;
  }

  void setRenderTarget(const ObjToken<ITextureR>& target) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::SetRenderTarget);
    cmd.target = target.get();
  }

  void setRenderTarget(const ObjToken<ITextureCubeR>& target, int face) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::SetCubeRenderTarget);
    cmd.target = target.get();
    cmd.bindIdx = face;
  }

  void setViewport(const SWindowRect& rect, float znear, float zfar) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::SetViewport);
    cmd.viewport.rect = rect;
    cmd.viewport.znear = znear;
    cmd.viewport.zfar = zfar;
  }

  void setScissor(const SWindowRect& rect) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::SetScissor);
    cmd.viewport.rect = rect;
  }

  void resizeRenderTexture(const ObjToken<ITextureR>& tex, size_t width, size_t height) override {
    std::unique_lock<std::mutex> lk(m_mt);
    GLTextureR* texgl = tex.cast<GLTextureR>();
    m_pendingResizes.push_back({texgl, width, height});
  }

  void resizeRenderTexture(const ObjToken<ITextureCubeR>& tex, size_t width, size_t mips) override {
    std::unique_lock<std::mutex> lk(m_mt);
    GLTextureCubeR* texgl = tex.cast<GLTextureCubeR>();
    m_pendingCubeResizes.push_back({texgl, width, mips});
  }

  void generateMipmaps(const ObjToken<ITextureCubeR>& tex) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::GenerateMips);
    cmd.target = tex.get();
  }

  void schedulePostFrameHandler(std::function<void()>&& func) override { m_pendingPosts1.push_back(std::move(func)); }

  void setClearColor(const float rgba[4]) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::SetClearColor);
    cmd.rgba = {rgba[0], rgba[1], rgba[2], rgba[3]};
  }

  void clearTarget(bool render = true, bool depth = true) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::ClearTarget);
    cmd.flags = 0;
    if (render) {
      cmd.flags |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) {
      cmd.flags |= GL_DEPTH_BUFFER_BIT;
    }
  }

  void draw(size_t start, size_t count) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::Draw);
    cmd.start = start;
    cmd.count = count;
  }

  void drawIndexed(size_t start, size_t count) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::DrawIndexed);
    cmd.start = start;
    cmd.count = count;
  }

  void drawInstances(size_t start, size_t count, size_t instCount, size_t startInst) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::DrawInstances);
    cmd.start = start;
    cmd.count = count;
    cmd.instCount = instCount;
    cmd.startInst = startInst;
  }

  void drawInstancesIndexed(size_t start, size_t count, size_t instCount, size_t startInst) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::DrawInstancesIndexed);
    cmd.start = start;
    cmd.count = count;
    cmd.instCount = instCount;
    cmd.startInst = startInst;
  }

  void resolveBindTexture(const ObjToken<ITextureR>& texture, const SWindowRect& rect, bool tlOrigin, int bindIdx,
                          bool color, bool depth, bool clearDepth) override {
    const auto* const tex = texture.cast<GLTextureR>();
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::ResolveBindTexture);
    cmd.resolveTex = texture;
    cmd.bindIdx = bindIdx;
    cmd.resolveColor = color;
    cmd.resolveDepth = depth;
    cmd.clearDepth = clearDepth;
    const SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
    SWindowRect& targetRect = cmd.viewport.rect;
    targetRect.location[0] = intersectRect.location[0];
    if (tlOrigin)
      targetRect.location[1] = tex->m_height - intersectRect.location[1] - intersectRect.size[1];
    else
      targetRect.location[1] = intersectRect.location[1];
    targetRect.size[0] = intersectRect.size[0];
    targetRect.size[1] = intersectRect.size[1];
  }

  void resolveDisplay(const ObjToken<ITextureR>& source) override {
    std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
    auto& cmd = cmds.emplace_back(Command::Op::Present);
    cmd.source = source;
  }

  void addVertexFormat(const ObjToken<IShaderDataBinding>& fmt) {
    std::unique_lock<std::recursive_mutex> lk(m_fmtMt);
    m_pendingFmtAdds.push_back(fmt);
  }

  void delVertexFormat(GLShaderDataBinding* fmt) {
    std::unique_lock<std::recursive_mutex> lk(m_fmtMt);
    m_pendingFmtDels.push_back(fmt->m_vao);
  }

  void addFBO(const ObjToken<ITextureR>& tex) {
    std::unique_lock<std::mutex> lk(m_mt);
    m_pendingFboAdds.push_back(tex);
  }

  void addFBO(const ObjToken<ITextureCubeR>& tex) {
    std::unique_lock<std::mutex> lk(m_mt);
    m_pendingCubeFboAdds.push_back(tex);
  }

  void execute() override {
    BOO_MSAN_NO_INTERCEPT
    SCOPED_GRAPHICS_DEBUG_GROUP(this, "GLCommandQueue::execute", {1.f, 0.f, 0.f, 1.f});
    std::unique_lock<std::mutex> lk(m_mt);
    m_completeBuf = m_fillBuf;
    for (size_t i = 0; i < m_cmdBufs.size(); ++i) {
      if (int(i) == m_completeBuf || int(i) == m_drawBuf)
        continue;
      m_fillBuf = int(i);
      break;
    }

    /* Update dynamic data here */
    GLDataFactoryImpl* gfxF = static_cast<GLDataFactoryImpl*>(m_parent->getDataFactory());
    std::unique_lock<std::recursive_mutex> datalk(gfxF->m_dataMutex);
    if (gfxF->m_dataHead) {
      for (BaseGraphicsData& d : *gfxF->m_dataHead) {
        if (d.m_DBufs)
          for (IGraphicsBufferD& b : *d.m_DBufs)
            static_cast<GLGraphicsBufferD<BaseGraphicsData>&>(b).update(m_completeBuf);
        if (d.m_DTexs)
          for (ITextureD& t : *d.m_DTexs)
            static_cast<GLTextureD&>(t).update(m_completeBuf);
      }
    }
    if (gfxF->m_poolHead) {
      for (BaseGraphicsPool& p : *gfxF->m_poolHead) {
        if (p.m_DBufs)
          for (IGraphicsBufferD& b : *p.m_DBufs)
            static_cast<GLGraphicsBufferD<BaseGraphicsData>&>(b).update(m_completeBuf);
      }
    }
    datalk.unlock();
    glFlush();

    for (auto& p : m_pendingPosts1)
      m_pendingPosts2.push_back(std::move(p));
    m_pendingPosts1.clear();

    lk.unlock();
    m_cv.notify_one();
    m_cmdBufs[m_fillBuf].clear();
  }

#ifdef BOO_GRAPHICS_DEBUG_GROUPS
  void pushDebugGroup(const char* name, const std::array<float, 4>& color) override {
    if (GLEW_KHR_debug) {
      std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
      auto& cmd = cmds.emplace_back(Command::Op::PushDebugGroup);
      cmd.name = name;
    }
  }

  void popDebugGroup() override {
    if (GLEW_KHR_debug) {
      std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
      cmds.emplace_back(Command::Op::PopDebugGroup);
    }
  }
#endif
};

ObjToken<IGraphicsBufferD> GLDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count) {
  BOO_MSAN_NO_INTERCEPT
  return {new GLGraphicsBufferD<BaseGraphicsData>(m_data, use, stride * count)};
}

ObjToken<ITextureD> GLDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt,
                                                              TextureClampMode clampMode) {
  BOO_MSAN_NO_INTERCEPT
  return {new GLTextureD(m_data, width, height, fmt, clampMode)};
}

GLTextureR::GLTextureR(const ObjToken<BaseGraphicsData>& parent, GLCommandQueue* q, size_t width, size_t height,
                       size_t samples, GLenum colorFormat, TextureClampMode clampMode, size_t colorBindingCount,
                       size_t depthBindingCount)
: GraphicsDataNode<ITextureR>(parent)
, m_q(q)
, m_width(width)
, m_height(height)
, m_samples(samples)
, m_colorFormat(colorFormat)
, m_colorBindCount(colorBindingCount)
, m_depthBindCount(depthBindingCount) {
  glGenTextures(GLsizei(m_texs.size()), m_texs.data());
  if (colorBindingCount) {
    if (colorBindingCount > MAX_BIND_TEXS) {
      Log.report(logvisor::Fatal, fmt("too many color bindings for render texture"));
    }
    glGenTextures(colorBindingCount, m_bindTexs[0].data());
  }
  if (depthBindingCount) {
    if (depthBindingCount > MAX_BIND_TEXS) {
      Log.report(logvisor::Fatal, fmt("too many depth bindings for render texture"));
    }
    glGenTextures(depthBindingCount, m_bindTexs[1].data());
  }

  GLenum compType = colorFormat == GL_RGBA16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
  if (samples > 1) {
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[0]);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, colorFormat, width, height, GL_FALSE);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[1]);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH_COMPONENT32F, width, height, GL_FALSE);
  } else {
    glBindTexture(GL_TEXTURE_2D, m_texs[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, colorFormat, width, height, 0, GL_RGBA, compType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, m_texs[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                 nullptr);
  }

  for (size_t i = 0; i < colorBindingCount; ++i) {
    glBindTexture(GL_TEXTURE_2D, m_bindTexs[0][i]);
    glTexImage2D(GL_TEXTURE_2D, 0, colorFormat, width, height, 0, GL_RGBA, compType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    SetClampMode(GL_TEXTURE_2D, clampMode);
  }
  for (size_t i = 0; i < depthBindingCount; ++i) {
    glBindTexture(GL_TEXTURE_2D, m_bindTexs[1][i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    SetClampMode(GL_TEXTURE_2D, clampMode);
  }

  m_q->addFBO(this);
}

ObjToken<ITextureR> GLDataFactory::Context::newRenderTexture(size_t width, size_t height, TextureClampMode clampMode,
                                                             size_t colorBindingCount, size_t depthBindingCount) {
  GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
  GLCommandQueue* q = static_cast<GLCommandQueue*>(factory.m_parent->getCommandQueue());
  BOO_MSAN_NO_INTERCEPT
  ObjToken<ITextureR> retval(new GLTextureR(m_data, q, width, height, factory.m_glCtx->m_sampleCount,
                                            factory.m_glCtx->m_deepColor ? GL_RGBA16 : GL_RGBA8, clampMode,
                                            colorBindingCount, depthBindingCount));
  q->resizeRenderTexture(retval, width, height);
  return retval;
}

GLTextureCubeR::GLTextureCubeR(const ObjToken<BaseGraphicsData>& parent, GLCommandQueue* q, size_t width, size_t mips, GLenum colorFormat)
: GraphicsDataNode<ITextureCubeR>(parent)
, m_q(q)
, m_width(width)
, m_mipCount(mips)
, m_colorFormat(colorFormat) {
  glGenTextures(GLsizei(m_texs.size()), m_texs.data());

  _allocateTextures();

  glBindTexture(GL_TEXTURE_CUBE_MAP, m_texs[0]);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

  m_q->addFBO(this);
}

ObjToken<ITextureCubeR> GLDataFactory::Context::newCubeRenderTexture(size_t width, size_t mips) {
  GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
  GLCommandQueue* q = static_cast<GLCommandQueue*>(factory.m_parent->getCommandQueue());
  BOO_MSAN_NO_INTERCEPT
  ObjToken<ITextureCubeR> retval(new GLTextureCubeR(m_data, q, width, mips,
                                                    factory.m_glCtx->m_deepColor ? GL_RGBA16 : GL_RGBA8));
  return retval;
}

ObjToken<IShaderDataBinding> GLDataFactory::Context::newShaderDataBinding(
    const ObjToken<IShaderPipeline>& pipeline, const ObjToken<IGraphicsBuffer>& vbo,
    const ObjToken<IGraphicsBuffer>& instVbo, const ObjToken<IGraphicsBuffer>& ibo, size_t ubufCount,
    const ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages, const size_t* ubufOffs,
    const size_t* ubufSizes, size_t texCount, const ObjToken<ITexture>* texs, const int* texBindIdx,
    const bool* depthBind, size_t baseVert, size_t baseInst) {
  GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
  GLCommandQueue* q = static_cast<GLCommandQueue*>(factory.m_parent->getCommandQueue());
  BOO_MSAN_NO_INTERCEPT
  ObjToken<GLShaderDataBinding> ret = {new GLShaderDataBinding(m_data, pipeline, vbo, instVbo, ibo, ubufCount, ubufs,
                                                               ubufOffs, ubufSizes, texCount, texs, texBindIdx,
                                                               depthBind, baseVert, baseInst, q)};
  return ret.get();
}

GLShaderDataBinding::
GLShaderDataBinding(const ObjToken<BaseGraphicsData>& d, const ObjToken<IShaderPipeline>& pipeline,
                    const ObjToken<IGraphicsBuffer>& vbo, const ObjToken<IGraphicsBuffer>& instVbo,
                    const ObjToken<IGraphicsBuffer>& ibo, size_t ubufCount, const ObjToken<IGraphicsBuffer>* ubufs,
                    const size_t* ubufOffs, const size_t* ubufSizes, size_t texCount, const ObjToken<ITexture>* texs,
                    const int* bindTexIdx, const bool* depthBind, size_t baseVert, size_t baseInst,
                    GLCommandQueue* q)
  : GraphicsDataNode<IShaderDataBinding>(d)
  , m_pipeline(pipeline)
  , m_vbo(vbo)
  , m_instVbo(instVbo)
  , m_ibo(ibo)
  , m_baseVert(baseVert)
  , m_baseInst(baseInst)
  , m_q(q) {
  if (ubufOffs && ubufSizes) {
    m_ubufOffs.reserve(ubufCount);
    for (size_t i = 0; i < ubufCount; ++i) {
#ifndef NDEBUG
      if (ubufOffs[i] % 256) {
        Log.report(logvisor::Fatal, fmt("non-256-byte-aligned uniform-offset {} provided to newShaderDataBinding"), i);
      }
#endif
      m_ubufOffs.emplace_back(ubufOffs[i], (ubufSizes[i] + 255) & ~255);
    }
  }
  m_ubufs.reserve(ubufCount);
  for (size_t i = 0; i < ubufCount; ++i) {
#ifndef NDEBUG
    if (!ubufs[i]) {
      Log.report(logvisor::Fatal, fmt("null uniform-buffer {} provided to newShaderDataBinding"), i);
    }
#endif
    m_ubufs.push_back(ubufs[i]);
  }
  m_texs.reserve(texCount);
  for (size_t i = 0; i < texCount; ++i) {
    m_texs.push_back({texs[i], bindTexIdx ? bindTexIdx[i] : 0, depthBind ? depthBind[i] : false});
  }
  q->addVertexFormat(this);
}

GLShaderDataBinding::~GLShaderDataBinding() {
  m_q->delVertexFormat(this);
}

std::unique_ptr<IGraphicsCommandQueue> _NewGLCommandQueue(IGraphicsContext* parent, GLContext* glCtx) {
  return std::make_unique<GLCommandQueue>(parent, glCtx);
}

std::unique_ptr<IGraphicsDataFactory> _NewGLDataFactory(IGraphicsContext* parent, GLContext* glCtx) {
  return std::make_unique<GLDataFactoryImpl>(parent, glCtx);
}

} // namespace boo

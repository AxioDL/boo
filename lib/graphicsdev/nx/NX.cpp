#include "logvisor/logvisor.hpp"
#include "boo/graphicsdev/NX.hpp"
#include "boo/IGraphicsContext.hpp"
#include "boo/graphicsdev/GLSLMacros.hpp"
#include "../Common.hpp"
#include "xxhash/xxhash.h"

#include "main/shaderobj.h"
#include "st_program.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "state_tracker/winsys_handle.h"

extern "C" {
#include "main/viewport.h"
#include "nouveau_winsys.h"
#include "nouveau_screen.h"
#include "nvc0/nvc0_program.h"
#include "gallium/winsys/nouveau/switch/nouveau_switch_public.h"
}

#include <switch.h>

namespace boo {
static logvisor::Module Log("boo::NX");
struct NXCommandQueue;

class NXDataFactoryImpl : public NXDataFactory, public GraphicsDataFactoryHead {
public:
  float m_gamma = 1.f;
  ObjToken<IShaderDataBinding> m_gammaBinding;
  IGraphicsContext* m_parent;
  NXContext* m_ctx;

  NXDataFactoryImpl(IGraphicsContext* parent, NXContext* ctx) : m_parent(parent), m_ctx(ctx) {}

  Platform platform() const { return Platform::NX; }
  const SystemChar* platformName() const { return _SYS_STR("NX"); }

  void SetupGammaResources() {}

  void DestroyGammaResources() {}

  void commitTransaction(const FactoryCommitFunc& __BooTraceArgs);

  boo::ObjToken<IGraphicsBufferD> newPoolBuffer(BufferUse use, size_t stride, size_t count __BooTraceArgs);

  void setDisplayGamma(float gamma) {
    m_gamma = gamma;
    // if (gamma != 1.f)
    //    UpdateGammaLUT(m_gammaLUT.get(), gamma);
  }

  bool isTessellationSupported(uint32_t& maxPatchSizeOut) {
    maxPatchSizeOut = 0;
    if (!m_ctx->m_st->ctx->Extensions.ARB_tessellation_shader)
      return false;
    maxPatchSizeOut = m_ctx->m_st->ctx->Const.MaxPatchVertices;
    return true;
  }
};

struct NXData : BaseGraphicsData {
  NXContext* m_ctx;

  /* Vertex, Index, Uniform */
  struct pipe_resource* m_constantBuffers[3] = {};

  explicit NXData(NXDataFactoryImpl& head __BooTraceArgs)
  : BaseGraphicsData(head __BooTraceArgsUse), m_ctx(head.m_ctx) {}
  ~NXData() {
    for (int i = 0; i < 3; ++i)
      pipe_resource_reference(&m_constantBuffers[i], nullptr);
  }
};

struct NXPool : BaseGraphicsPool {
  NXContext* m_ctx;
  struct pipe_resource* m_constantBuffer;

  explicit NXPool(NXDataFactoryImpl& head __BooTraceArgs)
  : BaseGraphicsPool(head __BooTraceArgsUse), m_ctx(head.m_ctx) {}
  ~NXPool() { pipe_resource_reference(&m_constantBuffer, nullptr); }
};

static const unsigned USE_TABLE[] = {0, PIPE_BIND_VERTEX_BUFFER, PIPE_BIND_INDEX_BUFFER, PIPE_BIND_CONSTANT_BUFFER};

union nx_buffer_info {
  pipe_vertex_buffer v;
  pipe_constant_buffer c;
};

class NXGraphicsBufferS : public GraphicsDataNode<IGraphicsBufferS> {
  friend class NXDataFactory;
  friend struct NXCommandQueue;
  NXContext* m_ctx;
  size_t m_sz;
  std::unique_ptr<uint8_t[]> m_stagingBuf;
  NXGraphicsBufferS(const boo::ObjToken<BaseGraphicsData>& parent, BufferUse use, NXContext* ctx, const void* data,
                    size_t stride, size_t count)
  : GraphicsDataNode<IGraphicsBufferS>(parent)
  , m_ctx(ctx)
  , m_sz(stride * count)
  , m_stagingBuf(new uint8_t[m_sz])
  , m_use(use) {
    memmove(m_stagingBuf.get(), data, m_sz);
    if (m_use == BufferUse::Vertex)
      m_bufferInfo.v.stride = uint16_t(stride);
  }

public:
  size_t size() const { return m_sz; }
  nx_buffer_info m_bufferInfo;
  BufferUse m_use;

  unsigned sizeForGPU(NXContext* ctx, unsigned offset) {
    if (m_use == BufferUse::Uniform) {
      unsigned minOffset = std::max(256u, ctx->m_st->ctx->Const.UniformBufferOffsetAlignment);
      offset = (offset + minOffset - 1) & ~(minOffset - 1);
      m_bufferInfo.c.buffer_offset = offset;
      m_bufferInfo.c.buffer_size = m_sz;
    } else {
      m_bufferInfo.v.buffer_offset = offset;
    }
    offset += m_sz;

    return offset;
  }

  void placeForGPU(struct pipe_resource* bufObj, uint8_t* buf) {
    if (m_use == BufferUse::Uniform)
      m_bufferInfo.c.buffer = bufObj;
    else
      m_bufferInfo.v.buffer.resource = bufObj;
    memmove(buf + m_bufferInfo.v.buffer_offset, m_stagingBuf.get(), m_sz);
    m_stagingBuf.reset();
  }
};

template <class DataCls>
class NXGraphicsBufferD : public GraphicsDataNode<IGraphicsBufferD, DataCls> {
  friend class NXDataFactory;
  friend class NXDataFactoryImpl;
  friend struct NXCommandQueue;
  NXContext* m_ctx;
  size_t m_cpuSz;
  std::unique_ptr<uint8_t[]> m_cpuBuf;
  int m_validSlots = 0;
  NXGraphicsBufferD(const boo::ObjToken<DataCls>& parent, BufferUse use, NXContext* ctx, size_t stride, size_t count)
  : GraphicsDataNode<IGraphicsBufferD, DataCls>(parent)
  , m_ctx(ctx)
  , m_cpuSz(stride * count)
  , m_cpuBuf(new uint8_t[m_cpuSz])
  , m_use(use) {
    if (m_use == BufferUse::Vertex) {
      m_bufferInfo[0].v.stride = stride;
      m_bufferInfo[1].v.stride = stride;
    }
  }
  void update(int b) {
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0) {
      memcpy(m_bufferPtrs[b], m_cpuBuf.get(), m_cpuSz);
      m_validSlots |= slot;
    }
  }

public:
  nx_buffer_info m_bufferInfo[2];
  uint8_t* m_bufferPtrs[2] = {};
  BufferUse m_use;
  void load(const void* data, size_t sz) {
    size_t bufSz = std::min(sz, m_cpuSz);
    memmove(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
  }
  void* map(size_t sz) {
    if (sz > m_cpuSz)
      return nullptr;
    return m_cpuBuf.get();
  }
  void unmap() { m_validSlots = 0; }

  unsigned sizeForGPU(NXContext* ctx, unsigned offset) {
    for (int i = 0; i < 2; ++i) {
      if (m_use == BufferUse::Uniform) {
        size_t minOffset = std::max(256u, ctx->m_st->ctx->Const.UniformBufferOffsetAlignment);
        offset = (offset + minOffset - 1) & ~(minOffset - 1);
        m_bufferInfo[i].c.buffer_offset = offset;
        m_bufferInfo[i].c.buffer_size = m_cpuSz;
      } else {
        m_bufferInfo[i].v.buffer_offset = offset;
      }
      offset += m_cpuSz;
    }

    return offset;
  }

  void placeForGPU(struct pipe_resource* bufObj, uint8_t* buf) {
    if (m_use == BufferUse::Uniform) {
      m_bufferInfo[0].c.buffer = bufObj;
      m_bufferInfo[1].c.buffer = bufObj;
      m_bufferPtrs[0] = buf + m_bufferInfo[0].c.buffer_offset;
      m_bufferPtrs[1] = buf + m_bufferInfo[1].c.buffer_offset;
    } else {
      m_bufferInfo[0].v.buffer.resource = bufObj;
      m_bufferInfo[1].v.buffer.resource = bufObj;
      m_bufferPtrs[0] = buf + m_bufferInfo[0].v.buffer_offset;
      m_bufferPtrs[1] = buf + m_bufferInfo[1].v.buffer_offset;
    }
  }
};

static void MakeSampler(NXContext* ctx, void*& sampOut, TextureClampMode mode, int mips) {
  uint32_t key = (uint32_t(mode) << 16) | mips;
  auto search = ctx->m_samplers.find(key);
  if (search != ctx->m_samplers.end()) {
    sampOut = search->second;
    return;
  }

  /* Create linear sampler */
  pipe_sampler_state samplerInfo = {};
  samplerInfo.min_img_filter = PIPE_TEX_FILTER_LINEAR;
  samplerInfo.min_mip_filter = PIPE_TEX_MIPFILTER_LINEAR;
  samplerInfo.mag_img_filter = PIPE_TEX_FILTER_LINEAR;
  samplerInfo.compare_mode = PIPE_TEX_COMPARE_NONE;
  samplerInfo.compare_func = PIPE_FUNC_ALWAYS;
  samplerInfo.normalized_coords = 1;
  samplerInfo.max_anisotropy = 16;
  samplerInfo.seamless_cube_map = 0;
  samplerInfo.lod_bias = 0;
  samplerInfo.min_lod = 0;
  samplerInfo.max_lod = mips - 1;
  switch (mode) {
  case TextureClampMode::Repeat:
  default:
    samplerInfo.wrap_s = PIPE_TEX_WRAP_REPEAT;
    samplerInfo.wrap_t = PIPE_TEX_WRAP_REPEAT;
    samplerInfo.wrap_r = PIPE_TEX_WRAP_REPEAT;
    break;
  case TextureClampMode::ClampToWhite:
    samplerInfo.wrap_s = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
    samplerInfo.wrap_t = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
    samplerInfo.wrap_r = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
    for (int i = 0; i < 4; ++i)
      samplerInfo.border_color.f[i] = 1.f;
    break;
  case TextureClampMode::ClampToBlack:
    samplerInfo.wrap_s = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
    samplerInfo.wrap_t = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
    samplerInfo.wrap_r = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
    samplerInfo.border_color.f[3] = 1.f;
    break;
  case TextureClampMode::ClampToEdge:
    samplerInfo.wrap_s = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
    samplerInfo.wrap_t = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
    samplerInfo.wrap_r = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
    break;
  case TextureClampMode::ClampToEdgeNearest:
    samplerInfo.mag_img_filter = PIPE_TEX_FILTER_NEAREST;
    samplerInfo.min_mip_filter = PIPE_TEX_MIPFILTER_NEAREST;
    samplerInfo.min_img_filter = PIPE_TEX_FILTER_NEAREST;
    samplerInfo.max_anisotropy = 0;
    samplerInfo.wrap_s = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
    samplerInfo.wrap_t = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
    samplerInfo.wrap_r = PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
    break;
  }
  sampOut = ctx->m_pctx->create_sampler_state(ctx->m_pctx, &samplerInfo);
  ctx->m_samplers[key] = sampOut;
}

class NXTextureS : public GraphicsDataNode<ITextureS> {
  friend class NXDataFactory;
  NXContext* m_ctx;
  TextureFormat m_fmt;
  size_t m_sz;
  size_t m_width, m_height, m_mips;
  TextureClampMode m_clampMode;
  pipe_format m_nxFmt;
  int m_pixelPitchNum = 1;
  int m_pixelPitchDenom = 1;

  NXTextureS(const boo::ObjToken<BaseGraphicsData>& parent, NXContext* ctx, size_t width, size_t height, size_t mips,
             TextureFormat fmt, TextureClampMode clampMode, const void* data, size_t sz)
  : GraphicsDataNode<ITextureS>(parent)
  , m_ctx(ctx)
  , m_fmt(fmt)
  , m_sz(sz)
  , m_width(width)
  , m_height(height)
  , m_mips(mips)
  , m_clampMode(clampMode) {
    pipe_format pfmt;
    switch (fmt) {
    case TextureFormat::RGBA8:
      pfmt = PIPE_FORMAT_R8G8B8A8_UNORM;
      m_pixelPitchNum = 4;
      break;
    case TextureFormat::I8:
      pfmt = PIPE_FORMAT_R8_UNORM;
      break;
    case TextureFormat::I16:
      pfmt = PIPE_FORMAT_R16_UNORM;
      m_pixelPitchNum = 2;
      break;
    case TextureFormat::DXT1:
      pfmt = PIPE_FORMAT_DXT1_RGBA;
      m_pixelPitchNum = 1;
      m_pixelPitchDenom = 2;
      break;
    default:
      Log.report(logvisor::Fatal, fmt("unsupported tex format"));
    }
    m_nxFmt = pfmt;

    struct pipe_resource texTempl = {};
    texTempl.target = PIPE_TEXTURE_2D;
    texTempl.format = m_nxFmt;
    texTempl.width0 = width;
    texTempl.height0 = height;
    texTempl.depth0 = 1;
    texTempl.last_level = mips - 1;
    texTempl.array_size = 1;
    texTempl.bind = PIPE_BIND_SAMPLER_VIEW;
    m_gpuTex = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
    if (!m_gpuTex) {
      Log.report(logvisor::Fatal, fmt("Failed to create texture"));
      return;
    }

    uint blockSize = util_format_get_blocksize(m_nxFmt);
    uint8_t* ptr = (uint8_t*)data;
    for (int i = 0; i < mips; ++i) {
      size_t regionPitch = width * height * m_pixelPitchNum / m_pixelPitchDenom;
      size_t rowStride = width * blockSize;
      size_t imageBytes = rowStride * height;

      struct pipe_box box;
      u_box_2d(0, 0, width, height, &box);

      ctx->m_pctx->texture_subdata(ctx->m_pctx, m_gpuTex, i, PIPE_TRANSFER_WRITE, &box, ptr, rowStride, imageBytes);

      if (width > 1)
        width /= 2;
      if (height > 1)
        height /= 2;
      ptr += regionPitch;
    }

    struct pipe_sampler_view svTempl = {};
    svTempl.format = m_nxFmt;
    svTempl.texture = m_gpuTex;
    svTempl.u.tex.last_level = mips - 1;
    svTempl.swizzle_r = PIPE_SWIZZLE_X;
    svTempl.swizzle_g = PIPE_SWIZZLE_Y;
    svTempl.swizzle_b = PIPE_SWIZZLE_Z;
    svTempl.swizzle_a = PIPE_SWIZZLE_W;
    m_gpuView = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_gpuTex, &svTempl);
  }

public:
  struct pipe_resource* m_gpuTex;
  struct pipe_sampler_view* m_gpuView = nullptr;
  void* m_sampler = nullptr;
  ~NXTextureS() {
    pipe_resource_reference(&m_gpuTex, nullptr);
    pipe_sampler_view_reference(&m_gpuView, nullptr);
  }

  void setClampMode(TextureClampMode mode) {
    m_clampMode = mode;
    MakeSampler(m_ctx, m_sampler, mode, m_mips);
  }

  TextureFormat format() const { return m_fmt; }
};

class NXTextureSA : public GraphicsDataNode<ITextureSA> {
  friend class NXDataFactory;
  NXContext* m_ctx;
  TextureFormat m_fmt;
  size_t m_sz;
  size_t m_width, m_height, m_layers, m_mips;
  TextureClampMode m_clampMode;
  pipe_format m_nxFmt;
  int m_pixelPitchNum = 1;
  int m_pixelPitchDenom = 1;

  NXTextureSA(const boo::ObjToken<BaseGraphicsData>& parent, NXContext* ctx, size_t width, size_t height, size_t layers,
              size_t mips, TextureFormat fmt, TextureClampMode clampMode, const void* data, size_t sz)
  : GraphicsDataNode<ITextureSA>(parent)
  , m_ctx(ctx)
  , m_fmt(fmt)
  , m_sz(sz)
  , m_width(width)
  , m_height(height)
  , m_layers(layers)
  , m_mips(mips)
  , m_clampMode(clampMode) {
    pipe_format pfmt;
    switch (fmt) {
    case TextureFormat::RGBA8:
      pfmt = PIPE_FORMAT_R8G8B8A8_UNORM;
      m_pixelPitchNum = 4;
      break;
    case TextureFormat::I8:
      pfmt = PIPE_FORMAT_R8_UNORM;
      break;
    case TextureFormat::I16:
      pfmt = PIPE_FORMAT_R16_UNORM;
      m_pixelPitchNum = 2;
      break;
    default:
      Log.report(logvisor::Fatal, fmt("unsupported tex format"));
    }
    m_nxFmt = pfmt;

    struct pipe_resource texTempl = {};
    texTempl.target = PIPE_TEXTURE_2D;
    texTempl.format = m_nxFmt;
    texTempl.width0 = width;
    texTempl.height0 = height;
    texTempl.depth0 = 1;
    texTempl.last_level = mips - 1;
    texTempl.array_size = layers;
    texTempl.bind = PIPE_BIND_SAMPLER_VIEW;
    m_gpuTex = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
    if (!m_gpuTex) {
      Log.report(logvisor::Fatal, fmt("Failed to create texture"));
      return;
    }

    uint blockSize = util_format_get_blocksize(m_nxFmt);
    uint8_t* ptr = (uint8_t*)data;
    for (int i = 0; i < mips; ++i) {
      size_t regionPitch = width * height * m_layers * m_pixelPitchNum / m_pixelPitchDenom;
      size_t rowStride = width * blockSize;
      size_t imageBytes = rowStride * height;

      struct pipe_box box;
      u_box_2d(0, 0, width, height, &box);

      ctx->m_pctx->texture_subdata(ctx->m_pctx, m_gpuTex, i, PIPE_TRANSFER_WRITE, &box, ptr, rowStride, imageBytes);

      if (width > 1)
        width /= 2;
      if (height > 1)
        height /= 2;
      ptr += regionPitch;
    }

    struct pipe_sampler_view svTempl = {};
    svTempl.format = m_nxFmt;
    svTempl.texture = m_gpuTex;
    svTempl.u.tex.last_layer = layers - 1;
    svTempl.u.tex.last_level = mips - 1;
    svTempl.swizzle_r = PIPE_SWIZZLE_X;
    svTempl.swizzle_g = PIPE_SWIZZLE_Y;
    svTempl.swizzle_b = PIPE_SWIZZLE_Z;
    svTempl.swizzle_a = PIPE_SWIZZLE_W;
    m_gpuView = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_gpuTex, &svTempl);
  }

public:
  struct pipe_resource* m_gpuTex;
  struct pipe_sampler_view* m_gpuView = nullptr;
  void* m_sampler = nullptr;
  ~NXTextureSA() {
    pipe_resource_reference(&m_gpuTex, nullptr);
    pipe_sampler_view_reference(&m_gpuView, nullptr);
  }

  void setClampMode(TextureClampMode mode) {
    m_clampMode = mode;
    MakeSampler(m_ctx, m_sampler, mode, m_mips);
  }

  TextureFormat format() const { return m_fmt; }
  size_t layers() const { return m_layers; }
};

class NXTextureD : public GraphicsDataNode<ITextureD> {
  friend class NXDataFactory;
  friend class NXCommandQueue;
  NXCommandQueue* m_q;
  size_t m_width;
  size_t m_height;
  TextureFormat m_fmt;
  TextureClampMode m_clampMode;
  std::unique_ptr<uint8_t[]> m_stagingBuf;
  size_t m_cpuSz;
  pipe_format m_nxFmt;
  int m_validSlots = 0;
  NXTextureD(const boo::ObjToken<BaseGraphicsData>& parent, NXCommandQueue* q, size_t width, size_t height,
             TextureFormat fmt, TextureClampMode clampMode);

  void update(int b);

public:
  struct pipe_resource* m_gpuTex[2];
  struct pipe_sampler_view* m_gpuView[2];
  void* m_sampler = nullptr;
  ~NXTextureD() {
    for (int i = 0; i < 2; ++i) {
      pipe_resource_reference(&m_gpuTex[i], nullptr);
      pipe_sampler_view_reference(&m_gpuView[i], nullptr);
    }
  }

  void setClampMode(TextureClampMode mode);

  void load(const void* data, size_t sz);
  void* map(size_t sz);
  void unmap();

  TextureFormat format() const { return m_fmt; }
};

#define MAX_BIND_TEXS 4

static constexpr pipe_format ColorFormat = PIPE_FORMAT_R8G8B8A8_UNORM;
static constexpr pipe_format DepthFormat = PIPE_FORMAT_Z32_FLOAT;

class NXTextureR : public GraphicsDataNode<ITextureR> {
  NXContext* m_ctx;

  friend class NXDataFactory;
  friend struct NXCommandQueue;
  size_t m_width = 0;
  size_t m_height = 0;
  unsigned m_samplesColor, m_samplesDepth;

  size_t m_colorBindCount;
  size_t m_depthBindCount;

  void Setup(NXContext* ctx) {
    /* no-ops on first call */
    doDestroy();

    /* color target */
    struct pipe_resource texTempl = {};
    texTempl.target = PIPE_TEXTURE_2D;
    texTempl.format = ColorFormat;
    texTempl.width0 = m_width;
    texTempl.height0 = m_height;
    texTempl.depth0 = 1;
    texTempl.array_size = 1;
    texTempl.nr_samples = texTempl.nr_storage_samples = m_samplesColor;
    texTempl.bind = PIPE_BIND_RENDER_TARGET;
    m_colorTex = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
    if (!m_colorTex) {
      Log.report(logvisor::Fatal, fmt("Failed to create color target texture"));
      return;
    }

    /* depth target */
    texTempl.format = DepthFormat;
    texTempl.nr_samples = texTempl.nr_storage_samples = m_samplesDepth;
    texTempl.bind = PIPE_BIND_DEPTH_STENCIL;
    m_depthTex = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
    if (!m_depthTex) {
      Log.report(logvisor::Fatal, fmt("Failed to create depth target texture"));
      return;
    }

    texTempl.nr_samples = texTempl.nr_storage_samples = 1;

    for (size_t i = 0; i < m_colorBindCount; ++i) {
      texTempl.format = ColorFormat;
      texTempl.bind = PIPE_BIND_SAMPLER_VIEW;
      m_colorBindTex[i] = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
      if (!m_colorBindTex[i]) {
        Log.report(logvisor::Fatal, fmt("Failed to create color bind texture"));
        return;
      }
    }

    for (size_t i = 0; i < m_depthBindCount; ++i) {
      texTempl.format = DepthFormat;
      texTempl.bind = PIPE_BIND_SAMPLER_VIEW;
      m_depthBindTex[i] = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
      if (!m_depthBindTex[i]) {
        Log.report(logvisor::Fatal, fmt("Failed to create depth bind texture"));
        return;
      }
    }

    /* Create resource views */
    struct pipe_sampler_view svTempl = {};
    svTempl.format = ColorFormat;
    svTempl.swizzle_r = PIPE_SWIZZLE_X;
    svTempl.swizzle_g = PIPE_SWIZZLE_Y;
    svTempl.swizzle_b = PIPE_SWIZZLE_Z;
    svTempl.swizzle_a = PIPE_SWIZZLE_W;
    svTempl.texture = m_colorTex;
    m_colorView = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_colorTex, &svTempl);
    if (!m_colorView) {
      Log.report(logvisor::Fatal, fmt("Failed to create color sampler view"));
      return;
    }

    svTempl.format = DepthFormat;
    svTempl.texture = m_depthTex;
    m_depthView = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_depthTex, &svTempl);
    if (!m_depthView) {
      Log.report(logvisor::Fatal, fmt("Failed to create depth sampler view"));
      return;
    }

    svTempl.format = ColorFormat;
    for (size_t i = 0; i < m_colorBindCount; ++i) {
      svTempl.texture = m_colorBindTex[i];
      m_colorBindView[i] = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_colorBindTex[i], &svTempl);
      if (!m_colorBindView[i]) {
        Log.report(logvisor::Fatal, fmt("Failed to create color bind sampler view"));
        return;
      }
    }

    svTempl.format = DepthFormat;
    for (size_t i = 0; i < m_depthBindCount; ++i) {
      svTempl.texture = m_depthBindTex[i];
      m_depthBindView[i] = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_depthBindTex[i], &svTempl);
      if (!m_depthBindView[i]) {
        Log.report(logvisor::Fatal, fmt("Failed to create depth bind sampler view"));
        return;
      }
    }

    /* surfaces */
    struct pipe_surface surfTempl = {};
    surfTempl.format = ColorFormat;
    m_colorSurface = ctx->m_pctx->create_surface(ctx->m_pctx, m_colorTex, &surfTempl);
    if (!m_colorSurface) {
      Log.report(logvisor::Fatal, fmt("Failed to create color surface"));
      return;
    }

    surfTempl.format = DepthFormat;
    m_depthSurface = ctx->m_pctx->create_surface(ctx->m_pctx, m_depthTex, &surfTempl);
    if (!m_depthSurface) {
      Log.report(logvisor::Fatal, fmt("Failed to create depth surface"));
      return;
    }

    /* framebuffer */
    m_framebuffer.width = uint16_t(m_width);
    m_framebuffer.height = uint16_t(m_height);
    m_framebuffer.nr_cbufs = 1;
    m_framebuffer.cbufs[0] = m_colorSurface;
    m_framebuffer.zsbuf = m_depthSurface;
  }

  NXTextureR(const boo::ObjToken<BaseGraphicsData>& parent, NXContext* ctx, size_t width, size_t height,
             TextureClampMode clampMode, size_t colorBindCount, size_t depthBindCount)
  : GraphicsDataNode<ITextureR>(parent), m_ctx(ctx) {
    if (colorBindCount > MAX_BIND_TEXS)
      Log.report(logvisor::Fatal, fmt("too many color bindings for render texture"));
    if (depthBindCount > MAX_BIND_TEXS)
      Log.report(logvisor::Fatal, fmt("too many depth bindings for render texture"));

    if (m_samplesColor == 0)
      m_samplesColor = 1;
    if (m_samplesDepth == 0)
      m_samplesDepth = 1;
    setClampMode(clampMode);
    Setup(ctx);
  }

public:
  struct pipe_resource* m_colorTex;
  struct pipe_sampler_view* m_colorView = nullptr;

  struct pipe_resource* m_depthTex;
  struct pipe_sampler_view* m_depthView = nullptr;

  struct pipe_resource* m_colorBindTex[MAX_BIND_TEXS] = {};
  struct pipe_sampler_view* m_colorBindView[MAX_BIND_TEXS] = {};

  struct pipe_resource* m_depthBindTex[MAX_BIND_TEXS] = {};
  struct pipe_sampler_view* m_depthBindView[MAX_BIND_TEXS] = {};

  struct pipe_surface* m_colorSurface = nullptr;
  struct pipe_surface* m_depthSurface = nullptr;

  void* m_sampler = nullptr;

  struct pipe_framebuffer_state m_framebuffer = {};

  void setClampMode(TextureClampMode mode) { MakeSampler(m_ctx, m_sampler, mode, 1); }

  void doDestroy() {
    pipe_resource_reference(&m_colorTex, nullptr);
    pipe_sampler_view_reference(&m_colorView, nullptr);

    pipe_resource_reference(&m_depthTex, nullptr);
    pipe_sampler_view_reference(&m_depthView, nullptr);

    for (size_t i = 0; i < MAX_BIND_TEXS; ++i) {
      pipe_resource_reference(&m_colorBindTex[i], nullptr);
      pipe_sampler_view_reference(&m_colorBindView[i], nullptr);

      pipe_resource_reference(&m_depthBindTex[i], nullptr);
      pipe_sampler_view_reference(&m_depthBindView[i], nullptr);
    }

    pipe_surface_reference(&m_colorSurface, nullptr);
    pipe_surface_reference(&m_depthSurface, nullptr);
  }

  ~NXTextureR() { doDestroy(); }

  void resize(NXContext* ctx, size_t width, size_t height) {
    if (width < 1)
      width = 1;
    if (height < 1)
      height = 1;
    m_width = width;
    m_height = height;
    Setup(ctx);
  }
};

static const size_t SEMANTIC_SIZE_TABLE[] = {0, 12, 16, 12, 16, 16, 4, 8, 16, 16, 16};

static const pipe_format SEMANTIC_TYPE_TABLE[] = {PIPE_FORMAT_NONE,
                                                  PIPE_FORMAT_R32G32B32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32A32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32A32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32A32_FLOAT,
                                                  PIPE_FORMAT_R8G8B8A8_UNORM,
                                                  PIPE_FORMAT_R32G32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32A32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32A32_FLOAT,
                                                  PIPE_FORMAT_R32G32B32A32_FLOAT};

struct NXVertexFormat {
  void* m_vtxElem;
  size_t m_stride = 0;
  size_t m_instStride = 0;

  NXVertexFormat(NXContext* ctx, const VertexFormatInfo& info) {
    std::unique_ptr<pipe_vertex_element[]> attributes(new pipe_vertex_element[info.elementCount]);
    for (size_t i = 0; i < info.elementCount; ++i) {
      const VertexElementDescriptor* elemin = &info.elements[i];
      pipe_vertex_element& attribute = attributes[i];
      int semantic = int(elemin->semantic & boo::VertexSemantic::SemanticMask);
      attribute.src_format = SEMANTIC_TYPE_TABLE[semantic];
      if ((elemin->semantic & boo::VertexSemantic::Instanced) != boo::VertexSemantic::None) {
        attribute.vertex_buffer_index = 1;
        attribute.instance_divisor = 1;
        attribute.src_offset = m_instStride;
        m_instStride += SEMANTIC_SIZE_TABLE[semantic];
      } else {
        attribute.vertex_buffer_index = 0;
        attribute.instance_divisor = 0;
        attribute.src_offset = m_stride;
        m_stride += SEMANTIC_SIZE_TABLE[semantic];
      }
    }

    uint64_t key = XXH64(attributes.get(), sizeof(pipe_vertex_element) * info.elementCount, 0);
    auto search = ctx->m_vtxElemStates.find(key);
    if (search != ctx->m_vtxElemStates.end()) {
      m_vtxElem = search->second;
      return;
    }
    m_vtxElem = ctx->m_pctx->create_vertex_elements_state(ctx->m_pctx, info.elementCount, attributes.get());
    ctx->m_vtxElemStates[key] = m_vtxElem;
  }
};

static const pipe_prim_type PRIMITIVE_TABLE[] = {PIPE_PRIM_TRIANGLES, PIPE_PRIM_TRIANGLE_STRIP, PIPE_PRIM_PATCHES};

static const nx_shader_stage SHADER_TYPE_TABLE[] = {
    nx_shader_stage::NONE,     nx_shader_stage::VERTEX,    nx_shader_stage::FRAGMENT,
    nx_shader_stage::GEOMETRY, nx_shader_stage::TESS_CTRL, nx_shader_stage::TESS_EVAL,
};

class NXShaderStage : public GraphicsDataNode<IShaderStage> {
  friend class NXDataFactory;
  nx_shader_stage_object m_obj;
  NXShaderStage(const boo::ObjToken<BaseGraphicsData>& parent, NXContext* ctx, const uint8_t* data, size_t size,
                PipelineStage stage)
  : GraphicsDataNode<IShaderStage>(parent), m_obj(ctx->m_compiler.compile(SHADER_TYPE_TABLE[int(stage)], (char*)data)) {
    if (!m_obj)
      Log.report(logvisor::Fatal, fmt("Shader compile fail:\n%s\n"), m_obj.info_log());
  }

public:
  const nx_shader_stage_object* shader() const { return &m_obj; }
};

static const unsigned BLEND_FACTOR_TABLE[] = {
    PIPE_BLENDFACTOR_ZERO,          PIPE_BLENDFACTOR_ONE,           PIPE_BLENDFACTOR_SRC_COLOR,
    PIPE_BLENDFACTOR_INV_SRC_COLOR, PIPE_BLENDFACTOR_DST_COLOR,     PIPE_BLENDFACTOR_INV_DST_COLOR,
    PIPE_BLENDFACTOR_SRC_ALPHA,     PIPE_BLENDFACTOR_INV_SRC_ALPHA, PIPE_BLENDFACTOR_DST_ALPHA,
    PIPE_BLENDFACTOR_INV_DST_ALPHA, PIPE_BLENDFACTOR_SRC1_COLOR,    PIPE_BLENDFACTOR_INV_SRC1_COLOR};

static void MakeBlendState(NXContext* ctx, void*& bsOut, const AdditionalPipelineInfo& info) {
  uint32_t key = (uint32_t(info.srcFac) << 16) | (uint32_t(info.dstFac) << 3) | info.colorWrite << 2 |
                 info.alphaWrite << 1 | info.overwriteAlpha;
  auto search = ctx->m_blendStates.find(key);
  if (search != ctx->m_blendStates.end()) {
    bsOut = search->second;
    return;
  }

  pipe_blend_state bs = {};
  bs.rt->blend_enable = info.srcFac != BlendFactor::One || info.dstFac != BlendFactor::Zero;
  if (info.srcFac == BlendFactor::Subtract || info.dstFac == BlendFactor::Subtract) {
    bs.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
    bs.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_ONE;
    bs.rt[0].rgb_func = PIPE_BLEND_REVERSE_SUBTRACT;
    if (info.overwriteAlpha) {
      bs.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
      bs.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ZERO;
      bs.rt[0].alpha_func = PIPE_BLEND_ADD;
    } else {
      bs.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
      bs.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ONE;
      bs.rt[0].alpha_func = PIPE_BLEND_REVERSE_SUBTRACT;
    }
  } else {
    bs.rt[0].rgb_src_factor = BLEND_FACTOR_TABLE[int(info.srcFac)];
    bs.rt[0].rgb_dst_factor = BLEND_FACTOR_TABLE[int(info.dstFac)];
    bs.rt[0].rgb_func = PIPE_BLEND_ADD;
    if (info.overwriteAlpha) {
      bs.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
      bs.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ZERO;
    } else {
      bs.rt[0].alpha_src_factor = BLEND_FACTOR_TABLE[int(info.srcFac)];
      bs.rt[0].alpha_dst_factor = BLEND_FACTOR_TABLE[int(info.dstFac)];
    }
    bs.rt[0].alpha_func = PIPE_BLEND_ADD;
  }

  bs.rt[0].colormask =
      (info.colorWrite ? (PIPE_MASK_R | PIPE_MASK_G | PIPE_MASK_B) : 0) | (info.alphaWrite ? PIPE_MASK_A : 0);

  bsOut = ctx->m_pctx->create_blend_state(ctx->m_pctx, &bs);
  ctx->m_blendStates[key] = bsOut;
}

static void MakeRasterizerState(NXContext* ctx, void*& rasOut, const AdditionalPipelineInfo& info) {
  uint32_t key = uint32_t(info.culling);
  auto search = ctx->m_rasStates.find(key);
  if (search != ctx->m_rasStates.end()) {
    rasOut = search->second;
    return;
  }

  pipe_rasterizer_state ras = {};
  ras.clamp_fragment_color = 1;
  ras.front_ccw = 1;
  switch (info.culling) {
  case CullMode::None:
  default:
    ras.cull_face = PIPE_FACE_NONE;
    break;
  case CullMode::Backface:
    ras.cull_face = PIPE_FACE_BACK;
    break;
  case CullMode::Frontface:
    ras.cull_face = PIPE_FACE_FRONT;
    break;
  }
  ras.scissor = 1;
  ras.multisample = unsigned(ctx->m_sampleCount > 1);
  ras.half_pixel_center = 1;
  ras.bottom_edge_rule = 1;
  ras.depth_clip_near = 1;
  ras.depth_clip_far = 1;

  rasOut = ctx->m_pctx->create_rasterizer_state(ctx->m_pctx, &ras);
  ctx->m_rasStates[key] = rasOut;
}

static void MakeDepthStencilState(NXContext* ctx, void*& dsOut, const AdditionalPipelineInfo& info) {
  uint32_t key = (uint32_t(info.depthTest) << 16) | info.depthWrite;
  auto search = ctx->m_dsStates.find(key);
  if (search != ctx->m_dsStates.end()) {
    dsOut = search->second;
    return;
  }

  pipe_depth_stencil_alpha_state ds = {};
  ds.depth.enabled = info.depthTest != ZTest::None;
  ds.depth.writemask = info.depthWrite;
  switch (info.depthTest) {
  case ZTest::None:
  default:
    ds.depth.func = PIPE_FUNC_ALWAYS;
    break;
  case ZTest::LEqual:
    ds.depth.func = PIPE_FUNC_LEQUAL;
    break;
  case ZTest::Greater:
    ds.depth.func = PIPE_FUNC_GREATER;
    break;
  case ZTest::Equal:
    ds.depth.func = PIPE_FUNC_EQUAL;
    break;
  case ZTest::GEqual:
    ds.depth.func = PIPE_FUNC_GEQUAL;
    break;
  }

  dsOut = ctx->m_pctx->create_depth_stencil_alpha_state(ctx->m_pctx, &ds);
  ctx->m_dsStates[key] = dsOut;
}

class NXShaderPipeline : public GraphicsDataNode<IShaderPipeline> {
protected:
  friend class NXDataFactory;
  friend struct NXShaderDataBinding;
  NXVertexFormat m_vtxFmt;
  nx_linked_shader m_shader;
  Primitive m_prim;
  pipe_prim_type m_nxPrim;
  uint32_t m_patchSize;

  void* m_blendState;
  void* m_rasState;
  void* m_dsState;

  NXShaderPipeline(const boo::ObjToken<BaseGraphicsData>& parent, NXContext* ctx, ObjToken<IShaderStage> vertex,
                   ObjToken<IShaderStage> fragment, ObjToken<IShaderStage> geometry, ObjToken<IShaderStage> control,
                   ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
                   const AdditionalPipelineInfo& info)
  : GraphicsDataNode<IShaderPipeline>(parent), m_vtxFmt(ctx, vtxFmt), m_prim(info.prim), m_patchSize(info.patchSize) {
    m_nxPrim = PRIMITIVE_TABLE[int(m_prim)];
    MakeBlendState(ctx, m_blendState, info);
    MakeRasterizerState(ctx, m_rasState, info);
    MakeDepthStencilState(ctx, m_dsState, info);

    const nx_shader_stage_object* stages[5];
    unsigned numStages = 0;
    if (vertex)
      stages[numStages++] = vertex.cast<NXShaderStage>()->shader();
    if (control)
      stages[numStages++] = control.cast<NXShaderStage>()->shader();
    if (evaluation)
      stages[numStages++] = evaluation.cast<NXShaderStage>()->shader();
    if (geometry)
      stages[numStages++] = geometry.cast<NXShaderStage>()->shader();
    if (fragment)
      stages[numStages++] = fragment.cast<NXShaderStage>()->shader();
    std::string infoLog;
    m_shader = ctx->m_compiler.link(numStages, stages, &infoLog);
    if (!m_shader)
      Log.report(logvisor::Fatal, fmt("Unable to link shader:\n%s\n"), infoLog.c_str());
  }

public:
  NXShaderPipeline& operator=(const NXShaderPipeline&) = delete;
  NXShaderPipeline(const NXShaderPipeline&) = delete;
  void bind(struct pipe_context* pctx) const {
    const struct gl_shader_program* prog = m_shader.program();
    if (gl_linked_shader* fs = prog->_LinkedShaders[MESA_SHADER_FRAGMENT]) {
      struct st_fragment_program* p = (struct st_fragment_program*)fs->Program;
      pctx->bind_fs_state(pctx, p->variants->driver_shader);
    }
    if (gl_linked_shader* gs = prog->_LinkedShaders[MESA_SHADER_GEOMETRY]) {
      struct st_common_program* p = (struct st_common_program*)gs->Program;
      pctx->bind_gs_state(pctx, p->variants->driver_shader);
    }
    if (gl_linked_shader* tes = prog->_LinkedShaders[MESA_SHADER_TESS_EVAL]) {
      struct st_common_program* p = (struct st_common_program*)tes->Program;
      pctx->bind_tes_state(pctx, p->variants->driver_shader);
    }
    if (gl_linked_shader* tcs = prog->_LinkedShaders[MESA_SHADER_TESS_CTRL]) {
      struct st_common_program* p = (struct st_common_program*)tcs->Program;
      pctx->bind_tcs_state(pctx, p->variants->driver_shader);
    }
    if (gl_linked_shader* vs = prog->_LinkedShaders[MESA_SHADER_VERTEX]) {
      struct st_vertex_program* p = (struct st_vertex_program*)vs->Program;
      pctx->bind_vs_state(pctx, p->variants->driver_shader);
    }

    pctx->bind_blend_state(pctx, m_blendState);
    pctx->bind_rasterizer_state(pctx, m_rasState);
    pctx->bind_depth_stencil_alpha_state(pctx, m_dsState);
  }
};

static const nx_buffer_info* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx) {
  if (buf->dynamic()) {
    const NXGraphicsBufferD<BaseGraphicsData>* cbuf = static_cast<const NXGraphicsBufferD<BaseGraphicsData>*>(buf);
    return &cbuf->m_bufferInfo[idx];
  } else {
    const NXGraphicsBufferS* cbuf = static_cast<const NXGraphicsBufferS*>(buf);
    return &cbuf->m_bufferInfo;
  }
}

static const struct pipe_sampler_view* GetTextureGPUResource(const ITexture* tex, int idx, int bindIdx, bool depth,
                                                             void*& samplerOut) {
  switch (tex->type()) {
  case TextureType::Dynamic: {
    const NXTextureD* ctex = static_cast<const NXTextureD*>(tex);
    samplerOut = ctex->m_sampler;
    return ctex->m_gpuView[idx];
  }
  case TextureType::Static: {
    const NXTextureS* ctex = static_cast<const NXTextureS*>(tex);
    samplerOut = ctex->m_sampler;
    return ctex->m_gpuView;
  }
  case TextureType::StaticArray: {
    const NXTextureSA* ctex = static_cast<const NXTextureSA*>(tex);
    samplerOut = ctex->m_sampler;
    return ctex->m_gpuView;
  }
  case TextureType::Render: {
    const NXTextureR* ctex = static_cast<const NXTextureR*>(tex);
    samplerOut = ctex->m_sampler;
    return depth ? ctex->m_depthBindView[bindIdx] : ctex->m_colorBindView[bindIdx];
  }
  default:
    break;
  }
  return nullptr;
}

struct NXShaderDataBinding : GraphicsDataNode<IShaderDataBinding> {
  NXContext* m_ctx;
  boo::ObjToken<IShaderPipeline> m_pipeline;
  boo::ObjToken<IGraphicsBuffer> m_vbuf;
  boo::ObjToken<IGraphicsBuffer> m_instVbuf;
  boo::ObjToken<IGraphicsBuffer> m_ibuf;
  std::vector<boo::ObjToken<IGraphicsBuffer>> m_ubufs;
  std::vector<std::array<size_t, 2>> m_ubufOffs;
  struct BindTex {
    boo::ObjToken<ITexture> tex;
    int idx;
    bool depth;
  };
  std::vector<BindTex> m_texs;

  struct pipe_vertex_buffer m_vboBufs[2][2] = {{}, {}};
  std::vector<std::array<struct pipe_constant_buffer, 2>> m_ubufBinds[MESA_SHADER_STAGES];

  size_t m_vertOffset;
  size_t m_instOffset;

#ifndef NDEBUG
  /* Debugging aids */
  bool m_committed = false;
#endif

  NXShaderDataBinding(const boo::ObjToken<BaseGraphicsData>& d, NXDataFactoryImpl& factory,
                      const boo::ObjToken<IShaderPipeline>& pipeline, const boo::ObjToken<IGraphicsBuffer>& vbuf,
                      const boo::ObjToken<IGraphicsBuffer>& instVbuf, const boo::ObjToken<IGraphicsBuffer>& ibuf,
                      size_t ubufCount, const boo::ObjToken<IGraphicsBuffer>* ubufs, const size_t* ubufOffs,
                      const size_t* ubufSizes, size_t texCount, const boo::ObjToken<ITexture>* texs,
                      const int* bindIdxs, const bool* depthBinds, size_t baseVert, size_t baseInst)
  : GraphicsDataNode<IShaderDataBinding>(d)
  , m_ctx(factory.m_ctx)
  , m_pipeline(pipeline)
  , m_vbuf(vbuf)
  , m_instVbuf(instVbuf)
  , m_ibuf(ibuf) {
    NXShaderPipeline* cpipeline = m_pipeline.cast<NXShaderPipeline>();
    NXVertexFormat& vtxFmt = cpipeline->m_vtxFmt;
    m_vertOffset = baseVert * vtxFmt.m_stride;
    m_instOffset = baseInst * vtxFmt.m_instStride;

    m_ubufs.reserve(ubufCount);
    if (ubufOffs && ubufSizes)
      m_ubufOffs.reserve(ubufCount);
    for (size_t i = 0; i < ubufCount; ++i) {
#ifndef NDEBUG
      if (!ubufs[i])
        Log.report(logvisor::Fatal, fmt("null uniform-buffer %d provided to newShaderDataBinding"), int(i));
#endif
      m_ubufs.push_back(ubufs[i]);
      if (ubufOffs && ubufSizes)
        m_ubufOffs.push_back({ubufOffs[i], ubufSizes[i]});
    }
    m_texs.reserve(texCount);
    for (size_t i = 0; i < texCount; ++i) {
      m_texs.push_back({texs[i], bindIdxs ? bindIdxs[i] : 0, depthBinds ? depthBinds[i] : false});
    }
  }

  void commit() {
    struct pipe_context* pctx = m_ctx->m_pctx;

    for (int i = 0; i < 2; ++i) {
      if (m_vbuf) {
        m_vboBufs[i][0] = GetBufferGPUResource(m_vbuf.get(), i)->v;
        m_vboBufs[i][0].buffer_offset += m_vertOffset;
      }
      if (m_instVbuf) {
        m_vboBufs[i][1] = GetBufferGPUResource(m_instVbuf.get(), i)->v;
        m_vboBufs[i][1].buffer_offset += m_instOffset;
      }
    }

    NXShaderPipeline* cpipeline = m_pipeline.cast<NXShaderPipeline>();
    const struct gl_shader_program* program = cpipeline->m_shader.program();
    for (uint i = 0; i < MESA_SHADER_STAGES; ++i) {
      if (const struct gl_linked_shader* lsh = program->_LinkedShaders[i]) {
        std::vector<std::array<struct pipe_constant_buffer, 2>>& bindings = m_ubufBinds[i];
        const struct gl_shader_program_data* data = lsh->Program->sh.data;
        bindings.reserve(data->NumUniformBlocks);
        for (uint j = 0; j < data->NumUniformBlocks; ++j) {
          const struct gl_uniform_block* block = &data->UniformBlocks[j];
          assert(block->Binding < m_ubufs.size() && "Uniform binding oob");
          bindings.emplace_back();
          for (int k = 0; k < 2; ++k) {
            struct pipe_constant_buffer& bufBind = bindings.back()[k];
            const nx_buffer_info* buf = GetBufferGPUResource(m_ubufs[block->Binding].get(), k);
            bufBind = buf->c;
            if (!m_ubufOffs.empty()) {
              bufBind.buffer_offset += m_ubufOffs[block->Binding][0];
              bufBind.buffer_size = unsigned(m_ubufOffs[block->Binding][1]);
            }
          }
        }
      }
    }

#ifndef NDEBUG
    m_committed = true;
#endif
  }

  void bind(int b) {
#ifndef NDEBUG
    if (!m_committed)
      Log.report(logvisor::Fatal, fmt("attempted to use uncommitted NXShaderDataBinding"));
#endif
    struct pipe_context* pctx = m_ctx->m_pctx;

    NXShaderPipeline* pipeline = m_pipeline.cast<NXShaderPipeline>();
    pipeline->bind(pctx);
    const struct gl_shader_program* program = pipeline->m_shader.program();
    for (uint i = 0; i < MESA_SHADER_STAGES; ++i) {
      uint j = 0;
      for (const auto& bind : m_ubufBinds[i])
        pctx->set_constant_buffer(pctx, pipe_shader_type(i), j++, &bind[b]);

      if (const struct gl_linked_shader* lsh = program->_LinkedShaders[i]) {
        void* samplers[BOO_GLSL_MAX_TEXTURE_COUNT] = {};
        struct pipe_sampler_view* samplerViews[BOO_GLSL_MAX_TEXTURE_COUNT] = {};
        unsigned numSamplers = 0;
        const struct gl_program* stprogram = lsh->Program;
        for (int t = 0; t < BOO_GLSL_MAX_TEXTURE_COUNT; ++t) {
          if (stprogram->SamplersUsed & (1 << t)) {
            GLubyte unit = GLubyte(stprogram->SamplerUnits[t] - BOO_GLSL_MAX_UNIFORM_COUNT);
            assert(unit < m_texs.size() && "Texture binding oob");
            const BindTex& tex = m_texs[unit];
            samplerViews[numSamplers] =
                (pipe_sampler_view*)GetTextureGPUResource(tex.tex.get(), t, tex.idx, tex.depth, samplers[numSamplers]);
            ++numSamplers;
          }
        }
        pctx->bind_sampler_states(pctx, pipe_shader_type(i), 0, numSamplers, samplers);
        pctx->set_sampler_views(pctx, pipe_shader_type(i), 0, numSamplers, samplerViews);
      }
    }

    if (m_vbuf && m_instVbuf)
      pctx->set_vertex_buffers(pctx, 0, 2, m_vboBufs[b]);
    else if (m_vbuf)
      pctx->set_vertex_buffers(pctx, 0, 1, m_vboBufs[b]);
    else if (m_instVbuf)
      pctx->set_vertex_buffers(pctx, 1, 1, &m_vboBufs[b][1]);
  }

  pipe_prim_type getPrimitive() const { return m_pipeline.cast<NXShaderPipeline>()->m_nxPrim; }
  uint32_t getPatchVerts() const { return m_pipeline.cast<NXShaderPipeline>()->m_patchSize; }
  struct pipe_resource* getIndexBuf(int b) const {
    return GetBufferGPUResource(m_ibuf.get(), b)->v.buffer.resource;
  }
};

struct NXCommandQueue : IGraphicsCommandQueue {
  Platform platform() const { return IGraphicsDataFactory::Platform::Vulkan; }
  const SystemChar* platformName() const { return _SYS_STR("NX"); }
  NXContext* m_ctx;
  IGraphicsContext* m_parent;

  bool m_running = true;

  int m_fillBuf = 0;
  int m_drawBuf = 0;

  std::vector<boo::ObjToken<boo::IObj>> m_drawResTokens[2];

  NXCommandQueue(NXContext* ctx, IGraphicsContext* parent) : m_ctx(ctx), m_parent(parent) {}

  void startRenderer() { static_cast<NXDataFactoryImpl*>(m_parent->getDataFactory())->SetupGammaResources(); }

  void stopRenderer() {
    m_running = false;
    static_cast<NXDataFactoryImpl*>(m_parent->getDataFactory())->DestroyGammaResources();
    m_drawResTokens[0].clear();
    m_drawResTokens[1].clear();
    m_boundTarget.reset();
    m_resolveDispSource.reset();
  }

  ~NXCommandQueue() {
    if (m_running)
      stopRenderer();
  }

  boo::ObjToken<IShaderDataBinding> m_curSDBinding;
  void setShaderDataBinding(const boo::ObjToken<IShaderDataBinding>& binding) {
    m_curSDBinding = binding;
    NXShaderDataBinding* cbind = binding.cast<NXShaderDataBinding>();
    cbind->bind(m_fillBuf);
    m_drawResTokens[m_fillBuf].push_back(binding.get());
  }

  boo::ObjToken<ITextureR> m_boundTarget;
  void setRenderTarget(const boo::ObjToken<ITextureR>& target) {
    NXTextureR* ctarget = target.cast<NXTextureR>();

    if (m_boundTarget.get() != ctarget) {
      m_boundTarget = target;
      m_drawResTokens[m_fillBuf].push_back(target.get());
    }

    m_ctx->m_pctx->set_framebuffer_state(m_ctx->m_pctx, &ctarget->m_framebuffer);
  }

  void setViewport(const SWindowRect& rect, float znear, float zfar) {
    if (m_boundTarget) {
      NXTextureR* ctarget = m_boundTarget.cast<NXTextureR>();

      struct gl_context* ctx = m_ctx->m_st->ctx;
      ctx->ViewportArray[0].X = float(rect.location[0]);
      ctx->ViewportArray[0].Y = float(std::max(0, int(ctarget->m_height) - rect.location[1] - rect.size[1]));
      ctx->ViewportArray[0].Width = float(rect.size[0]);
      ctx->ViewportArray[0].Height = float(rect.size[1]);
      ctx->ViewportArray[0].Near = znear;
      ctx->ViewportArray[0].Far = zfar;

      pipe_viewport_state vp = {};
      _mesa_get_viewport_xform(ctx, 0, vp.scale, vp.translate);
      m_ctx->m_pctx->set_viewport_states(m_ctx->m_pctx, 0, 1, &vp);
    }
  }

  void setScissor(const SWindowRect& rect) {
    if (m_boundTarget) {
      NXTextureR* ctarget = m_boundTarget.cast<NXTextureR>();

      pipe_scissor_state scissor = {};
      scissor.minx = unsigned(rect.location[0]);
      scissor.miny = unsigned(std::max(0, int(ctarget->m_height) - rect.location[1] - rect.size[1]));
      scissor.maxx = scissor.minx + unsigned(rect.size[0]);
      scissor.maxy = scissor.miny + unsigned(rect.size[1]);

      m_ctx->m_pctx->set_scissor_states(m_ctx->m_pctx, 0, 1, &scissor);
    }
  }

  std::unordered_map<NXTextureR*, std::pair<size_t, size_t>> m_texResizes;
  void resizeRenderTexture(const boo::ObjToken<ITextureR>& tex, size_t width, size_t height) {
    NXTextureR* ctex = tex.cast<NXTextureR>();
    m_texResizes[ctex] = std::make_pair(width, height);
    m_drawResTokens[m_fillBuf].push_back(tex.get());
  }

  void schedulePostFrameHandler(std::function<void(void)>&& func) { func(); }

  float m_clearColor[4] = {0.0, 0.0, 0.0, 0.0};
  void setClearColor(const float rgba[4]) {
    m_clearColor[0] = rgba[0];
    m_clearColor[1] = rgba[1];
    m_clearColor[2] = rgba[2];
    m_clearColor[3] = rgba[3];
  }

  void clearTarget(bool render = true, bool depth = true) {
    if (!m_boundTarget)
      return;
    unsigned buffers = 0;
    if (render)
      buffers |= PIPE_CLEAR_COLOR0;
    if (depth)
      buffers |= PIPE_CLEAR_DEPTH;
    pipe_color_union cunion;
    for (int i = 0; i < 4; ++i)
      cunion.f[i] = m_clearColor[i];
    m_ctx->m_pctx->clear(m_ctx->m_pctx, buffers, &cunion, 1.f, 0);
  }

  void draw(size_t start, size_t count) {
    pipe_draw_info info = {};
    NXShaderDataBinding* sdBinding = m_curSDBinding.cast<NXShaderDataBinding>();
    info.mode = sdBinding->getPrimitive();
    info.vertices_per_patch = sdBinding->getPatchVerts();
    info.start = start;
    info.count = count;
    m_ctx->m_pctx->draw_vbo(m_ctx->m_pctx, &info);
  }

  void drawIndexed(size_t start, size_t count) {
    pipe_draw_info info = {};
    NXShaderDataBinding* sdBinding = m_curSDBinding.cast<NXShaderDataBinding>();
    info.index_size = 4;
    info.mode = sdBinding->getPrimitive();
    info.primitive_restart = 1;
    info.vertices_per_patch = sdBinding->getPatchVerts();
    info.start = start;
    info.count = count;
    info.restart_index = 0xffffffff;
    info.index.resource = sdBinding->getIndexBuf(m_fillBuf);
    m_ctx->m_pctx->draw_vbo(m_ctx->m_pctx, &info);
  }

  void drawInstances(size_t start, size_t count, size_t instCount) {
    pipe_draw_info info = {};
    NXShaderDataBinding* sdBinding = m_curSDBinding.cast<NXShaderDataBinding>();
    info.mode = sdBinding->getPrimitive();
    info.vertices_per_patch = sdBinding->getPatchVerts();
    info.start = start;
    info.count = count;
    info.instance_count = instCount;
    m_ctx->m_pctx->draw_vbo(m_ctx->m_pctx, &info);
  }

  void drawInstancesIndexed(size_t start, size_t count, size_t instCount) {
    pipe_draw_info info = {};
    NXShaderDataBinding* sdBinding = m_curSDBinding.cast<NXShaderDataBinding>();
    info.index_size = 4;
    info.mode = sdBinding->getPrimitive();
    info.primitive_restart = 1;
    info.vertices_per_patch = sdBinding->getPatchVerts();
    info.start = start;
    info.count = count;
    info.instance_count = instCount;
    info.restart_index = 0xffffffff;
    info.index.resource = sdBinding->getIndexBuf(m_fillBuf);
    m_ctx->m_pctx->draw_vbo(m_ctx->m_pctx, &info);
  }

  boo::ObjToken<ITextureR> m_resolveDispSource;
  void resolveDisplay(const boo::ObjToken<ITextureR>& source) { m_resolveDispSource = source; }

  bool _resolveDisplay() {
    if (!m_resolveDispSource)
      return false;

    NXTextureR* csource = m_resolveDispSource.cast<NXTextureR>();
#ifndef NDEBUG
    if (!csource->m_colorBindCount)
      Log.report(logvisor::Fatal, fmt("texture provided to resolveDisplay() must have at least 1 color binding"));
#endif

    struct pipe_surface* backBuf = m_ctx->m_windowSurfaces[ST_ATTACHMENT_BACK_LEFT];

    NXDataFactoryImpl* dataFactory = static_cast<NXDataFactoryImpl*>(m_parent->getDataFactory());
    if (dataFactory->m_gamma != 1.f) {
      SWindowRect rect(0, 0, csource->m_width, csource->m_height);
      _resolveBindTexture(csource, rect, true, 0, true, false);
      NXShaderDataBinding* gammaBinding = dataFactory->m_gammaBinding.cast<NXShaderDataBinding>();

      pipe_framebuffer_state fstate = {};
      fstate.width = backBuf->texture->width0;
      fstate.height = backBuf->texture->height0;
      fstate.nr_cbufs = 1;
      fstate.cbufs[0] = backBuf;
      m_ctx->m_pctx->set_framebuffer_state(m_ctx->m_pctx, &fstate);

      gammaBinding->m_texs[0].tex = m_resolveDispSource.get();
      gammaBinding->bind(m_drawBuf);
      pipe_draw_info info = {};
      info.mode = PIPE_PRIM_TRIANGLE_STRIP;
      info.start = 0;
      info.count = 4;
      info.instance_count = 1;
      m_ctx->m_pctx->draw_vbo(m_ctx->m_pctx, &info);
      gammaBinding->m_texs[0].tex.reset();
    } else {
      pipe_blit_info binfo = {};
      binfo.src.resource = csource->m_colorTex;
      binfo.dst.resource = backBuf->texture;
      u_box_2d(0, 0, csource->m_width, csource->m_height, &binfo.src.box);
      binfo.dst.box = binfo.src.box;
      binfo.src.format = binfo.dst.format = PIPE_FORMAT_R8G8B8A8_UNORM;
      binfo.mask = PIPE_MASK_RGBA;
      binfo.filter = PIPE_TEX_FILTER_NEAREST;
      m_ctx->m_pctx->blit(m_ctx->m_pctx, &binfo);
    }

    m_resolveDispSource.reset();
    return true;
  }

  void _resolveBindTexture(NXTextureR* ctexture, const SWindowRect& rect, bool tlOrigin, int bindIdx, bool color,
                           bool depth) {
    SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, ctexture->m_width, ctexture->m_height));

    if (color && ctexture->m_colorBindCount) {
      pipe_blit_info binfo = {};
      binfo.src.resource = ctexture->m_colorTex;
      binfo.dst.resource = ctexture->m_colorBindTex[bindIdx];
      u_box_2d(intersectRect.location[0],
               tlOrigin ? intersectRect.location[1]
                        : (ctexture->m_height - intersectRect.size[1] - intersectRect.location[1]),
               intersectRect.size[0], intersectRect.size[1], &binfo.src.box);
      binfo.dst.box = binfo.src.box;
      binfo.src.format = binfo.dst.format = PIPE_FORMAT_R8G8B8A8_UNORM;
      binfo.mask = PIPE_MASK_RGBA;
      binfo.filter = PIPE_TEX_FILTER_NEAREST;
      m_ctx->m_pctx->blit(m_ctx->m_pctx, &binfo);
    }

    if (depth && ctexture->m_depthBindCount) {
      pipe_blit_info binfo = {};
      binfo.src.resource = ctexture->m_depthTex;
      binfo.dst.resource = ctexture->m_depthBindTex[bindIdx];
      u_box_2d(intersectRect.location[0],
               tlOrigin ? intersectRect.location[1]
                        : (ctexture->m_height - intersectRect.size[1] - intersectRect.location[1]),
               intersectRect.size[0], intersectRect.size[1], &binfo.src.box);
      binfo.dst.box = binfo.src.box;
      binfo.src.format = binfo.dst.format = PIPE_FORMAT_Z32_FLOAT;
      binfo.mask = PIPE_MASK_Z;
      binfo.filter = PIPE_TEX_FILTER_NEAREST;
      m_ctx->m_pctx->blit(m_ctx->m_pctx, &binfo);
    }
  }

  void resolveBindTexture(const boo::ObjToken<ITextureR>& texture, const SWindowRect& rect, bool tlOrigin, int bindIdx,
                          bool color, bool depth, bool clearDepth) {
    NXTextureR* ctexture = texture.cast<NXTextureR>();
    _resolveBindTexture(ctexture, rect, tlOrigin, bindIdx, color, depth);
    if (clearDepth)
      m_ctx->m_pctx->clear(m_ctx->m_pctx, PIPE_CLEAR_DEPTH, nullptr, 1.f, 0);
  }

  void execute();
};

NXDataFactory::Context::Context(NXDataFactory& parent __BooTraceArgs)
: m_parent(parent), m_data(new NXData(static_cast<NXDataFactoryImpl&>(parent) __BooTraceArgsUse)) {}
NXDataFactory::Context::~Context() {}

boo::ObjToken<IGraphicsBufferS> NXDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride,
                                                                        size_t count) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXGraphicsBufferS(m_data, use, factory.m_ctx, data, stride, count)};
}

boo::ObjToken<IGraphicsBufferD> NXDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXGraphicsBufferD<BaseGraphicsData>(m_data, use, factory.m_ctx, stride, count)};
}

boo::ObjToken<ITextureS> NXDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips,
                                                                  TextureFormat fmt, TextureClampMode clampMode,
                                                                  const void* data, size_t sz) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXTextureS(m_data, factory.m_ctx, width, height, mips, fmt, clampMode, data, sz)};
}

boo::ObjToken<ITextureSA> NXDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers,
                                                                        size_t mips, TextureFormat fmt,
                                                                        TextureClampMode clampMode, const void* data,
                                                                        size_t sz) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXTextureSA(m_data, factory.m_ctx, width, height, layers, mips, fmt, clampMode, data, sz)};
}

boo::ObjToken<ITextureD> NXDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt,
                                                                   TextureClampMode clampMode) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  NXCommandQueue* q = static_cast<NXCommandQueue*>(factory.m_parent->getCommandQueue());
  return {new NXTextureD(m_data, q, width, height, fmt, clampMode)};
}

boo::ObjToken<ITextureR> NXDataFactory::Context::newRenderTexture(size_t width, size_t height,
                                                                  TextureClampMode clampMode, size_t colorBindCount,
                                                                  size_t depthBindCount) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXTextureR(m_data, factory.m_ctx, width, height, clampMode, colorBindCount, depthBindCount)};
}

ObjToken<IShaderStage> NXDataFactory::Context::newShaderStage(const uint8_t* data, size_t size, PipelineStage stage) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXShaderStage(m_data, factory.m_ctx, data, size, stage)};
}

ObjToken<IShaderPipeline> NXDataFactory::Context::newShaderPipeline(
    ObjToken<IShaderStage> vertex, ObjToken<IShaderStage> fragment, ObjToken<IShaderStage> geometry,
    ObjToken<IShaderStage> control, ObjToken<IShaderStage> evaluation, const VertexFormatInfo& vtxFmt,
    const AdditionalPipelineInfo& info) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXShaderPipeline(m_data, factory.m_ctx, vertex, fragment, geometry, control, evaluation, vtxFmt, info)};
}

boo::ObjToken<IShaderDataBinding> NXDataFactory::Context::newShaderDataBinding(
    const boo::ObjToken<IShaderPipeline>& pipeline, const boo::ObjToken<IGraphicsBuffer>& vbo,
    const boo::ObjToken<IGraphicsBuffer>& instVbo, const boo::ObjToken<IGraphicsBuffer>& ibo, size_t ubufCount,
    const boo::ObjToken<IGraphicsBuffer>* ubufs, const PipelineStage* ubufStages, const size_t* ubufOffs,
    const size_t* ubufSizes, size_t texCount, const boo::ObjToken<ITexture>* texs, const int* bindIdxs,
    const bool* bindDepth, size_t baseVert, size_t baseInst) {
  NXDataFactoryImpl& factory = static_cast<NXDataFactoryImpl&>(m_parent);
  return {new NXShaderDataBinding(m_data, factory, pipeline, vbo, instVbo, ibo, ubufCount, ubufs, ubufOffs, ubufSizes,
                                  texCount, texs, bindIdxs, bindDepth, baseVert, baseInst)};
}

NXTextureD::NXTextureD(const boo::ObjToken<BaseGraphicsData>& parent, NXCommandQueue* q, size_t width, size_t height,
                       TextureFormat fmt, TextureClampMode clampMode)
: GraphicsDataNode<ITextureD>(parent), m_q(q), m_width(width), m_height(height), m_fmt(fmt), m_clampMode(clampMode) {
  NXContext* ctx = m_q->m_ctx;
  pipe_format pfmt;
  switch (fmt) {
  case TextureFormat::RGBA8:
    pfmt = PIPE_FORMAT_R8G8B8A8_UNORM;
    m_cpuSz = width * height * 4;
    break;
  case TextureFormat::I8:
    pfmt = PIPE_FORMAT_R8_UNORM;
    m_cpuSz = width * height;
    break;
  case TextureFormat::I16:
    pfmt = PIPE_FORMAT_R16_UNORM;
    m_cpuSz = width * height * 2;
    break;
  default:
    Log.report(logvisor::Fatal, fmt("unsupported tex format"));
  }
  m_nxFmt = pfmt;
  m_stagingBuf.reset(new uint8_t[m_cpuSz]);

  struct pipe_resource texTempl = {};
  texTempl.target = PIPE_TEXTURE_2D;
  texTempl.format = m_nxFmt;
  texTempl.width0 = width;
  texTempl.height0 = height;
  texTempl.depth0 = 1;
  texTempl.array_size = 1;
  texTempl.bind = PIPE_BIND_SAMPLER_VIEW;
  for (int i = 0; i < 2; ++i) {
    m_gpuTex[i] = ctx->m_screen->resource_create(ctx->m_screen, &texTempl);
    if (!m_gpuTex[i]) {
      Log.report(logvisor::Fatal, fmt("Failed to create texture"));
      return;
    }
  }

  struct pipe_sampler_view svTempl = {};
  svTempl.format = m_nxFmt;
  svTempl.swizzle_r = PIPE_SWIZZLE_X;
  svTempl.swizzle_g = PIPE_SWIZZLE_Y;
  svTempl.swizzle_b = PIPE_SWIZZLE_Z;
  svTempl.swizzle_a = PIPE_SWIZZLE_W;
  for (int i = 0; i < 2; ++i) {
    svTempl.texture = m_gpuTex[i];
    m_gpuView[i] = ctx->m_pctx->create_sampler_view(ctx->m_pctx, m_gpuTex[i], &svTempl);
  }
}

void NXTextureD::update(int b) {
  int slot = 1 << b;
  if ((slot & m_validSlots) == 0) {
    NXContext* ctx = m_q->m_ctx;
    uint blockSize = util_format_get_blocksize(m_nxFmt);
    uint8_t* ptr = m_stagingBuf.get();
    size_t rowStride = m_width * blockSize;
    size_t imageBytes = rowStride * m_height;

    struct pipe_box box;
    u_box_2d(0, 0, m_width, m_height, &box);

    ctx->m_pctx->texture_subdata(ctx->m_pctx, m_gpuTex[m_q->m_fillBuf], 0, PIPE_TRANSFER_WRITE, &box, ptr, rowStride,
                                 imageBytes);

    m_validSlots |= slot;
  }
}

void NXTextureD::setClampMode(TextureClampMode mode) {
  m_clampMode = mode;
  MakeSampler(m_q->m_ctx, m_sampler, mode, 1);
}

void NXTextureD::load(const void* data, size_t sz) {
  size_t bufSz = std::min(sz, m_cpuSz);
  memmove(m_stagingBuf.get(), data, bufSz);
  m_validSlots = 0;
}

void* NXTextureD::map(size_t sz) {
  if (sz > m_cpuSz)
    return nullptr;
  return m_stagingBuf.get();
}

void NXTextureD::unmap() { m_validSlots = 0; }

static inline struct pipe_resource* pipe_buffer_create_flags(struct pipe_screen* screen, unsigned bind,
                                                             enum pipe_resource_usage usage, unsigned flags,
                                                             unsigned size) {
  struct pipe_resource buffer;
  memset(&buffer, 0, sizeof buffer);
  buffer.target = PIPE_BUFFER;
  buffer.format = PIPE_FORMAT_R8_UNORM; /* want TYPELESS or similar */
  buffer.bind = bind;
  buffer.usage = usage;
  buffer.flags = flags;
  buffer.width0 = size;
  buffer.height0 = 1;
  buffer.depth0 = 1;
  buffer.array_size = 1;
  return screen->resource_create(screen, &buffer);
}

void NXDataFactoryImpl::commitTransaction(
    const std::function<bool(IGraphicsDataFactory::Context&)>& trans __BooTraceArgs) {
  Context ctx(*this __BooTraceArgsUse);
  if (!trans(ctx))
    return;

  NXData* data = ctx.m_data.cast<NXData>();

  /* size up resources */
  unsigned constantMemSizes[3] = {};

  if (data->m_SBufs)
    for (IGraphicsBufferS& buf : *data->m_SBufs) {
      auto& cbuf = static_cast<NXGraphicsBufferS&>(buf);
      if (cbuf.m_use == BufferUse::Null)
        continue;
      unsigned& sz = constantMemSizes[int(cbuf.m_use) - 1];
      sz = cbuf.sizeForGPU(m_ctx, sz);
    }

  if (data->m_DBufs)
    for (IGraphicsBufferD& buf : *data->m_DBufs) {
      auto& cbuf = static_cast<NXGraphicsBufferD<BaseGraphicsData>&>(buf);
      if (cbuf.m_use == BufferUse::Null)
        continue;
      unsigned& sz = constantMemSizes[int(cbuf.m_use) - 1];
      sz = cbuf.sizeForGPU(m_ctx, sz);
    }

  /* allocate memory and place buffers */
  for (int i = 0; i < 3; ++i) {
    if (constantMemSizes[i]) {
      struct pipe_resource* poolBuf = pipe_buffer_create_flags(
          m_ctx->m_screen, USE_TABLE[i + 1], PIPE_USAGE_DEFAULT,
          PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT, constantMemSizes[i]);
      data->m_constantBuffers[i] = poolBuf;
      pipe_transfer* xfer;
      uint8_t* mappedData = (uint8_t*)pipe_buffer_map(
          m_ctx->m_pctx, poolBuf,
          PIPE_TRANSFER_WRITE | PIPE_TRANSFER_MAP_DIRECTLY | PIPE_TRANSFER_PERSISTENT | PIPE_TRANSFER_COHERENT, &xfer);

      if (data->m_SBufs)
        for (IGraphicsBufferS& buf : *data->m_SBufs) {
          auto& cbuf = static_cast<NXGraphicsBufferS&>(buf);
          if (int(cbuf.m_use) - 1 != i)
            continue;
          cbuf.placeForGPU(poolBuf, mappedData);
        }

      if (data->m_DBufs)
        for (IGraphicsBufferD& buf : *data->m_DBufs) {
          auto& cbuf = static_cast<NXGraphicsBufferD<BaseGraphicsData>&>(buf);
          if (int(cbuf.m_use) - 1 != i)
            continue;
          cbuf.placeForGPU(poolBuf, mappedData);
        }
    }
  }

  /* Commit data bindings (create descriptor sets) */
  if (data->m_SBinds)
    for (IShaderDataBinding& bind : *data->m_SBinds)
      static_cast<NXShaderDataBinding&>(bind).commit();
}

boo::ObjToken<IGraphicsBufferD> NXDataFactoryImpl::newPoolBuffer(BufferUse use, size_t stride,
                                                                 size_t count __BooTraceArgs) {
  boo::ObjToken<BaseGraphicsPool> pool(new NXPool(*this __BooTraceArgsUse));
  NXPool* cpool = pool.cast<NXPool>();
  NXGraphicsBufferD<BaseGraphicsPool>* retval =
      new NXGraphicsBufferD<BaseGraphicsPool>(pool, use, m_ctx, stride, count);

  unsigned size = retval->sizeForGPU(m_ctx, 0);

  /* allocate memory */
  if (size) {
    struct pipe_resource* poolBuf =
        pipe_buffer_create_flags(m_ctx->m_screen, USE_TABLE[int(use)], PIPE_USAGE_DEFAULT,
                                 PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT, size);
    cpool->m_constantBuffer = poolBuf;
    pipe_transfer* xfer;
    uint8_t* mappedData = (uint8_t*)pipe_buffer_map(
        m_ctx->m_pctx, poolBuf,
        PIPE_TRANSFER_WRITE | PIPE_TRANSFER_MAP_DIRECTLY | PIPE_TRANSFER_PERSISTENT | PIPE_TRANSFER_COHERENT, &xfer);
    retval->placeForGPU(poolBuf, mappedData);
  }

  return {retval};
}

void NXCommandQueue::execute() {
  if (!m_running)
    return;

  /* Stage dynamic uploads */
  NXDataFactoryImpl* gfxF = static_cast<NXDataFactoryImpl*>(m_parent->getDataFactory());
  std::unique_lock<std::recursive_mutex> datalk(gfxF->m_dataMutex);
  if (gfxF->m_dataHead) {
    for (BaseGraphicsData& d : *gfxF->m_dataHead) {
      if (d.m_DBufs)
        for (IGraphicsBufferD& b : *d.m_DBufs)
          static_cast<NXGraphicsBufferD<BaseGraphicsData>&>(b).update(m_fillBuf);
      if (d.m_DTexs)
        for (ITextureD& t : *d.m_DTexs)
          static_cast<NXTextureD&>(t).update(m_fillBuf);
    }
  }
  if (gfxF->m_poolHead) {
    for (BaseGraphicsPool& p : *gfxF->m_poolHead) {
      if (p.m_DBufs)
        for (IGraphicsBufferD& b : *p.m_DBufs)
          static_cast<NXGraphicsBufferD<BaseGraphicsData>&>(b).update(m_fillBuf);
    }
  }
  datalk.unlock();

  /* Perform texture and swap-chain resizes */
  if (m_ctx->_resizeWindowSurfaces() || m_texResizes.size()) {
    for (const auto& resize : m_texResizes) {
      if (m_boundTarget.get() == resize.first)
        m_boundTarget.reset();
      resize.first->resize(m_ctx, resize.second.first, resize.second.second);
    }
    m_texResizes.clear();
    m_resolveDispSource = nullptr;
    return;
  }

  /* Clear dead data */
  m_drawResTokens[m_drawBuf].clear();

  /* Swap internal buffers */
  m_drawBuf = m_fillBuf;
  m_fillBuf ^= 1;

  /* Flush the pipe */
  m_ctx->m_pctx->flush(m_ctx->m_pctx, nullptr, PIPE_FLUSH_END_OF_FRAME);

  /* Set framebuffer fence */
  NvFence fence;
  struct pipe_surface* old_back = m_ctx->m_windowSurfaces[ST_ATTACHMENT_BACK_LEFT];
  fence.id = nouveau_switch_resource_get_syncpoint(old_back->texture, &fence.value);
  if ((int)fence.id >= 0) {
    NvFence* surf_fence = &m_ctx->m_fences[m_ctx->m_fence_swap];
    if (surf_fence->id != fence.id || surf_fence->value != fence.value) {
      *surf_fence = fence;

      NvMultiFence mf;
      nvMultiFenceCreate(&mf, &fence);
      gfxAppendFence(&mf);
    }
  }

  gfxSwapBuffers();

  /* Swap buffer attachments and invalidate framebuffer */
  m_ctx->m_fence_swap = !m_ctx->m_fence_swap;
  m_ctx->m_windowSurfaces[ST_ATTACHMENT_BACK_LEFT] = m_ctx->m_windowSurfaces[ST_ATTACHMENT_FRONT_LEFT];
  m_ctx->m_windowSurfaces[ST_ATTACHMENT_FRONT_LEFT] = old_back;
}

static void setMesaConfig() {
  // Uncomment below to disable error checking and save CPU time (useful for production):
  // setenv("MESA_NO_ERROR", "1", 1);

  // Uncomment below to enable Mesa logging:
  setenv("EGL_LOG_LEVEL", "debug", 1);
  setenv("MESA_VERBOSE", "all", 1);
  setenv("NOUVEAU_MESA_DEBUG", "1", 1);

  // Uncomment below to enable shader debugging in Nouveau:
  setenv("NV50_PROG_OPTIMIZE", "0", 1);
  setenv("NV50_PROG_DEBUG", "1", 1);
  setenv("NV50_PROG_CHIPSET", "0x120", 1);
}

bool NXContext::initialize() {
  /* Set mesa configuration (useful for debugging) */
  setMesaConfig();

  gfxInitDefault();
  gfxSetMode(GfxMode_TiledDouble);
  consoleInit(nullptr);
  printf("Activated console\n\n");
  m_screen = nouveau_switch_screen_create();
  if (!m_screen) {
    Log.report(logvisor::Fatal, fmt("Failed to create nouveau screen"));
    return false;
  }
  printf("nouveau_switch_screen_create done\n");
  fflush(stdout);

  m_pctx = m_screen->context_create(m_screen, nullptr, 0);
  if (!m_pctx) {
    Log.report(logvisor::Fatal, fmt("Failed to create pipe context"));
    m_screen->destroy(m_screen);
    return false;
  }
  printf("m_screen->context_create done\n");

  st_config_options opts = {};
  m_st = st_create_context(API_OPENGL_CORE, m_pctx, nullptr, nullptr, &opts, false);
  if (!m_st) {
    Log.report(logvisor::Fatal, fmt("Failed to create st context"));
    m_screen->destroy(m_screen);
    return false;
  }

  u32 width, height;
  gfxGetFramebufferResolution(&width, &height);

  for (int i = 0; i < 2; ++i) {
    /* color target */
    struct pipe_resource texTempl = {};
    texTempl.target = PIPE_TEXTURE_RECT;
    texTempl.format = ColorFormat;
    texTempl.width0 = width;
    texTempl.height0 = height;
    texTempl.depth0 = 1;
    texTempl.array_size = 1;
    texTempl.usage = PIPE_USAGE_DEFAULT;
    texTempl.nr_samples = texTempl.nr_storage_samples = 1;
    texTempl.bind = PIPE_BIND_RENDER_TARGET;
    u32 index = i == ST_ATTACHMENT_FRONT_LEFT ? 1 : 0;
    struct winsys_handle whandle;
    whandle.type = WINSYS_HANDLE_TYPE_SHARED;
    whandle.handle = gfxGetFramebufferHandle(index, &whandle.offset);
    whandle.stride = gfxGetFramebufferPitch();
    struct pipe_resource* tex = m_screen->resource_from_handle(m_screen, &texTempl, &whandle, 0);
    if (!tex) {
      Log.report(logvisor::Fatal, fmt("Failed to create color target texture"));
      return false;
    }

    /* surface */
    struct pipe_surface surfTempl = {};
    surfTempl.format = ColorFormat;
    m_windowSurfaces[i] = m_pctx->create_surface(m_pctx, tex, &surfTempl);
    if (!m_windowSurfaces[i]) {
      Log.report(logvisor::Fatal, fmt("Failed to create color surface"));
      return false;
    }

    m_fences[i].id = UINT32_MAX;
  }

  return m_compiler.initialize(m_screen, m_st);
}

bool NXContext::terminate() {
  if (m_st)
    st_destroy_context(m_st);
  if (m_screen)
    m_screen->destroy(m_screen);
  gfxExit();
  return true;
}

bool NXContext::_resizeWindowSurfaces() { return false; }

std::unique_ptr<IGraphicsCommandQueue> _NewNXCommandQueue(NXContext* ctx, IGraphicsContext* parent) {
  return std::make_unique<NXCommandQueue>(ctx, parent);
}

std::unique_ptr<IGraphicsDataFactory> _NewNXDataFactory(IGraphicsContext* parent, NXContext* ctx) {
  return std::make_unique<NXDataFactoryImpl>(parent, ctx);
}

} // namespace boo

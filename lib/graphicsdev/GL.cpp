#include "boo/graphicsdev/GL.hpp"
#include "boo/graphicsdev/glew.h"
#include "boo/IGraphicsContext.hpp"
#include "Common.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <functional>
#include "xxhash.h"

#include "logvisor/logvisor.hpp"

#undef min
#undef max

namespace boo
{
static logvisor::Module Log("boo::GL");
class GLDataFactoryImpl;

struct GLShareableShader : IShareableShader<GLDataFactoryImpl, GLShareableShader>
{
    GLuint m_shader = 0;
    GLShareableShader(GLDataFactoryImpl& fac, uint64_t srcKey, GLuint s)
    : IShareableShader(fac, srcKey, 0), m_shader(s) {}
    ~GLShareableShader() { glDeleteShader(m_shader); }
};

class GLDataFactoryImpl : public GLDataFactory
{
    friend struct GLCommandQueue;
    friend class GLDataFactory::Context;
    IGraphicsContext* m_parent;
    uint32_t m_drawSamples;
    static ThreadLocalPtr<struct GLData> m_deferredData;
    std::unordered_set<struct GLData*> m_committedData;
    std::unordered_set<struct GLPool*> m_committedPools;
    std::mutex m_committedMutex;
    std::unordered_map<uint64_t, std::unique_ptr<GLShareableShader>> m_sharedShaders;
    void destroyData(IGraphicsData*);
    void destroyAllData();
    void destroyPool(IGraphicsBufferPool*);
    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool* pool, BufferUse use,
                                    size_t stride, size_t count);
    void deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf);
public:
    GLDataFactoryImpl(IGraphicsContext* parent, uint32_t drawSamples);
    ~GLDataFactoryImpl() {destroyAllData();}

    Platform platform() const {return Platform::OpenGL;}
    const SystemChar* platformName() const {return _S("OpenGL");}

    GraphicsDataToken commitTransaction(const FactoryCommitFunc&);
    GraphicsBufferPoolToken newBufferPool();

    void _unregisterShareableShader(uint64_t srcKey, uint64_t binKey)
    {
        m_sharedShaders.erase(srcKey);
    }
};

ThreadLocalPtr<struct GLData> GLDataFactoryImpl::m_deferredData;
struct GLData : IGraphicsDataPriv
{
    std::vector<std::unique_ptr<class GLShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct GLShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class GLGraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class GLGraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class GLTextureS>> m_STexs;
    std::vector<std::unique_ptr<class GLTextureSA>> m_SATexs;
    std::vector<std::unique_ptr<class GLTextureD>> m_DTexs;
    std::vector<std::unique_ptr<class GLTextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct GLVertexFormat>> m_VFmts;
};

struct GLPoolItem : IGraphicsDataPriv
{
    std::unique_ptr<class GLGraphicsBufferD> m_buf;
};

struct GLPool : IGraphicsBufferPool
{
    std::unordered_set<GLPoolItem*> m_items;
    ~GLPool()
    {
        for (auto& item : m_items)
            item->decrement();
    }
};

static const GLenum USE_TABLE[] =
{
    GL_INVALID_ENUM,
    GL_ARRAY_BUFFER,
    GL_ELEMENT_ARRAY_BUFFER,
    GL_UNIFORM_BUFFER
};

class GLGraphicsBufferS : public IGraphicsBufferS
{
    friend class GLDataFactory;
    friend struct GLCommandQueue;
    GLuint m_buf;
    GLenum m_target;
    GLGraphicsBufferS(IGraphicsData* parent, BufferUse use, const void* data, size_t sz)
    : IGraphicsBufferS(parent)
    {
        m_target = USE_TABLE[int(use)];
        glGenBuffers(1, &m_buf);
        glBindBuffer(m_target, m_buf);
        glBufferData(m_target, sz, data, GL_STATIC_DRAW);
    }
public:
    ~GLGraphicsBufferS() {glDeleteBuffers(1, &m_buf);}

    void bindVertex() const
    {glBindBuffer(GL_ARRAY_BUFFER, m_buf);}
    void bindIndex() const
    {glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buf);}
    void bindUniform(size_t idx) const
    {glBindBufferBase(GL_UNIFORM_BUFFER, idx, m_buf);}
    void bindUniformRange(size_t idx, GLintptr off, GLsizeiptr size) const
    {glBindBufferRange(GL_UNIFORM_BUFFER, idx, m_buf, off, size);}
};

class GLGraphicsBufferD : public IGraphicsBufferD
{
    friend class GLDataFactory;
    friend class GLDataFactoryImpl;
    friend struct GLCommandQueue;
    GLuint m_bufs[3];
    GLenum m_target;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz = 0;
    int m_validMask = 0;
    GLGraphicsBufferD(IGraphicsData* parent, BufferUse use, size_t sz)
    : boo::IGraphicsBufferD(parent),
      m_target(USE_TABLE[int(use)]), m_cpuBuf(new uint8_t[sz]), m_cpuSz(sz)
    {
        glGenBuffers(3, m_bufs);
        for (int i=0 ; i<3 ; ++i)
        {
            glBindBuffer(m_target, m_bufs[i]);
            glBufferData(m_target, m_cpuSz, nullptr, GL_STREAM_DRAW);
        }
    }
    void update(int b);
public:
    ~GLGraphicsBufferD() {glDeleteBuffers(3, m_bufs);}

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    void bindVertex(int b);
    void bindIndex(int b);
    void bindUniform(size_t idx, int b);
    void bindUniformRange(size_t idx, GLintptr off, GLsizeiptr size, int b);
};

IGraphicsBufferS*
GLDataFactory::Context::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLGraphicsBufferS* retval = new GLGraphicsBufferS(d, use, data, stride * count);
    d->m_SBufs.emplace_back(retval);
    return retval;
}

class GLTextureS : public ITextureS
{
    friend class GLDataFactory;
    GLuint m_tex;
    GLTextureS(GLData* parent, size_t width, size_t height, size_t mips,
               TextureFormat fmt, const void* data, size_t sz)
    : ITextureS(parent)
    {
        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (mips > 1)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mips-1);
        }
        else
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        GLenum intFormat, format;
        int pxPitch;
        bool compressed = false;
        switch (fmt)
        {
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
        case TextureFormat::DXT1:
            intFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            compressed = true;
            break;
        default:
            Log.report(logvisor::Fatal, "unsupported tex format");
        }

        if (compressed)
        {
            for (size_t i=0 ; i<mips ; ++i)
            {
                size_t dataSz = width * height / 2;
                glCompressedTexImage2D(GL_TEXTURE_2D, i, intFormat, width, height, 0, dataSz, dataIt);
                dataIt += dataSz;
                if (width > 1)
                    width /= 2;
                if (height > 1)
                    height /= 2;
            }
        }
        else
        {
            for (size_t i=0 ; i<mips ; ++i)
            {
                glTexImage2D(GL_TEXTURE_2D, i, intFormat, width, height, 0, format, GL_UNSIGNED_BYTE, dataIt);
                dataIt += width * height * pxPitch;
                if (width > 1)
                    width /= 2;
                if (height > 1)
                    height /= 2;
            }
        }
    }
public:
    ~GLTextureS() {glDeleteTextures(1, &m_tex);}

    void bind(size_t idx) const
    {
        glActiveTexture(GL_TEXTURE0 + idx);
        glBindTexture(GL_TEXTURE_2D, m_tex);
    }
};

class GLTextureSA : public ITextureSA
{
    friend class GLDataFactory;
    GLuint m_tex;
    GLTextureSA(GLData* parent, size_t width, size_t height, size_t layers, size_t mips,
                TextureFormat fmt, const void* data, size_t sz)
    : ITextureSA(parent)
    {
        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (mips > 1)
        {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mips-1);
        }
        else
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        GLenum intFormat, format;
        int pxPitch;
        if (fmt == TextureFormat::RGBA8)
        {
            intFormat = GL_RGBA8;
            format = GL_RGBA;
            pxPitch = 4;
        }
        else if (fmt == TextureFormat::I8)
        {
            intFormat = GL_R8;
            format = GL_RED;
            pxPitch = 1;
        }

        for (size_t i=0 ; i<mips ; ++i)
        {
            glTexImage3D(GL_TEXTURE_2D_ARRAY, i, intFormat, width, height, layers, 0, format, GL_UNSIGNED_BYTE, dataIt);
            dataIt += width * height * layers * pxPitch;
            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }
    }
public:
    ~GLTextureSA() {glDeleteTextures(1, &m_tex);}

    void bind(size_t idx) const
    {
        glActiveTexture(GL_TEXTURE0 + idx);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
    }
};

class GLTextureD : public ITextureD
{
    friend class GLDataFactory;
    friend struct GLCommandQueue;
    GLuint m_texs[3];
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz = 0;
    GLenum m_intFormat, m_format;
    size_t m_width = 0;
    size_t m_height = 0;
    int m_validMask = 0;
    GLTextureD(IGraphicsData* parent, size_t width, size_t height, TextureFormat fmt);
    void update(int b);
public:
    ~GLTextureD();

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    void bind(size_t idx, int b);
};

#define MAX_BIND_TEXS 4

class GLTextureR : public ITextureR
{
    friend class GLDataFactory;
    friend struct GLCommandQueue;
    struct GLCommandQueue* m_q;
    GLuint m_texs[2] = {};
    GLuint m_bindTexs[2][MAX_BIND_TEXS] = {};
    GLuint m_fbo = 0;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;
    GLenum m_target;
    GLTextureR(IGraphicsData* parent, GLCommandQueue* q, size_t width, size_t height, size_t samples,
               size_t colorBindCount, size_t depthBindCount);
public:
    ~GLTextureR();

    void bind(size_t idx, int bindIdx, bool depth) const
    {
        glActiveTexture(GL_TEXTURE0 + idx);
        glBindTexture(m_target, m_bindTexs[depth][bindIdx]);
    }

    void resize(size_t width, size_t height)
    {
        m_width = width;
        m_height = height;

        if (m_samples > 1)
        {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[0]);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_samples, GL_RGBA, width, height, GL_FALSE);
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[1]);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_samples, GL_DEPTH_COMPONENT24, width, height, GL_FALSE);

            for (int i=0 ; i<MAX_BIND_TEXS ; ++i)
            {
                if (m_bindTexs[0][i])
                {
                    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_bindTexs[0][i]);
                    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_samples, GL_RGBA, width, height, GL_FALSE);
                }
            }

            for (int i=0 ; i<MAX_BIND_TEXS ; ++i)
            {
                if (m_bindTexs[1][i])
                {
                    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_bindTexs[1][i]);
                    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_samples, GL_DEPTH_COMPONENT24, width, height, GL_FALSE);
                }
            }
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, m_texs[0]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, m_texs[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            for (int i=0 ; i<MAX_BIND_TEXS ; ++i)
            {
                if (m_bindTexs[0][i])
                {
                    glBindTexture(GL_TEXTURE_2D, m_bindTexs[0][i]);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                }
            }

            for (int i=0 ; i<MAX_BIND_TEXS ; ++i)
            {
                if (m_bindTexs[1][i])
                {
                    glBindTexture(GL_TEXTURE_2D, m_bindTexs[1][i]);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
                }
            }
        }
    }
};

ITextureS*
GLDataFactory::Context::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                         const void* data, size_t sz)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLTextureS* retval = new GLTextureS(d, width, height, mips, fmt, data, sz);
    d->m_STexs.emplace_back(retval);
    return retval;
}

ITextureSA*
GLDataFactory::Context::newStaticArrayTexture(size_t width, size_t height, size_t layers, size_t mips,
                                              TextureFormat fmt, const void *data, size_t sz)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLTextureSA* retval = new GLTextureSA(d, width, height, layers, mips, fmt, data, sz);
    d->m_SATexs.emplace_back(retval);
    return retval;
}

class GLShaderPipeline : public IShaderPipeline
{
    friend class GLDataFactory;
    friend struct GLCommandQueue;
    friend struct GLShaderDataBinding;
    GLShareableShader::Token m_vert;
    GLShareableShader::Token m_frag;
    GLuint m_prog = 0;
    GLenum m_sfactor = GL_ONE;
    GLenum m_dfactor = GL_ZERO;
    GLenum m_drawPrim = GL_TRIANGLES;
    ZTest m_depthTest = ZTest::LEqual;
    bool m_depthWrite = true;
    bool m_colorWrite = true;
    bool m_alphaWrite = true;
    CullMode m_culling;
    std::vector<GLint> m_uniLocs;
    GLShaderPipeline(GLData* parent) : IShaderPipeline(parent) {}
public:
    operator bool() const {return m_prog != 0;}
    ~GLShaderPipeline() { if (m_prog) glDeleteProgram(m_prog); }
    GLShaderPipeline& operator=(const GLShaderPipeline&) = delete;
    GLShaderPipeline(const GLShaderPipeline&) = delete;
    GLShaderPipeline& operator=(GLShaderPipeline&& other)
    {
        m_vert = std::move(other.m_vert);
        m_frag = std::move(other.m_frag);
        m_prog = other.m_prog;
        other.m_prog = 0;
        m_sfactor = other.m_sfactor;
        m_dfactor = other.m_dfactor;
        m_drawPrim = other.m_drawPrim;
        m_depthTest = other.m_depthTest;
        m_depthWrite = other.m_depthWrite;
        m_colorWrite = other.m_colorWrite;
        m_alphaWrite = other.m_alphaWrite;
        m_culling = other.m_culling;
        m_uniLocs = std::move(other.m_uniLocs);
        return *this;
    }
    GLShaderPipeline(GLShaderPipeline&& other)
    : IShaderPipeline(other.m_parentData) { *this = std::move(other); }

    GLuint bind() const
    {
        glUseProgram(m_prog);

        if (m_dfactor != GL_ZERO)
        {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(m_sfactor, m_dfactor, GL_ONE, GL_ZERO);
        }
        else
            glDisable(GL_BLEND);

        if (m_depthTest != ZTest::None)
        {
            glEnable(GL_DEPTH_TEST);
            switch (m_depthTest)
            {
            case ZTest::LEqual:
            default:
                glDepthFunc(GL_LEQUAL);
                break;
            case ZTest::Greater:
                glDepthFunc(GL_GREATER);
                break;
            case ZTest::Equal:
                glDepthFunc(GL_EQUAL);
                break;
            }
        }
        else
            glDisable(GL_DEPTH_TEST);
        glDepthMask(m_depthWrite);
        glColorMask(m_colorWrite, m_colorWrite, m_colorWrite, m_alphaWrite);

        if (m_culling != CullMode::None)
        {
            glEnable(GL_CULL_FACE);
            glCullFace(m_culling == CullMode::Backface ? GL_BACK : GL_FRONT);
        }
        else
            glDisable(GL_CULL_FACE);

        return m_prog;
    }
};

static const GLenum PRIMITIVE_TABLE[] =
{
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP
};

static const GLenum BLEND_FACTOR_TABLE[] =
{
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_SRC1_COLOR,
    GL_ONE_MINUS_SRC1_COLOR
};

IShaderPipeline* GLDataFactory::Context::newShaderPipeline
(const char* vertSource, const char* fragSource,
 size_t texCount, const char** texNames,
 size_t uniformBlockCount, const char** uniformBlockNames,
 BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
 ZTest depthTest, bool depthWrite, bool colorWrite,
 bool alphaWrite, CullMode culling)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
    GLShaderPipeline shader(d);

    XXH64_state_t hashState;
    uint64_t hashes[2];
    XXH64_reset(&hashState, 0);
    XXH64_update(&hashState, vertSource, strlen(vertSource));
    hashes[0] = XXH64_digest(&hashState);
    XXH64_reset(&hashState, 0);
    XXH64_update(&hashState, fragSource, strlen(fragSource));
    hashes[1] = XXH64_digest(&hashState);

    GLint status;
    auto vertFind = factory.m_sharedShaders.find(hashes[0]);
    if (vertFind != factory.m_sharedShaders.end())
    {
        shader.m_vert = vertFind->second->lock();
    }
    else
    {
        GLuint sobj = glCreateShader(GL_VERTEX_SHADER);
        if (!sobj)
        {
            Log.report(logvisor::Error, "unable to create vert shader");
            return nullptr;
        }

        glShaderSource(sobj, 1, &vertSource, nullptr);
        glCompileShader(sobj);
        glGetShaderiv(sobj, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE)
        {
            GLint logLen;
            glGetShaderiv(sobj, GL_INFO_LOG_LENGTH, &logLen);
            char* log = (char*)malloc(logLen);
            glGetShaderInfoLog(sobj, logLen, nullptr, log);
            Log.report(logvisor::Error, "unable to compile vert source\n%s\n%s\n", log, vertSource);
            free(log);
            return nullptr;
        }

        auto it =
        factory.m_sharedShaders.emplace(std::make_pair(hashes[0],
            std::make_unique<GLShareableShader>(factory, hashes[0], sobj))).first;
        shader.m_vert = it->second->lock();
    }
    auto fragFind = factory.m_sharedShaders.find(hashes[1]);
    if (fragFind != factory.m_sharedShaders.end())
    {
        shader.m_frag = fragFind->second->lock();
    }
    else
    {
        GLuint sobj = glCreateShader(GL_FRAGMENT_SHADER);
        if (!sobj)
        {
            Log.report(logvisor::Error, "unable to create frag shader");
            return nullptr;
        }

        glShaderSource(sobj, 1, &fragSource, nullptr);
        glCompileShader(sobj);
        glGetShaderiv(sobj, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE)
        {
            GLint logLen;
            glGetShaderiv(sobj, GL_INFO_LOG_LENGTH, &logLen);
            char* log = (char*)malloc(logLen);
            glGetShaderInfoLog(sobj, logLen, nullptr, log);
            Log.report(logvisor::Error, "unable to compile frag source\n%s\n%s\n", log, fragSource);
            free(log);
            return nullptr;
        }

        auto it =
        factory.m_sharedShaders.emplace(std::make_pair(hashes[1],
            std::make_unique<GLShareableShader>(factory, hashes[1], sobj))).first;
        shader.m_frag = it->second->lock();
    }

    shader.m_prog = glCreateProgram();
    if (!shader.m_prog)
    {
        Log.report(logvisor::Error, "unable to create shader program");
        return nullptr;
    }

    glAttachShader(shader.m_prog, shader.m_vert.get().m_shader);
    glAttachShader(shader.m_prog, shader.m_frag.get().m_shader);

    glLinkProgram(shader.m_prog);
    glGetProgramiv(shader.m_prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint logLen;
        glGetProgramiv(shader.m_prog, GL_INFO_LOG_LENGTH, &logLen);
        char* log = (char*)malloc(logLen);
        glGetProgramInfoLog(shader.m_prog, logLen, nullptr, log);
        Log.report(logvisor::Error, "unable to link shader program\n%s\n", log);
        free(log);
        return nullptr;
    }

    glUseProgram(shader.m_prog);

    if (uniformBlockCount)
    {
        shader.m_uniLocs.reserve(uniformBlockCount);
        for (size_t i=0 ; i<uniformBlockCount ; ++i)
        {
            GLint uniLoc = glGetUniformBlockIndex(shader.m_prog, uniformBlockNames[i]);
            //if (uniLoc < 0)
            //    Log.report(logvisor::Warning, "unable to find uniform block '%s'", uniformBlockNames[i]);
            shader.m_uniLocs.push_back(uniLoc);
        }
    }

    if (texCount && texNames)
    {
        for (int i=0 ; i<texCount ; ++i)
        {
            GLint texLoc = glGetUniformLocation(shader.m_prog, texNames[i]);
            if (texLoc < 0)
            { /* Log.report(logvisor::Warning, "unable to find sampler variable '%s'", texNames[i]); */ }
            else
                glUniform1i(texLoc, i);
        }
    }

    shader.m_sfactor = BLEND_FACTOR_TABLE[int(srcFac)];
    shader.m_dfactor = BLEND_FACTOR_TABLE[int(dstFac)];
    shader.m_depthTest = depthTest;
    shader.m_depthWrite = depthWrite;
    shader.m_colorWrite = colorWrite;
    shader.m_alphaWrite = alphaWrite;
    shader.m_culling = culling;
    shader.m_drawPrim = PRIMITIVE_TABLE[int(prim)];

    GLShaderPipeline* retval = new GLShaderPipeline(std::move(shader));
    d->m_SPs.emplace_back(retval);
    return retval;
}

struct GLVertexFormat : IVertexFormat
{
    GLCommandQueue* m_q;
    GLuint m_vao[3] = {};
    size_t m_elementCount;
    GLuint m_baseVert, m_baseInst;
    std::unique_ptr<VertexElementDescriptor[]> m_elements;
    GLVertexFormat(IGraphicsData* parent, GLCommandQueue* q, size_t elementCount,
                   const VertexElementDescriptor* elements,
                   size_t baseVert, size_t baseInst);
    ~GLVertexFormat();
    void bind(int idx) const {glBindVertexArray(m_vao[idx]);}
};

struct GLShaderDataBinding : IShaderDataBindingPriv
{
    const GLShaderPipeline* m_pipeline;
    const GLVertexFormat* m_vtxFormat;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    std::vector<std::pair<size_t,size_t>> m_ubufOffs;
    size_t m_texCount;
    struct BoundTex
    {
        ITexture* tex;
        int idx;
        bool depth;
    };
    std::unique_ptr<BoundTex[]> m_texs;

    GLShaderDataBinding(GLData* d,
                        IShaderPipeline* pipeline,
                        IVertexFormat* vtxFormat,
                        size_t ubufCount, IGraphicsBuffer** ubufs,
                        const size_t* ubufOffs, const size_t* ubufSizes,
                        size_t texCount, ITexture** texs,
                        const int* bindTexIdx,
                        const bool* depthBind)
    : IShaderDataBindingPriv(d),
      m_pipeline(static_cast<GLShaderPipeline*>(pipeline)),
      m_vtxFormat(static_cast<GLVertexFormat*>(vtxFormat)),
      m_ubufCount(ubufCount),
      m_ubufs(new IGraphicsBuffer*[ubufCount]),
      m_texCount(texCount),
      m_texs(new BoundTex[texCount])
    {
        addDepData(m_pipeline->m_parentData);
        addDepData(m_vtxFormat->m_parentData);
        if (ubufOffs && ubufSizes)
        {
            m_ubufOffs.reserve(ubufCount);
            for (size_t i=0 ; i<ubufCount ; ++i)
            {
#ifndef NDEBUG
                if (ubufOffs[i] % 256)
                    Log.report(logvisor::Fatal, "non-256-byte-aligned uniform-offset %d provided to newShaderDataBinding", int(i));
#endif
                m_ubufOffs.emplace_back(ubufOffs[i], (ubufSizes[i] + 255) & ~255);
            }
        }
        for (size_t i=0 ; i<ubufCount ; ++i)
        {
#ifndef NDEBUG
            if (!ubufs[i])
                Log.report(logvisor::Fatal, "null uniform-buffer %d provided to newShaderDataBinding", int(i));
#endif
            m_ubufs[i] = ubufs[i];
            if (ubufs[i])
                addDepData(ubufs[i]->m_parentData);
        }
        for (size_t i=0 ; i<texCount ; ++i)
        {
            m_texs[i] = {texs[i], bindTexIdx ? bindTexIdx[i] : 0, depthBind ? depthBind[i] : false};
            if (texs[i])
                addDepData(texs[i]->m_parentData);
        }
    }
    void bind(int b) const
    {
        GLuint prog = m_pipeline->bind();
        m_vtxFormat->bind(b);
        if (m_ubufOffs.size())
        {
            for (size_t i=0 ; i<m_ubufCount && i<m_pipeline->m_uniLocs.size() ; ++i)
            {
                GLint loc = m_pipeline->m_uniLocs[i];
                if (loc < 0)
                    continue;
                IGraphicsBuffer* ubuf = m_ubufs[i];
                const std::pair<size_t,size_t>& offset = m_ubufOffs[i];
                if (ubuf->dynamic())
                    static_cast<GLGraphicsBufferD*>(ubuf)->bindUniformRange(i, offset.first, offset.second, b);
                else
                    static_cast<GLGraphicsBufferS*>(ubuf)->bindUniformRange(i, offset.first, offset.second);
                glUniformBlockBinding(prog, loc, i);
            }
        }
        else
        {
            for (size_t i=0 ; i<m_ubufCount && i<m_pipeline->m_uniLocs.size() ; ++i)
            {
                GLint loc = m_pipeline->m_uniLocs[i];
                if (loc < 0)
                    continue;
                IGraphicsBuffer* ubuf = m_ubufs[i];
                if (ubuf->dynamic())
                    static_cast<GLGraphicsBufferD*>(ubuf)->bindUniform(i, b);
                else
                    static_cast<GLGraphicsBufferS*>(ubuf)->bindUniform(i);
                glUniformBlockBinding(prog, loc, i);
            }
        }
        for (size_t i=0 ; i<m_texCount ; ++i)
        {
            BoundTex& tex = m_texs[i];
            if (tex.tex)
            {
                switch (tex.tex->type())
                {
                case TextureType::Dynamic:
                    static_cast<GLTextureD*>(tex.tex)->bind(i, b);
                    break;
                case TextureType::Static:
                    static_cast<GLTextureS*>(tex.tex)->bind(i);
                    break;
                case TextureType::StaticArray:
                    static_cast<GLTextureSA*>(tex.tex)->bind(i);
                    break;
                case TextureType::Render:
                    static_cast<GLTextureR*>(tex.tex)->bind(i, tex.idx, tex.depth);
                    break;
                default: break;
                }
            }
        }
    }
};

IShaderDataBinding*
GLDataFactory::Context::newShaderDataBinding(IShaderPipeline* pipeline,
                                             IVertexFormat* vtxFormat,
                                             IGraphicsBuffer*, IGraphicsBuffer*, IGraphicsBuffer*,
                                             size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                                             const size_t* ubufOffs, const size_t* ubufSizes,
                                             size_t texCount, ITexture** texs,
                                             const int* texBindIdx, const bool* depthBind,
                                             size_t baseVert, size_t baseInst)
{
    GLShaderDataBinding* retval =
    new GLShaderDataBinding(GLDataFactoryImpl::m_deferredData.get(), pipeline, vtxFormat, ubufCount, ubufs,
                            ubufOffs, ubufSizes, texCount, texs, texBindIdx, depthBind);
    GLDataFactoryImpl::m_deferredData->m_SBinds.emplace_back(retval);
    return retval;
}

GLDataFactoryImpl::GLDataFactoryImpl(IGraphicsContext* parent, uint32_t drawSamples)
: m_parent(parent), m_drawSamples(drawSamples) {}


GraphicsDataToken GLDataFactoryImpl::commitTransaction(const FactoryCommitFunc& trans)
{
    if (m_deferredData.get())
        Log.report(logvisor::Fatal, "nested commitTransaction usage detected");
    m_deferredData.reset(new GLData());

    GLDataFactory::Context ctx(*this);
    if (!trans(ctx))
    {
        delete m_deferredData.get();
        m_deferredData.reset();
        return GraphicsDataToken(this, nullptr);
    }

    std::unique_lock<std::mutex> lk(m_committedMutex);
    GLData* retval = m_deferredData.get();
    m_deferredData.reset();
    m_committedData.insert(retval);

    lk.unlock();
    /* Let's go ahead and flush to ensure our data gets to the GPU
       While this isn't strictly required, some drivers might behave
       differently */
    //glFlush();
    return GraphicsDataToken(this, retval);
}

GraphicsBufferPoolToken GLDataFactoryImpl::newBufferPool()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    GLPool* retval = new GLPool;
    m_committedPools.insert(retval);
    return GraphicsBufferPoolToken(this, retval);
}

void GLDataFactoryImpl::destroyData(IGraphicsData* d)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    GLData* data = static_cast<GLData*>(d);
    m_committedData.erase(data);
    data->decrement();
}

void GLDataFactoryImpl::destroyAllData()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    for (GLData* data : m_committedData)
        data->decrement();
    for (GLPool* pool : m_committedPools)
        delete pool;
    m_committedData.clear();
    m_committedPools.clear();
}

void GLDataFactoryImpl::destroyPool(IGraphicsBufferPool* p)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    GLPool* pool = static_cast<GLPool*>(p);
    m_committedPools.erase(pool);
    delete pool;
}

IGraphicsBufferD* GLDataFactoryImpl::newPoolBuffer(IGraphicsBufferPool* p, BufferUse use,
                                                   size_t stride, size_t count)
{
    GLPool* pool = static_cast<GLPool*>(p);
    GLPoolItem* item = new GLPoolItem;
    GLGraphicsBufferD* retval = new GLGraphicsBufferD(item, use, stride * count);
    item->m_buf.reset(retval);
    pool->m_items.emplace(item);
    return retval;
}

void GLDataFactoryImpl::deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf)
{
    GLPool* pool = static_cast<GLPool*>(p);
    auto search = pool->m_items.find(static_cast<GLPoolItem*>(buf->m_parentData));
    if (search != pool->m_items.end())
    {
        (*search)->decrement();
        pool->m_items.erase(search);
    }
}

static const GLint SEMANTIC_COUNT_TABLE[] =
{
    0,
    3,
    4,
    3,
    4,
    4,
    4,
    2,
    4,
    4,
    4
};

static const size_t SEMANTIC_SIZE_TABLE[] =
{
    0,
    12,
    16,
    12,
    16,
    16,
    4,
    8,
    16,
    16,
    16
};

static const GLenum SEMANTIC_TYPE_TABLE[] =
{
    GL_INVALID_ENUM,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT,
    GL_UNSIGNED_BYTE,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT
};

struct GLCommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::Platform::OpenGL;}
    const SystemChar* platformName() const {return _S("OpenGL");}
    IGraphicsContext* m_parent = nullptr;

    std::mutex m_mt;
    std::condition_variable m_cv;
    std::mutex m_initmt;
    std::condition_variable m_initcv;
    std::unique_lock<std::mutex> m_initlk;
    std::thread m_thr;
    
    struct Command
    {
        enum class Op
        {
            SetShaderDataBinding,
            SetRenderTarget,
            SetViewport,
            SetScissor,
            SetClearColor,
            ClearTarget,
            Draw,
            DrawIndexed,
            DrawInstances,
            DrawInstancesIndexed,
            ResolveBindTexture,
            Present
        } m_op;
        union
        {
            const IShaderDataBinding* binding;
            const ITextureR* target;
            const ITextureR* source;
            struct
            {
                SWindowRect rect;
                float znear, zfar;
            } viewport;
            float rgba[4];
            GLbitfield flags;
            struct
            {
                size_t start;
                size_t count;
                size_t instCount;
            };
        };
        IShaderDataBindingPriv::Token resToken;
        const ITextureR* resolveTex;
        int bindIdx;
        bool resolveColor : 1;
        bool resolveDepth : 1;
        Command(Op op) : m_op(op) {}
        Command(const Command&) = delete;
        Command& operator=(const Command&) = delete;
        Command(Command&&) = default;
        Command& operator=(Command&&) = default;
    };
    std::vector<Command> m_cmdBufs[3];
    size_t m_fillBuf = 0;
    size_t m_completeBuf = 0;
    size_t m_drawBuf = 0;
    bool m_running = true;

    struct RenderTextureResize
    {
        GLTextureR* tex;
        size_t width;
        size_t height;
    };

    /* These members are locked for multithreaded access */
    std::vector<RenderTextureResize> m_pendingResizes;
    std::vector<std::function<void(void)>> m_pendingPosts1;
    std::vector<std::function<void(void)>> m_pendingPosts2;
    std::vector<GLVertexFormat*> m_pendingFmtAdds;
    std::vector<std::array<GLuint, 3>> m_pendingFmtDels;
    std::vector<GLTextureR*> m_pendingFboAdds;
    std::vector<GLuint> m_pendingFboDels;

    static void ConfigureVertexFormat(GLVertexFormat* fmt)
    {
        glGenVertexArrays(3, fmt->m_vao);

        size_t stride = 0;
        size_t instStride = 0;
        for (size_t i=0 ; i<fmt->m_elementCount ; ++i)
        {
            const VertexElementDescriptor* desc = &fmt->m_elements[i];
            if ((desc->semantic & VertexSemantic::Instanced) != VertexSemantic::None)
                instStride += SEMANTIC_SIZE_TABLE[int(desc->semantic & VertexSemantic::SemanticMask)];
            else
                stride += SEMANTIC_SIZE_TABLE[int(desc->semantic & VertexSemantic::SemanticMask)];
        }

        for (int b=0 ; b<3 ; ++b)
        {
            size_t offset = fmt->m_baseVert * stride;
            size_t instOffset = fmt->m_baseInst * instStride;
            glBindVertexArray(fmt->m_vao[b]);
            IGraphicsBuffer* lastVBO = nullptr;
            IGraphicsBuffer* lastEBO = nullptr;
            for (size_t i=0 ; i<fmt->m_elementCount ; ++i)
            {
                const VertexElementDescriptor* desc = &fmt->m_elements[i];
                if (desc->vertBuffer != lastVBO)
                {
                    lastVBO = desc->vertBuffer;
                    if (lastVBO->dynamic())
                        static_cast<GLGraphicsBufferD*>(lastVBO)->bindVertex(b);
                    else
                        static_cast<GLGraphicsBufferS*>(lastVBO)->bindVertex();
                }
                if (desc->indexBuffer != lastEBO)
                {
                    lastEBO = desc->indexBuffer;
                    if (lastEBO->dynamic())
                        static_cast<GLGraphicsBufferD*>(lastEBO)->bindIndex(b);
                    else
                        static_cast<GLGraphicsBufferS*>(lastEBO)->bindIndex();
                }
                glEnableVertexAttribArray(i);
                int maskedSem = int(desc->semantic & VertexSemantic::SemanticMask);
                if ((desc->semantic & VertexSemantic::Instanced) != VertexSemantic::None)
                {
                    glVertexAttribPointer(i, SEMANTIC_COUNT_TABLE[maskedSem],
                            SEMANTIC_TYPE_TABLE[maskedSem], GL_TRUE, instStride, (void*)instOffset);
                    glVertexAttribDivisor(i, 1);
                    instOffset += SEMANTIC_SIZE_TABLE[maskedSem];
                }
                else
                {
                    glVertexAttribPointer(i, SEMANTIC_COUNT_TABLE[maskedSem],
                            SEMANTIC_TYPE_TABLE[maskedSem], GL_TRUE, stride, (void*)offset);
                    offset += SEMANTIC_SIZE_TABLE[maskedSem];
                }
            }
        }
    }

    static void ConfigureFBO(GLTextureR* tex)
    {
        glGenFramebuffers(1, &tex->m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, tex->m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->m_texs[0], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex->m_texs[1], 0);
    }

    static void RenderingWorker(GLCommandQueue* self)
    {
        {
            std::unique_lock<std::mutex> lk(self->m_initmt);
            self->m_parent->makeCurrent();
            if (glewInit() != GLEW_OK)
                Log.report(logvisor::Fatal, "unable to init glew");
            const GLubyte* version = glGetString(GL_VERSION);
            Log.report(logvisor::Info, "OpenGL Version: %s", version);
            self->m_parent->postInit();
            glClearColor(0.f, 0.f, 0.f, 0.f);
        }
        self->m_initcv.notify_one();
        while (self->m_running)
        {
            std::vector<std::function<void(void)>> posts;
            {
                std::unique_lock<std::mutex> lk(self->m_mt);
                self->m_cv.wait(lk);
                if (!self->m_running)
                    break;
                self->m_drawBuf = self->m_completeBuf;

                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                if (self->m_pendingFboAdds.size())
                {
                    for (GLTextureR* tex : self->m_pendingFboAdds)
                        ConfigureFBO(tex);
                    self->m_pendingFboAdds.clear();
                }

                if (self->m_pendingResizes.size())
                {
                    for (const RenderTextureResize& resize : self->m_pendingResizes)
                        resize.tex->resize(resize.width, resize.height);
                    self->m_pendingResizes.clear();
                }

                if (self->m_pendingFmtAdds.size())
                {
                    for (GLVertexFormat* fmt : self->m_pendingFmtAdds)
                        if (fmt) ConfigureVertexFormat(fmt);
                    self->m_pendingFmtAdds.clear();
                }

                if (self->m_pendingFmtDels.size())
                {
                    for (const auto& fmt : self->m_pendingFmtDels)
                        glDeleteVertexArrays(3, fmt.data());
                    self->m_pendingFmtDels.clear();
                }

                if (self->m_pendingFboDels.size())
                {
                    for (GLuint fbo : self->m_pendingFboDels)
                        glDeleteFramebuffers(1, &fbo);
                    self->m_pendingFboDels.clear();
                }

                if (self->m_pendingPosts2.size())
                    posts.swap(self->m_pendingPosts2);
            }
            std::vector<Command>& cmds = self->m_cmdBufs[self->m_drawBuf];
            GLenum currentPrim = GL_TRIANGLES;
            for (const Command& cmd : cmds)
            {
                switch (cmd.m_op)
                {
                case Command::Op::SetShaderDataBinding:
                {
                    const GLShaderDataBinding* binding = static_cast<const GLShaderDataBinding*>(cmd.binding);
                    binding->bind(self->m_drawBuf);
                    currentPrim = binding->m_pipeline->m_drawPrim;
                    break;
                }
                case Command::Op::SetRenderTarget:
                {
                    const GLTextureR* tex = static_cast<const GLTextureR*>(cmd.target);
                    if (!tex)
                        glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    else
                        glBindFramebuffer(GL_FRAMEBUFFER, tex->m_fbo);
                    break;
                }
                case Command::Op::SetViewport:
                    glViewport(cmd.viewport.rect.location[0], cmd.viewport.rect.location[1],
                               cmd.viewport.rect.size[0], cmd.viewport.rect.size[1]);
                    glDepthRange(cmd.viewport.znear, cmd.viewport.zfar);
                    break;
                case Command::Op::SetScissor:
                    if (cmd.viewport.rect.size[0] == 0 && cmd.viewport.rect.size[1] == 0)
                        glDisable(GL_SCISSOR_TEST);
                    else
                    {
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(cmd.viewport.rect.location[0], cmd.viewport.rect.location[1],
                                  cmd.viewport.rect.size[0], cmd.viewport.rect.size[1]);
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
                    glDrawElements(currentPrim, cmd.count, GL_UNSIGNED_INT,
                                   reinterpret_cast<void*>(cmd.start * 4));
                    break;
                case Command::Op::DrawInstances:
                    glDrawArraysInstanced(currentPrim, cmd.start, cmd.count, cmd.instCount);
                    break;
                case Command::Op::DrawInstancesIndexed:
                    glDrawElementsInstanced(currentPrim, cmd.count, GL_UNSIGNED_INT,
                                            reinterpret_cast<void*>(cmd.start * 4), cmd.instCount);
                    break;
                case Command::Op::ResolveBindTexture:
                {
                    const GLTextureR* tex = static_cast<const GLTextureR*>(cmd.resolveTex);
                    GLenum target = (tex->m_samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, tex->m_fbo);
                    glActiveTexture(GL_TEXTURE9);
                    if (cmd.resolveColor && tex->m_bindTexs[0][cmd.bindIdx])
                    {
                        glBindTexture(target, tex->m_bindTexs[0][cmd.bindIdx]);
                        glCopyTexSubImage2D(target, 0, cmd.viewport.rect.location[0], cmd.viewport.rect.location[1],
                                            cmd.viewport.rect.location[0], cmd.viewport.rect.location[1],
                                            cmd.viewport.rect.size[0], cmd.viewport.rect.size[1]);
                    }
                    if (cmd.resolveDepth && tex->m_bindTexs[1][cmd.bindIdx])
                    {
                        glBindTexture(target, tex->m_bindTexs[1][cmd.bindIdx]);
                        glCopyTexSubImage2D(target, 0, cmd.viewport.rect.location[0], cmd.viewport.rect.location[1],
                                            cmd.viewport.rect.location[0], cmd.viewport.rect.location[1],
                                            cmd.viewport.rect.size[0], cmd.viewport.rect.size[1]);
                    }
                    break;
                }
                case Command::Op::Present:
                {
                    const GLTextureR* tex = static_cast<const GLTextureR*>(cmd.source);
                    if (tex)
                    {
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, tex->m_fbo);
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                        glBlitFramebuffer(0, 0, tex->m_width, tex->m_height, 0, 0,
                                          tex->m_width, tex->m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    }
                    self->m_parent->present();
                    break;
                }
                default: break;
                }
            }
            cmds.clear();
            for (auto& p : posts)
                p();
        }
    }

    GLCommandQueue(IGraphicsContext* parent)
    : m_parent(parent),
      m_initlk(m_initmt),
      m_thr(RenderingWorker, this)
    {
        m_initcv.wait(m_initlk);
        m_initlk.unlock();
    }

    void stopRenderer()
    {
        if (m_running)
        {
            m_running = false;
            m_cv.notify_one();
            if (m_thr.joinable())
                m_thr.join();
            for (int i=0 ; i<3 ; ++i)
                m_cmdBufs[i].clear();
        }
    }

    ~GLCommandQueue()
    {
        stopRenderer();
    }

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::SetShaderDataBinding);
        cmds.back().binding = binding;
        cmds.back().resToken = static_cast<IShaderDataBindingPriv*>(binding)->lock();
    }

    void setRenderTarget(ITextureR* target)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::SetRenderTarget);
        cmds.back().target = target;
    }

    void setViewport(const SWindowRect& rect, float znear, float zfar)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::SetViewport);
        cmds.back().viewport.rect = rect;
        cmds.back().viewport.znear = znear;
        cmds.back().viewport.zfar = zfar;
    }

    void setScissor(const SWindowRect& rect)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::SetScissor);
        cmds.back().viewport.rect = rect;
    }

    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        GLTextureR* texgl = static_cast<GLTextureR*>(tex);
        m_pendingResizes.push_back({texgl, width, height});
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        m_pendingPosts1.push_back(std::move(func));
    }

    void setClearColor(const float rgba[4])
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::SetClearColor);
        cmds.back().rgba[0] = rgba[0];
        cmds.back().rgba[1] = rgba[1];
        cmds.back().rgba[2] = rgba[2];
        cmds.back().rgba[3] = rgba[3];
    }

    void clearTarget(bool render=true, bool depth=true)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::ClearTarget);
        cmds.back().flags = 0;
        if (render)
            cmds.back().flags |= GL_COLOR_BUFFER_BIT;
        if (depth)
            cmds.back().flags |= GL_DEPTH_BUFFER_BIT;
    }

    void draw(size_t start, size_t count)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::Draw);
        cmds.back().start = start;
        cmds.back().count = count;
    }

    void drawIndexed(size_t start, size_t count)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::DrawIndexed);
        cmds.back().start = start;
        cmds.back().count = count;
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::DrawInstances);
        cmds.back().start = start;
        cmds.back().count = count;
        cmds.back().instCount = instCount;
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::DrawInstancesIndexed);
        cmds.back().start = start;
        cmds.back().count = count;
        cmds.back().instCount = instCount;
    }

    void resolveBindTexture(ITextureR* texture, const SWindowRect& rect, bool tlOrigin,
                            int bindIdx, bool color, bool depth)
    {
        GLTextureR* tex = static_cast<GLTextureR*>(texture);
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::ResolveBindTexture);
        cmds.back().resolveTex = texture;
        cmds.back().bindIdx = bindIdx;
        cmds.back().resolveColor = color;
        cmds.back().resolveDepth = depth;
        SWindowRect intersectRect = rect.intersect(SWindowRect(0, 0, tex->m_width, tex->m_height));
        SWindowRect& targetRect = cmds.back().viewport.rect;
        targetRect.location[0] = intersectRect.location[0];
        if (tlOrigin)
            targetRect.location[1] = tex->m_height - intersectRect.location[1] - intersectRect.size[1];
        else
            targetRect.location[1] = intersectRect.location[1];
        targetRect.size[0] = intersectRect.size[0];
        targetRect.size[1] = intersectRect.size[1];
    }

    void resolveDisplay(ITextureR* source)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::Op::Present);
        cmds.back().source = source;
    }

    void addVertexFormat(GLVertexFormat* fmt)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_pendingFmtAdds.push_back(fmt);
    }

    void delVertexFormat(GLVertexFormat* fmt)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        bool foundAdd = false;
        for (GLVertexFormat*& afmt : m_pendingFmtAdds)
            if (afmt == fmt)
            {
                foundAdd = true;
                afmt = nullptr;
                break;
            }
        if (!foundAdd)
            m_pendingFmtDels.push_back({fmt->m_vao[0], fmt->m_vao[1], fmt->m_vao[2]});
    }

    void addFBO(GLTextureR* tex)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_pendingFboAdds.push_back(tex);
    }

    void delFBO(GLTextureR* tex)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_pendingFboDels.push_back(tex->m_fbo);
    }

    void execute()
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_completeBuf = m_fillBuf;
        for (size_t i=0 ; i<3 ; ++i)
        {
            if (i == m_completeBuf || i == m_drawBuf)
                continue;
            m_fillBuf = i;
            break;
        }

        /* Update dynamic data here */
        GLDataFactoryImpl* gfxF = static_cast<GLDataFactoryImpl*>(m_parent->getDataFactory());
        std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
        for (GLData* d : gfxF->m_committedData)
        {
            for (std::unique_ptr<GLGraphicsBufferD>& b : d->m_DBufs)
                b->update(m_completeBuf);
            for (std::unique_ptr<GLTextureD>& t : d->m_DTexs)
                t->update(m_completeBuf);
        }
        for (GLPool* p : gfxF->m_committedPools)
        {
            for (auto& b : p->m_items)
                b->m_buf->update(m_completeBuf);
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
};

void GLGraphicsBufferD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validMask) == 0)
    {
        glBindBuffer(m_target, m_bufs[b]);
        glBufferSubData(m_target, 0, m_cpuSz, m_cpuBuf.get());
        m_validMask |= slot;
    }
}

void GLGraphicsBufferD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validMask = 0;
}
void* GLGraphicsBufferD::map(size_t sz)
{
    if (sz < m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void GLGraphicsBufferD::unmap()
{
    m_validMask = 0;
}
void GLGraphicsBufferD::bindVertex(int b)
{glBindBuffer(GL_ARRAY_BUFFER, m_bufs[b]);}
void GLGraphicsBufferD::bindIndex(int b)
{glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_bufs[b]);}
void GLGraphicsBufferD::bindUniform(size_t idx, int b)
{glBindBufferBase(GL_UNIFORM_BUFFER, idx, m_bufs[b]);}
void GLGraphicsBufferD::bindUniformRange(size_t idx, GLintptr off, GLsizeiptr size, int b)
{glBindBufferRange(GL_UNIFORM_BUFFER, idx, m_bufs[b], off, size);}

IGraphicsBufferD*
GLDataFactory::Context::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLGraphicsBufferD* retval = new GLGraphicsBufferD(d, use, stride * count);
    d->m_DBufs.emplace_back(retval);
    return retval;
}

GLTextureD::GLTextureD(IGraphicsData* parent, size_t width, size_t height, TextureFormat fmt)
: boo::ITextureD(parent), m_width(width), m_height(height)
{
    int pxPitch = 4;
    switch (fmt)
    {
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
    default:
        Log.report(logvisor::Fatal, "unsupported tex format");
    }
    m_cpuSz = width * height * pxPitch;
    m_cpuBuf.reset(new uint8_t[m_cpuSz]);

    glGenTextures(3, m_texs);
    for (int i=0 ; i<3 ; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, m_texs[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, m_intFormat, width, height, 0, m_format, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
}
GLTextureD::~GLTextureD() {glDeleteTextures(3, m_texs);}

void GLTextureD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validMask) == 0)
    {
        glBindTexture(GL_TEXTURE_2D, m_texs[b]);
        glTexImage2D(GL_TEXTURE_2D, 0, m_intFormat, m_width, m_height, 0, m_format, GL_UNSIGNED_BYTE, m_cpuBuf.get());
        m_validMask |= slot;
    }
}

void GLTextureD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validMask = 0;
}
void* GLTextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void GLTextureD::unmap()
{
    m_validMask = 0;
}

void GLTextureD::bind(size_t idx, int b)
{
    glActiveTexture(GL_TEXTURE0 + idx);
    glBindTexture(GL_TEXTURE_2D, m_texs[b]);
}

ITextureD*
GLDataFactory::Context::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLTextureD* retval = new GLTextureD(d, width, height, fmt);
    d->m_DTexs.emplace_back(retval);
    return retval;
}

GLTextureR::GLTextureR(IGraphicsData* parent, GLCommandQueue* q, size_t width, size_t height, size_t samples,
                       size_t colorBindingCount, size_t depthBindingCount)
: boo::ITextureR(parent), m_q(q), m_width(width), m_height(height), m_samples(samples)
{
    glGenTextures(2, m_texs);
    if (colorBindingCount)
    {
        if (colorBindingCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many color bindings for render texture");
        glGenTextures(colorBindingCount, m_bindTexs[0]);
    }
    if (depthBindingCount)
    {
        if (depthBindingCount > MAX_BIND_TEXS)
            Log.report(logvisor::Fatal, "too many depth bindings for render texture");
        glGenTextures(depthBindingCount, m_bindTexs[1]);
    }
    if (samples > 1)
    {
        m_target = GL_TEXTURE_2D_MULTISAMPLE;
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[0]);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA, width, height, GL_FALSE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_texs[1]);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH_COMPONENT24, width, height, GL_FALSE);

        for (int i=0 ; i<colorBindingCount ; ++i)
        {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_bindTexs[0][i]);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA, width, height, GL_FALSE);
        }

        for (int i=0 ; i<depthBindingCount ; ++i)
        {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_bindTexs[1][i]);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH_COMPONENT24, width, height, GL_FALSE);
        }
    }
    else
    {
        m_target = GL_TEXTURE_2D;
        glBindTexture(GL_TEXTURE_2D, m_texs[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, m_texs[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

        for (int i=0 ; i<colorBindingCount ; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, m_bindTexs[0][i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        for (int i=0 ; i<depthBindingCount ; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, m_bindTexs[1][i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
    }
    m_q->addFBO(this);
}
GLTextureR::~GLTextureR()
{
    glDeleteTextures(2, m_texs);
    glDeleteTextures(MAX_BIND_TEXS * 2, m_bindTexs[0]);
    m_q->delFBO(this);
}

ITextureR*
GLDataFactory::Context::newRenderTexture(size_t width, size_t height,
                                         size_t colorBindingCount, size_t depthBindingCount)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
    GLCommandQueue* q = static_cast<GLCommandQueue*>(factory.m_parent->getCommandQueue());
    GLTextureR* retval = new GLTextureR(d, q, width, height, factory.m_drawSamples,
                                        colorBindingCount, depthBindingCount);
    q->resizeRenderTexture(retval, width, height);
    GLDataFactoryImpl::m_deferredData->m_RTexs.emplace_back(retval);
    return retval;
}

GLVertexFormat::GLVertexFormat(IGraphicsData* parent, GLCommandQueue* q, size_t elementCount,
                               const VertexElementDescriptor* elements,
                               size_t baseVert, size_t baseInst)
: boo::IVertexFormat(parent),
  m_q(q),
  m_elementCount(elementCount),
  m_elements(new VertexElementDescriptor[elementCount]),
  m_baseVert(baseVert), m_baseInst(baseInst)
{
    for (size_t i=0 ; i<elementCount ; ++i)
        m_elements[i] = elements[i];
    m_q->addVertexFormat(this);
}
GLVertexFormat::~GLVertexFormat() {m_q->delVertexFormat(this);}

IVertexFormat* GLDataFactory::Context::newVertexFormat
(size_t elementCount, const VertexElementDescriptor* elements,
 size_t baseVert, size_t baseInst)
{
    GLData* d = GLDataFactoryImpl::m_deferredData.get();
    GLDataFactoryImpl& factory = static_cast<GLDataFactoryImpl&>(m_parent);
    GLCommandQueue* q = static_cast<GLCommandQueue*>(factory.m_parent->getCommandQueue());
    GLVertexFormat* retval = new struct GLVertexFormat(d, q, elementCount, elements, baseVert, baseInst);
    d->m_VFmts.emplace_back(retval);
    return retval;
}

IGraphicsCommandQueue* _NewGLCommandQueue(IGraphicsContext* parent)
{
    return new struct GLCommandQueue(parent);
}

IGraphicsDataFactory* _NewGLDataFactory(IGraphicsContext* parent, uint32_t drawSamples)
{
    return new class GLDataFactoryImpl(parent, drawSamples);
}

}

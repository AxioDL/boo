#include "boo/graphicsdev/GL.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <LogVisor/LogVisor.hpp>

namespace boo
{
static LogVisor::LogModule Log("boo::GL");

struct GLES3Data : IGraphicsData
{
    std::vector<std::unique_ptr<class GLES3ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct GLES3ShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class GLES3GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class GLES3GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class GLES3TextureS>> m_STexs;
    std::vector<std::unique_ptr<class GLES3TextureD>> m_DTexs;
    std::vector<std::unique_ptr<struct GLES3VertexFormat>> m_VFmts;
};

static const GLenum USE_TABLE[] =
{
    GL_INVALID_ENUM,
    GL_ARRAY_BUFFER,
    GL_ELEMENT_ARRAY_BUFFER,
    GL_UNIFORM_BUFFER
};

class GLES3GraphicsBufferS : IGraphicsBufferS
{
    friend class GLES3DataFactory;
    friend struct GLES3CommandQueue;
    GLuint m_buf;
    GLenum m_target;
    GLES3GraphicsBufferS(BufferUse use, const void* data, size_t sz)
    {
        m_target = USE_TABLE[use];
        glGenBuffers(1, &m_buf);
        glBindBuffer(m_target, m_buf);
        glBufferData(m_target, sz, data, GL_STATIC_DRAW);
    }
public:
    ~GLES3GraphicsBufferS() {glDeleteBuffers(1, &m_buf);}

    void bindVertex() const
    {glBindBuffer(GL_ARRAY_BUFFER, m_buf);}
    void bindIndex() const
    {glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buf);}
    void bindUniform(size_t idx) const
    {glBindBufferBase(GL_UNIFORM_BUFFER, idx, m_buf);}
};

class GLES3GraphicsBufferD : IGraphicsBufferD
{
    friend class GLES3DataFactory;
    friend struct GLES3CommandQueue;
    struct GLES3CommandQueue* m_q;
    GLuint m_bufs[3];
    GLenum m_target;
    void* m_mappedBuf = nullptr;
    size_t m_mappedSize = 0;
    GLES3GraphicsBufferD(GLES3CommandQueue* q, BufferUse use)
    : m_q(q)
    {
        m_target = USE_TABLE[use];
        glGenBuffers(3, m_bufs);
    }
public:
    ~GLES3GraphicsBufferD() {glDeleteBuffers(3, m_bufs);}

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    void bindVertex() const;
    void bindIndex() const;
    void bindUniform(size_t idx) const;
};

const IGraphicsBufferS*
GLES3DataFactory::newStaticBuffer(BufferUse use, const void* data, size_t sz)
{
    GLES3GraphicsBufferS* retval = new GLES3GraphicsBufferS(use, data, sz);
    static_cast<GLES3Data*>(m_deferredData.get())->m_SBufs.emplace_back(retval);
    return retval;
}

class GLES3TextureS : ITextureS
{
    friend class GLES3DataFactory;
    GLuint m_tex;
    GLES3TextureS(size_t width, size_t height, size_t mips,
                  TextureFormat fmt, const void* data, size_t sz)
    {
        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        if (fmt == TextureFormatRGBA8)
        {
            for (size_t i=0 ; i<mips ; ++i)
            {
                glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, dataIt);
                dataIt += width * height * 4;
                width /= 2;
                height /= 2;
            }
        }
    }
public:
    ~GLES3TextureS() {glDeleteTextures(1, &m_tex);}

    void bind(size_t idx) const
    {
        glActiveTexture(GL_TEXTURE0 + idx);
        glBindTexture(GL_TEXTURE_2D, m_tex);
    }
};

class GLES3TextureD : ITextureD
{
    friend class GLES3DataFactory;
    friend struct GLES3CommandQueue;
    struct GLES3CommandQueue* m_q;
    GLuint m_texs[2];
    GLuint m_fbo = 0;
    void* m_mappedBuf = nullptr;
    size_t m_mappedSize = 0;
    size_t m_width = 0;
    size_t m_height = 0;
    GLES3TextureD(GLES3CommandQueue* q, size_t width, size_t height, TextureFormat fmt);
public:
    ~GLES3TextureD();

    void load(const void* data, size_t sz)
    {
        glBindTexture(GL_TEXTURE_2D, m_texs[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }
    void* map(size_t sz)
    {
        if (m_mappedBuf)
            free(m_mappedBuf);
        m_mappedBuf = malloc(sz);
        m_mappedSize = sz;
        return m_mappedBuf;
    }
    void unmap()
    {
        glBindTexture(GL_TEXTURE_2D, m_texs[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_mappedBuf);
        free(m_mappedBuf);
        m_mappedBuf = nullptr;
    }

    void bind(size_t idx) const
    {
        glActiveTexture(GL_TEXTURE0 + idx);
        glBindTexture(GL_TEXTURE_2D, m_texs[0]);
    }
};

const ITextureS*
GLES3DataFactory::newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                   const void* data, size_t sz)
{
    GLES3TextureS* retval = new GLES3TextureS(width, height, mips, fmt, data, sz);
    static_cast<GLES3Data*>(m_deferredData.get())->m_STexs.emplace_back(retval);
    return retval;
}

class GLES3ShaderPipeline : public IShaderPipeline
{
    friend class GLES3DataFactory;
    GLuint m_vert = 0;
    GLuint m_frag = 0;
    GLuint m_prog = 0;
    GLenum m_sfactor = GL_ONE;
    GLenum m_dfactor = GL_ZERO;
    bool m_depthTest = true;
    bool m_depthWrite = true;
    bool m_backfaceCulling = true;
    bool initObjects()
    {
        m_vert = glCreateShader(GL_VERTEX_SHADER);
        m_frag = glCreateShader(GL_FRAGMENT_SHADER);
        m_prog = glCreateProgram();
        if (!m_vert || !m_frag || !m_prog)
        {

            glDeleteShader(m_vert);
            m_vert = 0;
            glDeleteShader(m_frag);
            m_frag = 0;
            glDeleteProgram(m_prog);
            m_prog = 0;
            return false;
        }
        glAttachShader(m_prog, m_vert);
        glAttachShader(m_prog, m_frag);
        return true;
    }
    void clearObjects()
    {
        if (m_vert)
            glDeleteShader(m_vert);
        if (m_frag)
            glDeleteShader(m_frag);
        if (m_prog)
            glDeleteProgram(m_prog);
    }
    GLES3ShaderPipeline() = default;
public:
    operator bool() const {return m_prog != 0;}
    ~GLES3ShaderPipeline() {clearObjects();}
    GLES3ShaderPipeline& operator=(const GLES3ShaderPipeline&) = delete;
    GLES3ShaderPipeline(const GLES3ShaderPipeline&) = delete;
    GLES3ShaderPipeline& operator=(GLES3ShaderPipeline&& other)
    {
        m_vert = other.m_vert;
        other.m_vert = 0;
        m_frag = other.m_frag;
        other.m_frag = 0;
        m_prog = other.m_prog;
        other.m_prog = 0;
        return *this;
    }
    GLES3ShaderPipeline(GLES3ShaderPipeline&& other) {*this = std::move(other);}

    GLuint bind() const
    {
        glUseProgram(m_prog);

        if (m_dfactor != GL_ZERO)
        {
            glEnable(GL_BLEND);
            glBlendFunc(m_sfactor, m_dfactor);
        }
        else
            glDisable(GL_BLEND);

        if (m_depthTest)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        glDepthMask(m_depthWrite);

        if (m_backfaceCulling)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);

        return m_prog;
    }
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
    GL_ONE_MINUS_DST_ALPHA
};

const IShaderPipeline* GLES3DataFactory::newShaderPipeline
(const char* vertSource, const char* fragSource,
 size_t texCount, const char** texNames,
 BlendFactor srcFac, BlendFactor dstFac,
 bool depthTest, bool depthWrite, bool backfaceCulling)
{
    GLES3ShaderPipeline shader;
    if (!shader.initObjects())
    {
        Log.report(LogVisor::Error, "unable to create shader objects\n");
        return nullptr;
    }
    shader.m_sfactor = BLEND_FACTOR_TABLE[srcFac];
    shader.m_dfactor = BLEND_FACTOR_TABLE[dstFac];
    shader.m_depthTest = depthTest;
    shader.m_depthWrite = depthWrite;
    shader.m_backfaceCulling = backfaceCulling;

    glShaderSource(shader.m_vert, 1, &vertSource, nullptr);
    glCompileShader(shader.m_vert);
    GLint status;
    glGetShaderiv(shader.m_vert, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint logLen;
        glGetShaderiv(shader.m_vert, GL_INFO_LOG_LENGTH, &logLen);
        char* log = (char*)malloc(logLen);
        glGetShaderInfoLog(shader.m_vert, logLen, nullptr, log);
        Log.report(LogVisor::Error, "unable to compile vert source\n%s\n%s\n", log, vertSource);
        free(log);
        return nullptr;
    }

    glShaderSource(shader.m_frag, 1, &fragSource, nullptr);
    glCompileShader(shader.m_frag);
    glGetShaderiv(shader.m_frag, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint logLen;
        glGetShaderiv(shader.m_frag, GL_INFO_LOG_LENGTH, &logLen);
        char* log = (char*)malloc(logLen);
        glGetShaderInfoLog(shader.m_frag, logLen, nullptr, log);
        Log.report(LogVisor::Error, "unable to compile frag source\n%s\n%s\n", log, fragSource);
        free(log);
        return nullptr;
    }

    glLinkProgram(shader.m_prog);
    glGetProgramiv(shader.m_prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint logLen;
        glGetProgramiv(shader.m_prog, GL_INFO_LOG_LENGTH, &logLen);
        char* log = (char*)malloc(logLen);
        glGetProgramInfoLog(shader.m_prog, logLen, nullptr, log);
        Log.report(LogVisor::Error, "unable to link shader program\n%s\n", log);
        free(log);
        return nullptr;
    }

    glUseProgram(shader.m_prog);
    for (size_t i=0 ; i<texCount ; ++i)
    {
        GLint loc;
        if ((loc = glGetUniformLocation(shader.m_prog, texNames[i])) >= 0)
            glUniform1i(loc, i);
    }

    GLES3ShaderPipeline* retval = new GLES3ShaderPipeline(std::move(shader));
    static_cast<GLES3Data*>(m_deferredData.get())->m_SPs.emplace_back(retval);
    return retval;
}

struct GLES3VertexFormat : IVertexFormat
{
    GLES3CommandQueue* m_q;
    GLuint m_vao = 0;
    size_t m_elementCount;
    std::unique_ptr<VertexElementDescriptor[]> m_elements;
    GLES3VertexFormat(GLES3CommandQueue* q, size_t elementCount,
                      const VertexElementDescriptor* elements);
    ~GLES3VertexFormat();
    void bind() const {glBindVertexArray(m_vao);}
};

struct GLES3ShaderDataBinding : IShaderDataBinding
{
    const GLES3ShaderPipeline* m_pipeline;
    const GLES3VertexFormat* m_vtxFormat;
    size_t m_ubufCount;
    std::unique_ptr<const IGraphicsBuffer*[]> m_ubufs;
    size_t m_texCount;
    std::unique_ptr<const ITexture*[]> m_texs;
    GLES3ShaderDataBinding(const IShaderPipeline* pipeline,
                           const IVertexFormat* vtxFormat,
                           size_t ubufCount, const IGraphicsBuffer** ubufs,
                           size_t texCount, const ITexture** texs)
    : m_pipeline(static_cast<const GLES3ShaderPipeline*>(pipeline)),
      m_vtxFormat(static_cast<const GLES3VertexFormat*>(vtxFormat)),
      m_ubufCount(ubufCount),
      m_ubufs(new const IGraphicsBuffer*[ubufCount]),
      m_texCount(texCount),
      m_texs(new const ITexture*[texCount])
    {
        for (size_t i=0 ; i<ubufCount ; ++i)
            m_ubufs[i] = ubufs[i];
        for (size_t i=0 ; i<texCount ; ++i)
            m_texs[i] = texs[i];
    }
    void bind() const
    {
        GLuint prog = m_pipeline->bind();
        m_vtxFormat->bind();
        for (size_t i=0 ; i<m_ubufCount ; ++i)
        {
            m_ubufs[i]->bindUniform(i);
            glUniformBlockBinding(prog, i, i);
        }
        for (size_t i=0 ; i<m_texCount ; ++i)
            m_texs[i]->bind(i);
    }
};

const IShaderDataBinding*
GLES3DataFactory::newShaderDataBinding(const IShaderPipeline* pipeline,
                                       const IVertexFormat* vtxFormat,
                                       const IGraphicsBuffer*, const IGraphicsBuffer*,
                                       size_t ubufCount, const IGraphicsBuffer** ubufs,
                                       size_t texCount, const ITexture** texs)
{
    GLES3ShaderDataBinding* retval =
    new GLES3ShaderDataBinding(pipeline, vtxFormat, ubufCount, ubufs, texCount, texs);
    static_cast<GLES3Data*>(m_deferredData.get())->m_SBinds.emplace_back(retval);
    return retval;
}

GLES3DataFactory::GLES3DataFactory(IGraphicsContext* parent)
: m_parent(parent), m_deferredData(new struct GLES3Data()) {}

void GLES3DataFactory::reset()
{
    m_deferredData.reset(new struct GLES3Data());
}

std::unique_ptr<IGraphicsData> GLES3DataFactory::commit()
{
    std::unique_ptr<IGraphicsData> retval(new struct GLES3Data());
    m_deferredData.swap(retval);
    return retval;
}

static const GLint SEMANTIC_COUNT_TABLE[] =
{
    3,
    3,
    4,
    2,
    4
};

static const size_t SEMANTIC_SIZE_TABLE[] =
{
    12,
    12,
    4,
    8,
    16
};

static const GLenum SEMANTIC_TYPE_TABLE[] =
{
    GL_FLOAT,
    GL_FLOAT,
    GL_UNSIGNED_BYTE,
    GL_FLOAT,
    GL_FLOAT
};

struct GLES3CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::PlatformOGLES3;}
    const char* platformName() const {return "OpenGL ES 3.0";}
    IGraphicsContext* m_parent = nullptr;

    struct Command
    {
        enum Op
        {
            OpSetShaderDataBinding,
            OpSetRenderTarget,
            OpSetClearColor,
            OpClearTarget,
            OpSetDrawPrimitive,
            OpDraw,
            OpDrawIndexed,
            OpDrawInstances,
            OpDrawInstancesIndexed,
            OpPresent
        } m_op;
        union
        {
            const IShaderDataBinding* binding;
            const ITextureD* target;
            float rgba[4];
            GLbitfield flags;
            GLenum prim;
            struct
            {
                size_t start;
                size_t count;
                size_t instCount;
            };
        };
        Command(Op op) : m_op(op) {}
    };
    std::vector<Command> m_cmdBufs[3];
    size_t m_fillBuf = 0;
    size_t m_completeBuf = 0;
    size_t m_drawBuf = 0;
    bool m_running = true;

    std::mutex m_mt;
    std::condition_variable m_cv;
    std::unique_lock<std::mutex> m_initlk;
    std::condition_variable m_initcv;
    std::thread m_thr;

    /* These members are locked for multithreaded access */
    std::vector<GLES3VertexFormat*> m_pendingFmtAdds;
    std::vector<GLuint> m_pendingFmtDels;
    std::vector<GLES3TextureD*> m_pendingFboAdds;
    std::vector<GLuint> m_pendingFboDels;

    static void ConfigureVertexFormat(GLES3VertexFormat* fmt)
    {
        glGenVertexArrays(1, &fmt->m_vao);

        size_t stride = 0;
        for (size_t i=0 ; i<fmt->m_elementCount ; ++i)
        {
            const VertexElementDescriptor* desc = &fmt->m_elements[i];
            stride += SEMANTIC_SIZE_TABLE[desc->semantic];
        }

        size_t offset = 0;
        glBindVertexArray(fmt->m_vao);
        const IGraphicsBuffer* lastVBO = nullptr;
        const IGraphicsBuffer* lastEBO = nullptr;
        for (size_t i=0 ; i<fmt->m_elementCount ; ++i)
        {
            const VertexElementDescriptor* desc = &fmt->m_elements[i];
            if (desc->vertBuffer != lastVBO)
            {
                lastVBO = desc->vertBuffer;
                lastVBO->bindVertex();
            }
            if (desc->indexBuffer != lastEBO)
            {
                lastEBO = desc->indexBuffer;
                lastEBO->bindIndex();
            }
            glVertexAttribPointer(i, SEMANTIC_COUNT_TABLE[desc->semantic],
                    SEMANTIC_TYPE_TABLE[desc->semantic], GL_TRUE, stride, (void*)offset);
            offset += SEMANTIC_SIZE_TABLE[desc->semantic];
        }
    }

    static void ConfigureFBO(GLES3TextureD* tex)
    {
        glGenFramebuffers(1, &tex->m_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tex->m_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->m_texs[0], 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex->m_texs[1], 0);
    }

    static void RenderingWorker(GLES3CommandQueue* self)
    {
        {
            std::unique_lock<std::mutex> lk(self->m_mt);
            self->m_parent->makeCurrent();
            if (glewInit() != GLEW_OK)
                Log.report(LogVisor::FatalError, "unable to init glew");
            self->m_parent->postInit();
        }
        self->m_initcv.notify_one();
        while (self->m_running)
        {
            {
                std::unique_lock<std::mutex> lk(self->m_mt);
                self->m_cv.wait(lk);
                if (!self->m_running)
                    break;
                self->m_drawBuf = self->m_completeBuf;

                if (self->m_pendingFmtAdds.size())
                    for (GLES3VertexFormat* fmt : self->m_pendingFmtAdds)
                        ConfigureVertexFormat(fmt);
                self->m_pendingFmtAdds.clear();

                if (self->m_pendingFmtDels.size())
                    for (GLuint fmt : self->m_pendingFmtDels)
                        glDeleteVertexArrays(1, &fmt);
                self->m_pendingFmtDels.clear();

                if (self->m_pendingFboAdds.size())
                    for (GLES3TextureD* tex : self->m_pendingFboAdds)
                        ConfigureFBO(tex);
                self->m_pendingFboAdds.clear();

                if (self->m_pendingFboDels.size())
                    for (GLuint fbo : self->m_pendingFboDels)
                        glDeleteFramebuffers(1, &fbo);
                self->m_pendingFboDels.clear();
            }
            std::vector<Command>& cmds = self->m_cmdBufs[self->m_drawBuf];
            GLenum prim = GL_TRIANGLES;
            for (const Command& cmd : cmds)
            {
                switch (cmd.m_op)
                {
                case Command::OpSetShaderDataBinding:
                    static_cast<const GLES3ShaderDataBinding*>(cmd.binding)->bind();
                    break;
                case Command::OpSetRenderTarget:
                {
                    const GLES3TextureD* tex = static_cast<const GLES3TextureD*>(cmd.target);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tex->m_fbo);
                    break;
                }
                case Command::OpSetClearColor:
                    glClearColor(cmd.rgba[0], cmd.rgba[1], cmd.rgba[2], cmd.rgba[3]);
                    break;
                case Command::OpClearTarget:
                    glClear(cmd.flags);
                    break;
                case Command::OpSetDrawPrimitive:
                    prim = cmd.prim;
                    break;
                case Command::OpDraw:
                    glDrawArrays(prim, cmd.start, cmd.count);
                    break;
                case Command::OpDrawIndexed:
                    glDrawElements(prim, cmd.count, GL_UNSIGNED_INT, (void*)cmd.start);
                    break;
                case Command::OpDrawInstances:
                    glDrawArraysInstanced(prim, cmd.start, cmd.count, cmd.instCount);
                    break;
                case Command::OpDrawInstancesIndexed:
                    glDrawElementsInstanced(prim, cmd.count, GL_UNSIGNED_INT, (void*)cmd.start, cmd.instCount);
                    break;
                case Command::OpPresent:
                    self->m_parent->present();
                    break;
                default: break;
                }
            }
            cmds.clear();
        }
    }

    GLES3CommandQueue(IGraphicsContext* parent)
    : m_parent(parent),
      m_initlk(m_mt),
      m_thr(RenderingWorker, this)
    {
        m_initcv.wait(m_initlk);
        m_initlk.unlock();
    }

    ~GLES3CommandQueue()
    {
        m_running = false;
        m_cv.notify_one();
        m_thr.join();
    }

    void setShaderDataBinding(const IShaderDataBinding* binding)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetShaderDataBinding);
        cmds.back().binding = binding;
    }
    void setRenderTarget(const ITextureD* target)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetRenderTarget);
        cmds.back().target = target;
    }

    void setClearColor(const float rgba[4])
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetClearColor);
        cmds.back().rgba[0] = rgba[0];
        cmds.back().rgba[1] = rgba[1];
        cmds.back().rgba[2] = rgba[2];
        cmds.back().rgba[3] = rgba[3];
    }
    void clearTarget(bool render=true, bool depth=true)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpClearTarget);
        cmds.back().flags = 0;
        if (render)
            cmds.back().flags |= GL_COLOR_BUFFER_BIT;
        if (depth)
            cmds.back().flags |= GL_DEPTH_BUFFER_BIT;
    }

    void setDrawPrimitive(Primitive prim)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpSetDrawPrimitive);
        if (prim == PrimitiveTriangles)
            cmds.back().prim = GL_TRIANGLES;
        else if (prim == PrimitiveTriStrips)
            cmds.back().prim = GL_TRIANGLE_STRIP;
    }
    void draw(size_t start, size_t count)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDraw);
        cmds.back().start = start;
        cmds.back().count = count;
    }
    void drawIndexed(size_t start, size_t count)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDrawIndexed);
        cmds.back().start = start;
        cmds.back().count = count;
    }
    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDrawInstances);
        cmds.back().start = start;
        cmds.back().count = count;
        cmds.back().instCount = instCount;
    }
    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpDrawInstancesIndexed);
        cmds.back().start = start;
        cmds.back().count = count;
        cmds.back().instCount = instCount;
    }

    void present()
    {
        std::vector<Command>& cmds = m_cmdBufs[m_fillBuf];
        cmds.emplace_back(Command::OpPresent);
    }

    void addVertexFormat(GLES3VertexFormat* fmt)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_pendingFmtAdds.push_back(fmt);
    }

    void delVertexFormat(GLES3VertexFormat* fmt)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_pendingFmtDels.push_back(fmt->m_vao);
    }

    void addFBO(GLES3TextureD* tex)
    {
        std::unique_lock<std::mutex> lk(m_mt);
        m_pendingFboAdds.push_back(tex);
    }

    void delFBO(GLES3TextureD* tex)
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
        lk.unlock();
        m_cv.notify_one();
        m_cmdBufs[m_fillBuf].clear();
    }
};

void GLES3GraphicsBufferD::load(const void* data, size_t sz)
{
    glBindBuffer(m_target, m_bufs[m_q->m_fillBuf]);
    glBufferData(m_target, sz, data, GL_DYNAMIC_DRAW);
}
void* GLES3GraphicsBufferD::map(size_t sz)
{
    if (m_mappedBuf)
        free(m_mappedBuf);
    m_mappedBuf = malloc(sz);
    m_mappedSize = sz;
    return m_mappedBuf;
}
void GLES3GraphicsBufferD::unmap()
{
    glBindBuffer(m_target, m_bufs[m_q->m_fillBuf]);
    glBufferData(m_target, m_mappedSize, m_mappedBuf, GL_DYNAMIC_DRAW);
    free(m_mappedBuf);
    m_mappedBuf = nullptr;
}
void GLES3GraphicsBufferD::bindVertex() const
{glBindBuffer(GL_ARRAY_BUFFER, m_bufs[m_q->m_drawBuf]);}
void GLES3GraphicsBufferD::bindIndex() const
{glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_bufs[m_q->m_drawBuf]);}
void GLES3GraphicsBufferD::bindUniform(size_t idx) const
{glBindBufferBase(GL_UNIFORM_BUFFER, idx, m_bufs[m_q->m_drawBuf]);}

IGraphicsBufferD*
GLES3DataFactory::newDynamicBuffer(BufferUse use)
{
    GLES3CommandQueue* q = static_cast<GLES3CommandQueue*>(m_parent->getCommandQueue());
    GLES3GraphicsBufferD* retval = new GLES3GraphicsBufferD(q, use);
    static_cast<GLES3Data*>(m_deferredData.get())->m_DBufs.emplace_back(retval);
    return retval;
}

GLES3TextureD::GLES3TextureD(GLES3CommandQueue* q, size_t width, size_t height, TextureFormat fmt)
: m_q(q)
{
    m_width = width;
    m_height = height;
    glGenTextures(2, m_texs);
    glBindTexture(GL_TEXTURE_2D, m_texs[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_texs[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    m_q->addFBO(this);
}
GLES3TextureD::~GLES3TextureD() {glDeleteTextures(2, m_texs); m_q->delFBO(this);}

ITextureD*
GLES3DataFactory::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    GLES3CommandQueue* q = static_cast<GLES3CommandQueue*>(m_parent->getCommandQueue());
    GLES3TextureD* retval = new GLES3TextureD(q, width, height, fmt);
    static_cast<GLES3Data*>(m_deferredData.get())->m_DTexs.emplace_back(retval);
    return retval;
}

GLES3VertexFormat::GLES3VertexFormat(GLES3CommandQueue* q, size_t elementCount,
                                     const VertexElementDescriptor* elements)
: m_q(q),
  m_elementCount(elementCount),
  m_elements(new VertexElementDescriptor[elementCount])
{
    for (size_t i=0 ; i<elementCount ; ++i)
        m_elements[i] = elements[i];
    m_q->addVertexFormat(this);
}
GLES3VertexFormat::~GLES3VertexFormat() {m_q->delVertexFormat(this);}

const IVertexFormat* GLES3DataFactory::newVertexFormat
(size_t elementCount, const VertexElementDescriptor* elements)
{
    GLES3CommandQueue* q = static_cast<GLES3CommandQueue*>(m_parent->getCommandQueue());
    GLES3VertexFormat* retval = new struct GLES3VertexFormat(q, elementCount, elements);
    static_cast<GLES3Data*>(m_deferredData.get())->m_VFmts.emplace_back(retval);
    return retval;
}

IGraphicsCommandQueue* _NewGLES3CommandQueue(IGraphicsContext* parent)
{
    return new struct GLES3CommandQueue(parent);
}

}

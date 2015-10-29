#include "boo/graphicsdev/GLES3.hpp"
#include "boo/IGraphicsContext.hpp"
#include <GLES3/gl3ext.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace boo
{

struct GLES3Data : IGraphicsData
{
    std::vector<std::unique_ptr<class GLES3ShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<class GLES3GraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class GLES3GraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class GLES3TextureS>> m_STexs;
    std::vector<std::unique_ptr<class GLES3TextureD>> m_DTexs;
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
    GLuint m_buf;
    GLenum m_target;
    GLES3GraphicsBufferS(IGraphicsDataFactory::BufferUse use, const void* data, size_t sz)
    {
        m_target = USE_TABLE[use];
        glGenBuffers(1, &m_buf);
        glBindBuffer(m_target, m_buf);
        glBufferData(m_target, sz, data, GL_STATIC_DRAW);
    }
public:
    ~GLES3GraphicsBufferS() {glDeleteBuffers(1, &m_buf);}
};

class GLES3GraphicsBufferD : IGraphicsBufferD
{
    friend class GLES3DataFactory;
    GLuint m_buf;
    GLenum m_target;
    void* m_mappedBuf = nullptr;
    size_t m_mappedSize = 0;
    GLES3GraphicsBufferD(IGraphicsDataFactory::BufferUse use)
    {
        m_target = USE_TABLE[use];
        glGenBuffers(1, &m_buf);
    }
public:
    ~GLES3GraphicsBufferD() {glDeleteBuffers(1, &m_buf);}

    void load(const void* data, size_t sz)
    {
        glBindBuffer(m_target, m_buf);
        glBufferData(m_target, sz, data, GL_DYNAMIC_DRAW);
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
        glBindBuffer(m_target, m_buf);
        glBufferData(m_target, m_mappedSize, m_mappedBuf, GL_DYNAMIC_DRAW);
        free(m_mappedBuf);
        m_mappedBuf = nullptr;
    }
};

const IGraphicsBufferS*
GLES3DataFactory::newStaticBuffer(BufferUse use, const void* data, size_t sz)
{
    GLES3GraphicsBufferS* retval = new GLES3GraphicsBufferS(use, data, sz);
    static_cast<GLES3Data*>(m_deferredData.get())->m_SBufs.emplace_back(retval);
    return retval;
}

IGraphicsBufferD*
GLES3DataFactory::newDynamicBuffer(BufferUse use)
{
    GLES3GraphicsBufferD* retval = new GLES3GraphicsBufferD(use);
    static_cast<GLES3Data*>(m_deferredData.get())->m_DBufs.emplace_back(retval);
    return retval;
}

class GLES3TextureS : ITextureS
{
    friend class GLES3DataFactory;
    GLuint m_tex;
    GLES3TextureS(size_t width, size_t height, size_t mips,
                  IGraphicsDataFactory::TextureFormat fmt,
                  const void* data, size_t sz)
    {
        const uint8_t* dataIt = static_cast<const uint8_t*>(data);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        if (fmt == IGraphicsDataFactory::TextureFormatRGBA8)
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
};

class GLES3TextureD : ITextureD
{
    friend class GLES3DataFactory;
    friend class GLES3CommandQueue;
    GLuint m_tex;
    GLuint m_fbo;
    void* m_mappedBuf = nullptr;
    size_t m_mappedSize = 0;
    size_t m_width = 0;
    size_t m_height = 0;
    GLES3TextureD(size_t width, size_t height,
                  IGraphicsDataFactory::TextureFormat fmt)
    {
        m_width = width;
        m_height = height;
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
public:
    ~GLES3TextureD() {glDeleteTextures(1, &m_tex);}

    void load(const void* data, size_t sz)
    {
        glBindTexture(GL_TEXTURE_2D, m_tex);
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
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_mappedBuf);
        free(m_mappedBuf);
        m_mappedBuf = nullptr;
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
ITextureD*
GLES3DataFactory::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    GLES3TextureD* retval = new GLES3TextureD(width, height, fmt);
    static_cast<GLES3Data*>(m_deferredData.get())->m_DTexs.emplace_back(retval);
    return retval;
}

class GLES3ShaderPipeline : IShaderPipeline
{
    friend class GLES3DataFactory;
    GLuint m_vert = 0;
    GLuint m_frag = 0;
    GLuint m_prog = 0;
    GLenum m_sfactor = GL_ONE;
    GLenum m_dfactor = GL_ZERO;
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
};

bool GLES3VertexArray::initObjects()
{
    glGenVertexArrays(1, &m_vao);
    if (!m_vao)
        return false;
    return true;
}

void GLES3VertexArray::clearObjects()
{
    glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
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

GLES3VertexArray GLES3DataFactory::newVertexArray
(size_t elementCount, const VertexElementDescriptor* elements)
{
    GLES3VertexArray vertArray;
    if (!vertArray.initObjects())
    {
        fprintf(stderr, "unable to create vertex array object\n");
        return vertArray;
    }

    size_t stride = 0;
    for (size_t i=0 ; i<elementCount ; ++i)
    {
        const VertexElementDescriptor* desc = &elements[i];
        stride += SEMANTIC_SIZE_TABLE[desc->semantic];
    }

    size_t offset = 0;
    glBindVertexArray(vertArray.m_vao);
    const IGraphicsBuffer* lastVBO = nullptr;
    const IGraphicsBuffer* lastEBO = nullptr;
    for (size_t i=0 ; i<elementCount ; ++i)
    {
        const VertexElementDescriptor* desc = &elements[i];
        if (desc->vertBuffer != lastVBO)
        {
            lastVBO = desc->vertBuffer;
            if (lastVBO->dynamic())
            {
                const GLES3GraphicsBufferD* vbo = static_cast<const GLES3GraphicsBufferD*>(lastVBO);
                glBindBuffer(GL_ARRAY_BUFFER, vbo->m_buf);
            }
            else
            {
                const GLES3GraphicsBufferS* vbo = static_cast<const GLES3GraphicsBufferS*>(lastVBO);
                glBindBuffer(GL_ARRAY_BUFFER, vbo->m_buf);
            }
        }
        if (desc->indexBuffer != lastEBO)
        {
            lastEBO = desc->indexBuffer;
            if (lastEBO->dynamic())
            {
                const GLES3GraphicsBufferD* ebo = static_cast<const GLES3GraphicsBufferD*>(lastEBO);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo->m_buf);
            }
            else
            {
                const GLES3GraphicsBufferS* ebo = static_cast<const GLES3GraphicsBufferS*>(lastEBO);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo->m_buf);
            }
        }
        glVertexAttribPointer(i, SEMANTIC_COUNT_TABLE[desc->semantic],
                SEMANTIC_TYPE_TABLE[desc->semantic], GL_TRUE, stride, (void*)offset);
        offset += SEMANTIC_SIZE_TABLE[desc->semantic];
    }

    return vertArray;
}

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
 BlendFactor srcFac, BlendFactor dstFac,
 bool depthTest, bool depthWrite, bool backfaceCulling)
{
    GLES3ShaderPipeline shader;
    if (!shader.initObjects())
    {
        fprintf(stderr, "unable to create shader objects\n");
        return nullptr;
    }
    shader.m_sfactor = BLEND_FACTOR_TABLE[srcFac];
    shader.m_dfactor = BLEND_FACTOR_TABLE[dstFac];

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
        fprintf(stderr, "unable to compile vert source\n%s\n%s\n", log, vertSource);
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
        fprintf(stderr, "unable to compile frag source\n%s\n%s\n", log, fragSource);
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
        fprintf(stderr, "unable to link shader program\n%s\n", log);
        free(log);
        return nullptr;
    }

    GLES3ShaderPipeline* retval = new GLES3ShaderPipeline(std::move(shader));
    static_cast<GLES3Data*>(m_deferredData.get())->m_SPs.emplace_back(retval);
    return retval;
}

struct GLES3ShaderDataBinding : IShaderDataBinding
{
    void bind() const
    {
    }
};

const IShaderDataBinding*
GLES3DataFactory::newShaderDataBinding(const IShaderPipeline* pipeline,
                                       size_t bufCount, const IGraphicsBuffer** bufs,
                                       size_t texCount, const ITexture** texs)
{
    return nullptr;
}

GLES3DataFactory::GLES3DataFactory()
: m_deferredData(new struct GLES3Data()) {}

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

struct GLES3CommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::PlatformOGLES3;}
    const char* platformName() const {return "OpenGL ES 3.0";}
    IGraphicsContext& m_parent;

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
            OpDrawInstancesIndexed
        } m_op;
        union
        {
            const IShaderDataBinding* binding;
            const ITextureD* target;
            float rgba[4];
            GLbitfield flags;
            Primitive prim;
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
    std::thread m_thr;
    std::mutex m_mt;
    std::condition_variable m_cv;

    static void RenderingWorker(GLES3CommandQueue* self)
    {
        self->m_parent.makeCurrent();
        while (self->m_running)
        {
            {
                std::unique_lock<std::mutex> lk(self->m_mt);
                self->m_cv.wait(lk);
                if (!self->m_running)
                    break;
                self->m_drawBuf = self->m_completeBuf;
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
                    if (cmd.prim == PrimitiveTriangles)
                        prim = GL_TRIANGLES;
                    else if (cmd.prim == TrimitiveTriStrips)
                        prim = GL_TRIANGLE_STRIP;
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
                default: break;
                }
            }
            cmds.clear();
        }
    }

    GLES3CommandQueue(IGraphicsContext& parent)
    : m_parent(parent),
      m_thr(RenderingWorker, this) {}

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
        cmds.back().prim = prim;
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

IGraphicsCommandQueue* _NewGLES3CommandQueue(IGraphicsContext& parent)
{
    return new struct GLES3CommandQueue(parent);
}

}

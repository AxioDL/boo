#ifndef GDEV_GLES3_HPP
#define GDEV_GLES3_HPP

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include <GLES3/gl3.h>
#include <vector>

namespace boo
{

class GLES3VertexArray
{
    friend class GLES3DataFactory;
    GLuint m_vao = 0;
    bool initObjects();
    void clearObjects();
    GLES3VertexArray() = default;
public:
    operator bool() const {return m_vao != 0;}
    ~GLES3VertexArray() {clearObjects();}
    GLES3VertexArray& operator=(const GLES3VertexArray&) = delete;
    GLES3VertexArray(const GLES3VertexArray&) = delete;
    GLES3VertexArray& operator=(GLES3VertexArray&& other)
    {
        m_vao = other.m_vao;
        other.m_vao = 0;
        return *this;
    }
    GLES3VertexArray(GLES3VertexArray&& other) {*this = std::move(other);}
};

class GLES3DataFactory : public IGraphicsDataFactory
{
    std::unique_ptr<IGraphicsData> m_deferredData;
public:
    GLES3DataFactory();

    Platform platform() const {return PlatformOGLES3;}
    const char* platformName() const {return "OpenGL ES 3.0";}

    const IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t sz);
    IGraphicsBufferD* newDynamicBuffer(BufferUse use);

    GLES3VertexArray newVertexArray(size_t elementCount, const VertexElementDescriptor* elements);

    const ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                      const void* data, size_t sz);
    ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);

    const IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                             BlendFactor srcFac, BlendFactor dstFac,
                                             bool depthTest, bool depthWrite, bool backfaceCulling);

    const IShaderDataBinding*
    newShaderDataBinding(const IShaderPipeline* pipeline,
                         size_t bufCount, const IGraphicsBuffer** bufs,
                         size_t texCount, const ITexture** texs);

    void reset();
    std::unique_ptr<IGraphicsData> commit();
};

}

#endif // GDEV_GLES3_HPP

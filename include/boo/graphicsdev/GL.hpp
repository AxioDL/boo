#ifndef GDEV_GL_HPP
#define GDEV_GL_HPP

#include "IGraphicsDataFactory.hpp"
#include "IGraphicsCommandQueue.hpp"
#include "boo/IGraphicsContext.hpp"
#include "GLSLMacros.hpp"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace boo
{

class GLDataFactory : public IGraphicsDataFactory
{
    friend struct GLCommandQueue;
    IGraphicsContext* m_parent;
    uint32_t m_drawSamples;
    static ThreadLocalPtr<struct GLData> m_deferredData;
    std::unordered_set<struct GLData*> m_committedData;
    std::unordered_set<struct GLPool*> m_committedPools;
    std::mutex m_committedMutex;
    void destroyData(IGraphicsData*);
    void destroyAllData();
    void destroyPool(IGraphicsBufferPool*);
    IGraphicsBufferD* newPoolBuffer(IGraphicsBufferPool* pool, BufferUse use,
                                    size_t stride, size_t count);
    void deletePoolBuffer(IGraphicsBufferPool* p, IGraphicsBufferD* buf);
public:
    GLDataFactory(IGraphicsContext* parent, uint32_t drawSamples);
    ~GLDataFactory() {destroyAllData();}

    Platform platform() const {return Platform::OpenGL;}
    const SystemChar* platformName() const {return _S("OpenGL");}

    class Context : public IGraphicsDataFactory::Context
    {
        friend class GLDataFactory;
        GLDataFactory& m_parent;
        Context(GLDataFactory& parent) : m_parent(parent) {}
    public:
        Platform platform() const {return Platform::OpenGL;}
        const SystemChar* platformName() const {return _S("OpenGL");}

        IGraphicsBufferS* newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count);
        IGraphicsBufferD* newDynamicBuffer(BufferUse use, size_t stride, size_t count);

        ITextureS* newStaticTexture(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                    const void* data, size_t sz);
        ITextureSA* newStaticArrayTexture(size_t width, size_t height, size_t layers, TextureFormat fmt,
                                          const void* data, size_t sz);
        ITextureD* newDynamicTexture(size_t width, size_t height, TextureFormat fmt);
        ITextureR* newRenderTexture(size_t width, size_t height,
                                    bool enableShaderColorBinding, bool enableShaderDepthBinding);

        bool bindingNeedsVertexFormat() const {return true;}
        IVertexFormat* newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements,
                                       size_t baseVert = 0, size_t baseInst = 0);

        IShaderPipeline* newShaderPipeline(const char* vertSource, const char* fragSource,
                                           size_t texCount, const char** texNames,
                                           size_t uniformBlockCount, const char** uniformBlockNames,
                                           BlendFactor srcFac, BlendFactor dstFac, Primitive prim,
                                           bool depthTest, bool depthWrite, bool backfaceCulling);

        IShaderDataBinding*
        newShaderDataBinding(IShaderPipeline* pipeline,
                             IVertexFormat* vtxFormat,
                             IGraphicsBuffer* vbo, IGraphicsBuffer* instVbo, IGraphicsBuffer* ibo,
                             size_t ubufCount, IGraphicsBuffer** ubufs, const PipelineStage* ubufStages,
                             const size_t* ubufOffs, const size_t* ubufSizes,
                             size_t texCount, ITexture** texs, size_t baseVert = 0, size_t baseInst = 0);
    };

    GraphicsDataToken commitTransaction(const FactoryCommitFunc&);
    GraphicsBufferPoolToken newBufferPool();
};

}

#endif // GDEV_GL_HPP

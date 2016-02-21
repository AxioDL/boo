#include "boo/graphicsdev/Vulkan.hpp"
#include "boo/IGraphicsContext.hpp"
#include <vector>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <LogVisor/LogVisor.hpp>

#undef min
#undef max

#define MAX_UNIFORM_COUNT 8
#define MAX_TEXTURE_COUNT 8

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

static inline void ThrowIfFailed(VkResult res)
{
    if (res != VK_SUCCESS)
        Log.report(LogVisor::FatalError, "%d\n", res);
}

static inline void ThrowIfFalse(bool res)
{
    if (!res)
        Log.report(LogVisor::FatalError, "operation failed\n", res);
}

struct VulkanData : IGraphicsData
{
    VulkanContext* m_ctx;
    VkDeviceMemory m_bufMem = VK_NULL_HANDLE;
    VkDeviceMemory m_texMem = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<class VulkanShaderPipeline>> m_SPs;
    std::vector<std::unique_ptr<struct VulkanShaderDataBinding>> m_SBinds;
    std::vector<std::unique_ptr<class VulkanGraphicsBufferS>> m_SBufs;
    std::vector<std::unique_ptr<class VulkanGraphicsBufferD>> m_DBufs;
    std::vector<std::unique_ptr<class VulkanTextureS>> m_STexs;
    std::vector<std::unique_ptr<class VulkanTextureSA>> m_SATexs;
    std::vector<std::unique_ptr<class VulkanTextureD>> m_DTexs;
    std::vector<std::unique_ptr<class VulkanTextureR>> m_RTexs;
    std::vector<std::unique_ptr<struct VulkanVertexFormat>> m_VFmts;
    VulkanData(VulkanContext* ctx) : m_ctx(ctx) {}
    ~VulkanData()
    {
        vkFreeMemory(m_ctx->m_dev, m_bufMem, nullptr);
        vkFreeMemory(m_ctx->m_dev, m_texMem, nullptr);
    }
};

static const VkBufferUsageFlagBits USE_TABLE[] =
{
    VkBufferUsageFlagBits(0),
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
};

static bool MemoryTypeFromProperties(VulkanContext* ctx, uint32_t typeBits,
                                     VkFlags requirementsMask,
                                     uint32_t *typeIndex)
{
    /* Search memtypes to find first index with those properties */
    for (uint32_t i = 0; i < 32; i++)
    {
        if ((typeBits & 1) == 1)
        {
            /* Type is available, does it match user properties? */
            if ((ctx->m_memoryProperties.memoryTypes[i].propertyFlags &
                 requirementsMask) == requirementsMask) {
                *typeIndex = i;
                return true;
            }
        }
        typeBits >>= 1;
    }
    /* No memory types matched, return failure */
    return false;
}

static void SetImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageAspectFlags aspectMask,
                           VkImageLayout old_image_layout,
                           VkImageLayout new_image_layout)
{
    VkImageMemoryBarrier image_memory_barrier = {};
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = NULL;
    image_memory_barrier.srcAccessMask = 0;
    image_memory_barrier.dstAccessMask = 0;
    image_memory_barrier.oldLayout = old_image_layout;
    image_memory_barrier.newLayout = new_image_layout;
    image_memory_barrier.image = image;
    image_memory_barrier.subresourceRange.aspectMask = aspectMask;
    image_memory_barrier.subresourceRange.baseMipLevel = 0;
    image_memory_barrier.subresourceRange.levelCount = 1;
    image_memory_barrier.subresourceRange.layerCount = 1;

    if (old_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        image_memory_barrier.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        /* Make sure anything that was copying from this image has completed */
        image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        /* Make sure any Copy or CPU writes to image are flushed */
        image_memory_barrier.srcAccessMask =
            VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        image_memory_barrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    }

    if (new_image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        image_memory_barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }

    VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dest_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(cmd, src_stages, dest_stages, 0, 0, NULL, 0, NULL,
                         1, &image_memory_barrier);
}

class VulkanGraphicsBufferS : public IGraphicsBufferS
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    VulkanContext* m_ctx;
    size_t m_sz;
    std::unique_ptr<uint8_t[]> m_stagingBuf;
    VulkanGraphicsBufferS(BufferUse use, VulkanContext* ctx, const void* data, size_t stride, size_t count)
    : m_ctx(ctx), m_stride(stride), m_count(count), m_sz(stride * count),
      m_stagingBuf(new uint8_t[m_sz]), m_uniform(use == BufferUse::Uniform)
    {
        memcpy(m_stagingBuf.get(), data, m_sz);

        VkBufferCreateInfo bufInfo = {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.pNext = nullptr;
        bufInfo.usage = USE_TABLE[int(use)];
        bufInfo.size = m_sz;
        bufInfo.queueFamilyIndexCount = 0;
        bufInfo.pQueueFamilyIndices = nullptr;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufInfo.flags = 0;
        ThrowIfFailed(vkCreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo.buffer));
    }
public:
    size_t m_stride;
    size_t m_count;
    VkDescriptorBufferInfo m_bufferInfo;
    bool m_uniform = false;
    ~VulkanGraphicsBufferS()
    {
        vkDestroyBuffer(m_ctx->m_dev, m_bufferInfo.buffer, nullptr);
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        if (m_uniform && ctx->m_devProps.limits.minUniformBufferOffsetAlignment)
        {
            offset = (offset +
                ctx->m_devProps.limits.minUniformBufferOffsetAlignment - 1) &
                ~(ctx->m_devProps.limits.minUniformBufferOffsetAlignment - 1);
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(ctx->m_dev, m_bufferInfo.buffer, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        m_bufferInfo.offset = offset;

        offset += m_sz;
        m_bufferInfo.range = offset - m_bufferInfo.offset;

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem, uint8_t* buf)
    {
        memcpy(buf + m_bufferInfo.offset, m_stagingBuf.get(), m_sz);
        m_stagingBuf.reset();
        ThrowIfFailed(vkBindBufferMemory(ctx->m_dev, m_bufferInfo.buffer, mem, m_bufferInfo.offset));
    }
};

class VulkanGraphicsBufferD : public IGraphicsBufferD
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    struct VulkanCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    int m_validSlots = 0;
    VulkanGraphicsBufferD(VulkanCommandQueue* q, BufferUse use, VulkanContext* ctx, size_t stride, size_t count)
    : m_q(q), m_stride(stride), m_count(count), m_cpuSz(stride * count),
      m_uniform(use == BufferUse::Uniform)
    {
        VkBufferCreateInfo bufInfo = {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.pNext = nullptr;
        bufInfo.usage = USE_TABLE[int(use)];
        bufInfo.size = m_cpuSz;
        bufInfo.queueFamilyIndexCount = 0;
        bufInfo.pQueueFamilyIndices = nullptr;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufInfo.flags = 0;
        ThrowIfFailed(vkCreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo[0].buffer));
        ThrowIfFailed(vkCreateBuffer(ctx->m_dev, &bufInfo, nullptr, &m_bufferInfo[1].buffer));
    }
    void update(int b);
public:
    size_t m_stride;
    size_t m_count;
    VkDeviceMemory m_mem;
    VkDescriptorBufferInfo m_bufferInfo[2];
    bool m_uniform = false;
    ~VulkanGraphicsBufferD();
    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            if (m_uniform && ctx->m_devProps.limits.minUniformBufferOffsetAlignment)
            {
                offset = (offset +
                    ctx->m_devProps.limits.minUniformBufferOffsetAlignment - 1) &
                    ~(ctx->m_devProps.limits.minUniformBufferOffsetAlignment - 1);
            }

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(ctx->m_dev, m_bufferInfo[i].buffer, &memReqs);
            memTypeBits &= memReqs.memoryTypeBits;
            offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            m_bufferInfo[i].offset = offset;

            offset += memReqs.size;
            m_bufferInfo[i].range = offset - m_bufferInfo[i].offset;
        }

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        m_mem = mem;
        ThrowIfFailed(vkBindBufferMemory(ctx->m_dev, m_bufferInfo[0].buffer, mem, m_bufferInfo[0].offset));
        ThrowIfFailed(vkBindBufferMemory(ctx->m_dev, m_bufferInfo[1].buffer, mem, m_bufferInfo[1].offset));
    }
};

class VulkanTextureS : public ITextureS
{
    friend class VulkanDataFactory;
    VulkanContext* m_ctx;
    TextureFormat m_fmt;
    size_t m_sz;
    size_t m_width, m_height, m_mips;
    VkFormat m_vkFmt;
    VulkanTextureS(VulkanContext* ctx, size_t width, size_t height, size_t mips,
                   TextureFormat fmt, const void* data, size_t sz)
    : m_ctx(ctx), m_fmt(fmt), m_sz(sz), m_width(width), m_height(height), m_mips(mips)
    {
        VkFormat pfmt;
        int pxPitchNum = 1;
        int pxPitchDenom = 1;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            pxPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            break;
        case TextureFormat::DXT1:
            pfmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            pxPitchNum = 1;
            pxPitchDenom = 2;
            break;
        default:
            Log.report(LogVisor::FatalError, "unsupported tex format");
        }
        m_vkFmt = pfmt;

        /* create cpu image */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = mips;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_cpuTex));

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_cpuTex, &memReqs);

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = memReqs.size;
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_cpuTex, m_cpuMem, 0));

        /* map memory */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(ctx->m_dev, m_cpuMem, 0, memReqs.size, 0, reinterpret_cast<void**>(&mappedData)));

        /* copy pitch-linear data */
        const uint8_t* srcDataIt = static_cast<const uint8_t*>(data);
        VkImageSubresource subres = {};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.arrayLayer = 0;
        for (size_t i=0 ; i<mips ; ++i)
        {
            subres.mipLevel = i;
            VkSubresourceLayout layout;
            vkGetImageSubresourceLayout(ctx->m_dev, m_cpuTex, &subres, &layout);
            uint8_t* dstDataIt = static_cast<uint8_t*>(mappedData) + layout.offset;

            size_t srcRowPitch = width * pxPitchNum / pxPitchDenom;

            for (size_t y=0 ; y<height ; ++i)
            {
                memcpy(dstDataIt, srcDataIt, srcRowPitch);
                srcDataIt += srcRowPitch;
                dstDataIt += layout.rowPitch;
            }

            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_cpuMem;
        mappedRange.offset = 0;
        mappedRange.size = memReqs.size;
        ThrowIfFailed(vkFlushMappedMemoryRanges(ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(ctx->m_dev, m_cpuMem);

        /* create gpu image */
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        /* create image view */
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = m_gpuTex;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pfmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mips;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageView = m_gpuView;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
public:
    VkImage m_cpuTex;
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex;
    VkImageView m_gpuView;
    VkDescriptorImageInfo m_descInfo;
    VkDeviceSize m_gpuOffset;
    ~VulkanTextureS()
    {
        vkDestroyImageView(m_ctx->m_dev, m_gpuView, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_gpuTex, nullptr);
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
    }

    void deleteUploadObjects()
    {
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        m_cpuTex = VK_NULL_HANDLE;
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
        m_cpuMem = VK_NULL_HANDLE;
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        m_gpuOffset = offset;
        offset += memReqs.size;

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex, mem, m_gpuOffset));

        /* Since we're going to blit from the mappable image, set its layout to
         * SOURCE_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_cpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_PREINITIALIZED,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        /* Since we're going to blit to the texture image, set its layout to
         * DESTINATION_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegions[16];
        size_t width = m_width;
        size_t height = m_height;
        size_t regionCount = std::min(size_t(16), m_mips);
        for (int i=0 ; i<regionCount ; ++i)
        {
            copyRegions[i].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegions[i].srcSubresource.mipLevel = i;
            copyRegions[i].srcSubresource.baseArrayLayer = 0;
            copyRegions[i].srcSubresource.layerCount = 1;
            copyRegions[i].srcOffset.x = 0;
            copyRegions[i].srcOffset.y = 0;
            copyRegions[i].srcOffset.z = 0;
            copyRegions[i].dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegions[i].dstSubresource.mipLevel = i;
            copyRegions[i].dstSubresource.baseArrayLayer = 0;
            copyRegions[i].dstSubresource.layerCount = 1;
            copyRegions[i].dstOffset.x = 0;
            copyRegions[i].dstOffset.y = 0;
            copyRegions[i].dstOffset.z = 0;
            copyRegions[i].extent.width = width;
            copyRegions[i].extent.height = height;
            copyRegions[i].extent.depth = 1;

            if (width > 1)
                width /= 2;
            if (height > 1)
                height /= 2;
        }

        /* Put the copy command into the command buffer */
        vkCmdCopyImage(ctx->m_loadCmdBuf, m_cpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_gpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regionCount, copyRegions);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TextureFormat format() const {return m_fmt;}
};

class VulkanTextureSA : public ITextureSA
{
    friend class VulkanDataFactory;
    VulkanContext* m_ctx;
    TextureFormat m_fmt;
    size_t m_sz;
    size_t m_width, m_height, m_layers;
    VkFormat m_vkFmt;
    VulkanTextureSA(VulkanContext* ctx, size_t width, size_t height, size_t layers,
                   TextureFormat fmt, const void* data, size_t sz)
    : m_ctx(ctx), m_fmt(fmt), m_width(width), m_height(height), m_layers(layers), m_sz(sz)
    {
        VkFormat pfmt;
        int pxPitchNum = 1;
        int pxPitchDenom = 1;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            pxPitchNum = 4;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            break;
        default:
            Log.report(LogVisor::FatalError, "unsupported tex format");
        }
        m_vkFmt = pfmt;

        /* create cpu image */
        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = 1;
        texCreateInfo.arrayLayers = layers;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_cpuTex));

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_cpuTex, &memReqs);

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = memReqs.size;
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_cpuTex, m_cpuMem, 0));

        /* map memory */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(ctx->m_dev, m_cpuMem, 0, memReqs.size, 0, reinterpret_cast<void**>(&mappedData)));

        /* copy pitch-linear data */
        const uint8_t* srcDataIt = static_cast<const uint8_t*>(data);
        VkImageSubresource subres = {};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.mipLevel = 0;
        for (size_t i=0 ; i<layers ; ++i)
        {
            subres.arrayLayer = i;
            VkSubresourceLayout layout;
            vkGetImageSubresourceLayout(ctx->m_dev, m_cpuTex, &subres, &layout);
            uint8_t* dstDataIt = static_cast<uint8_t*>(mappedData) + layout.offset;

            size_t srcRowPitch = width * pxPitchNum / pxPitchDenom;

            for (size_t y=0 ; y<height ; ++i)
            {
                memcpy(dstDataIt, srcDataIt, srcRowPitch);
                srcDataIt += srcRowPitch;
                dstDataIt += layout.rowPitch;
            }
        }

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_cpuMem;
        mappedRange.offset = 0;
        mappedRange.size = memReqs.size;
        ThrowIfFailed(vkFlushMappedMemoryRanges(ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(ctx->m_dev, m_cpuMem);

        /* create gpu image */
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        /* create image view */
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = m_gpuTex;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pfmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = layers;

        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageView = m_gpuView;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
public:
    VkImage m_cpuTex;
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex;
    VkImageView m_gpuView;
    VkDescriptorImageInfo m_descInfo;
    VkDeviceSize m_gpuOffset;
    ~VulkanTextureSA()
    {
        vkDestroyImageView(m_ctx->m_dev, m_gpuView, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        vkDestroyImage(m_ctx->m_dev, m_gpuTex, nullptr);
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
    }

    void deleteUploadObjects()
    {
        vkDestroyImage(m_ctx->m_dev, m_cpuTex, nullptr);
        m_cpuTex = VK_NULL_HANDLE;
        vkFreeMemory(m_ctx->m_dev, m_cpuMem, nullptr);
        m_cpuMem = VK_NULL_HANDLE;
    }

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memTypeBits &= memReqs.memoryTypeBits;
        offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

        m_gpuOffset = offset;
        offset += memReqs.size;

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex, mem, m_gpuOffset));

        /* Since we're going to blit from the mappable image, set its layout to
         * SOURCE_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_cpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_PREINITIALIZED,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        /* Since we're going to blit to the texture image, set its layout to
         * DESTINATION_OPTIMAL */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion;
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.layerCount = m_layers;
        copyRegion.srcOffset.x = 0;
        copyRegion.srcOffset.y = 0;
        copyRegion.srcOffset.z = 0;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.mipLevel = 0;
        copyRegion.dstSubresource.baseArrayLayer = 0;
        copyRegion.dstSubresource.layerCount = m_layers;
        copyRegion.dstOffset.x = 0;
        copyRegion.dstOffset.y = 0;
        copyRegion.dstOffset.z = 0;
        copyRegion.extent.width = m_width;
        copyRegion.extent.height = m_height;
        copyRegion.extent.depth = 1;

        /* Put the copy command into the command buffer */
        vkCmdCopyImage(ctx->m_loadCmdBuf, m_cpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_gpuTex,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(ctx->m_loadCmdBuf, m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TextureFormat format() const {return m_fmt;}
    size_t layers() const {return m_layers;}
};

class VulkanTextureD : public ITextureD
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    size_t m_width;
    size_t m_height;
    TextureFormat m_fmt;
    VulkanCommandQueue* m_q;
    std::unique_ptr<uint8_t[]> m_cpuBuf;
    size_t m_cpuSz;
    VkDeviceSize m_srcRowPitch;
    VkDeviceSize m_cpuOffsets[2];
    int m_validSlots = 0;
    VulkanTextureD(VulkanCommandQueue* q, VulkanContext* ctx, size_t width, size_t height, TextureFormat fmt)
    : m_width(width), m_height(height), m_fmt(fmt), m_q(q)
    {
        VkFormat pfmt;
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            pfmt = VK_FORMAT_R8G8B8A8_UNORM;
            m_srcRowPitch = width * 4;
            m_cpuSz = m_srcRowPitch * height;
            break;
        case TextureFormat::I8:
            pfmt = VK_FORMAT_R8_UNORM;
            m_srcRowPitch = width;
            m_cpuSz = m_srcRowPitch * height;
            break;
        default:
            Log.report(LogVisor::FatalError, "unsupported tex format");
        }
        m_cpuBuf.reset(new uint8_t[m_cpuSz]);

        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = pfmt;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = 1;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;

        /* create images and compute size for host-mappable images */
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = 0;
        uint32_t memTypeBits = ~0;
        for (int i=0 ; i<2 ; ++i)
        {
            m_cpuOffsets[i] = memAlloc.allocationSize;

            /* create cpu image */
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_cpuTex[i]));
            m_cpuTexLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;

            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(ctx->m_dev, m_cpuTex[i], &memReqs);
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

        }
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_cpuMem));

        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = nullptr;
        viewInfo.image = nullptr;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pfmt;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        for (int i=0 ; i<2 ; ++i)
        {
            /* bind cpu memory */
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_cpuTex[i], m_cpuMem, m_cpuOffsets[i]));

            /* create gpu image */
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex[i]));

            /* create image view */
            viewInfo.image = m_gpuTex[i];
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewInfo, nullptr, &m_gpuView[i]));

            m_descInfo[i].sampler = ctx->m_linearSampler;
            m_descInfo[i].imageView = m_gpuView[i];
            m_descInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    void update(int b);
public:
    VkImageLayout m_cpuTexLayout[2];
    VkImage m_cpuTex[2];
    VkDeviceMemory m_cpuMem;
    VkImage m_gpuTex[2];
    VkImageView m_gpuView[2];
    VkDeviceSize m_gpuOffset[2];
    VkDescriptorImageInfo m_descInfo[2];
    ~VulkanTextureD();

    void load(const void* data, size_t sz);
    void* map(size_t sz);
    void unmap();

    VkDeviceSize sizeForGPU(VulkanContext* ctx, uint32_t& memTypeBits, VkDeviceSize offset)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex[i], &memReqs);
            memTypeBits &= memReqs.memoryTypeBits;
            offset = (offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1);

            m_gpuOffset[i] = offset;
            offset += memReqs.size;
        }

        return offset;
    }

    void placeForGPU(VulkanContext* ctx, VkDeviceMemory mem)
    {
        for (int i=0 ; i<2 ; ++i)
        {
            /* bind memory */
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex[i], mem, m_gpuOffset[i]));
        }
    }

    TextureFormat format() const {return m_fmt;}
};

class VulkanTextureR : public ITextureR
{
    friend class VulkanDataFactory;
    friend struct VulkanCommandQueue;
    size_t m_width = 0;
    size_t m_height = 0;
    size_t m_samples = 0;

    void Setup(VulkanContext* ctx, size_t width, size_t height, size_t samples)
    {
        /* no-ops on first call */
        doDestroy();

        VkImageCreateInfo texCreateInfo = {};
        texCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        texCreateInfo.pNext = nullptr;
        texCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        texCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        texCreateInfo.extent.width = width;
        texCreateInfo.extent.height = height;
        texCreateInfo.extent.depth = 1;
        texCreateInfo.mipLevels = 1;
        texCreateInfo.arrayLayers = 1;
        texCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        texCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        texCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        texCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        texCreateInfo.queueFamilyIndexCount = 0;
        texCreateInfo.pQueueFamilyIndices = nullptr;
        texCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        texCreateInfo.flags = 0;
        ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuTex));

        m_descInfo.sampler = ctx->m_linearSampler;
        m_descInfo.imageView = m_gpuView;
        m_descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.pNext = nullptr;
        viewCreateInfo.image = m_gpuTex;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;
        ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_gpuView));

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.pNext = nullptr;
        fbCreateInfo.renderPass = ctx->m_pass;
        fbCreateInfo.attachmentCount = 2;
        fbCreateInfo.width = width;
        fbCreateInfo.height = height;
        fbCreateInfo.layers = 1;

        /* tally total memory requirements */
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.pNext = nullptr;
        memAlloc.memoryTypeIndex = 0;
        memAlloc.allocationSize = 0;
        uint32_t memTypeBits = ~0;

        vkGetImageMemoryRequirements(ctx->m_dev, m_gpuTex, &memReqs);
        memAlloc.allocationSize += memReqs.size;
        memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
        memTypeBits &= memReqs.memoryTypeBits;

        VkDeviceSize gpuOffsets[2];

        if (samples > 1)
        {
            texCreateInfo.samples = VkSampleCountFlagBits(samples);
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuMsaaTex));
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_gpuMsaaView));
            gpuOffsets[0] = memAlloc.allocationSize;

            vkGetImageMemoryRequirements(ctx->m_dev, m_gpuMsaaTex, &memReqs);
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            texCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            viewCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuDepthTex));
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_gpuDepthView));
            gpuOffsets[1] = memAlloc.allocationSize;

            vkGetImageMemoryRequirements(ctx->m_dev, m_gpuDepthTex, &memReqs);
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            VkImageView attachments[2] = {m_gpuMsaaView, m_gpuDepthView};
            fbCreateInfo.pAttachments = attachments;
            ThrowIfFailed(vkCreateFramebuffer(ctx->m_dev, &fbCreateInfo, nullptr, &m_framebuffer));
        }
        else
        {
            texCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            viewCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
            ThrowIfFailed(vkCreateImage(ctx->m_dev, &texCreateInfo, nullptr, &m_gpuDepthTex));
            ThrowIfFailed(vkCreateImageView(ctx->m_dev, &viewCreateInfo, nullptr, &m_gpuDepthView));
            gpuOffsets[0] = memAlloc.allocationSize;

            vkGetImageMemoryRequirements(ctx->m_dev, m_gpuDepthTex, &memReqs);
            memAlloc.allocationSize += memReqs.size;
            memAlloc.allocationSize = (memAlloc.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
            memTypeBits &= memReqs.memoryTypeBits;

            VkImageView attachments[2] = {m_gpuView, m_gpuDepthView};
            fbCreateInfo.pAttachments = attachments;
            ThrowIfFailed(vkCreateFramebuffer(ctx->m_dev, &fbCreateInfo, nullptr, &m_framebuffer));
        }
        ThrowIfFalse(MemoryTypeFromProperties(ctx, memTypeBits, 0, &memAlloc.memoryTypeIndex));

        /* allocate memory */
        ThrowIfFailed(vkAllocateMemory(ctx->m_dev, &memAlloc, nullptr, &m_gpuMem));

        /* bind memory */
        ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuTex, m_gpuMem, 0));
        if (samples > 1)
        {
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuMsaaTex, m_gpuMem, gpuOffsets[0]));
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuDepthTex, m_gpuMem, gpuOffsets[1]));
        }
        else
            ThrowIfFailed(vkBindImageMemory(ctx->m_dev, m_gpuDepthTex, m_gpuMem, gpuOffsets[0]));

        m_passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        m_passBeginInfo.pNext = nullptr;
        m_passBeginInfo.renderPass = ctx->m_pass;
        m_passBeginInfo.framebuffer = m_framebuffer;
        m_passBeginInfo.renderArea.offset.x = 0;
        m_passBeginInfo.renderArea.offset.y = 0;
        m_passBeginInfo.renderArea.extent.width = width;
        m_passBeginInfo.renderArea.extent.height = height;
        m_passBeginInfo.clearValueCount = 0;
        m_passBeginInfo.pClearValues = nullptr;
    }

    VulkanCommandQueue* m_q;
    VulkanTextureR(VulkanContext* ctx, VulkanCommandQueue* q, size_t width, size_t height, size_t samples)
    : m_q(q), m_width(width), m_height(height), m_samples(samples)
    {
        if (samples == 0) m_samples = 1;
        Setup(ctx, width, height, samples);
    }
public:
    size_t samples() const {return m_samples;}
    VkDeviceMemory m_gpuMem = VK_NULL_HANDLE;
    VkImage m_gpuTex = VK_NULL_HANDLE;
    VkImageView m_gpuView = VK_NULL_HANDLE;
    VkDescriptorImageInfo m_descInfo;
    VkImage m_gpuMsaaTex = VK_NULL_HANDLE;
    VkImageView m_gpuMsaaView = VK_NULL_HANDLE;
    VkImage m_gpuDepthTex = VK_NULL_HANDLE;
    VkImageView m_gpuDepthView = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPassBeginInfo m_passBeginInfo;
    void doDestroy();
    ~VulkanTextureR();

    void resize(VulkanContext* ctx, size_t width, size_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        m_width = width;
        m_height = height;
        Setup(ctx, width, height, m_samples);
    }

    VkImage getRenderColorRes() const
    {if (m_samples > 1) return m_gpuMsaaTex; return m_gpuTex;}
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

static const VkFormat SEMANTIC_TYPE_TABLE[] =
{
    VK_FORMAT_UNDEFINED,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R32G32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT
};

struct VulkanVertexFormat : IVertexFormat
{
    VkVertexInputBindingDescription m_bindings[2];
    std::unique_ptr<VkVertexInputAttributeDescription[]> m_attributes;
    VkPipelineVertexInputStateCreateInfo m_info;
    VulkanVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
    : m_attributes(new VkVertexInputAttributeDescription[elementCount])
    {
        m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        m_info.pNext = nullptr;
        m_info.flags = 0;
        m_info.vertexBindingDescriptionCount = 1;
        m_info.pVertexBindingDescriptions = m_bindings;
        m_info.vertexAttributeDescriptionCount = elementCount;
        m_info.pVertexAttributeDescriptions = m_attributes.get();

        size_t offset = 0;
        size_t instOffset = 0;
        for (size_t i=0 ; i<elementCount ; ++i)
        {
            const VertexElementDescriptor* elemin = &elements[i];
            VkVertexInputAttributeDescription& attribute = m_attributes[i];
            int semantic = int(elemin->semantic & boo::VertexSemantic::SemanticMask);
            attribute.location = i;
            attribute.format = SEMANTIC_TYPE_TABLE[semantic];
            if ((elemin->semantic & boo::VertexSemantic::Instanced) != boo::VertexSemantic::None)
            {
                attribute.binding = 1;
                attribute.offset = instOffset;
                instOffset += SEMANTIC_SIZE_TABLE[semantic];
            }
            else
            {
                attribute.binding = 0;
                attribute.offset = offset;
                offset += SEMANTIC_SIZE_TABLE[semantic];
            }
        }

        m_bindings[0].binding = 1;
        m_bindings[0].stride = offset;
        m_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        if (instOffset)
        {
            m_info.vertexBindingDescriptionCount = 2;
            m_bindings[1].binding = 1;
            m_bindings[1].stride = instOffset;
            m_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        }
    }
};

static const VkBlendFactor BLEND_FACTOR_TABLE[] =
{
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VK_BLEND_FACTOR_DST_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_FACTOR_DST_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    VK_BLEND_FACTOR_SRC1_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR
};

class VulkanShaderPipeline : public IShaderPipeline
{
    friend class VulkanDataFactory;
    VulkanContext* m_ctx;
    VulkanShaderPipeline(VulkanContext* ctx,
                         VkShaderModule vert,
                         VkShaderModule frag,
                         VkPipelineCache pipelineCache,
                         const VulkanVertexFormat* vtxFmt,
                         BlendFactor srcFac, BlendFactor dstFac,
                         bool depthTest, bool depthWrite, bool backfaceCulling)
    : m_ctx(ctx)
    {
        VkDynamicState dynamicStateEnables[VK_DYNAMIC_STATE_RANGE_SIZE];
        memset(dynamicStateEnables, 0, sizeof(dynamicStateEnables));
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.pNext = nullptr;
        dynamicState.pDynamicStates = dynamicStateEnables;
        dynamicState.dynamicStateCount = 0;

        VkPipelineShaderStageCreateInfo stages[2] = {};

        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].pNext = nullptr;
        stages[0].flags = 0;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[0].pSpecializationInfo = nullptr;

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].pNext = nullptr;
        stages[1].flags = 0;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";
        stages[1].pSpecializationInfo = nullptr;

        VkPipelineInputAssemblyStateCreateInfo assemblyInfo = {};
        assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assemblyInfo.pNext = nullptr;
        assemblyInfo.flags = 0;
        assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        assemblyInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportInfo = {};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.pNext = nullptr;
        viewportInfo.flags = 0;
        viewportInfo.viewportCount = 1;
        viewportInfo.pViewports = nullptr;
        viewportInfo.scissorCount = 1;
        viewportInfo.pScissors = nullptr;
        dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
        dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;

        VkPipelineRasterizationStateCreateInfo rasterizationInfo = {};
        rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.pNext = nullptr;
        rasterizationInfo.flags = 0;
        rasterizationInfo.depthClampEnable = VK_TRUE;
        rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationInfo.cullMode = backfaceCulling ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationInfo.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.pNext = nullptr;
        multisampleInfo.flags = 0;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.pNext = nullptr;
        depthStencilInfo.flags = 0;
        depthStencilInfo.depthTestEnable = depthTest;
        depthStencilInfo.depthWriteEnable = depthWrite;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState colorAttachment = {};
        colorAttachment.blendEnable = dstFac != BlendFactor::Zero;
        colorAttachment.srcColorBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
        colorAttachment.dstColorBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
        colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.srcAlphaBlendFactor = BLEND_FACTOR_TABLE[int(srcFac)];
        colorAttachment.dstAlphaBlendFactor = BLEND_FACTOR_TABLE[int(dstFac)];
        colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.colorWriteMask = 0xf;

        VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.pNext = nullptr;
        colorBlendInfo.flags = 0;
        colorBlendInfo.logicOpEnable = VK_FALSE;
        colorBlendInfo.logicOp = VK_LOGIC_OP_NO_OP;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &colorAttachment;
        colorBlendInfo.blendConstants[0] = 1.f;
        colorBlendInfo.blendConstants[1] = 1.f;
        colorBlendInfo.blendConstants[2] = 1.f;
        colorBlendInfo.blendConstants[3] = 1.f;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.pNext = nullptr;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = stages;
        pipelineCreateInfo.pVertexInputState = &vtxFmt->m_info;
        pipelineCreateInfo.pInputAssemblyState = &assemblyInfo;
        pipelineCreateInfo.pViewportState = &viewportInfo;
        pipelineCreateInfo.pRasterizationState = &rasterizationInfo;
        pipelineCreateInfo.pMultisampleState = &multisampleInfo;
        pipelineCreateInfo.pDepthStencilState = &depthStencilInfo;
        pipelineCreateInfo.pColorBlendState = &colorBlendInfo;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = ctx->m_layout;
        pipelineCreateInfo.renderPass = ctx->m_pass;

        ThrowIfFailed(vkCreateGraphicsPipelines(ctx->m_dev, pipelineCache, 1, &pipelineCreateInfo,
                                                nullptr, &m_pipeline));
    }
public:
    VkPipeline m_pipeline;
    ~VulkanShaderPipeline()
    {
        vkDestroyPipeline(m_ctx->m_dev, m_pipeline, nullptr);
    }
    VulkanShaderPipeline& operator=(const VulkanShaderPipeline&) = delete;
    VulkanShaderPipeline(const VulkanShaderPipeline&) = delete;
};

static VkDeviceSize SizeBufferForGPU(IGraphicsBuffer* buf, VulkanContext* ctx,
                                     uint32_t& memTypeBits, VkDeviceSize offset)
{
    if (buf->dynamic())
        return static_cast<VulkanGraphicsBufferD*>(buf)->sizeForGPU(ctx, memTypeBits, offset);
    else
        return static_cast<VulkanGraphicsBufferS*>(buf)->sizeForGPU(ctx, memTypeBits, offset);
}

static VkDeviceSize SizeTextureForGPU(ITexture* tex, VulkanContext* ctx,
                                      uint32_t& memTypeBits, VkDeviceSize offset)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
        return static_cast<VulkanTextureD*>(tex)->sizeForGPU(ctx, memTypeBits, offset);
    case TextureType::Static:
        return static_cast<VulkanTextureS*>(tex)->sizeForGPU(ctx, memTypeBits, offset);
    case TextureType::StaticArray:
        return static_cast<VulkanTextureSA*>(tex)->sizeForGPU(ctx, memTypeBits, offset);
    }
    return offset;
}

static void PlaceTextureForGPU(ITexture* tex, VulkanContext* ctx, VkDeviceMemory mem)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
        static_cast<VulkanTextureD*>(tex)->placeForGPU(ctx, mem);
        break;
    case TextureType::Static:
        static_cast<VulkanTextureS*>(tex)->placeForGPU(ctx, mem);
        break;
    case TextureType::StaticArray:
        static_cast<VulkanTextureSA*>(tex)->placeForGPU(ctx, mem);
        break;
    }
}

static const VkDescriptorBufferInfo* GetBufferGPUResource(const IGraphicsBuffer* buf, int idx)
{
    if (buf->dynamic())
    {
        const VulkanGraphicsBufferD* cbuf = static_cast<const VulkanGraphicsBufferD*>(buf);
        return &cbuf->m_bufferInfo[idx];
    }
    else
    {
        const VulkanGraphicsBufferS* cbuf = static_cast<const VulkanGraphicsBufferS*>(buf);
        return &cbuf->m_bufferInfo;
    }
}

static const VkDescriptorImageInfo* GetTextureGPUResource(const ITexture* tex, int idx)
{
    switch (tex->type())
    {
    case TextureType::Dynamic:
    {
        const VulkanTextureD* ctex = static_cast<const VulkanTextureD*>(tex);
        return &ctex->m_descInfo[idx];
    }
    case TextureType::Static:
    {
        const VulkanTextureS* ctex = static_cast<const VulkanTextureS*>(tex);
        return &ctex->m_descInfo;
    }
    case TextureType::StaticArray:
    {
        const VulkanTextureSA* ctex = static_cast<const VulkanTextureSA*>(tex);
        return &ctex->m_descInfo;
    }
    case TextureType::Render:
    {
        const VulkanTextureR* ctex = static_cast<const VulkanTextureR*>(tex);
        return &ctex->m_descInfo;
    }
    default: break;
    }
    return nullptr;
}

struct VulkanShaderDataBinding : IShaderDataBinding
{
    VulkanContext* m_ctx;
    VulkanShaderPipeline* m_pipeline;
    IGraphicsBuffer* m_vbuf;
    IGraphicsBuffer* m_instVbuf;
    IGraphicsBuffer* m_ibuf;
    size_t m_ubufCount;
    std::unique_ptr<IGraphicsBuffer*[]> m_ubufs;
    size_t m_texCount;
    std::unique_ptr<ITexture*[]> m_texs;

    VkBuffer m_vboBufs[2][2] = {{},{}};
    VkDeviceSize m_vboOffs[2][2] = {{},{}};
    VkBuffer m_iboBufs[2] = {};
    VkDeviceSize m_iboOffs[2] = {};

    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descSets[2];

#ifndef NDEBUG
    /* Debugging aids */
    bool m_committed = false;
#endif

    VulkanShaderDataBinding(VulkanContext* ctx,
                            IShaderPipeline* pipeline,
                            IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
                            size_t ubufCount, IGraphicsBuffer** ubufs,
                            size_t texCount, ITexture** texs)
    : m_ctx(ctx),
      m_pipeline(static_cast<VulkanShaderPipeline*>(pipeline)),
      m_vbuf(vbuf),
      m_instVbuf(instVbuf),
      m_ibuf(ibuf),
      m_ubufCount(ubufCount),
      m_ubufs(new IGraphicsBuffer*[ubufCount]),
      m_texCount(texCount),
      m_texs(new ITexture*[texCount])
    {
        for (size_t i=0 ; i<ubufCount ; ++i)
            m_ubufs[i] = ubufs[i];
        for (size_t i=0 ; i<texCount ; ++i)
            m_texs[i] = texs[i];

        size_t totalDescs = ubufCount + texCount;
        if (totalDescs > 0)
        {
            VkDescriptorPoolSize poolSizes[2] = {};
            VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
            descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolInfo.pNext = nullptr;
            descriptorPoolInfo.maxSets = 2;
            descriptorPoolInfo.poolSizeCount = 0;
            descriptorPoolInfo.pPoolSizes = poolSizes;

            if (ubufCount)
            {
                poolSizes[descriptorPoolInfo.poolSizeCount].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                poolSizes[descriptorPoolInfo.poolSizeCount].descriptorCount = ubufCount;
                ++descriptorPoolInfo.poolSizeCount;
            }

            if (texCount)
            {
                poolSizes[descriptorPoolInfo.poolSizeCount].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                poolSizes[descriptorPoolInfo.poolSizeCount].descriptorCount = texCount;
                ++descriptorPoolInfo.poolSizeCount;
            }
            ThrowIfFailed(vkCreateDescriptorPool(ctx->m_dev, &descriptorPoolInfo, nullptr, &m_descPool));

            VkDescriptorSetLayout layouts[] = {ctx->m_descSetLayout, ctx->m_descSetLayout};
            VkDescriptorSetAllocateInfo descAllocInfo;
            descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descAllocInfo.pNext = nullptr;
            descAllocInfo.descriptorPool = m_descPool;
            descAllocInfo.descriptorSetCount = 2;
            descAllocInfo.pSetLayouts = layouts;
            ThrowIfFailed(vkAllocateDescriptorSets(ctx->m_dev, &descAllocInfo, m_descSets));
        }
    }

    ~VulkanShaderDataBinding()
    {
        vkDestroyDescriptorPool(m_ctx->m_dev, m_descPool, nullptr);
    }

    void commit(VulkanContext* ctx)
    {
        VkWriteDescriptorSet writes[(MAX_UNIFORM_COUNT + MAX_TEXTURE_COUNT) * 2] = {};
        size_t totalWrites = 0;
        for (int b=0 ; b<2 ; ++b)
        {
            if (m_vbuf)
            {
                const VkDescriptorBufferInfo* vbufInfo = GetBufferGPUResource(m_vbuf, b);
                m_vboBufs[b][0] = vbufInfo->buffer;
                m_vboOffs[b][0] = vbufInfo->offset;
            }
            if (m_instVbuf)
            {
                const VkDescriptorBufferInfo* vbufInfo = GetBufferGPUResource(m_instVbuf, b);
                m_vboBufs[b][1] = vbufInfo->buffer;
                m_vboOffs[b][1] = vbufInfo->offset;
            }
            if (m_ibuf)
            {
                const VkDescriptorBufferInfo* ibufInfo = GetBufferGPUResource(m_ibuf, b);
                m_iboBufs[b] = ibufInfo->buffer;
                m_iboOffs[b] = ibufInfo->offset;
            }

            size_t binding = 0;
            for (size_t i=0 ; i<MAX_UNIFORM_COUNT ; ++i)
            {
                if (i<m_ubufCount)
                {
                    writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[totalWrites].pNext = nullptr;
                    writes[totalWrites].dstSet = m_descSets[b];
                    writes[totalWrites].descriptorCount = 1;
                    writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    writes[totalWrites].pBufferInfo = GetBufferGPUResource(m_ubufs[i], b);
                    writes[totalWrites].dstArrayElement = 0;
                    writes[totalWrites].dstBinding = binding;
                    ++totalWrites;
                }
                ++binding;
            }
            for (size_t i=0 ; i<MAX_TEXTURE_COUNT ; ++i)
            {
                if (i<m_texCount)
                {
                    writes[totalWrites].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[totalWrites].pNext = nullptr;
                    writes[totalWrites].dstSet = m_descSets[b];
                    writes[totalWrites].descriptorCount = 1;
                    writes[totalWrites].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[totalWrites].pImageInfo = GetTextureGPUResource(m_texs[i], b);
                    writes[totalWrites].dstArrayElement = 0;
                    writes[totalWrites].dstBinding = binding;
                    ++totalWrites;
                }
                ++binding;
            }
        }
        if (totalWrites)
            vkUpdateDescriptorSets(ctx->m_dev, totalWrites, writes, 0, nullptr);

#ifndef NDEBUG
        m_committed = true;
#endif
    }

    void bind(VkCommandBuffer cmdBuf, int b)
    {
#ifndef NDEBUG
        if (!m_committed)
            Log.report(LogVisor::FatalError,
                       "attempted to use uncommitted VulkanShaderDataBinding");
#endif

        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->m_pipeline);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ctx->m_layout, 0, 1, &m_descSets[b], 0, nullptr);
        vkCmdBindVertexBuffers(cmdBuf, 0, 2, m_vboBufs[b], m_vboOffs[b]);
        if (m_ibuf)
            vkCmdBindIndexBuffer(cmdBuf, m_iboBufs[b], m_iboOffs[b], VK_INDEX_TYPE_UINT32);
    }
};

struct VulkanCommandQueue : IGraphicsCommandQueue
{
    Platform platform() const {return IGraphicsDataFactory::Platform::Vulkan;}
    const SystemChar* platformName() const {return _S("Vulkan");}
    VulkanContext* m_ctx;
    VulkanContext::Window* m_windowCtx;
    IGraphicsContext* m_parent;

    VkCommandPool m_cmdPool;
    VkCommandBuffer m_cmdBufs[2];
    VkSemaphore m_swapChainReadySem;
    VkSemaphore m_drawCompleteSem;
    VkFence m_drawCompleteFence;

    VkCommandPool m_dynamicCmdPool;
    VkCommandBuffer m_dynamicCmdBufs[2];
    VkFence m_dynamicBufFence;

    bool m_running = true;
    bool m_dynamicNeedsReset = false;

    size_t m_fillBuf = 0;
    size_t m_drawBuf = 0;

    void resetCommandBuffer()
    {
        ThrowIfFailed(vkResetCommandBuffer(m_cmdBufs[m_fillBuf], 0));
    }

    void resetDynamicCommandBuffer()
    {
        ThrowIfFailed(vkResetCommandBuffer(m_dynamicCmdBufs[m_fillBuf], 0));
        m_dynamicNeedsReset = false;
    }

    void stallDynamicUpload()
    {
        if (m_dynamicNeedsReset)
        {
            ThrowIfFailed(vkWaitForFences(m_ctx->m_dev, 1, &m_dynamicBufFence, VK_FALSE, -1));
            resetDynamicCommandBuffer();
        }
    }

    VulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx, IGraphicsContext* parent)
    : m_ctx(ctx), m_windowCtx(windowCtx), m_parent(parent)
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ThrowIfFailed(vkCreateCommandPool(ctx->m_dev, &poolInfo, nullptr, &m_cmdPool));
        ThrowIfFailed(vkCreateCommandPool(ctx->m_dev, &poolInfo, nullptr, &m_dynamicCmdPool));

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 2;
        ThrowIfFailed(vkAllocateCommandBuffers(m_ctx->m_dev, &allocInfo, m_cmdBufs));

        VkCommandBufferAllocateInfo dynAllocInfo =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            nullptr,
            m_dynamicCmdPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            2
        };
        ThrowIfFailed(vkAllocateCommandBuffers(m_ctx->m_dev, &dynAllocInfo, m_dynamicCmdBufs));

        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ThrowIfFailed(vkCreateSemaphore(ctx->m_dev, &semInfo, nullptr, &m_swapChainReadySem));
        ThrowIfFailed(vkCreateSemaphore(ctx->m_dev, &semInfo, nullptr, &m_drawCompleteSem));

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        ThrowIfFailed(vkCreateFence(m_ctx->m_dev, &fenceInfo, nullptr, &m_drawCompleteFence));
        ThrowIfFailed(vkCreateFence(m_ctx->m_dev, &fenceInfo, nullptr, &m_dynamicBufFence));
    }

    void stopRenderer()
    {
        m_running = false;
        vkWaitForFences(m_ctx->m_dev, 1, &m_drawCompleteFence, VK_FALSE, -1);
    }

    ~VulkanCommandQueue()
    {
        if (m_running)
            stopRenderer();

        vkDestroyFence(m_ctx->m_dev, m_dynamicBufFence, nullptr);
        vkDestroyFence(m_ctx->m_dev, m_drawCompleteFence, nullptr);
        vkDestroySemaphore(m_ctx->m_dev, m_drawCompleteSem, nullptr);
        vkDestroySemaphore(m_ctx->m_dev, m_swapChainReadySem, nullptr);
        vkDestroyCommandPool(m_ctx->m_dev, m_dynamicCmdPool, nullptr);
        vkDestroyCommandPool(m_ctx->m_dev, m_cmdPool, nullptr);
    }

    void setShaderDataBinding(IShaderDataBinding* binding)
    {
        VulkanShaderDataBinding* cbind = static_cast<VulkanShaderDataBinding*>(binding);
        cbind->bind(m_cmdBufs[m_fillBuf], m_fillBuf);
    }

    VulkanTextureR* m_boundTarget = nullptr;
    void setRenderTarget(ITextureR* target)
    {
        VulkanTextureR* ctarget = static_cast<VulkanTextureR*>(target);

        if (m_boundTarget)
        {
            VkImageMemoryBarrier toShaderResBarrier = {};
            toShaderResBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toShaderResBarrier.pNext = nullptr;
            toShaderResBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toShaderResBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShaderResBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toShaderResBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShaderResBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShaderResBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShaderResBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            toShaderResBarrier.subresourceRange.baseMipLevel = 0;
            toShaderResBarrier.subresourceRange.levelCount = 1;
            toShaderResBarrier.subresourceRange.baseArrayLayer = 0;
            toShaderResBarrier.subresourceRange.layerCount = 1;
            toShaderResBarrier.image = m_boundTarget->getRenderColorRes();
            vkCmdPipelineBarrier(m_cmdBufs[m_fillBuf], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &toShaderResBarrier);
        }

        VkImageMemoryBarrier toRenderTargetBarrier = {};
        toRenderTargetBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRenderTargetBarrier.pNext = nullptr;
        toRenderTargetBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRenderTargetBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toRenderTargetBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRenderTargetBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toRenderTargetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRenderTargetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRenderTargetBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toRenderTargetBarrier.subresourceRange.baseMipLevel = 0;
        toRenderTargetBarrier.subresourceRange.levelCount = 1;
        toRenderTargetBarrier.subresourceRange.baseArrayLayer = 0;
        toRenderTargetBarrier.subresourceRange.layerCount = 1;
        toRenderTargetBarrier.image = ctarget->getRenderColorRes();
        vkCmdPipelineBarrier(m_cmdBufs[m_fillBuf], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &toRenderTargetBarrier);

        vkCmdBeginRenderPass(m_cmdBufs[m_fillBuf], &ctarget->m_passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        m_boundTarget = ctarget;
    }

    void setViewport(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            VkViewport vp = {float(rect.location[0]), float(m_boundTarget->m_height - rect.location[1]),
                             float(rect.size[0]), float(rect.size[1]), 0.0f, 1.0f};
            vkCmdSetViewport(m_cmdBufs[m_fillBuf], 0, 1, &vp);
        }
    }

    void setScissor(const SWindowRect& rect)
    {
        if (m_boundTarget)
        {
            VkRect2D vkrect =
            {
                {int32_t(rect.location[0]), int32_t(m_boundTarget->m_height) - int32_t(rect.location[1])},
                {uint32_t(rect.size[0]), uint32_t(rect.size[1])}
            };
            vkCmdSetScissor(m_cmdBufs[m_fillBuf], 0, 1, &vkrect);
        }
    }

    std::unordered_map<VulkanTextureR*, std::pair<size_t, size_t>> m_texResizes;
    void resizeRenderTexture(ITextureR* tex, size_t width, size_t height)
    {
        VulkanTextureR* ctex = static_cast<VulkanTextureR*>(tex);
        m_texResizes[ctex] = std::make_pair(width, height);
    }

    void schedulePostFrameHandler(std::function<void(void)>&& func)
    {
        func();
    }

    float m_clearColor[4] = {0.0,0.0,0.0,1.0};
    void setClearColor(const float rgba[4])
    {
        m_clearColor[0] = rgba[0];
        m_clearColor[1] = rgba[1];
        m_clearColor[2] = rgba[2];
        m_clearColor[3] = rgba[3];
    }

    void clearTarget(bool render=true, bool depth=true)
    {
        if (!m_boundTarget)
            return;
        setRenderTarget(m_boundTarget);
    }

    void draw(size_t start, size_t count)
    {
        vkCmdDraw(m_cmdBufs[m_fillBuf], count, 1, start, 0);
    }

    void drawIndexed(size_t start, size_t count)
    {
        vkCmdDrawIndexed(m_cmdBufs[m_fillBuf], count, 1, start, 0, 0);
    }

    void drawInstances(size_t start, size_t count, size_t instCount)
    {
        vkCmdDraw(m_cmdBufs[m_fillBuf], count, instCount, start, 0);
    }

    void drawInstancesIndexed(size_t start, size_t count, size_t instCount)
    {
        vkCmdDrawIndexed(m_cmdBufs[m_fillBuf], count, instCount, start, 0, 0);
    }

    bool m_doPresent = false;
    void resolveDisplay(ITextureR* source)
    {
        VkCommandBuffer cmdBuf = m_cmdBufs[m_fillBuf];
        VulkanTextureR* csource = static_cast<VulkanTextureR*>(source);

        ThrowIfFailed(vkAcquireNextImageKHR(m_ctx->m_dev, m_windowCtx->m_swapChain, UINT64_MAX,
                                            m_swapChainReadySem, nullptr, &m_windowCtx->m_backBuf));
        VulkanContext::Window::Buffer& dest = m_windowCtx->m_bufs[m_windowCtx->m_backBuf];

        if (csource->m_samples > 1)
        {
            SetImageLayout(cmdBuf, csource->m_gpuMsaaTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            SetImageLayout(cmdBuf, dest.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageResolve resolveInfo = {};
            resolveInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resolveInfo.srcSubresource.mipLevel = 0;
            resolveInfo.srcSubresource.baseArrayLayer = 0;
            resolveInfo.srcSubresource.layerCount = 1;
            resolveInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resolveInfo.dstSubresource.mipLevel = 0;
            resolveInfo.dstSubresource.baseArrayLayer = 0;
            resolveInfo.dstSubresource.layerCount = 1;
            resolveInfo.extent.width = csource->m_width;
            resolveInfo.extent.height = csource->m_height;
            resolveInfo.extent.depth = 1;
            vkCmdResolveImage(cmdBuf,
                              csource->m_gpuMsaaTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              1, &resolveInfo);

            SetImageLayout(cmdBuf, csource->m_gpuMsaaTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            SetImageLayout(cmdBuf, dest.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        else
        {
            SetImageLayout(cmdBuf, csource->m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            SetImageLayout(cmdBuf, dest.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyInfo = {};
            copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.srcSubresource.mipLevel = 0;
            copyInfo.srcSubresource.baseArrayLayer = 0;
            copyInfo.srcSubresource.layerCount = 1;
            copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyInfo.dstSubresource.mipLevel = 0;
            copyInfo.dstSubresource.baseArrayLayer = 0;
            copyInfo.dstSubresource.layerCount = 1;
            copyInfo.extent.width = csource->m_width;
            copyInfo.extent.height = csource->m_height;
            copyInfo.extent.depth = 1;
            vkCmdCopyImage(cmdBuf,
                           csource->m_gpuTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyInfo);

            SetImageLayout(cmdBuf, csource->m_gpuTex, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            SetImageLayout(cmdBuf, dest.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        m_doPresent = true;
    }

    void execute();
};

VulkanGraphicsBufferD::~VulkanGraphicsBufferD()
{
    vkDestroyBuffer(m_q->m_ctx->m_dev, m_bufferInfo[0].buffer, nullptr);
    vkDestroyBuffer(m_q->m_ctx->m_dev, m_bufferInfo[1].buffer, nullptr);
}

VulkanTextureD::~VulkanTextureD()
{
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuView[0], nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuView[1], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_cpuTex[0], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_cpuTex[1], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuTex[0], nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuTex[1], nullptr);
    vkFreeMemory(m_q->m_ctx->m_dev, m_cpuMem, nullptr);
}

void VulkanTextureR::doDestroy()
{
    vkDestroyFramebuffer(m_q->m_ctx->m_dev, m_framebuffer, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuView, nullptr);
    m_gpuView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuTex, nullptr);
    m_gpuTex = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuMsaaView, nullptr);
    m_gpuMsaaView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuMsaaTex, nullptr);
    m_gpuMsaaTex = VK_NULL_HANDLE;
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuDepthView, nullptr);
    m_gpuDepthView = VK_NULL_HANDLE;
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuDepthTex, nullptr);
    m_gpuDepthTex = VK_NULL_HANDLE;
    vkFreeMemory(m_q->m_ctx->m_dev, m_gpuMem, nullptr);
    m_gpuMem = VK_NULL_HANDLE;
}

VulkanTextureR::~VulkanTextureR()
{
    vkDestroyFramebuffer(m_q->m_ctx->m_dev, m_framebuffer, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuTex, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuMsaaView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuMsaaTex, nullptr);
    vkDestroyImageView(m_q->m_ctx->m_dev, m_gpuDepthView, nullptr);
    vkDestroyImage(m_q->m_ctx->m_dev, m_gpuDepthTex, nullptr);
    vkFreeMemory(m_q->m_ctx->m_dev, m_gpuMem, nullptr);
    if (m_q->m_boundTarget == this)
        m_q->m_boundTarget = nullptr;
}

void VulkanGraphicsBufferD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        void* ptr;
        ThrowIfFailed(vkMapMemory(m_q->m_ctx->m_dev, m_mem,
                                  m_bufferInfo[slot].offset, m_bufferInfo[slot].range, 0, &ptr));
        memcpy(ptr, m_cpuBuf.get(), m_cpuSz);

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_mem;
        mappedRange.offset = m_bufferInfo[slot].offset;
        mappedRange.size = m_bufferInfo[slot].range;
        ThrowIfFailed(vkFlushMappedMemoryRanges(m_q->m_ctx->m_dev, 1, &mappedRange));

        vkUnmapMemory(m_q->m_ctx->m_dev, m_mem);
        m_validSlots |= slot;
    }
}

void VulkanGraphicsBufferD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* VulkanGraphicsBufferD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void VulkanGraphicsBufferD::unmap()
{
    m_validSlots = 0;
}

void VulkanTextureD::update(int b)
{
    int slot = 1 << b;
    if ((slot & m_validSlots) == 0)
    {
        m_q->stallDynamicUpload();
        VkCommandBuffer cmdBuf = m_q->m_dynamicCmdBufs[b];

        /* initialize texture layouts if needed */
        if (m_cpuTexLayout[b] == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            SetImageLayout(cmdBuf, m_cpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            m_cpuTexLayout[b] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        else
        {
            SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        /* map memory */
        uint8_t* mappedData;
        ThrowIfFailed(vkMapMemory(m_q->m_ctx->m_dev, m_cpuMem, m_cpuOffsets[b], m_cpuSz, 0, reinterpret_cast<void**>(&mappedData)));

        /* copy pitch-linear data */
        const uint8_t* srcDataIt = static_cast<const uint8_t*>(m_cpuBuf.get());
        VkImageSubresource subres = {};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.arrayLayer = 0;
        subres.mipLevel = 0;
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(m_q->m_ctx->m_dev, m_cpuTex[b], &subres, &layout);
        uint8_t* dstDataIt = static_cast<uint8_t*>(mappedData);

        for (size_t y=0 ; y<m_height ; ++y)
        {
            memcpy(dstDataIt, srcDataIt, m_srcRowPitch);
            srcDataIt += m_srcRowPitch;
            dstDataIt += layout.rowPitch;
        }

        /* flush to gpu */
        VkMappedMemoryRange mappedRange;
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.pNext = nullptr;
        mappedRange.memory = m_cpuMem;
        mappedRange.offset = m_cpuOffsets[b];
        mappedRange.size = m_cpuSz;
        ThrowIfFailed(vkFlushMappedMemoryRanges(m_q->m_ctx->m_dev, 1, &mappedRange));
        vkUnmapMemory(m_q->m_ctx->m_dev, m_cpuMem);

        /* Put the copy command into the command buffer */
        VkImageCopy copyRegion;
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.srcOffset.x = 0;
        copyRegion.srcOffset.y = 0;
        copyRegion.srcOffset.z = 0;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.mipLevel = 0;
        copyRegion.dstSubresource.baseArrayLayer = 0;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.dstOffset.x = 0;
        copyRegion.dstOffset.y = 0;
        copyRegion.dstOffset.z = 0;
        copyRegion.extent.width = m_width;
        copyRegion.extent.height = m_height;
        copyRegion.extent.depth = 1;
        vkCmdCopyImage(cmdBuf, m_cpuTex[b],
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_gpuTex[b],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        /* Set the layout for the texture image from DESTINATION_OPTIMAL to
         * SHADER_READ_ONLY */
        SetImageLayout(cmdBuf, m_gpuTex[b], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_validSlots |= slot;
    }
}
void VulkanTextureD::load(const void* data, size_t sz)
{
    size_t bufSz = std::min(sz, m_cpuSz);
    memcpy(m_cpuBuf.get(), data, bufSz);
    m_validSlots = 0;
}
void* VulkanTextureD::map(size_t sz)
{
    if (sz > m_cpuSz)
        return nullptr;
    return m_cpuBuf.get();
}
void VulkanTextureD::unmap()
{
    m_validSlots = 0;
}

void VulkanDataFactory::destroyData(IGraphicsData* d)
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    VulkanData* data = static_cast<VulkanData*>(d);
    m_committedData.erase(data);
    delete data;
}

void VulkanDataFactory::destroyAllData()
{
    std::unique_lock<std::mutex> lk(m_committedMutex);
    for (IGraphicsData* data : m_committedData)
        delete static_cast<VulkanData*>(data);
    m_committedData.clear();
}

VulkanDataFactory::VulkanDataFactory(IGraphicsContext* parent, VulkanContext* ctx)
: m_parent(parent), m_ctx(ctx)
{
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = nullptr;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ThrowIfFailed(vkCreateSampler(ctx->m_dev, &samplerInfo, nullptr, &ctx->m_linearSampler));

    VkDescriptorSetLayoutBinding layoutBindings[MAX_UNIFORM_COUNT + MAX_TEXTURE_COUNT];
    for (int i=0 ; i<MAX_UNIFORM_COUNT ; ++i)
    {
        layoutBindings[i].binding = i;
        layoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        layoutBindings[i].descriptorCount = 1;
        layoutBindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        layoutBindings[i].pImmutableSamplers = nullptr;
    }
    for (int i=MAX_UNIFORM_COUNT ; i<MAX_UNIFORM_COUNT+MAX_TEXTURE_COUNT ; ++i)
    {
        layoutBindings[i].binding = i;
        layoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBindings[i].descriptorCount = 1;
        layoutBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        layoutBindings[i].pImmutableSamplers = &ctx->m_linearSampler;
    }

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = MAX_UNIFORM_COUNT + MAX_TEXTURE_COUNT;
    descriptorLayout.pBindings = layoutBindings;

    ThrowIfFailed(vkCreateDescriptorSetLayout(ctx->m_dev, &descriptorLayout, nullptr,
                                              &ctx->m_descSetLayout));
}

IShaderPipeline* VulkanDataFactory::newShaderPipeline
(const char* vertSource, const char* fragSource,
 std::vector<unsigned int>& vertBlobOut, std::vector<unsigned int>& fragBlobOut,
 std::vector<unsigned char>& pipelineBlob, IVertexFormat* vtxFmt,
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

    VkShaderModuleCreateInfo smCreateInfo = {};
    smCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCreateInfo.pNext = nullptr;
    smCreateInfo.flags = 0;

    smCreateInfo.codeSize = vertBlobOut.size() * sizeof(unsigned int);
    smCreateInfo.pCode = vertBlobOut.data();
    VkShaderModule vertModule;
    ThrowIfFailed(vkCreateShaderModule(m_ctx->m_dev, &smCreateInfo, nullptr, &vertModule));

    smCreateInfo.codeSize = fragBlobOut.size() * sizeof(unsigned int);
    smCreateInfo.pCode = fragBlobOut.data();
    VkShaderModule fragModule;
    ThrowIfFailed(vkCreateShaderModule(m_ctx->m_dev, &smCreateInfo, nullptr, &fragModule));

    VkPipelineCacheCreateInfo cacheDataInfo = {};
    cacheDataInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheDataInfo.pNext = nullptr;
    cacheDataInfo.initialDataSize = pipelineBlob.size();
    if (cacheDataInfo.initialDataSize)
        cacheDataInfo.pInitialData = pipelineBlob.data();

    VkPipelineCache pipelineCache;
    ThrowIfFailed(vkCreatePipelineCache(m_ctx->m_dev, &cacheDataInfo, nullptr, &pipelineCache));

    VulkanShaderPipeline* retval = new VulkanShaderPipeline(m_ctx, vertModule, fragModule, pipelineCache,
                                                            static_cast<const VulkanVertexFormat*>(vtxFmt),
                                                            srcFac, dstFac, depthTest, depthWrite, backfaceCulling);

    if (pipelineBlob.empty())
    {
        size_t cacheSz = 0;
        ThrowIfFailed(vkGetPipelineCacheData(m_ctx->m_dev, pipelineCache, &cacheSz, nullptr));
        if (cacheSz)
        {
            pipelineBlob.resize(cacheSz);
            ThrowIfFailed(vkGetPipelineCacheData(m_ctx->m_dev, pipelineCache, &cacheSz, pipelineBlob.data()));
            pipelineBlob.resize(cacheSz);
        }
    }

    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_SPs.emplace_back(retval);
    return retval;
}

IGraphicsBufferS* VulkanDataFactory::newStaticBuffer(BufferUse use, const void* data, size_t stride, size_t count)
{
    VulkanGraphicsBufferS* retval = new VulkanGraphicsBufferS(use, m_ctx, data, stride, count);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_SBufs.emplace_back(retval);
    return retval;
}

IGraphicsBufferD* VulkanDataFactory::newDynamicBuffer(BufferUse use, size_t stride, size_t count)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent->getCommandQueue());
    VulkanGraphicsBufferD* retval = new VulkanGraphicsBufferD(q, use, m_ctx, stride, count);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_DBufs.emplace_back(retval);
    return retval;
}

ITextureS* VulkanDataFactory::newStaticTexture(size_t width, size_t height, size_t mips,
                                               TextureFormat fmt, const void* data, size_t sz)
{
    VulkanTextureS* retval = new VulkanTextureS(m_ctx, width, height, mips, fmt, data, sz);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_STexs.emplace_back(retval);
    return retval;
}

GraphicsDataToken
VulkanDataFactory::newStaticTextureNoContext(size_t width, size_t height, size_t mips, TextureFormat fmt,
                                             const void* data, size_t sz, ITextureS*& texOut)
{
    VulkanTextureS* retval = new VulkanTextureS(m_ctx, width, height, mips, fmt, data, sz);
    VulkanData* tokData = new struct VulkanData(m_ctx);
    tokData->m_STexs.emplace_back(retval);
    texOut = retval;

    uint32_t memTypeBits = ~0;
    VkDeviceSize memSize = SizeTextureForGPU(retval, m_ctx, memTypeBits, 0);

    /* allocate memory */
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = nullptr;
    memAlloc.memoryTypeIndex = 0;
    memAlloc.allocationSize = memSize;
    ThrowIfFalse(MemoryTypeFromProperties(m_ctx, memTypeBits, 0, &memAlloc.memoryTypeIndex));
    ThrowIfFailed(vkAllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &tokData->m_texMem));

    /* Place texture */
    PlaceTextureForGPU(retval, m_ctx, tokData->m_texMem);

    /* Execute static uploads */
    ThrowIfFailed(vkEndCommandBuffer(m_ctx->m_loadCmdBuf));
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_ctx->m_loadCmdBuf;
    ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_ctx->m_loadFence));

    /* Block handle return until data is ready on GPU */
    ThrowIfFailed(vkWaitForFences(m_ctx->m_dev, 1, &m_ctx->m_loadFence, VK_TRUE, -1));

    /* Reset fence and command buffer */
    ThrowIfFailed(vkResetFences(m_ctx->m_dev, 1, &m_ctx->m_loadFence));
    ThrowIfFailed(vkResetCommandBuffer(m_ctx->m_loadCmdBuf, 0));
    VkCommandBufferBeginInfo cmdBufBeginInfo = {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(m_ctx->m_loadCmdBuf, &cmdBufBeginInfo));

    /* Delete upload objects */
    retval->deleteUploadObjects();

    /* All set! */
    std::unique_lock<std::mutex> lk(m_committedMutex);
    m_committedData.insert(tokData);
    return GraphicsDataToken(this, tokData);
}

ITextureSA* VulkanDataFactory::newStaticArrayTexture(size_t width, size_t height, size_t layers,
                                                     TextureFormat fmt, const void* data, size_t sz)
{
    VulkanTextureSA* retval = new VulkanTextureSA(m_ctx, width, height, layers, fmt, data, sz);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_SATexs.emplace_back(retval);
    return retval;
}

ITextureD* VulkanDataFactory::newDynamicTexture(size_t width, size_t height, TextureFormat fmt)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent->getCommandQueue());
    VulkanTextureD* retval = new VulkanTextureD(q, m_ctx, width, height, fmt);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_DTexs.emplace_back(retval);
    return retval;
}

ITextureR* VulkanDataFactory::newRenderTexture(size_t width, size_t height, size_t samples)
{
    VulkanCommandQueue* q = static_cast<VulkanCommandQueue*>(m_parent->getCommandQueue());
    VulkanTextureR* retval = new VulkanTextureR(m_ctx, q, width, height, samples);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_RTexs.emplace_back(retval);
    return retval;
}

IVertexFormat* VulkanDataFactory::newVertexFormat(size_t elementCount, const VertexElementDescriptor* elements)
{
    VulkanVertexFormat* retval = new struct VulkanVertexFormat(elementCount, elements);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_VFmts.emplace_back(retval);
    return retval;
}

IShaderDataBinding* VulkanDataFactory::newShaderDataBinding(IShaderPipeline* pipeline,
        IVertexFormat* vtxFormat,
        IGraphicsBuffer* vbuf, IGraphicsBuffer* instVbuf, IGraphicsBuffer* ibuf,
        size_t ubufCount, IGraphicsBuffer** ubufs,
        size_t texCount, ITexture** texs)
{
    VulkanShaderDataBinding* retval =
        new VulkanShaderDataBinding(m_ctx, pipeline, vbuf, instVbuf, ibuf, ubufCount, ubufs, texCount, texs);
    if (!m_deferredData.get())
        m_deferredData.reset(new struct VulkanData(m_ctx));
    static_cast<VulkanData*>(m_deferredData.get())->m_SBinds.emplace_back(retval);
    return retval;
}

void VulkanDataFactory::reset()
{
    delete static_cast<VulkanData*>(m_deferredData.get());
    m_deferredData.reset();
}

GraphicsDataToken VulkanDataFactory::commit()
{
    if (!m_deferredData.get())
        return GraphicsDataToken(this, nullptr);

    VulkanData* retval = static_cast<VulkanData*>(m_deferredData.get());

    /* size up resources */
    uint32_t bufMemTypeBits = ~0;
    VkDeviceSize bufMemSize = 0;
    uint32_t texMemTypeBits = ~0;
    VkDeviceSize texMemSize = 0;

    for (std::unique_ptr<VulkanGraphicsBufferS>& buf : retval->m_SBufs)
        bufMemSize = buf->sizeForGPU(m_ctx, bufMemTypeBits, bufMemSize);

    for (std::unique_ptr<VulkanGraphicsBufferD>& buf : retval->m_DBufs)
        bufMemSize = buf->sizeForGPU(m_ctx, bufMemTypeBits, bufMemSize);

    for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
        texMemSize = tex->sizeForGPU(m_ctx, texMemTypeBits, texMemSize);

    for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
        texMemSize = tex->sizeForGPU(m_ctx, texMemTypeBits, texMemSize);

    for (std::unique_ptr<VulkanTextureD>& tex : retval->m_DTexs)
        texMemSize = tex->sizeForGPU(m_ctx, texMemTypeBits, texMemSize);

    /* allocate memory */
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = nullptr;
    memAlloc.memoryTypeIndex = 0;
    memAlloc.allocationSize = bufMemSize;
    ThrowIfFalse(MemoryTypeFromProperties(m_ctx, bufMemTypeBits, 0, &memAlloc.memoryTypeIndex));
    ThrowIfFailed(vkAllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &retval->m_bufMem));

    memAlloc.allocationSize = texMemSize;
    ThrowIfFalse(MemoryTypeFromProperties(m_ctx, texMemTypeBits, 0, &memAlloc.memoryTypeIndex));
    ThrowIfFailed(vkAllocateMemory(m_ctx->m_dev, &memAlloc, nullptr, &retval->m_texMem));

    /* place resources */
    uint8_t* mappedData;
    ThrowIfFailed(vkMapMemory(m_ctx->m_dev, retval->m_bufMem, 0, bufMemSize, 0, reinterpret_cast<void**>(&mappedData)));

    for (std::unique_ptr<VulkanGraphicsBufferS>& buf : retval->m_SBufs)
        buf->placeForGPU(m_ctx, retval->m_bufMem, mappedData);

    /* flush static buffers to gpu */
    VkMappedMemoryRange mappedRange;
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.pNext = nullptr;
    mappedRange.memory = retval->m_bufMem;
    mappedRange.offset = 0;
    mappedRange.size = bufMemSize;
    ThrowIfFailed(vkFlushMappedMemoryRanges(m_ctx->m_dev, 1, &mappedRange));
    vkUnmapMemory(m_ctx->m_dev, retval->m_bufMem);

    for (std::unique_ptr<VulkanGraphicsBufferD>& buf : retval->m_DBufs)
        buf->placeForGPU(m_ctx, retval->m_bufMem);

    for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
        tex->placeForGPU(m_ctx, retval->m_texMem);

    for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
        tex->placeForGPU(m_ctx, retval->m_texMem);

    for (std::unique_ptr<VulkanTextureD>& tex : retval->m_DTexs)
        tex->placeForGPU(m_ctx, retval->m_texMem);

    /* Execute static uploads */
    ThrowIfFailed(vkEndCommandBuffer(m_ctx->m_loadCmdBuf));
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_ctx->m_loadCmdBuf;
    ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_ctx->m_loadFence));

    /* Commit data bindings (create descriptor heaps) */
    for (std::unique_ptr<VulkanShaderDataBinding>& bind : retval->m_SBinds)
        bind->commit(m_ctx);

    /* Block handle return until data is ready on GPU */
    ThrowIfFailed(vkWaitForFences(m_ctx->m_dev, 1, &m_ctx->m_loadFence, VK_TRUE, -1));

    /* Reset fence and command buffer */
    ThrowIfFailed(vkResetFences(m_ctx->m_dev, 1, &m_ctx->m_loadFence));
    ThrowIfFailed(vkResetCommandBuffer(m_ctx->m_loadCmdBuf, 0));
    VkCommandBufferBeginInfo cmdBufBeginInfo = {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(m_ctx->m_loadCmdBuf, &cmdBufBeginInfo));

    /* Delete upload objects */
    for (std::unique_ptr<VulkanTextureS>& tex : retval->m_STexs)
        tex->deleteUploadObjects();

    for (std::unique_ptr<VulkanTextureSA>& tex : retval->m_SATexs)
        tex->deleteUploadObjects();

    /* All set! */
    m_deferredData.reset();
    std::unique_lock<std::mutex> lk(m_committedMutex);
    m_committedData.insert(retval);
    return GraphicsDataToken(this, retval);
}

ThreadLocalPtr<struct VulkanData> VulkanDataFactory::m_deferredData;

void VulkanCommandQueue::execute()
{
    if (!m_running)
        return;

    /* Stage dynamic uploads */
    VulkanDataFactory* gfxF = static_cast<VulkanDataFactory*>(m_parent->getDataFactory());
    std::unique_lock<std::mutex> datalk(gfxF->m_committedMutex);
    for (VulkanData* d : gfxF->m_committedData)
    {
        for (std::unique_ptr<VulkanGraphicsBufferD>& b : d->m_DBufs)
            b->update(m_fillBuf);
        for (std::unique_ptr<VulkanTextureD>& t : d->m_DTexs)
            t->update(m_fillBuf);
    }
    datalk.unlock();

    /* Perform dynamic uploads */
    if (!m_dynamicNeedsReset)
    {
        vkEndCommandBuffer(m_dynamicCmdBufs[m_fillBuf]);

        VkSubmitInfo submitInfo = {};
        submitInfo.pNext = nullptr;
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 0;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_dynamicCmdBufs[m_fillBuf];
        submitInfo.signalSemaphoreCount = 0;
        submitInfo.pSignalSemaphores = nullptr;
        ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_dynamicBufFence));
    }

    /* Check on fence */
    if (vkGetFenceStatus(m_ctx->m_dev, m_drawCompleteFence) == VK_NOT_READY)
    {
        /* Abandon this list (renderer too slow) */
        resetCommandBuffer();
        m_dynamicNeedsReset = true;
        m_doPresent = false;
        return;
    }

    /* Perform texture resizes */
    if (m_texResizes.size())
    {
        for (const auto& resize : m_texResizes)
            resize.first->resize(m_ctx, resize.second.first, resize.second.second);
        m_texResizes.clear();
        resetCommandBuffer();
        m_dynamicNeedsReset = true;
        m_doPresent = false;
        return;
    }

    vkCmdEndRenderPass(m_cmdBufs[m_fillBuf]);

    m_drawBuf = m_fillBuf;
    m_fillBuf ^= 1;

    /* Queue the command buffer for execution */
    VkPipelineStageFlags pipeStageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.pNext = nullptr;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_swapChainReadySem;
    submitInfo.pWaitDstStageMask = &pipeStageFlags;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmdBufs[m_drawBuf];
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;
    if (m_doPresent)
    {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_drawCompleteSem;
    }
    ThrowIfFailed(vkQueueSubmit(m_ctx->m_queue, 1, &submitInfo, m_drawCompleteFence));

    if (m_doPresent)
    {
        VkPresentInfoKHR present;
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.pNext = nullptr;
        present.swapchainCount = 1;
        present.pSwapchains = &m_windowCtx->m_swapChain;
        present.pImageIndices = &m_windowCtx->m_backBuf;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &m_drawCompleteSem;
        present.pResults = nullptr;

        ThrowIfFailed(vkQueuePresentKHR(m_ctx->m_queue, &present));
        m_doPresent = false;
    }

    resetCommandBuffer();
    resetDynamicCommandBuffer();
}

IGraphicsCommandQueue* _NewVulkanCommandQueue(VulkanContext* ctx, VulkanContext::Window* windowCtx,
                                              IGraphicsContext* parent)
{
    return new struct VulkanCommandQueue(ctx, windowCtx, parent);
}

IGraphicsDataFactory* _NewVulkanDataFactory(VulkanContext* ctx, IGraphicsContext* parent)
{
    return new VulkanDataFactory(parent, ctx);
}

}

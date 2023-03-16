#pragma once
#include "nvvkhl/appbase_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/memallocator_dma_vk.hpp"
#include "nvvk/resourceallocator_vk.hpp"
#include "shaders/host_device.h"

class HelloVulkan : public nvvkhl::AppBaseVk{
public:
    void setup(const VkInstance& instance, const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t queueFamily) override;
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void loadModel(const std::string& filename, nvmath::mat4f transform = nvmath::mat4f(1));
    void updateDescriptorSet();
    void createUniformBuffer();
    void createObjDescriptionBuffer();
    void CreateTextureImage(const VkCommandBuffer& cmdBuf, const std::vector<std::string>& textures);
    void updateUniformBuffer(const VkCommandBuffer& cmdBuf);
    void onResize(int /*w*/, int /*h*/)override;
    void destroyResource();
    void rasterize(const VkCommandBuffer& cmdBuf);

    struct ObjModel{
        uint32_t nbIndices = 0;
        uint32_t nvVertices = 0;
        nvvk::Buffer vertexBuffer;
        nvvk::Buffer indexBuffer;
        nvvk::Buffer matColorBuffer;
        nvvk::Buffer matIndexBuffer;
    };

    struct ObjInstance
    {
        nvmath::mat4f transform;
        uint32_t objIndex = 0;
    };

    PushConstantRaster m_pvRaster{
        {1},
        {10.f,15.f,8.f},
        0,
        100.f,
        0
    };

    std::vector<ObjModel> m_objModel;
    std::vector<ObjDesc> m_objDesc;
    std::vector<ObjInstance> m_instances;

    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_graphicPipeline;
    nvvk::DescriptorSetBindings m_descSetLayoutBind;
    VkDescriptorPool m_descPool;
    VkDescriptorSetLayout m_descSetLayout;
    VkDescriptorSet m_descSet;

    nvvk::Buffer m_bGlobals;
    nvvk::Buffer m_bObjDesc;

    std::vector<nvvk::Texture> m_textures;
    nvvk::ResourceAllocatorDma m_alloc;
    nvvk::DebugUtil m_debug;

    void createOffscreenRender();
    void createPostPipeline();
    void createPostDescriptor();
    void updatePostDescriptorSet();
    void drawPost(VkCommandBuffer cmdBuf);

    nvvk::DescriptorSetBindings m_postDescSetLayoutBind;
    VkDescriptorPool m_postDescPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_postDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_postDescSet = VK_NULL_HANDLE;
    VkPipeline m_postPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_postPipelineLayout = VK_NULL_HANDLE;
    VkRenderPass m_offscreenRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_offscreenFrameBuffer = VK_NULL_HANDLE;
    nvvk::Texture m_offscreenColor;
    nvvk::Texture m_offscreenDepth;
    VkFormat m_offscreenColorFormat{VK_FORMAT_R32G32B32A32_SFLOAT};
    VkFormat m_offscreenDepthFormat{VK_FORMAT_X8_D24_UNORM_PACK32};
    



}；
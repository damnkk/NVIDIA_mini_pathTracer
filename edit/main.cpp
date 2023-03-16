#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <nvvk/context_vk.cpp>
// You can use this utility to create some vk structure like createInfo by a more simple way
#include <nvvk/structs_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>
#include <nvvk/error_vk.hpp>
#include <nvh/fileoperations.hpp>
#include <nvvk/shaders_vk.hpp>
#include <nvvk/descriptorsets_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>
#include <array>



static const uint64_t render_width = 1920;
static const uint64_t render_height = 1080;
static const uint32_t workgroup_width = 16;
static const uint32_t workgroup_height = 8;

VkCommandBuffer AllocationAndBeginOneTimeCommandBuffer(VkDevice device, VkCommandPool cmdPool){
    VkCommandBufferAllocateInfo cmdAllocInfo = nvvk::make<VkCommandBufferAllocateInfo>();
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer cmdBuffer;
    NVVK_CHECK(vkAllocateCommandBuffers(device,& cmdAllocInfo,&cmdBuffer));

    VkCommandBufferBeginInfo beginInfo = nvvk::make<VkCommandBufferBeginInfo>();
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NVVK_CHECK(vkBeginCommandBuffer(cmdBuffer,&beginInfo));
    return cmdBuffer;
}

void EndSubmitWaitAndFreeCommandBuffer(VkDevice device, VkQueue queue,VkCommandPool cmdPool, VkCommandBuffer cmdBuffer){
    NVVK_CHECK(vkEndCommandBuffer(cmdBuffer));
    VkSubmitInfo submitInfo = nvvk::make<VkSubmitInfo>();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    NVVK_CHECK(vkQueueSubmit(queue,1,&submitInfo,VK_NULL_HANDLE));
    NVVK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, cmdPool,1,&cmdBuffer);
}

VkDeviceAddress GetBufferDevcieAddress(VkDevice device, VkBuffer buffer){
    VkBufferDeviceAddressInfo addressInfo = nvvk::make<VkBufferDeviceAddressInfo>();
    addressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &addressInfo);
}

int main(int argc, const char** argv){
    nvvk::ContextCreateInfo deviceInfo;
    deviceInfo.apiMajor = 1;
    deviceInfo.apiMinor = 2;

    deviceInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    VkPhysicalDeviceAccelerationStructureFeaturesKHR  asFeatures = nvvk::make<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
    deviceInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &asFeatures);
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = nvvk::make<VkPhysicalDeviceRayQueryFeaturesKHR>();
    deviceInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayQueryFeatures);

    nvvk::Context context;
    context.init(deviceInfo);

    nvvk::ResourceAllocatorDedicated allocator;
    allocator.init(context, context.m_physicalDevice);

    VkDeviceSize bufferSize = render_width*render_height*3*sizeof(float);
    VkBufferCreateInfo createInfo = nvvk::make<VkBufferCreateInfo>();
    createInfo.size = bufferSize;
    createInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT| VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    nvvk::Buffer buffer = allocator.createBuffer(createInfo,VK_MEMORY_PROPERTY_HOST_CACHED_BIT|
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const std::string exePath(argv[0], std::string(argv[0]).find_last_of("/\\")+1);
    std::vector<std::string> searchPaths = {exePath + PROJECT_RELDIRECTORY, exePath + PROJECT_RELDIRECTORY "..",
                                          exePath + PROJECT_RELDIRECTORY "../..", exePath + PROJECT_NAME};
    tinyobj::ObjReader reader;
    reader.ParseFromFile(nvh::findFile("scenes/CornellBox-Original-Merged.obj",searchPaths));
    assert(reader.Valid());
    const std::vector<tinyobj::real_t> objVertices = reader.GetAttrib().GetVertices();
    const std::vector<tinyobj::shape_t>& objShapes = reader.GetShapes();
    assert(objShapes.size()==1);
    const tinyobj::shape_t& objShape = objShapes[0];
    std::vector<uint32_t> objIndices;
    objIndices.reserve(objShape.mesh.indices.size());
    for(const tinyobj::index_t& index:objShape.mesh.indices){
        objIndices.push_back(index.vertex_index);
    }

    VkCommandPoolCreateInfo cmdPoolCreateInfo = nvvk::make<VkCommandPoolCreateInfo>();
    cmdPoolCreateInfo.queueFamilyIndex = context.m_queueGCT;
    VkCommandPool cmdPool;
    NVVK_CHECK(vkCreateCommandPool(context,&cmdPoolCreateInfo, nullptr,&cmdPool));

    nvvk::Buffer vertexBuffer, indexBuffer;
    {
        VkCommandBuffer uploadCmdBuffer = AllocationAndBeginOneTimeCommandBuffer(context,  cmdPool);
        const VkBufferUsageFlags usage= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        vertexBuffer = allocator.createBuffer(uploadCmdBuffer, objVertices, usage);
        indexBuffer = allocator.createBuffer(uploadCmdBuffer,objIndices, usage);
        EndSubmitWaitAndFreeCommandBuffer(context,context.m_queueGCT, cmdPool,uploadCmdBuffer);
        allocator.finalizeAndReleaseStaging();
    }

    std::vector<nvvk::RaytracingBuilderKHR::BlasInput> blases;
    {
        nvvk::RaytracingBuilderKHR::BlasInput blas;
        VkDeviceAddress vertexBufferAddress = GetBufferDevcieAddress(context, vertexBuffer.buffer);
        VkDeviceAddress indexBufferAddress = GetBufferDevcieAddress(context, indexBuffer.buffer);
        VkAccelerationStructureGeometryTrianglesDataKHR triangles = nvvk::make<VkAccelerationStructureGeometryTrianglesDataKHR>();
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = vertexBufferAddress;
        triangles.vertexStride = 3*sizeof(float);
        triangles.maxVertex = static_cast<uint32_t>(objVertices.size());
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = indexBufferAddress;
        triangles.transformData.deviceAddress = 0;
        VkAccelerationStructureGeometryKHR geometry = nvvk::make<VkAccelerationStructureGeometryKHR>();
        geometry.geometry.triangles = triangles;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR|VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
        blas.asGeometry.push_back(geometry);
        VkAccelerationStructureBuildRangeInfoKHR offsetInfo;
        offsetInfo.firstVertex = 0;
        offsetInfo.primitiveCount = static_cast<uint32_t>(objIndices.size()/3);
        offsetInfo.primitiveOffset = 0;
        offsetInfo.transformOffset = 0;
        blas.asBuildOffsetInfo.push_back(offsetInfo);
        blases.push_back(blas);
    }
    nvvk::RaytracingBuilderKHR raytracingBuilder;
    raytracingBuilder.setup(context, &allocator, context.m_queueGCT);
    raytracingBuilder.buildBlas(blases,VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    {
        VkAccelerationStructureInstanceKHR instance{};
        instance.accelerationStructureReference = raytracingBuilder.getBlasDeviceAddress(0);
        instance.transform.matrix[0][0] = instance.transform.matrix[1][1] = instance.transform.matrix[2][2] = 1.0f;
        instance.instanceCustomIndex = 0;
        instance.instanceShaderBindingTableRecordOffset=0;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instances.push_back(instance);
    }
    raytracingBuilder.buildTlas(instances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    nvvk::DescriptorSetContainer descriptorSetContainer(context);
    descriptorSetContainer.addBinding(0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.addBinding(1,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.addBinding(2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.addBinding(3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    descriptorSetContainer.initLayout();
    descriptorSetContainer.initPool(1);
    descriptorSetContainer.initPipeLayout();
    descriptorSetContainer.getSetsCount();
    

    std::array<VkWriteDescriptorSet, 4> writedescriptorsets;
    //0
    VkDescriptorBufferInfo descriptorBufferInfo = {};
    descriptorBufferInfo.buffer = buffer.buffer;
    descriptorBufferInfo.range = bufferSize;
    writedescriptorsets[0]  = descriptorSetContainer.makeWrite(0, 0, &descriptorBufferInfo);

    VkWriteDescriptorSetAccelerationStructureKHR descriptorAS = nvvk::make<VkWriteDescriptorSetAccelerationStructureKHR>();
    VkAccelerationStructureKHR tlasCopy = raytracingBuilder.getAccelerationStructure();
    descriptorAS.accelerationStructureCount = 1;
    descriptorAS.pAccelerationStructures = &tlasCopy;
    writedescriptorsets[1] = descriptorSetContainer.makeWrite(0, 1, &descriptorAS);

    VkDescriptorBufferInfo vertexBufferInfo = {};
    vertexBufferInfo.buffer = vertexBuffer.buffer;
    vertexBufferInfo.range = VK_WHOLE_SIZE;
    writedescriptorsets[2] = descriptorSetContainer.makeWrite(0,2, &vertexBufferInfo);

    VkDescriptorBufferInfo indexBufferInfo = {};
    indexBufferInfo.buffer = indexBuffer.buffer;
    indexBufferInfo.range = VK_WHOLE_SIZE;
    writedescriptorsets[3] = descriptorSetContainer.makeWrite(0,3,&indexBufferInfo);

    vkUpdateDescriptorSets(context,
                           static_cast<uint32_t>(writedescriptorsets.size()),
                           writedescriptorsets.data(),
                           0,nullptr);

    VkShaderModule rayTraceModule = nvvk::createShaderModule(context,nvh::loadFile("shaders/raytrace.comp.glsl.spv", true, searchPaths));
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = nvvk::make<VkPipelineShaderStageCreateInfo>();
    shaderStageCreateInfo.module = rayTraceModule;
    shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    // U can use this pName to set which entry point that current pipeline will use.
    shaderStageCreateInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineCreateInfo = nvvk::make<VkComputePipelineCreateInfo>();
    pipelineCreateInfo.layout = descriptorSetContainer.getPipeLayout();
    pipelineCreateInfo.stage = shaderStageCreateInfo;
    VkPipeline computePipeline;
    NVVK_CHECK(vkCreateComputePipelines(context, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &computePipeline));
    VkCommandBuffer cmdBuffer = AllocationAndBeginOneTimeCommandBuffer(context, cmdPool);
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);

    VkDescriptorSet descriptorSet = descriptorSetContainer.getSet(0);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,descriptorSetContainer.getPipeLayout(),0,1, &descriptorSet, 0, nullptr);
    VkMemoryBarrier memoryBarrier = nvvk::make<VkMemoryBarrier>();
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmdBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&memoryBarrier,0,nullptr,0,nullptr);
    vkCmdDispatch(cmdBuffer,(uint32_t(render_width)+workgroup_width-1)/workgroup_width,(uint32_t(render_height)+workgroup_height-1)/workgroup_height,1);
    EndSubmitWaitAndFreeCommandBuffer(context,context.m_queueGCT,cmdPool,cmdBuffer);

    NVVK_CHECK(vkQueueWaitIdle(context.m_queueGCT));
    void* data = allocator.map(buffer);
    stbi_write_hdr("out.hdr", render_width,render_height, 3,reinterpret_cast<const float*>(data));
    allocator.unmap(buffer);
    vkDestroyCommandPool(context, cmdPool, nullptr);


    raytracingBuilder.destroy();
    allocator.destroy(buffer);
    allocator.destroy(vertexBuffer);
    allocator.destroy(indexBuffer);
    vkDestroyPipeline(context, computePipeline, nullptr);
    vkDestroyShaderModule(context, rayTraceModule, nullptr);
    descriptorSetContainer.deinit();

    // allocator.deinit();
    context.deinit();
}

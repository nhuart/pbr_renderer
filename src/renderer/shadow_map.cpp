#include "renderer/shadow_map.hpp"
#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "scene/types.hpp"

#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

ShadowMap::ShadowMap(VulkanContext const& ctx, CommandService const& cmds, uint32_t objectCount) {
    createDepthImage(ctx, cmds);
    createFragmentUboBuffers(ctx);
    createPipeline(ctx, objectCount);
    allocateObjectData(ctx, objectCount);
}

void ShadowMap::createDepthImage(VulkanContext const& ctx, CommandService const& cmds) {
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    auto [depthImage, depthMemory] = vkutil::createImage(ctx, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1,
            vk::SampleCountFlagBits::e1, depthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    image = std::move(depthImage);
    memory = std::move(depthMemory);

    imageView = vkutil::createImageView(ctx, *image, depthFormat, vk::ImageAspectFlagBits::eDepth);

    sampler = vk::raii::Sampler(ctx.device,
            vk::SamplerCreateInfo{
                .magFilter = vk::Filter::eNearest,
                .minFilter = vk::Filter::eNearest,
                .mipmapMode = vk::SamplerMipmapMode::eNearest,
                .addressModeU = vk::SamplerAddressMode::eClampToBorder,
                .addressModeV = vk::SamplerAddressMode::eClampToBorder,
                .addressModeW = vk::SamplerAddressMode::eClampToBorder,
                .mipLodBias = 0.0f,
                .anisotropyEnable = vk::False,
                .compareEnable = vk::False,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = vk::BorderColor::eFloatOpaqueWhite,
                .unnormalizedCoordinates = vk::False,
            });

    // Transition to shader-read layout so the forward pass can safely sample on frame 0
    // before the shadow pass has had a chance to write anything.
    vk::raii::CommandBuffer cmd =
            std::move(ctx.device
                              .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                  .commandPool = *cmds.commandPool,
                                  .level = vk::CommandBufferLevel::ePrimary,
                                  .commandBufferCount = 1,
                              })
                              .front());
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eNone,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eDepth);
    cmd.end();
    vk::CommandBuffer cmdHandle = *cmd;
    vk::raii::Fence fence(ctx.device, vk::FenceCreateInfo{});
    ctx.graphicsQueue.submit(
            vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmdHandle }, *fence);
    std::ignore = ctx.device.waitForFences(*fence, vk::True, UINT64_MAX);
}

void ShadowMap::createFragmentUboBuffers(VulkanContext const& ctx) {
    // Per-frame UBO for the fragment shader: lightSpaceTransform VP (no model matrix)
    for (int frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        auto [uboBuffer, uboMemory] = vkutil::createBuffer(ctx, sizeof(ShadowUBO),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
        fragmentUbo[frameIndex].mapped = uboMemory.mapMemory(0, sizeof(ShadowUBO));
        fragmentUbo[frameIndex].buffer = std::move(uboBuffer);
        fragmentUbo[frameIndex].memory = std::move(uboMemory);
    }
}

void ShadowMap::createPipeline(VulkanContext const& ctx, uint32_t objectCount) {
    // Descriptor set layout: binding 0 = per-object lightSpaceTransform UBO (vertex stage)
    std::array<vk::DescriptorSetLayoutBinding, 1> descriptorBindings{ { {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    } } };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(descriptorBindings.size()),
                .pBindings = descriptorBindings.data(),
            });

    uint32_t maxSets = std::max(objectCount, 1u) * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<vk::DescriptorPoolSize, 1> poolSizes{ {
        { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxSets },
    } };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = maxSets,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    vk::DescriptorSetLayout descriptorSetLayoutHandle = *descriptorSetLayout;
    pipelineLayout = vk::raii::PipelineLayout(ctx.device,
            vk::PipelineLayoutCreateInfo{
                .setLayoutCount = 1,
                .pSetLayouts = &descriptorSetLayoutHandle,
            });

    auto vertexShaderCode = vkutil::readSpirv("shaders/compiled/shadow.vert.spv");
    vk::raii::ShaderModule vertexShaderModule = vkutil::createShaderModule(ctx, vertexShaderCode);

    std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertexShaderModule,
            .pName = "main",
        },
    };

    std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport,
        vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    auto bindingDesc = Vertex::getBindingDescription();
    vk::VertexInputAttributeDescription positionAttrib{
        .location = 0,
        .binding = 0,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(Vertex, pos),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDesc,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &positionAttrib,
    };
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
    };
    vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
    };
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .attachmentCount = 0,
        .pAttachments = nullptr,
    };

    vk::Format shadowDepthFormat = vkutil::findDepthFormat(ctx);
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
            pipelineChain = {
                {
                    .stageCount = static_cast<uint32_t>(shaderStages.size()),
                    .pStages = shaderStages.data(),
                    .pVertexInputState = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState = &multisample,
                    .pDepthStencilState = &depthStencil,
                    .pColorBlendState = &colorBlending,
                    .pDynamicState = &dynamicStateInfo,
                    .layout = *pipelineLayout,
                    .renderPass = nullptr,
                },
                {
                    .colorAttachmentCount = 0,
                    .pColorAttachmentFormats = nullptr,
                    .depthAttachmentFormat = shadowDepthFormat,
                },
            };

    pipeline = vk::raii::Pipeline(ctx.device, nullptr,
            pipelineChain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "Shadow pipeline: created\n";
}

void ShadowMap::allocateObjectData(VulkanContext const& ctx, uint32_t objectCount) {
    objects.clear();
    objects.resize(objectCount);
    for (uint32_t o = 0; o < objectCount; ++o) {
        auto& obj = objects[o];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            auto [buf, mem] = vkutil::createBuffer(ctx, sizeof(ShadowUBO),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent);
            obj.frames[i].mapped = mem.mapMemory(0, sizeof(ShadowUBO));
            obj.frames[i].buffer = std::move(buf);
            obj.frames[i].memory = std::move(mem);
        }

        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
        obj.descriptorSets = vk::raii::DescriptorSets(ctx.device,
                vk::DescriptorSetAllocateInfo{
                    .descriptorPool = *descriptorPool,
                    .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                    .pSetLayouts = layouts.data(),
                });

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::DescriptorBufferInfo uboInfo{
                .buffer = *obj.frames[i].buffer,
                .offset = 0,
                .range = sizeof(ShadowUBO),
            };
            std::array<vk::WriteDescriptorSet, 1> writes{ { {
                .dstSet = *obj.descriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uboInfo,
            } } };
            ctx.device.updateDescriptorSets(writes, {});
        }
    }
}

void ShadowMap::updateLightSpaceMatrix(DirectionalLight const& light) {
    glm::vec3 dir = glm::normalize(light.direction);
    glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 lightPos = dir * 10.0f;
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
    // Orthographic frustum sized to cover the scene; GLM_FORCE_DEPTH_ZERO_TO_ONE is active
    // so glm::ortho already emits [0,1] depth — no manual remap needed.
    glm::mat4 lightProj = glm::ortho(-3.0f, 3.0f, -3.0f, 3.0f, 0.1f, 30.0f);
    lightProj[1][1] *= -1.0f; // Flip Y column so rasterized NDC matches Vulkan's Y-down convention.
    lightSpaceMatrix = lightProj * lightView;
}

void ShadowMap::updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex,
        glm::mat4 const& model) {
    glm::mat4 mvp = lightSpaceMatrix * model;
    ShadowUBO ubo{ .lightSpaceTransform = mvp };
    memcpy(objects[objectIndex].frames[frameIndex].mapped, &ubo, sizeof(ubo));
}

void ShadowMap::updateFragmentUBO(uint32_t frameIndex) {
    ShadowUBO ubo{ .lightSpaceTransform = lightSpaceMatrix, .shadowBias = shadowBias };
    memcpy(fragmentUbo[frameIndex].mapped, &ubo, sizeof(ubo));
}

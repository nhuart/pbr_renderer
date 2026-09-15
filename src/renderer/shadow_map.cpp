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
    // Depth image
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    auto [img, mem] = vkutil::createImage(ctx, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1,
            vk::SampleCountFlagBits::e1, depthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    image = std::move(img);
    memory = std::move(mem);

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

    // Transition image to shader-read layout (steady-state start-of-frame layout)
    {
        auto cmd = std::move(ctx.device
                                     .allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                         .commandPool = *cmds.commandPool,
                                         .level = vk::CommandBufferLevel::ePrimary,
                                         .commandBufferCount = 1,
                                     })
                                     .front());
        cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eNone,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eTopOfPipe,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eDepth);
        cmd.end();
        vk::CommandBuffer cmdHandle = *cmd;
        vk::raii::Fence fence(ctx.device, vk::FenceCreateInfo{});
        ctx.graphicsQueue.submit(
                vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmdHandle }, *fence);
        std::ignore = ctx.device.waitForFences(*fence, vk::True, UINT64_MAX);
    }

    // Per-frame fragment UBO (lightSpaceMatrix only — used by fragment shader for shadow lookup)
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto [buf, bufMem] = vkutil::createBuffer(ctx, sizeof(ShadowUBO),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
        shadowUboMapped.push_back(bufMem.mapMemory(0, sizeof(ShadowUBO)));
        shadowUboBuffers.push_back(std::move(buf));
        shadowUboMemory.push_back(std::move(bufMem));
    }

    // Descriptor set layout for shadow depth pass: binding 0 = per-object lightSpaceMVP UBO
    std::array<vk::DescriptorSetLayoutBinding, 1> bindings{ { {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    } } };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    uint32_t maxObjects = std::max(objectCount, 1u);
    uint32_t setCount = maxObjects * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<vk::DescriptorPoolSize, 1> poolSizes{ {
        { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = setCount },
    } };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = setCount,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    vk::DescriptorSetLayout dslHandle = *descriptorSetLayout;
    pipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                               .setLayoutCount = 1,
                                                               .pSetLayouts = &dslHandle,
                                                           });

    // Depth-only graphics pipeline
    auto vertCode = vkutil::readSpirv("shaders/compiled/shadow.vert.spv");
    vk::raii::ShaderModule vertModule = vkutil::createShaderModule(ctx, vertCode);

    std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertModule,
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
    auto attribDescs = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDesc,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size()),
        .pVertexAttributeDescriptions = attribDescs.data(),
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

    allocateObjectData(ctx, objectCount);
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
            obj.uboMapped.push_back(mem.mapMemory(0, sizeof(ShadowUBO)));
            obj.uboBuffers.push_back(std::move(buf));
            obj.uboMemory.push_back(std::move(mem));
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
                .buffer = *obj.uboBuffers[i],
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
    // light.direction is the direction the light travels toward (e.g. [1,2,1] → light comes
    // from above-right). Light source is placed opposite, looking at scene center.
    glm::vec3 dir = glm::normalize(light.direction);
    glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 lightPos = dir * 10.0f;
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
    // Orthographic frustum sized to cover the scene; GLM_FORCE_DEPTH_ZERO_TO_ONE is active
    // so glm::ortho already emits [0,1] depth — no manual remap needed.
    // Flip Y column so rasterized NDC matches Vulkan's Y-down convention.
    float extent = 3.0f;
    glm::mat4 lightProj = glm::ortho(-extent, extent, -extent, extent, 0.1f, 30.0f);
    lightProj[1][1] *= -1.0f;
    lightSpaceMatrix = lightProj * lightView;
}

void ShadowMap::updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex,
        glm::mat4 const& model) {
    glm::mat4 mvp = lightSpaceMatrix * model;
    ShadowUBO ubo{ .lightSpaceMVP = mvp };
    memcpy(objects[objectIndex].uboMapped[frameIndex], &ubo, sizeof(ubo));
}

void ShadowMap::updateFragmentUBO(uint32_t frameIndex) {
    // Fragment shader needs lightSpaceMatrix (no model — applied to world-space fragWorldPos)
    ShadowUBO ubo{ .lightSpaceMVP = lightSpaceMatrix };
    memcpy(shadowUboMapped[frameIndex], &ubo, sizeof(ubo));
}

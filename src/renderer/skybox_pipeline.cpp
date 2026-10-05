#include "renderer/skybox_pipeline.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "renderer/ibl_environment.hpp"
#include "core/config.hpp"

#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>


SkyboxPipeline::SkyboxPipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        IblEnvironment const& ibl) {
    constexpr uint32_t frameCount = MAX_FRAMES_IN_FLIGHT;

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{ {
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
    } };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = frameCount },
        { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = frameCount },
    } };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = frameCount,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    // Allocate per-frame UBOs
    for (uint32_t i = 0; i < frameCount; i++) {
        auto [buf, mem] = vkutil::createBuffer(ctx, sizeof(SkyboxUBO),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
        uniformBuffersMapped.push_back(mem.mapMemory(0, sizeof(SkyboxUBO)));
        uniformBuffers.push_back(std::move(buf));
        uniformBuffersMemory.push_back(std::move(mem));
    }

    // Allocate descriptor sets
    std::vector<vk::DescriptorSetLayout> layouts(frameCount, *descriptorSetLayout);
    auto sets = vk::raii::DescriptorSets(ctx.device, vk::DescriptorSetAllocateInfo{
                                                         .descriptorPool = *descriptorPool,
                                                         .descriptorSetCount = frameCount,
                                                         .pSetLayouts = layouts.data(),
                                                     });
    for (auto& s: sets) {
        descriptorSets.push_back(std::move(s));
    }

    vk::DescriptorImageInfo skyboxInfo{
        .sampler = *ibl.prefilter.sampler,
        .imageView = *ibl.prefilter.imageView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    for (uint32_t i = 0; i < frameCount; i++) {
        vk::DescriptorBufferInfo uboInfo{
            .buffer = *uniformBuffers[i],
            .offset = 0,
            .range = sizeof(SkyboxUBO),
        };
        std::array<vk::WriteDescriptorSet, 2> writes{ {
            {
                .dstSet = *descriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uboInfo,
            },
            {
                .dstSet = *descriptorSets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &skyboxInfo,
            },
        } };
        ctx.device.updateDescriptorSets(writes, {});
    }

    // Pipeline
    auto vertCode = vkutil::readSpirv("shaders/compiled/skybox.vert.spv");
    auto fragCode = vkutil::readSpirv("shaders/compiled/skybox.frag.spv");
    vk::raii::ShaderModule vertModule = vkutil::createShaderModule(ctx, vertCode);
    vk::raii::ShaderModule fragModule = vkutil::createShaderModule(ctx, fragCode);

    std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertModule,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *fragModule,
            .pName = "main",
        },
    };

    std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport,
        vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    // No vertex input — vertices are generated in the vertex shader
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk::PipelineViewportStateCreateInfo viewportStateInfo{
        .viewportCount = 1,
        .scissorCount = 1,
    };
    vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
        .rasterizationSamples = ctx.msaaSamples,
        .sampleShadingEnable = vk::False,
    };
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
        .logicOpEnable = vk::False,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };
    // Depth test eLessOrEqual so skybox passes at depth=1.0 (far plane); depth write off
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::False,
        .depthCompareOp = vk::CompareOp::eLessOrEqual,
    };

    vk::DescriptorSetLayout dslHandle = *descriptorSetLayout;
    pipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                              .setLayoutCount = 1,
                                                              .pSetLayouts = &dslHandle,
                                                          });

    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
            pipelineChain = {
                {
                    .stageCount = static_cast<uint32_t>(shaderStages.size()),
                    .pStages = shaderStages.data(),
                    .pVertexInputState = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssemblyInfo,
                    .pViewportState = &viewportStateInfo,
                    .pRasterizationState = &rasterizerInfo,
                    .pMultisampleState = &multisamplingInfo,
                    .pDepthStencilState = &depthStencilInfo,
                    .pColorBlendState = &colorBlendingInfo,
                    .pDynamicState = &dynamicStateInfo,
                    .layout = *pipelineLayout,
                },
                {
                    .colorAttachmentCount = 1,
                    .pColorAttachmentFormats = &swapchain.surfaceFormat.format,
                    .depthAttachmentFormat = depthFormat,
                },
            };

    pipeline = vk::raii::Pipeline(ctx.device, nullptr,
            pipelineChain.get<vk::GraphicsPipelineCreateInfo>());
}

void SkyboxPipeline::updateUBO(uint32_t frameIndex, SkyboxUBO const& ubo) {
    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

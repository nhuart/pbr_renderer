#include "renderer/particle_pipeline.hpp"
#include "core/command_service.hpp"
#include "core/config.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "renderer/gpu_types.hpp"
#include "renderer/vertex.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

ParticlePipeline::ParticlePipeline(VulkanContext const& ctx, Swapchain const& swapchain,
        CommandService const& cmds, ParticleSystem const& particleSystem) {
    uint32_t particleCount = particleSystem.count;

    // --- SSBO ---
    std::default_random_engine rndEngine(static_cast<unsigned>(time(nullptr)));
    std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

    std::vector<Particle> particles(particleCount);
    for (auto& particle: particles) {
        float radius = 0.25f * std::sqrt(rndDist(rndEngine));
        float theta = rndDist(rndEngine) * 2.0f * std::numbers::pi_v<float>;
        float posX =
                radius * std::cos(theta) * static_cast<float>(HEIGHT) / static_cast<float>(WIDTH);
        float posY = radius * std::sin(theta);
        particle.position = glm::vec2(posX, posY);
        particle.velocity = glm::normalize(glm::vec2(posX, posY)) * 0.00025f;
        particle.color =
                glm::vec4(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine), 1.0f);
    }

    vk::DeviceSize bufferSize = sizeof(Particle) * particleCount;

    auto [stagingBuffer, stagingMemory] = vkutil::createBuffer(ctx, bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, particles.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto [ssbo, ssboMemory] = vkutil::createBuffer(ctx, bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
        vkutil::copyBuffer(ctx, cmds.commandPool, stagingBuffer, ssbo, bufferSize);
        shaderStorageBuffers.push_back(std::move(ssbo));
        shaderStorageBuffersMemory.push_back(std::move(ssboMemory));
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto [uboBuf, uboMemory] = vkutil::createBuffer(ctx, sizeof(ComputeUBO),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
        computeUniformBuffersMapped.push_back(uboMemory.mapMemory(0, sizeof(ComputeUBO)));
        computeUniformBuffers.push_back(std::move(uboBuf));
        computeUniformBuffersMemory.push_back(std::move(uboMemory));
    }

    std::cout << "Shader storage buffers: " << particleCount << " particles, "
              << MAX_FRAMES_IN_FLIGHT << " SSBO pairs\n";

    // --- Compute descriptor layout ---
    std::array<vk::DescriptorSetLayoutBinding, 3> computeBindings{ {
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 2,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
    } };
    computeDescriptorSetLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(computeBindings.size()),
                .pBindings = computeBindings.data(),
            });

    // --- Compute pipeline ---
    auto compCode = vkutil::readSpirv("shaders/compiled/particle.comp.spv");
    vk::raii::ShaderModule compModule = vkutil::createShaderModule(ctx, compCode);
    vk::PipelineShaderStageCreateInfo compStageInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = *compModule,
        .pName = "main",
    };
    vk::DescriptorSetLayout cdslHandle = *computeDescriptorSetLayout;
    computePipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                                     .setLayoutCount = 1,
                                                                     .pSetLayouts = &cdslHandle,
                                                                 });
    computePipeline = vk::raii::Pipeline(ctx.device, nullptr,
            vk::ComputePipelineCreateInfo{
                .stage = compStageInfo,
                .layout = *computePipelineLayout,
            });
    std::cout << "Compute pipeline: created\n";

    // --- Compute descriptor pool + sets ---
    std::array poolSizes = {
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2,
        },
    };
    computeDescriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *computeDescriptorSetLayout);
    computeDescriptorSets = vk::raii::DescriptorSets(ctx.device,
            vk::DescriptorSetAllocateInfo{
                .descriptorPool = *computeDescriptorPool,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DescriptorBufferInfo uboInfo{
            .buffer = *computeUniformBuffers[i],
            .offset = 0,
            .range = sizeof(ComputeUBO),
        };
        int prevFrame = (i - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
        vk::DescriptorBufferInfo ssboLastInfo{
            .buffer = *shaderStorageBuffers[prevFrame],
            .offset = 0,
            .range = sizeof(Particle) * particleCount,
        };
        vk::DescriptorBufferInfo ssboCurrInfo{
            .buffer = *shaderStorageBuffers[i],
            .offset = 0,
            .range = sizeof(Particle) * particleCount,
        };
        std::array<vk::WriteDescriptorSet, 3> writes{ {
            {
                .dstSet = *computeDescriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uboInfo,
            },
            {
                .dstSet = *computeDescriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo = &ssboLastInfo,
            },
            {
                .dstSet = *computeDescriptorSets[i],
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo = &ssboCurrInfo,
            },
        } };
        ctx.device.updateDescriptorSets(writes, {});
    }

    // --- Particle graphics pipeline ---
    auto vertCode = vkutil::readSpirv("shaders/compiled/particle.vert.spv");
    auto fragCode = vkutil::readSpirv("shaders/compiled/particle.frag.spv");
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

    auto particlesBinding = Particle::getBindingDescription();
    auto attrDescs = Particle::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &particlesBinding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size()),
        .pVertexAttributeDescriptions = attrDescs.data(),
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        .topology = vk::PrimitiveTopology::ePointList,
        .primitiveRestartEnable = vk::False,
    };
    vk::PipelineViewportStateCreateInfo viewportStateInfo{
        .viewportCount = 1,
        .scissorCount = 1,
    };
    vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
        .rasterizationSamples = ctx.msaaSamples,
        .sampleShadingEnable = vk::False,
    };
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
        .logicOpEnable = vk::False,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
        .depthTestEnable = vk::False,
        .depthWriteEnable = vk::False,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    particlePipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                                      .setLayoutCount = 0,
                                                                  });

    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain = {
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
                    .layout = *particlePipelineLayout,
                },
                {
                    .colorAttachmentCount = 1,
                    .pColorAttachmentFormats = &swapchain.surfaceFormat.format,
                    .depthAttachmentFormat = depthFormat,
                },
            };

    particlePipeline = vk::raii::Pipeline(ctx.device, nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "Particle pipeline: created\n";
}

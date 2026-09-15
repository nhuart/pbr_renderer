#include "renderer/material.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "renderer/ibl_environment.hpp"
#include "renderer/shadow_map.hpp"
#include "renderer/texture_atlas.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

void MaterialInstance::updateUBO(uint32_t frameIndex, UniformBufferObject const& ubo) {
    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

Material::Material(VulkanContext const& ctx, Swapchain const& swapchain,
        std::string const& vertexShaderFilename, std::string const& fragmentShaderFilename,
        bool doubleSided) {
    constexpr uint32_t maxInstances = 64;
    // Descriptor set layout — 9 bindings: UBO, albedo, lights, irradiance, prefilter, BRDF LUT,
    // normal map, shadow map sampler, shadow UBO
    std::array<vk::DescriptorSetLayoutBinding, 9> bindings{ {
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 2,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 3,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 4,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 5,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 6,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 7,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        {
            .binding = 8,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
    } };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    // Descriptor pool sized for all instances upfront
    auto setCount = maxInstances * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = setCount * 3 }, // UBO + lights + shadowUBO
        { .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount =
                    setCount * 6 }, // albedo + irradiance + prefilter + brdfLut + normalMap + shadowMap
    } };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = setCount,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });

    // Graphics pipeline
    auto vertCode = vkutil::readSpirv("shaders/compiled/" + vertexShaderFilename + ".vert.spv");
    auto fragCode = vkutil::readSpirv("shaders/compiled/" + fragmentShaderFilename + ".frag.spv");
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

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data(),
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
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
        .cullMode = doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
        .rasterizationSamples = ctx.msaaSamples,
        .sampleShadingEnable = vk::True,
        .minSampleShading = 0.2f,
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
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    vk::DescriptorSetLayout dslHandle = *descriptorSetLayout;
    pipelineLayout = vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                              .setLayoutCount = 1,
                                                              .pSetLayouts = &dslHandle,
                                                              .pushConstantRangeCount = 0,
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
                    .layout = *pipelineLayout,
                    .renderPass = nullptr,
                },
                {
                    .colorAttachmentCount = 1,
                    .pColorAttachmentFormats = &swapchain.surfaceFormat.format,
                    .depthAttachmentFormat = depthFormat,
                },
            };

    pipeline = vk::raii::Pipeline(ctx.device, nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    std::cout << "Graphics pipeline: created\n";
}

MaterialInstance Material::createInstance(VulkanContext const& ctx, TextureAtlas const& texture,
        vk::raii::Buffer const& lightBuffer, IblEnvironment const* ibl,
        TextureAtlas const* normalMap, ShadowMap const* shadowMap) const {
    MaterialInstance inst;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto [buf, mem] = vkutil::createBuffer(ctx, sizeof(UniformBufferObject),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
        inst.uniformBuffersMapped.push_back(mem.mapMemory(0, sizeof(UniformBufferObject)));
        inst.uniformBuffers.push_back(std::move(buf));
        inst.uniformBuffersMemory.push_back(std::move(mem));
    }

    vk::DescriptorImageInfo albedoInfo{
        .sampler = *texture.sampler,
        .imageView = *texture.imageView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    inst.descriptorSets = vk::raii::DescriptorSets(ctx.device,
            vk::DescriptorSetAllocateInfo{
                .descriptorPool = *descriptorPool,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });

    // Fallback for IBL bindings when no IBL is present — keeps validation happy
    vk::DescriptorImageInfo irradianceInfo = albedoInfo;
    vk::DescriptorImageInfo prefilterInfo = albedoInfo;
    vk::DescriptorImageInfo brdfLutInfo = albedoInfo;

    if (ibl) {
        irradianceInfo = { *ibl->irradiance.sampler, *ibl->irradiance.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
        prefilterInfo = { *ibl->prefilter.sampler, *ibl->prefilter.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
        brdfLutInfo = { *ibl->brdfLut.sampler, *ibl->brdfLut.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
    }

    vk::DescriptorImageInfo normalMapInfo = albedoInfo;
    if (normalMap) {
        normalMapInfo = { *normalMap->sampler, *normalMap->imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DescriptorBufferInfo uboInfo{
            .buffer = *inst.uniformBuffers[i],
            .offset = 0,
            .range = sizeof(UniformBufferObject),
        };
        vk::DescriptorBufferInfo lightInfo{
            .buffer = *lightBuffer,
            .offset = 0,
            .range = sizeof(LightUBO),
        };

        // Shadow map bindings — use dummy albedo/UBO when no shadow map is present
        vk::DescriptorImageInfo shadowMapInfo = albedoInfo;
        vk::DescriptorBufferInfo shadowUboInfo{
            .buffer = *inst.uniformBuffers[i], // dummy — same UBO, won't be read without USE_SHADOW
            .offset = 0,
            .range = sizeof(UniformBufferObject),
        };
        if (shadowMap) {
            shadowMapInfo = { *shadowMap->sampler, *shadowMap->imageView,
                vk::ImageLayout::eShaderReadOnlyOptimal };
            shadowUboInfo.buffer = *shadowMap->fragmentUbo[i].buffer;
            shadowUboInfo.offset = 0;
            shadowUboInfo.range = sizeof(ShadowUBO);
        }

        std::array<vk::WriteDescriptorSet, 9> writes{ {
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uboInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &albedoInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &lightInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 3,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &irradianceInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 4,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &prefilterInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 5,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &brdfLutInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 6,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &normalMapInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 7,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &shadowMapInfo,
            },
            {
                .dstSet = *inst.descriptorSets[i],
                .dstBinding = 8,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &shadowUboInfo,
            },
        } };
        ctx.device.updateDescriptorSets(writes, {});
    }

    return inst;
}

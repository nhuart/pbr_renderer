#include "renderer/material.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "renderer/ao_pipeline.hpp"
#include "renderer/ibl_environment.hpp"
#include "renderer/shadow_pipeline.hpp"
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
        ShaderFeatures features, bool doubleSided) {
    constexpr uint32_t maxInstances = 64;

    // Build descriptor set layout from active features only
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    auto addUbo = [&](uint32_t binding, vk::ShaderStageFlags stages) {
        bindings.push_back({ .binding = binding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = stages });
    };
    auto addSampler = [&](uint32_t binding) {
        bindings.push_back({ .binding = binding,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment });
    };

    addUbo(0, vk::ShaderStageFlagBits::eVertex |
                      vk::ShaderStageFlagBits::eFragment); // per-instance UBO
    addSampler(1);                                         // albedo
    addUbo(2, vk::ShaderStageFlagBits::eFragment);         // lights
    if (hasFeature(features, ShaderFeatures::Ibl)) {
        addUbo(3, vk::ShaderStageFlagBits::eFragment); // SH irradiance
        addSampler(4);                                 // prefilter
        addSampler(5);                                 // BRDF LUT
    }
    if (hasFeature(features, ShaderFeatures::NormalMap)) {
        addSampler(6); // normal map
    }
    if (hasFeature(features, ShaderFeatures::HardShadow) ||
            hasFeature(features, ShaderFeatures::PcfShadow)) {
        addSampler(7);                                 // shadow map
        addUbo(8, vk::ShaderStageFlagBits::eFragment); // shadow UBO
    }
    if (hasFeature(features, ShaderFeatures::Ao)) {
        addSampler(9); // AO map
    }

    descriptorSetLayout = vk::raii::DescriptorSetLayout(ctx.device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });

    // Pool sized for active bindings only
    auto setCount = maxInstances * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    uint32_t uboCount = 2;     // binding 0 + binding 2 always present
    uint32_t samplerCount = 1; // binding 1 always present
    if (hasFeature(features, ShaderFeatures::Ibl)) {
        samplerCount += 2;
        uboCount += 1;
    }
    if (hasFeature(features, ShaderFeatures::NormalMap)) {
        samplerCount += 1;
    }
    if (hasFeature(features, ShaderFeatures::HardShadow) ||
            hasFeature(features, ShaderFeatures::PcfShadow)) {
        samplerCount += 1;
        uboCount += 1;
    }
    if (hasFeature(features, ShaderFeatures::Ao)) {
        samplerCount += 1;
    }
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = setCount * uboCount },
        { .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = setCount * samplerCount },
    };
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
        TextureAtlas const* normalMap, ShadowPipeline const* shadowMap,
        AoPipeline const* aoPipeline) const {
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

    if (ibl) {
        auto [buf, mem] =
                vkutil::createBuffer(ctx, sizeof(IblSHUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);
        void* mapped = mem.mapMemory(0, sizeof(IblSHUBO));
        memcpy(mapped, &ibl->sh, sizeof(IblSHUBO));
        mem.unmapMemory();
        inst.shBuffer = std::move(buf);
        inst.shBufferMemory = std::move(mem);
    }

    // Build image infos for optional features (only used when present)
    vk::DescriptorImageInfo prefilterInfo, brdfLutInfo;
    if (ibl) {
        prefilterInfo = { *ibl->prefilter.sampler, *ibl->prefilter.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
        brdfLutInfo = { *ibl->brdfLut.sampler, *ibl->brdfLut.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
    }

    vk::DescriptorImageInfo normalMapInfo;
    if (normalMap) {
        normalMapInfo = { *normalMap->sampler, *normalMap->imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal };
    }

    vk::DescriptorImageInfo aoMapInfo;
    if (aoPipeline) {
        aoMapInfo = { aoPipeline->finalAoSampler(), aoPipeline->finalAoView(),
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

        // Collect all infos first so their addresses stay valid for updateDescriptorSets
        std::vector<vk::DescriptorBufferInfo> uboInfos;
        std::vector<vk::DescriptorImageInfo> imageInfos;

        uboInfos.push_back(uboInfo);
        uboInfos.push_back(lightInfo);
        if (ibl) {
            uboInfos.push_back({ *inst.shBuffer, 0, sizeof(IblSHUBO) });
        }
        imageInfos.push_back(albedoInfo);
        if (ibl) {
            imageInfos.push_back(prefilterInfo);
            imageInfos.push_back(brdfLutInfo);
        }
        if (normalMap) {
            imageInfos.push_back(normalMapInfo);
        }
        if (shadowMap) {
            imageInfos.push_back({ *shadowMap->sampler, *shadowMap->imageView,
                vk::ImageLayout::eShaderReadOnlyOptimal });
            uboInfos.push_back({ *shadowMap->fragmentUbo[i].buffer, 0, sizeof(ShadowUBO) });
        }
        if (aoPipeline) {
            imageInfos.push_back(aoMapInfo);
        }

        // Build writes pointing into the stable vectors above
        uint32_t uboIdx = 0;
        uint32_t imgIdx = 0;
        std::vector<vk::WriteDescriptorSet> writes;
        auto writeUbo = [&](uint32_t binding) {
            writes.push_back({ .dstSet = *inst.descriptorSets[i],
                .dstBinding = binding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uboInfos[uboIdx++] });
        };
        auto writeSampler = [&](uint32_t binding) {
            writes.push_back({ .dstSet = *inst.descriptorSets[i],
                .dstBinding = binding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfos[imgIdx++] });
        };

        writeUbo(0);
        writeSampler(1);
        writeUbo(2);
        if (ibl) {
            writeUbo(3);
            writeSampler(4);
            writeSampler(5);
        }
        if (normalMap) {
            writeSampler(6);
        }
        if (shadowMap) {
            writeSampler(7);
            writeUbo(8);
        }
        if (aoPipeline) {
            writeSampler(9);
        }

        ctx.device.updateDescriptorSets(writes, {});
    }

    return inst;
}

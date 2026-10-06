#include "renderer/material.hpp"
#include "core/config.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/swapchain.hpp"
#include "core/vulkan_logging.hpp"
#include "renderer/vertex.hpp"

#include <array>
#include <iostream>

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
    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "Graphics pipeline: created\n";
    }
}

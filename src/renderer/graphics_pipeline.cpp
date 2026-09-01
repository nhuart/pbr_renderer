#include "core/application.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

std::vector<char> Renderer::readFile(std::string const& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + filename);
  }
  std::vector<char> buffer(static_cast<size_t>(file.tellg()));
  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  return buffer;
}

vk::raii::ShaderModule Renderer::createShaderModule(std::vector<char> const& code) const {
  return {device, vk::ShaderModuleCreateInfo{
      .codeSize = code.size(),
      .pCode    = std::bit_cast<uint32_t const*>(code.data()),
  }};
}

void Renderer::createGraphicsPipeline() {
  auto vertCode = readFile("shaders/compiled/triangle.vert.spv");
  auto fragCode = readFile("shaders/compiled/triangle.frag.spv");

  vk::raii::ShaderModule vertModule = createShaderModule(vertCode);
  vk::raii::ShaderModule fragModule = createShaderModule(fragCode);

  std::array shaderStages = {
      vk::PipelineShaderStageCreateInfo{
          .stage  = vk::ShaderStageFlagBits::eVertex,
          .module = *vertModule,
          .pName  = "main",
      },
      vk::PipelineShaderStageCreateInfo{
          .stage  = vk::ShaderStageFlagBits::eFragment,
          .module = *fragModule,
          .pName  = "main",
      },
  };

  std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates    = dynamicStates.data(),
  };

  auto bindingDescription   = Vertex::getBindingDescription();
  auto attributeDescriptions = Vertex::getAttributeDescriptions();
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
      .vertexBindingDescriptionCount   = 1,
      .pVertexBindingDescriptions      = &bindingDescription,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
      .pVertexAttributeDescriptions    = attributeDescriptions.data(),
  };

  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
      .topology               = vk::PrimitiveTopology::eTriangleList,
      .primitiveRestartEnable = vk::False,
  };
  vk::PipelineViewportStateCreateInfo viewportStateInfo{
      .viewportCount = 1,
      .scissorCount  = 1,
  };
  vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
      .depthClampEnable        = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode             = vk::PolygonMode::eFill,
      .cullMode                = vk::CullModeFlagBits::eBack,
      .frontFace               = vk::FrontFace::eCounterClockwise,
      .depthBiasEnable         = vk::False,
      .lineWidth               = 1.0f,
  };
  vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
      .rasterizationSamples = msaaSamples,
      .sampleShadingEnable  = vk::True,
      .minSampleShading     = 0.2f,
  };
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable    = vk::False,
      .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };
  vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
      .logicOpEnable  = vk::False,
      .attachmentCount = 1,
      .pAttachments   = &colorBlendAttachment,
  };
  vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
      .depthTestEnable       = vk::True,
      .depthWriteEnable      = vk::True,
      .depthCompareOp        = vk::CompareOp::eLess,
      .depthBoundsTestEnable = vk::False,
      .stencilTestEnable     = vk::False,
  };

  vk::DescriptorSetLayout dslHandle = *descriptorSetLayout;
  pipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
      .setLayoutCount         = 1,
      .pSetLayouts            = &dslHandle,
      .pushConstantRangeCount = 0,
  });

  vk::Format depthFormat = findDepthFormat();
  vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
      {
          .stageCount          = static_cast<uint32_t>(shaderStages.size()),
          .pStages             = shaderStages.data(),
          .pVertexInputState   = &vertexInputInfo,
          .pInputAssemblyState = &inputAssemblyInfo,
          .pViewportState      = &viewportStateInfo,
          .pRasterizationState = &rasterizerInfo,
          .pMultisampleState   = &multisamplingInfo,
          .pDepthStencilState  = &depthStencilInfo,
          .pColorBlendState    = &colorBlendingInfo,
          .pDynamicState       = &dynamicStateInfo,
          .layout              = *pipelineLayout,
          .renderPass          = nullptr,
      },
      {
          .colorAttachmentCount  = 1,
          .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
          .depthAttachmentFormat = depthFormat,
      },
  };

  graphicsPipeline =
      vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  std::cout << "Graphics pipeline: created\n";
}

void Renderer::createParticlePipeline() {
  auto vertCode = readFile("shaders/compiled/particle.vert.spv");
  auto fragCode = readFile("shaders/compiled/particle.frag.spv");

  vk::raii::ShaderModule vertModule = createShaderModule(vertCode);
  vk::raii::ShaderModule fragModule = createShaderModule(fragCode);

  std::array shaderStages = {
      vk::PipelineShaderStageCreateInfo{
          .stage  = vk::ShaderStageFlagBits::eVertex,
          .module = *vertModule,
          .pName  = "main",
      },
      vk::PipelineShaderStageCreateInfo{
          .stage  = vk::ShaderStageFlagBits::eFragment,
          .module = *fragModule,
          .pName  = "main",
      },
  };

  std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates    = dynamicStates.data(),
  };

  auto bindingDesc = Particle::getBindingDescription();
  auto attrDescs   = Particle::getAttributeDescriptions();
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
      .vertexBindingDescriptionCount   = 1,
      .pVertexBindingDescriptions      = &bindingDesc,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size()),
      .pVertexAttributeDescriptions    = attrDescs.data(),
  };

  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
      .topology               = vk::PrimitiveTopology::ePointList,
      .primitiveRestartEnable = vk::False,
  };
  vk::PipelineViewportStateCreateInfo viewportStateInfo{
      .viewportCount = 1,
      .scissorCount  = 1,
  };
  vk::PipelineRasterizationStateCreateInfo rasterizerInfo{
      .depthClampEnable        = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode             = vk::PolygonMode::eFill,
      .cullMode                = vk::CullModeFlagBits::eNone,
      .frontFace               = vk::FrontFace::eCounterClockwise,
      .depthBiasEnable         = vk::False,
      .lineWidth               = 1.0f,
  };
  vk::PipelineMultisampleStateCreateInfo multisamplingInfo{
      .rasterizationSamples = msaaSamples,
      .sampleShadingEnable  = vk::False,
  };
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable           = vk::True,
      .srcColorBlendFactor   = vk::BlendFactor::eSrcAlpha,
      .dstColorBlendFactor   = vk::BlendFactor::eOneMinusSrcAlpha,
      .colorBlendOp          = vk::BlendOp::eAdd,
      .srcAlphaBlendFactor   = vk::BlendFactor::eOne,
      .dstAlphaBlendFactor   = vk::BlendFactor::eZero,
      .alphaBlendOp          = vk::BlendOp::eAdd,
      .colorWriteMask        = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };
  vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
      .logicOpEnable  = vk::False,
      .attachmentCount = 1,
      .pAttachments   = &colorBlendAttachment,
  };
  vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
      .depthTestEnable       = vk::False,
      .depthWriteEnable      = vk::False,
      .depthCompareOp        = vk::CompareOp::eLess,
      .depthBoundsTestEnable = vk::False,
      .stencilTestEnable     = vk::False,
  };

  particlePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
      .setLayoutCount = 0,
  });

  vk::Format depthFormat = findDepthFormat();
  vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
      {
          .stageCount          = static_cast<uint32_t>(shaderStages.size()),
          .pStages             = shaderStages.data(),
          .pVertexInputState   = &vertexInputInfo,
          .pInputAssemblyState = &inputAssemblyInfo,
          .pViewportState      = &viewportStateInfo,
          .pRasterizationState = &rasterizerInfo,
          .pMultisampleState   = &multisamplingInfo,
          .pDepthStencilState  = &depthStencilInfo,
          .pColorBlendState    = &colorBlendingInfo,
          .pDynamicState       = &dynamicStateInfo,
          .layout              = *particlePipelineLayout,
      },
      {
          .colorAttachmentCount  = 1,
          .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
          .depthAttachmentFormat = depthFormat,
      },
  };

  particlePipeline =
      vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  std::cout << "Particle pipeline: created\n";
}

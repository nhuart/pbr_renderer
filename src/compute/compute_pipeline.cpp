#include "core/application.hpp"

#include <array>
#include <iostream>

void Renderer::createComputeDescriptorSetLayout() {
  std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
      {
          .binding         = 0,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .descriptorCount = 1,
          .stageFlags      = vk::ShaderStageFlagBits::eCompute,
      },
      {
          .binding         = 1,
          .descriptorType  = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags      = vk::ShaderStageFlagBits::eCompute,
      },
      {
          .binding         = 2,
          .descriptorType  = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags      = vk::ShaderStageFlagBits::eCompute,
      },
  }};
  computeDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo{
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings    = bindings.data(),
  });
}

void Renderer::createComputePipeline() {
  auto compCode = readFile("shaders/compiled/particle.comp.spv");
  vk::raii::ShaderModule compModule = createShaderModule(compCode);

  vk::PipelineShaderStageCreateInfo compStageInfo{
      .stage  = vk::ShaderStageFlagBits::eCompute,
      .module = *compModule,
      .pName  = "main",
  };

  vk::DescriptorSetLayout dslHandle = *computeDescriptorSetLayout;
  computePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
      .setLayoutCount = 1,
      .pSetLayouts    = &dslHandle,
  });

  computePipeline = vk::raii::Pipeline(device, nullptr, vk::ComputePipelineCreateInfo{
      .stage  = compStageInfo,
      .layout = *computePipelineLayout,
  });
  std::cout << "Compute pipeline: created\n";
}

void Renderer::createComputeDescriptorSets() {
  std::array poolSizes = {
      vk::DescriptorPoolSize{
          .type            = vk::DescriptorType::eUniformBuffer,
          .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
      },
      vk::DescriptorPoolSize{
          .type            = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2,
      },
  };
  computeDescriptorPool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo{
      .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes    = poolSizes.data(),
  });

  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *computeDescriptorSetLayout);
  computeDescriptorSets = vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo{
      .descriptorPool     = *computeDescriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts        = layouts.data(),
  });

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DescriptorBufferInfo uboInfo{
        .buffer = *computeUniformBuffers[i],
        .offset = 0,
        .range  = sizeof(ComputeUBO),
    };
    int prevFrame = (i - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
    vk::DescriptorBufferInfo ssboLastInfo{
        .buffer = *shaderStorageBuffers[prevFrame],
        .offset = 0,
        .range  = sizeof(Particle) * PARTICLE_COUNT,
    };
    vk::DescriptorBufferInfo ssboCurrInfo{
        .buffer = *shaderStorageBuffers[i],
        .offset = 0,
        .range  = sizeof(Particle) * PARTICLE_COUNT,
    };
    std::array<vk::WriteDescriptorSet, 3> writes{{
        {
            .dstSet          = *computeDescriptorSets[i],
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &uboInfo,
        },
        {
            .dstSet          = *computeDescriptorSets[i],
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &ssboLastInfo,
        },
        {
            .dstSet          = *computeDescriptorSets[i],
            .dstBinding      = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &ssboCurrInfo,
        },
    }};
    device.updateDescriptorSets(writes, {});
  }
}

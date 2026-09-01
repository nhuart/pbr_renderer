#include "core/application.hpp"

#include <array>
#include <iostream>

void Renderer::createDescriptorSetLayout() {
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
    descriptorSetLayout = vk::raii::DescriptorSetLayout(device,
            vk::DescriptorSetLayoutCreateInfo{
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            });
}

void Renderer::setupGameObjects() {
    for (auto const& instance: scene.meshInstances) {
        GameObject obj;
        obj.position = instance.position;
        obj.rotation = glm::radians(instance.rotation);
        obj.scale = instance.scale;
        gameObjects.push_back(std::move(obj));
    }
    std::cout << "Game objects: " << gameObjects.size() << " created\n";
}

void Renderer::createDescriptorPool() {
    auto objectCount = static_cast<uint32_t>(gameObjects.size());
    auto setCount = objectCount * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<vk::DescriptorPoolSize, 2> poolSizes{ {
        {
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = setCount,
        },
        {
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = setCount,
        },
    } };
    descriptorPool = vk::raii::DescriptorPool(device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = setCount,
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data(),
            });
}

void Renderer::createDescriptorSets() {
    vk::DescriptorImageInfo imageInfo{
        .sampler = *textureSampler,
        .imageView = *textureImageView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    for (auto& obj: gameObjects) {
        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
        obj.descriptorSets = vk::raii::DescriptorSets(device,
                vk::DescriptorSetAllocateInfo{
                    .descriptorPool = *descriptorPool,
                    .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                    .pSetLayouts = layouts.data(),
                });

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = *obj.uniformBuffers[i],
                .offset = 0,
                .range = sizeof(UniformBufferObject),
            };
            std::array<vk::WriteDescriptorSet, 2> descriptorWrites{ {
                {
                    .dstSet = *obj.descriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfo,
                },
                {
                    .dstSet = *obj.descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo,
                },
            } };
            device.updateDescriptorSets(descriptorWrites, {});
        }
    }
}

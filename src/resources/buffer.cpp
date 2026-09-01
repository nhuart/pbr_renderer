#include "core/application.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

uint32_t Renderer::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
                (memProperties.memoryTypes.at(i).propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> Renderer::createBuffer(vk::DeviceSize size,
        vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
    vk::raii::Buffer buffer(device, vk::BufferCreateInfo{
                                        .size = size,
                                        .usage = usage,
                                        .sharingMode = vk::SharingMode::eExclusive,
                                    });

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::raii::DeviceMemory memory(device,
            vk::MemoryAllocateInfo{
                .allocationSize = memRequirements.size,
                .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
            });
    buffer.bindMemory(*memory, 0);
    return { std::move(buffer), std::move(memory) };
}

void Renderer::copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer,
        vk::DeviceSize size) {
    vk::raii::CommandBuffer cmd =
            std::move(device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                                                        .commandPool = *commandPool,
                                                        .level = vk::CommandBufferLevel::ePrimary,
                                                        .commandBufferCount = 1,
                                                    })
                              .front());

    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    cmd.copyBuffer(*srcBuffer, *dstBuffer,
            vk::BufferCopy{
                .srcOffset = 0,
                .dstOffset = 0,
                .size = size,
            });
    cmd.end();

    vk::CommandBuffer cmdHandle = *cmd;
    graphicsQueue.submit(
            vk::SubmitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdHandle,
            },
            nullptr);
    graphicsQueue.waitIdle();
}

void Renderer::createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    auto [stagingBuffer, stagingMemory] = createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    std::tie(vertexBuffer, vertexBufferMemory) = createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
}

void Renderer::createIndexBuffer() {
    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    auto [stagingBuffer, stagingMemory] = createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    std::tie(indexBuffer, indexBufferMemory) = createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);
}

void Renderer::createUniformBuffers() {
    for (auto& obj: gameObjects) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            auto [buffer, memory] = createBuffer(sizeof(UniformBufferObject),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent);
            obj.uniformBuffersMapped.push_back(memory.mapMemory(0, sizeof(UniformBufferObject)));
            obj.uniformBuffers.push_back(std::move(buffer));
            obj.uniformBuffersMemory.push_back(std::move(memory));
        }
    }
}

void Renderer::createShaderStorageBuffers() {
    std::default_random_engine rndEngine(static_cast<unsigned>(time(nullptr)));
    std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

    std::vector<Particle> particles(PARTICLE_COUNT);
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

    vk::DeviceSize bufferSize = sizeof(Particle) * PARTICLE_COUNT;

    auto [stagingBuffer, stagingMemory] = createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingMemory.mapMemory(0, bufferSize);
    memcpy(data, particles.data(), static_cast<size_t>(bufferSize));
    stagingMemory.unmapMemory();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto [ssbo, ssboMemory] = createBuffer(bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
        copyBuffer(stagingBuffer, ssbo, bufferSize);
        shaderStorageBuffers.push_back(std::move(ssbo));
        shaderStorageBuffersMemory.push_back(std::move(ssboMemory));
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto [uboBuf, uboMemory] =
                createBuffer(sizeof(ComputeUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);
        computeUniformBuffersMapped.push_back(uboMemory.mapMemory(0, sizeof(ComputeUBO)));
        computeUniformBuffers.push_back(std::move(uboBuf));
        computeUniformBuffersMemory.push_back(std::move(uboMemory));
    }

    std::cout << "Shader storage buffers: " << PARTICLE_COUNT << " particles, "
              << MAX_FRAMES_IN_FLIGHT << " SSBO pairs\n";
}

void Renderer::updateUniformBuffer() {
    static auto startTime = std::chrono::high_resolution_clock::now();
    static auto lastTime = startTime;
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count() * 1000.0f;
    lastTime = currentTime;

    glm::mat4 view =
            glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
            static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height),
            0.1f, 10.0f);
    proj[1][1] *= -1; // GLM uses OpenGL clip space (Y up); Vulkan is Y down.

    for (auto& obj: gameObjects) {
        obj.rotation.z = time * glm::radians(15.0f);
        UniformBufferObject ubo{
            .model = obj.getModelMatrix(),
            .view = view,
            .proj = proj,
        };
        memcpy(obj.uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
    }

    ComputeUBO cubo{ .deltaTime = deltaTime };
    memcpy(computeUniformBuffersMapped[frameIndex], &cubo, sizeof(cubo));
}

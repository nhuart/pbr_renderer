#include "renderer/shadow_pipeline.hpp"
#include "core/command_service.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "core/vulkan_logging.hpp"
#include "renderer/game_object.hpp"
#include "renderer/gpu_types.hpp"
#include "renderer/mesh_buffer.hpp"
#include "renderer/vertex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

constexpr float SHADOW_BOUNDS_MARGIN = 0.25f;
constexpr float MIN_SHADOW_NEAR_DISTANCE = 0.1f;
constexpr float MIN_SHADOW_DEPTH_RANGE = 1.0f;
constexpr float LIGHT_CAMERA_OFFSET = 2.0f;
constexpr float MAX_VIEW_UP_ABS_DOT = 0.99f;
constexpr float FALLBACK_BOUNDS_HALF_EXTENT = 1.0f;
constexpr float SHADOW_CONSTANT_DEPTH_BIAS = 1.0f;
constexpr float SHADOW_SLOPE_DEPTH_BIAS = 0.75f;

struct ShadowBounds {
    glm::vec3 min{ std::numeric_limits<float>::max() };
    glm::vec3 max{ std::numeric_limits<float>::lowest() };

    void expandToInclude(glm::vec3 const& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    float enclosingRadius() const { return glm::length(max - min) * 0.5f; }
};

void appendBoxCorners(std::vector<glm::vec3>& worldCorners, glm::vec3 boundsMin,
        glm::vec3 boundsMax, glm::mat4 const& localToWorld = glm::mat4(1.0f)) {
    for (uint32_t cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
        glm::vec3 localCorner((cornerIndex & 4) ? boundsMax.x : boundsMin.x,
                (cornerIndex & 2) ? boundsMax.y : boundsMin.y,
                (cornerIndex & 1) ? boundsMax.z : boundsMin.z);
        worldCorners.push_back(glm::vec3(localToWorld * glm::vec4(localCorner, 1.0f)));
    }
}

std::vector<glm::vec3> collectShadowWorldCorners(Scene const& scene, MeshBuffer const& meshBuffer,
        std::vector<GameObject> const& gameObjects) {
    std::vector<glm::vec3> corners;
    for (size_t instanceIndex = 0; instanceIndex < scene.meshInstances.size(); ++instanceIndex) {
        auto const& instance = scene.meshInstances[instanceIndex];
        if (!instance.castShadows && !instance.receiveShadows) {
            continue;
        }
        auto const& meshRange = meshBuffer.meshRanges.at(instance.gltfPath);
        if (meshRange.indexCount == 0) {
            continue;
        }
        appendBoxCorners(corners, meshRange.boundsMin, meshRange.boundsMax,
                gameObjects[instanceIndex].getModelMatrix());
    }
    if (scene.volumetric && scene.volumetric->bounded) {
        appendBoxCorners(corners, scene.volumetric->boundsMin, scene.volumetric->boundsMax);
    }
    if (corners.empty()) {
        appendBoxCorners(corners, glm::vec3(-FALLBACK_BOUNDS_HALF_EXTENT),
                glm::vec3(FALLBACK_BOUNDS_HALF_EXTENT));
    }
    return corners;
}

ShadowBounds calculateBounds(std::vector<glm::vec3> const& corners,
        glm::mat4 const& worldToTargetSpace = glm::mat4(1.0f)) {
    ShadowBounds bounds;
    for (auto const& worldCorner: corners) {
        bounds.expandToInclude(glm::vec3(worldToTargetSpace * glm::vec4(worldCorner, 1.0f)));
    }
    return bounds;
}

glm::vec3 chooseNonParallelUpVector(glm::vec3 const& normalizedViewDirection) {
    glm::vec3 const worldUp{ 0.0f, 1.0f, 0.0f };
    glm::vec3 const fallbackUp{ 1.0f, 0.0f, 0.0f };
    float alignmentWithWorldUp = std::abs(glm::dot(normalizedViewDirection, worldUp));

    // lookAt() cannot form a stable camera basis when the view and up vectors are nearly parallel.
    if (alignmentWithWorldUp > MAX_VIEW_UP_ABS_DOT) {
        return fallbackUp;
    }
    return worldUp;
}

glm::mat4 createFittedShadowProjection(ShadowBounds const& bounds) {
    // Fit casters, receivers and participating medium with a small margin for rasterization.
    float nearPlane = std::max(MIN_SHADOW_NEAR_DISTANCE, -bounds.max.z - SHADOW_BOUNDS_MARGIN);
    float farPlane =
            std::max(nearPlane + MIN_SHADOW_DEPTH_RANGE, -bounds.min.z + SHADOW_BOUNDS_MARGIN);
    glm::mat4 projection = glm::ortho(bounds.min.x - SHADOW_BOUNDS_MARGIN,
            bounds.max.x + SHADOW_BOUNDS_MARGIN, bounds.min.y - SHADOW_BOUNDS_MARGIN,
            bounds.max.y + SHADOW_BOUNDS_MARGIN, nearPlane, farPlane);
    // Invert the entire Y row, including its translation when the fitted view is off-center.
    projection[1][1] *= -1.0f;
    projection[3][1] *= -1.0f;
    return projection;
}

ShadowFrameBuffer createShadowUniformBuffer(VulkanContext const& ctx) {
    auto [buffer, memory] = vkutil::createBuffer(ctx, sizeof(ShadowUBO),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* mapped = memory.mapMemory(0, sizeof(ShadowUBO));
    return { .buffer = std::move(buffer), .memory = std::move(memory), .mapped = mapped };
}

void writeShadowUniformDescriptor(VulkanContext const& ctx, vk::DescriptorSet descriptorSet,
        ShadowFrameBuffer const& frameBuffer) {
    vk::DescriptorBufferInfo bufferInfo{ *frameBuffer.buffer, 0, sizeof(ShadowUBO) };
    vk::WriteDescriptorSet descriptorWrite{
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo,
    };
    ctx.device.updateDescriptorSets(descriptorWrite, {});
}

struct ShadowGraphicsState {
    std::array<vk::DynamicState, 2> dynamicStates{ vk::DynamicState::eViewport,
        vk::DynamicState::eScissor };
    vk::VertexInputBindingDescription vertexBinding = Vertex::getBindingDescription();
    vk::VertexInputAttributeDescription positionAttribute{
        .location = 0,
        .binding = 0,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(Vertex, pos),
    };
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &positionAttribute,
    };
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk::PipelineViewportStateCreateInfo viewport{ .viewportCount = 1, .scissorCount = 1 };
    vk::PipelineRasterizationStateCreateInfo rasterization{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        // Slope-scaled depth bias reduces self-shadowing on grazing surfaces.
        .depthBiasEnable = vk::True,
        .depthBiasConstantFactor = SHADOW_CONSTANT_DEPTH_BIAS,
        .depthBiasSlopeFactor = SHADOW_SLOPE_DEPTH_BIAS,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
    };
    vk::PipelineColorBlendStateCreateInfo colorBlend{};

    // Vulkan state contains pointers to this object's members, so it must stay at one address.
    ShadowGraphicsState() = default;
    ShadowGraphicsState(ShadowGraphicsState const&) = delete;
    ShadowGraphicsState& operator=(ShadowGraphicsState const&) = delete;
};

} // namespace

ShadowPipeline::ShadowPipeline(VulkanContext const& ctx, CommandService const& cmds,
        uint32_t objectCount) {
    createDepthImage(ctx, cmds);
    createFragmentUniformBuffers(ctx);
    createDescriptorResources(ctx, objectCount);
    createGraphicsPipeline(ctx);
    allocateObjectData(ctx, objectCount);
}

void ShadowPipeline::createDepthImage(VulkanContext const& ctx, CommandService const& cmds) {
    vk::Format depthFormat = vkutil::findDepthFormat(ctx);
    auto [depthImage, depthMemory] = vkutil::createImage(ctx, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1,
            vk::SampleCountFlagBits::e1, depthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    image = std::move(depthImage);
    memory = std::move(depthMemory);

    imageView = vkutil::createImageView(ctx, *image, depthFormat, vk::ImageAspectFlagBits::eDepth);
    createDepthSampler(ctx);
    initializeDepthImageLayout(cmds);
}

void ShadowPipeline::createDepthSampler(VulkanContext const& ctx) {
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
}

void ShadowPipeline::initializeDepthImageLayout(CommandService const& cmds) {
    // Match the initial layout declared when the shadow image is imported into the render graph.
    auto cmd = cmds.beginSingleTimeCommands();
    vkutil::transitionImageLayout(cmd, *image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eNone,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eDepth);
    cmds.endSingleTimeCommands(std::move(cmd));
}

void ShadowPipeline::createFragmentUniformBuffers(VulkanContext const& ctx) {
    for (auto& frameBuffer: fragmentUbo) {
        frameBuffer = createShadowUniformBuffer(ctx);
    }
}

void ShadowPipeline::createDescriptorResources(VulkanContext const& ctx, uint32_t objectCount) {
    vk::DescriptorSetLayoutBinding uniformBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    };
    descriptorSetLayout =
            vk::raii::DescriptorSetLayout(ctx.device, vk::DescriptorSetLayoutCreateInfo{
                                                          .bindingCount = 1,
                                                          .pBindings = &uniformBinding,
                                                      });

    uint32_t descriptorSetCount = std::max(objectCount, 1u) * MAX_FRAMES_IN_FLIGHT;
    vk::DescriptorPoolSize uniformPoolSize{
        .type = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = descriptorSetCount,
    };
    descriptorPool = vk::raii::DescriptorPool(ctx.device,
            vk::DescriptorPoolCreateInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = descriptorSetCount,
                .poolSizeCount = 1,
                .pPoolSizes = &uniformPoolSize,
            });

    vk::DescriptorSetLayout descriptorSetLayoutHandle = *descriptorSetLayout;
    pipelineLayout =
            vk::raii::PipelineLayout(ctx.device, vk::PipelineLayoutCreateInfo{
                                                     .setLayoutCount = 1,
                                                     .pSetLayouts = &descriptorSetLayoutHandle,
                                                 });
}

void ShadowPipeline::createGraphicsPipeline(VulkanContext const& ctx) {
    auto vertexShader =
            vkutil::createShaderModule(ctx, vkutil::readSpirv("shaders/compiled/shadow.vert.spv"));
    vk::PipelineShaderStageCreateInfo vertexStage{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = *vertexShader,
        .pName = "main",
    };

    ShadowGraphicsState state;
    vk::Format shadowDepthFormat = vkutil::findDepthFormat(ctx);
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
            pipelineChain = {
                {
                    .stageCount = 1,
                    .pStages = &vertexStage,
                    .pVertexInputState = &state.vertexInput,
                    .pInputAssemblyState = &state.inputAssembly,
                    .pViewportState = &state.viewport,
                    .pRasterizationState = &state.rasterization,
                    .pMultisampleState = &state.multisample,
                    .pDepthStencilState = &state.depthStencil,
                    .pColorBlendState = &state.colorBlend,
                    .pDynamicState = &state.dynamicState,
                    .layout = *pipelineLayout,
                },
                {
                    .depthAttachmentFormat = shadowDepthFormat,
                },
            };

    pipeline = vk::raii::Pipeline(ctx.device, nullptr,
            pipelineChain.get<vk::GraphicsPipelineCreateInfo>());
    if (vkutil::vulkanLoggingEnabled) {
        std::cout << "Shadow pipeline: created\n";
    }
}

void ShadowPipeline::allocateObjectData(VulkanContext const& ctx, uint32_t objectCount) {
    objects.clear();
    objects.resize(objectCount);
    for (auto& objectData: objects) {
        for (auto& frameBuffer: objectData.frames) {
            frameBuffer = createShadowUniformBuffer(ctx);
        }
        allocateObjectDescriptorSets(ctx, objectData);
    }
}

void ShadowPipeline::allocateObjectDescriptorSets(VulkanContext const& ctx,
        ShadowObjectData& objectData) {
    std::array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(*descriptorSetLayout);
    objectData.descriptorSets = vk::raii::DescriptorSets(ctx.device,
            vk::DescriptorSetAllocateInfo{
                .descriptorPool = *descriptorPool,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
            });
    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        writeShadowUniformDescriptor(ctx, *objectData.descriptorSets[frameIndex],
                objectData.frames[frameIndex]);
    }
}

void ShadowPipeline::updateLightSpaceMatrix(DirectionalLight const& light, Scene const& scene,
        MeshBuffer const& meshBuffer, std::vector<GameObject> const& gameObjects) {
    auto corners = collectShadowWorldCorners(scene, meshBuffer, gameObjects);
    auto worldBounds = calculateBounds(corners);
    glm::vec3 direction = glm::normalize(light.direction);
    glm::vec3 center = worldBounds.center();
    float distanceToCenter = worldBounds.enclosingRadius() + LIGHT_CAMERA_OFFSET;
    glm::mat4 lightView = glm::lookAt(center + direction * distanceToCenter, center,
            chooseNonParallelUpVector(direction));
    auto viewBounds = calculateBounds(corners, lightView);
    lightSpaceMatrix = createFittedShadowProjection(viewBounds) * lightView;
}

void ShadowPipeline::updateLightSpaceMatrix(SpotLight const& light) {
    glm::vec3 direction = glm::normalize(light.direction);
    glm::mat4 lightView = glm::lookAt(light.position, light.position + direction,
            chooseNonParallelUpVector(direction));
    float nearPlane = SPOT_SHADOW_NEAR_DISTANCE;
    // Cone angles are half-angles; the square shadow map covers the entire outer cone.
    glm::mat4 projection = glm::perspective(glm::radians(2.0f * light.outerConeAngle), 1.0f,
            nearPlane, light.range);
    // Flip GLM's projected Y axis for Vulkan's positive-height viewport and our face winding.
    projection[1][1] *= -1.0f;
    lightSpaceMatrix = projection * lightView;
}

void ShadowPipeline::updateObjectUBO(uint32_t objectIndex, uint32_t frameIndex,
        glm::mat4 const& model) {
    ShadowUBO ubo{ .lightSpaceTransform = lightSpaceMatrix * model };
    std::memcpy(objects[objectIndex].frames[frameIndex].mapped, &ubo, sizeof(ubo));
}

void ShadowPipeline::updateFragmentUBO(uint32_t frameIndex) {
    ShadowUBO ubo{ .lightSpaceTransform = lightSpaceMatrix, .shadowBias = shadowBias };
    std::memcpy(fragmentUbo[frameIndex].mapped, &ubo, sizeof(ubo));
}

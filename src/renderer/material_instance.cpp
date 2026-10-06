#include "renderer/material.hpp"

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/resource_allocator.hpp"
#include "renderer/ao_pipeline.hpp"
#include "renderer/ibl_environment.hpp"
#include "renderer/shadow_pipeline.hpp"
#include "renderer/texture_atlas.hpp"

#include <cstring>

void MaterialInstance::updateUBO(uint32_t frameIndex, UniformBufferObject const& ubo) {
    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
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

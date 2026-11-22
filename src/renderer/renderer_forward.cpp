#include "renderer/renderer.hpp"

#include <algorithm>
#include <array>

void Renderer::drawMesh(vk::raii::CommandBuffer const& cmd, MaterialBinding const& binding,
        MeshPrimitive const& primitive) const {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *binding.pipeline->pipeline);
    std::array sets{ *binding.materialInstance.descriptorSets[mFrameIndex],
        *binding.surface.descriptorSets[0] };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *binding.pipeline->pipelineLayout, 0,
            sets, {});
    cmd.drawIndexed(primitive.range.indexCount, 1, primitive.range.firstIndex, 0, 0);
}

void Renderer::drawSkybox(vk::raii::CommandBuffer const& cmd) const {
    if (!mSkyboxPipeline) {
        return;
    }
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mSkyboxPipeline->pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *mSkyboxPipeline->pipelineLayout, 0,
            *mSkyboxPipeline->descriptorSets[mFrameIndex], {});
    cmd.draw(3, 1, 0, 0);
}

void Renderer::drawParticles(vk::raii::CommandBuffer const& cmd) const {
    if (!mScene.particles) {
        return;
    }
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mParticlePipeline->particlePipeline);
    cmd.bindVertexBuffers(0, *mParticlePipeline->shaderStorageBuffers[mFrameIndex],
            { vk::DeviceSize{ 0 } });
    cmd.draw(mScene.particles->count, 1, 0, 0);
}

void Renderer::drawForwardScene(vk::raii::CommandBuffer const& cmd) const {
    for (auto const& object: mRenderObjects) {
        for (auto const& primitive: object.model->primitives) {
            drawMesh(cmd, object.materialBindings.at(primitive.materialIndex), primitive);
        }
    }
    drawParticles(cmd);
    drawSkybox(cmd);
}

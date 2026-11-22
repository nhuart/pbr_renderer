#include "renderer/renderer.hpp"

#include <algorithm>
#include <array>

namespace {

struct MeshDraw {
    MaterialBinding const* binding;
    MeshPrimitive const* primitive;
    float viewDepth;
};

struct TransparentDrawQueue {
    std::vector<MeshDraw> draws;

    void appendObject(RenderObject const& object, glm::mat4 const& modelView) {
        for (auto const& primitive: object.model->primitives) {
            auto const& binding = object.materialBindings.at(primitive.materialIndex);
            if (binding.pipelineKey.alphaMode != AlphaMode::Blend) {
                continue;
            }
            float viewDepth = -(modelView * glm::vec4(primitive.center, 1.0f)).z;
            draws.push_back({ &binding, &primitive, viewDepth });
        }
    }

    void sortBackToFront() {
        std::stable_sort(draws.begin(), draws.end(),
                [](MeshDraw const& farther, MeshDraw const& nearer) {
                    return farther.viewDepth > nearer.viewDepth;
                });
    }
};

TransparentDrawQueue buildTransparentDrawQueue(std::vector<RenderObject> const& objects,
        std::vector<GameObject> const& gameObjects, glm::mat4 const& view) {
    TransparentDrawQueue queue;
    for (auto const& object: objects) {
        glm::mat4 modelView = view * gameObjects[object.gameObjectIndex].getModelMatrix();
        queue.appendObject(object, modelView);
    }
    queue.sortBackToFront();
    return queue;
}

} // namespace

void Renderer::drawMesh(vk::raii::CommandBuffer const& cmd, MaterialBinding const& binding,
        MeshPrimitive const& primitive) const {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *binding.pipeline->pipeline);
    std::array sets{ *binding.materialInstance.descriptorSets[mFrameIndex],
        *binding.surface.descriptorSets[0] };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *binding.pipeline->pipelineLayout, 0,
            sets, {});
    if (hasFeature(binding.pipelineKey.features, ShaderFeatures::VolumetricFog)) {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *binding.pipeline->pipelineLayout,
                2, mVolumetricPipeline->frameDescriptorSet(mFrameIndex), {});
    }
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
    // Particles retain their existing overlay ordering after transparent mesh surfaces.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mParticlePipeline->particlePipeline);
    cmd.bindVertexBuffers(0, *mParticlePipeline->shaderStorageBuffers[mFrameIndex],
            { vk::DeviceSize{ 0 } });
    cmd.draw(mScene.particles->count, 1, 0, 0);
}

void Renderer::drawOpaqueScene(vk::raii::CommandBuffer const& cmd) const {
    for (auto const& object: mRenderObjects) {
        for (auto const& primitive: object.model->primitives) {
            auto const& binding = object.materialBindings.at(primitive.materialIndex);
            if (binding.pipelineKey.alphaMode != AlphaMode::Blend) {
                drawMesh(cmd, binding, primitive);
            }
        }
    }
    drawSkybox(cmd);
}

void Renderer::drawTransparentScene(vk::raii::CommandBuffer const& cmd) const {
    auto queue = buildTransparentDrawQueue(mRenderObjects, mGameObjects, mCamera.viewMatrix());
    for (auto const& draw: queue.draws) {
        drawMesh(cmd, *draw.binding, *draw.primitive);
    }
    drawParticles(cmd);
}

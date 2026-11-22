#include "renderer/renderer.hpp"

#include <algorithm>
#include <array>

namespace {

struct MeshDraw {
    MaterialBinding const* binding;
    MeshPrimitive const* primitive;
    float viewDepth;
};

struct ForwardDrawQueues {
    std::vector<MeshDraw> opaque;
    std::vector<MeshDraw> transparent;

    void appendObject(RenderObject const& object, glm::mat4 const& modelView) {
        for (auto const& primitive: object.model->primitives) {
            auto const& binding = object.materialBindings.at(primitive.materialIndex);
            auto& queue = binding.pipelineKey.alphaMode == AlphaMode::Blend ? transparent : opaque;
            float viewDepth = -(modelView * glm::vec4(primitive.center, 1.0f)).z;
            queue.push_back({ &binding, &primitive, viewDepth });
        }
    }

    void sortTransparentBackToFront() {
        std::stable_sort(transparent.begin(), transparent.end(),
                [](MeshDraw const& farther, MeshDraw const& nearer) {
                    return farther.viewDepth > nearer.viewDepth;
                });
    }
};

ForwardDrawQueues buildDrawQueues(std::vector<RenderObject> const& objects,
        std::vector<GameObject> const& gameObjects, glm::mat4 const& view) {
    ForwardDrawQueues queues;
    for (auto const& object: objects) {
        glm::mat4 modelView = view * gameObjects[object.gameObjectIndex].getModelMatrix();
        queues.appendObject(object, modelView);
    }
    queues.sortTransparentBackToFront();
    return queues;
}

} // namespace

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
    auto queues = buildDrawQueues(mRenderObjects, mGameObjects, mCamera.viewMatrix());
    for (auto const& draw: queues.opaque) {
        drawMesh(cmd, *draw.binding, *draw.primitive);
    }
    for (auto const& draw: queues.transparent) {
        drawMesh(cmd, *draw.binding, *draw.primitive);
    }
    drawParticles(cmd);
    drawSkybox(cmd);
}

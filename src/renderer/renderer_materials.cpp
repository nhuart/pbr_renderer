#include "renderer/renderer.hpp"

#include <utility>

void Renderer::prepareRenderObjects() {
    for (uint32_t index = 0; index < mScene.meshInstances.size(); ++index) {
        auto const& instance = mScene.meshInstances[index];
        RenderObject object{ .gameObjectIndex = index,
            .model = &mMeshBuffer->models.at(instance.gltfPath) };
        for (auto const& primitive: object.model->primitives) {
            auto [entry, inserted] = object.materialBindings.try_emplace(primitive.materialIndex);
            if (inserted) {
                entry->second.pipelineKey = resolveMaterialPipeline(instance,
                        object.model->materials.at(primitive.materialIndex));
            }
        }
        mRenderObjects.push_back(std::move(object));
    }
}

MaterialPipelineKey Renderer::resolveMaterialPipeline(MeshInstance const& instance,
        MaterialDefinition const& definition) const {
    MaterialPipelineKey key{ .vertexShader = instance.vertexShader,
        .fragmentShader = instance.fragmentShader,
        .doubleSided = definition.doubleSided };
    if (instance.fragmentShader != "pbr") {
        return key;
    }
    key.features = ShaderFeatures::BrdfLut;
    auto enableFeature = [&key](bool enabled, ShaderFeatures feature, char const* suffix) {
        if (enabled) {
            key.features = key.features | feature;
            key.fragmentShader += suffix;
        }
    };
    bool hasShadow = instance.receiveShadows && mShadowPipeline.has_value();
    bool hasPcf = hasShadow && mShadowPipeline->shadowType == ShadowType::PCF;
    enableFeature(mIblEnvironment.has_value(), ShaderFeatures::Ibl, "_ibl");
    enableFeature(instance.useNormalMap && definition.normalTexture.hasTexture(),
            ShaderFeatures::NormalMap, "_normal");
    enableFeature(hasPcf, ShaderFeatures::PcfShadow, "_pcf");
    enableFeature(hasShadow && !hasPcf, ShaderFeatures::HardShadow, "_shadow");
    // SAO and GTAO share the same surface-shader sampler and variant.
    enableFeature(mScene.sao.has_value() || mScene.gtao.has_value(), ShaderFeatures::Ao, "_sao");
    return key;
}

void Renderer::createMaterialPipelines() {
    std::map<MaterialPipelineKey, uint32_t> bindingCounts;
    for (auto const& object: mRenderObjects) {
        for (auto const& [index, binding]: object.materialBindings) {
            ++bindingCounts[binding.pipelineKey];
        }
    }
    for (auto const& [key, count]: bindingCounts) {
        mMaterialPipelines.emplace(key, Material(*mCtx, key, count));
    }
}

void Renderer::createMaterialBindings() {
    for (auto& object: mRenderObjects) {
        auto const& instance = mScene.meshInstances[object.gameObjectIndex];
        for (auto& [index, binding]: object.materialBindings) {
            auto const& definition = object.model->materials.at(index);
            binding.texture =
                    instance.texturePath.empty()
                            ? &mResources->getEmbeddedTexture(*mCtx, *mCmds, instance.gltfPath,
                                      *object.model, definition.baseColorTexture,
                                      TextureUsage::BaseColor)
                            : &mResources->getTexture(*mCtx, *mCmds, instance.texturePath);
            if (hasFeature(binding.pipelineKey.features, ShaderFeatures::NormalMap)) {
                binding.normalMap =
                        &mResources->getEmbeddedTexture(*mCtx, *mCmds, instance.gltfPath,
                                *object.model, definition.normalTexture, TextureUsage::NormalMap);
            }
            binding.pipeline = &mMaterialPipelines.at(binding.pipelineKey);
            createMaterialInstance(binding);
        }
    }
}

void Renderer::createMaterialInstance(MaterialBinding& binding) {
    auto features = binding.pipelineKey.features;
    bool hasShadow = hasFeature(features, ShaderFeatures::HardShadow) ||
                     hasFeature(features, ShaderFeatures::PcfShadow);
    MaterialResources resources{
        .texture = *binding.texture,
        .lightBuffer = mLightBuffer->buffer,
        .ibl = hasFeature(features, ShaderFeatures::Ibl) ? &*mIblEnvironment : nullptr,
        .brdfLut = hasFeature(features, ShaderFeatures::BrdfLut)
                           ? &mResources->getTexture(*mCtx, *mCmds, "textures/ibl/brdf_lut.ktx2")
                           : nullptr,
        .normalMap = binding.normalMap,
        .shadowMap = hasShadow ? &*mShadowPipeline : nullptr,
        .aoPipeline = hasFeature(features, ShaderFeatures::Ao) ? &*mAoPipeline : nullptr,
    };
    // Free old descriptors before allocating replacements from the exactly-sized pool.
    binding.materialInstance = MaterialInstance{};
    binding.materialInstance = binding.pipeline->createInstance(*mCtx, resources);
}

void Renderer::recreateAoMaterialInstances() {
    for (auto& object: mRenderObjects) {
        for (auto& [index, binding]: object.materialBindings) {
            if (hasFeature(binding.pipelineKey.features, ShaderFeatures::Ao)) {
                createMaterialInstance(binding);
            }
        }
    }
}

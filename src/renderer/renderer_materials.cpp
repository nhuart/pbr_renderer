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
        .doubleSided = definition.doubleSided,
        .alphaMode = definition.alphaMode };
    if (instance.fragmentShader == "pbr") {
        configurePbrFeatures(key, instance, definition);
    }
    bool usesFragmentFog =
            definition.alphaMode == AlphaMode::Blend && mScene.volumetric.has_value();
    key.enableFeatureIf(usesFragmentFog, ShaderFeatures::VolumetricFog, "_fog");
    return key;
}

void Renderer::configurePbrFeatures(MaterialPipelineKey& key, MeshInstance const& instance,
        MaterialDefinition const& definition) const {
    key.features = ShaderFeatures::BrdfLut;
    bool hasShadow = instance.receiveShadows && mShadowPipeline.has_value();
    bool hasPcf = hasShadow && mShadowPipeline->shadowType == ShadowType::PCF;
    key.enableFeatureIf(mIblEnvironment.has_value(), ShaderFeatures::Ibl, "_ibl");
    key.enableFeatureIf(instance.useNormalMap && definition.normalTexture.hasTexture(),
            ShaderFeatures::NormalMap, "_normal");
    key.enableFeatureIf(hasPcf, ShaderFeatures::PcfShadow, "_pcf");
    key.enableFeatureIf(hasShadow && !hasPcf, ShaderFeatures::HardShadow, "_shadow");
    // SAO and GTAO share the same surface-shader sampler and variant.
    bool receivesAo = definition.alphaMode != AlphaMode::Blend && (mScene.sao || mScene.gtao);
    key.enableFeatureIf(receivesAo, ShaderFeatures::Ao, "_sao");
}

void Renderer::createMaterialPipelines() {
    std::map<MaterialPipelineKey, uint32_t> bindingCounts;
    for (auto const& object: mRenderObjects) {
        for (auto const& [index, binding]: object.materialBindings) {
            ++bindingCounts[binding.pipelineKey];
        }
    }
    MaterialPassLayouts layouts{ .surface = *mMaterialSurfaces->layout,
        .volumetric = mVolumetricPipeline ? mVolumetricPipeline->materialDescriptorLayout()
                                          : vk::DescriptorSetLayout{} };
    for (auto const& [key, count]: bindingCounts) {
        mMaterialPipelines.emplace(key, Material(*mCtx, key, count, layouts));
    }
}

void Renderer::createMaterialBindings() {
    for (auto& object: mRenderObjects) {
        for (auto& [index, binding]: object.materialBindings) {
            createMaterialBinding(object, index, binding);
        }
    }
}

TextureAtlas const& Renderer::resolveBaseColorTexture(MeshInstance const& instance,
        ModelAsset const& model, TextureReference reference) {
    if (!instance.texturePath.empty()) {
        return mResources->getTexture(*mCtx, *mCmds, instance.texturePath);
    }
    return mResources->getEmbeddedTexture(*mCtx, *mCmds, instance.gltfPath, model, reference,
            TextureUsage::BaseColor);
}

void Renderer::createMaterialBinding(RenderObject const& object, uint32_t materialIndex,
        MaterialBinding& binding) {
    auto const& instance = mScene.meshInstances[object.gameObjectIndex];
    auto const& definition = object.model->materials.at(materialIndex);
    binding.texture =
            &resolveBaseColorTexture(instance, *object.model, definition.baseColorTexture);
    if (hasFeature(binding.pipelineKey.features, ShaderFeatures::NormalMap)) {
        binding.normalMap = &mResources->getEmbeddedTexture(*mCtx, *mCmds, instance.gltfPath,
                *object.model, definition.normalTexture, TextureUsage::NormalMap);
    }
    binding.pipeline = &mMaterialPipelines.at(binding.pipelineKey);
    createMaterialSurface(binding, instance, *object.model, definition);
    createMaterialInstance(binding);
}

void Renderer::createMaterialSurface(MaterialBinding& binding, MeshInstance const& instance,
        ModelAsset const& model, MaterialDefinition const& definition) {
    auto const& specularTexture = mResources->getEmbeddedTexture(*mCtx, *mCmds, instance.gltfPath,
            model, definition.specularGlossinessTexture, TextureUsage::SpecularGlossiness);
    SurfaceUniforms uniforms{ .specularGlossiness =
                                      glm::vec4(definition.specular, definition.glossiness),
        .alphaCutoff = definition.alphaCutoff,
        .alphaMode = static_cast<float>(definition.alphaMode),
        .workflow = static_cast<float>(definition.workflow),
        .opacityFactor = instance.baseColor.value_or(definition.baseColor).a };
    binding.surface = mMaterialSurfaces->create(*mCtx, uniforms,
            SurfaceTextures{ *binding.texture, specularTexture });
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

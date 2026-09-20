#include "mesh_loader.h"

#include <iostream>
#include <vector>

using namespace filament;
using namespace filament::gltfio;

void loadMesh(MeshLoaderContext const& loaderContext, MeshEntry& entry) {
    std::vector<uint8_t> fileData = loaderContext.appLoader->load(entry.path);
    if (fileData.empty()) {
        std::cerr << "Could not load: " << entry.path.c_str() << "\n";
        return;
    }

    entry.asset = loaderContext.gltfLoader->createAsset(fileData.data(), fileData.size());
    fileData.clear();
    if (!entry.asset) {
        std::cerr << "Could not parse: " << entry.path.c_str() << "\n";
        return;
    }

    ResourceConfiguration resourceConfig{};
    resourceConfig.engine = loaderContext.engine;

    entry.resourceLoader = new ResourceLoader(resourceConfig);
    entry.textureProvider = createStbProvider(loaderContext.engine);
    entry.resourceLoader->addTextureProvider("image/png", entry.textureProvider);

    size_t resourceCount = entry.asset->getResourceUriCount();
    for (size_t index = 0; index < resourceCount; ++index) {
        const char* resourceUri = entry.asset->getResourceUris()[index];
        utils::Path resourcePath = entry.path.getParent() + resourceUri;
        auto resourceData = loaderContext.appLoader->load(resourcePath);
        if (!resourceData.empty()) {
            auto ownedBuffer = std::make_shared<std::vector<uint8_t>>(std::move(resourceData));
            auto bufferDescriptor = ResourceLoader::BufferDescriptor::make(ownedBuffer->data(),
                    ownedBuffer->size(), [ownedBuffer](void*, size_t) {});
            entry.resourceLoader->addResourceData(resourceUri, std::move(bufferDescriptor));
        }
    }

    entry.resourceLoader->asyncBeginLoad(entry.asset);

    auto& transformManager = loaderContext.engine->getTransformManager();
    auto transformInstance = transformManager.getInstance(entry.asset->getRoot());
    transformManager.setTransform(transformInstance, entry.transform);
}

void applyMaterial(Engine& engine, MeshEntry const& entry) {
    auto& renderableManager = engine.getRenderableManager();
    size_t entityCount = entry.asset->getEntityCount();
    for (size_t entityIndex = 0; entityIndex < entityCount; ++entityIndex) {
        utils::Entity entity = entry.asset->getEntities()[entityIndex];
        auto renderableInstance = renderableManager.getInstance(entity);
        if (!renderableInstance) {
            continue;
        }
        renderableManager.setCastShadows(renderableInstance, entry.castShadows);
        renderableManager.setReceiveShadows(renderableInstance, entry.receiveShadows);

        size_t primitiveCount = renderableManager.getPrimitiveCount(renderableInstance);
        for (size_t primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex) {
            MaterialInstance* materialInstance =
                    renderableManager.getMaterialInstanceAt(renderableInstance, primitiveIndex);
            if (!materialInstance) {
                continue;
            }
            materialInstance->setParameter("baseColorFactor", entry.baseColor);
            materialInstance->setParameter("metallicFactor", entry.metallic);
            materialInstance->setParameter("roughnessFactor", entry.roughness);
            if (!entry.useNormalMap) {
                materialInstance->setParameter("normalScale", 0.0f);
            }
        }
    }
}

void addMeshToScene(Scene& scene, MeshEntry& entry) {
    entry.asset->addEntitiesToScene(scene, entry.asset->getEntities(),
            entry.asset->getEntityCount(), ~FilamentAsset::SceneMask(0));
}

void destroyMesh(AssetLoader& gltfLoader, MeshEntry& entry) {
    if (entry.asset) {
        gltfLoader.destroyAsset(entry.asset);
    }
    delete entry.resourceLoader;
    delete entry.textureProvider;
}

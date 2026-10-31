#include "renderer/gltf_model.hpp"
#include "renderer/gltf_tangent_baker.hpp"

#include <tiny_gltf.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, char const* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int addAttribute(tinygltf::Model& model, std::vector<float> const& values, int type,
        size_t components) {
    auto& buffer = model.buffers.front().data;
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = buffer.size();
    view.byteLength = values.size() * sizeof(float);
    view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    buffer.resize(buffer.size() + view.byteLength);
    memcpy(buffer.data() + view.byteOffset, values.data(), view.byteLength);
    tinygltf::Accessor accessor;
    accessor.bufferView = static_cast<int>(model.bufferViews.size());
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.type = type;
    accessor.count = values.size() / components;
    model.bufferViews.push_back(view);
    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size() - 1);
}

tinygltf::Model makeModel() {
    tinygltf::Model model;
    model.asset.version = "2.0";
    model.buffers.emplace_back();
    tinygltf::Primitive primitive;
    primitive.mode = TINYGLTF_MODE_TRIANGLES;
    primitive.attributes["POSITION"] =
            addAttribute(model, { 0, 0, 0, 1, 0, 0, 0, 1, 0, -1, 0, 0 }, TINYGLTF_TYPE_VEC3, 3);
    primitive.attributes["NORMAL"] =
            addAttribute(model, { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 }, TINYGLTF_TYPE_VEC3, 3);
    primitive.attributes["TEXCOORD_0"] =
            addAttribute(model, { 0, 0, 1, 0, 0, 1, 1, 0 }, TINYGLTF_TYPE_VEC2, 2);
    primitive.attributes["TEXCOORD_1"] =
            addAttribute(model, { 0, 0, 1, 0, 0, 1, 1, 0 }, TINYGLTF_TYPE_VEC2, 2);
    primitive.targets.push_back({ { "POSITION",
        addAttribute(model, { 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0, 4 }, TINYGLTF_TYPE_VEC3, 3) } });
    int indices = addAttribute(model, { 0, 1, 2, 0, 2, 3 }, TINYGLTF_TYPE_SCALAR, 1);
    auto& indexAccessor = model.accessors[indices];
    indexAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    auto& indexView = model.bufferViews[indexAccessor.bufferView];
    indexView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    uint32_t corners[] = { 0, 1, 2, 0, 2, 3 };
    memcpy(model.buffers[0].data.data() + indexView.byteOffset, corners, sizeof(corners));
    primitive.indices = indices;
    primitive.material = 0;
    tinygltf::Mesh mesh;
    mesh.primitives.push_back(primitive);
    primitive.material = 1;
    mesh.primitives.push_back(primitive); // No normal map: should not be baked.
    primitive.material = 0;
    primitive.attributes["TANGENT"] = addAttribute(model,
            { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 }, TINYGLTF_TYPE_VEC4, 4);
    mesh.primitives.push_back(primitive); // Existing authored tangents must survive unchanged.
    tinygltf::Primitive nonIndexed;
    nonIndexed.mode = TINYGLTF_MODE_TRIANGLES;
    nonIndexed.material = 0;
    nonIndexed.attributes["POSITION"] =
            addAttribute(model, { 0, 0, 0, 1, 0, 0, 0, 1, 0 }, TINYGLTF_TYPE_VEC3, 3);
    nonIndexed.attributes["NORMAL"] =
            addAttribute(model, { 0, 0, 1, 0, 0, 1, 0, 0, 1 }, TINYGLTF_TYPE_VEC3, 3);
    nonIndexed.attributes["TEXCOORD_0"] =
            addAttribute(model, { 0, 0, 1, 0, 0, 1 }, TINYGLTF_TYPE_VEC2, 2);
    nonIndexed.attributes["TEXCOORD_1"] = nonIndexed.attributes["TEXCOORD_0"];
    mesh.primitives.push_back(nonIndexed);
    model.meshes.push_back(mesh);
    tinygltf::Material material;
    material.normalTexture.index = 0;
    material.normalTexture.texCoord = 1;
    model.materials.push_back(material);
    model.materials.emplace_back();
    tinygltf::Image image;
    image.width = image.height = 1;
    image.component = 4;
    image.bits = 8;
    image.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image.mimeType = "image/png";
    image.image = { 128, 128, 255, 255 };
    model.images.push_back(image);
    tinygltf::Texture texture;
    texture.source = 0;
    model.textures.push_back(texture);
    tinygltf::Node node;
    node.mesh = 0;
    model.nodes.push_back(node);
    node.translation = { 3, 0, 0 };
    node.scale = { -1, 1, 1 };
    model.nodes.push_back(node);
    tinygltf::Scene scene;
    scene.nodes = { 0, 1 };
    model.scenes.push_back(scene);
    model.defaultScene = 0;
    return model;
}

void compareGeometry(gltf::ImportedModel const& original, gltf::ImportedModel const& baked) {
    require(original.indices.size() == baked.indices.size(), "corner count changed");
    for (size_t corner = 0; corner < original.indices.size(); ++corner) {
        require(original.vertices.at(original.indices[corner]) ==
                        baked.vertices.at(baked.indices[corner]),
                "persisting tangents changed a rendered corner");
    }
}

tinygltf::Model readModel(std::filesystem::path const& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error, warning;
    require(loader.LoadBinaryFromFile(&model, &error, &warning, path.string()),
            "could not read persisted GLB");
    return model;
}

void checkSeams(tinygltf::Model const& model) {
    auto const& primitive = model.meshes[0].primitives[0];
    auto count = model.accessors.at(primitive.attributes.at("POSITION")).count;
    require(count > 4, "mirrored UV seam did not split shared vertices");
    for (auto const& [name, accessor]: primitive.attributes) {
        require(model.accessors.at(accessor).count == count, "split attribute counts differ");
    }
    require(model.accessors.at(primitive.targets[0].at("POSITION")).count == count,
            "morph target did not follow split vertices");
    require(!model.meshes[0].primitives[1].attributes.contains("TANGENT"),
            "primitive without a normal map was unnecessarily baked");
    require(model.meshes[0].primitives[3].attributes.contains("TANGENT"),
            "non-indexed normal-mapped primitive was not baked");
}

void checkPersistence(std::filesystem::path const& directory) {
    auto path = directory / "model.glb";
    auto model = makeModel();
    auto expected = gltf::importModel(model);
    tinygltf::TinyGLTF writer;
    require(writer.WriteGltfSceneToFile(&model, path.string(), true, true, false, true),
            "could not create fixture GLB");
    auto first = gltf::loadModel(path.string());
    compareGeometry(expected, first);
    auto persisted = readModel(path);
    checkSeams(persisted);
    require(persisted.images[0].image == model.images[0].image, "normal map pixels changed");
    require(!gltf::bakeMissingTangents(persisted), "persisted GLB still needs generated tangents");
    auto sentinel = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    std::filesystem::last_write_time(path, sentinel);
    compareGeometry(expected, gltf::loadModel(path.string()));
    require(std::filesystem::last_write_time(path) == sentinel, "second load rewrote the GLB");
}

void checkUnmappedModel(std::filesystem::path const& directory) {
    auto path = directory / "unmapped.glb";
    auto model = makeModel();
    model.materials[0].normalTexture.index = -1;
    tinygltf::TinyGLTF writer;
    require(writer.WriteGltfSceneToFile(&model, path.string(), true, true, false, true),
            "could not create unmapped fixture");
    auto before = std::filesystem::last_write_time(path);
    gltf::loadModel(path.string());
    require(std::filesystem::last_write_time(path) == before, "unmapped model was rewritten");
    require(!readModel(path).meshes[0].primitives[0].attributes.contains("TANGENT"),
            "unmapped model received generated tangents");
}

void checkWriteFailure(std::filesystem::path const& directory) {
    auto path = directory / "readonly.glb";
    auto model = makeModel();
    tinygltf::TinyGLTF writer;
    require(writer.WriteGltfSceneToFile(&model, path.string(), true, true, false, true),
            "could not create read-only fixture");
    auto expected = gltf::importModel(model);
    auto permissions = std::filesystem::status(directory).permissions();
    std::filesystem::permissions(directory,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    try {
        compareGeometry(expected, gltf::loadModel(path.string()));
    } catch (...) {
        std::filesystem::permissions(directory, permissions);
        throw;
    }
    std::filesystem::permissions(directory, permissions);
    require(!readModel(path).meshes[0].primitives[0].attributes.contains("TANGENT"),
            "failed write modified the original GLB");
}

} // namespace

int main() {
    auto directory =
            std::filesystem::temp_directory_path() /
            ("pbr-tangent-test-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(directory);
        checkPersistence(directory);
        checkUnmappedModel(directory);
        checkWriteFailure(directory);
        std::filesystem::remove_all(directory);
        std::cout << "Tangent persistence regression checks passed\n";
    } catch (std::exception const& error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#undef TINYGLTF_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#include "renderer/gltf_model_io.hpp"

#include "renderer/gltf_buffer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

bool retainEncodedImage(tinygltf::Image* image, int, std::string*, std::string*, int, int,
        unsigned char const* bytes, int size, void*) {
    image->as_is = true;
    // GLB images already live in the binary buffer. Only URI images need another copy.
    if (image->bufferView < 0) {
        image->image.assign(bytes, bytes + size);
    }
    return true;
}

void embedDataUriImages(tinygltf::Model& model) {
    for (auto& image: model.images) {
        if (image.bufferView < 0 && image.uri.empty()) {
            // TinyGLTF discards data URIs; retain their compressed bytes when writing back.
            image.bufferView = gltf::appendBufferView(model, image.image);
        }
    }
}

std::filesystem::path temporaryModelPath(std::string const& sourcePath) {
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return sourcePath + ".tangents-" + std::to_string(timestamp) + ".tmp";
}

void writeBinaryModel(tinygltf::Model const& model, std::filesystem::path const& path) {
    std::ofstream stream(path, std::ios::binary);
    tinygltf::TinyGLTF writer;
    // Keep original compressed images and external URIs rather than re-encoding pixels.
    writer.SetImageWriter(nullptr, nullptr);
    if (!stream || !writer.WriteGltfSceneToStream(&model, stream, false, true)) {
        throw std::runtime_error("could not serialize the tangent-enriched GLB");
    }
    stream.close();
    if (!stream) {
        throw std::runtime_error("could not finish writing the tangent-enriched GLB");
    }
}

} // namespace

tinygltf::Model gltf::readBinaryModel(std::string const& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(retainEncodedImage, nullptr);
    std::string warning;
    std::string error;
    bool isLoaded = loader.LoadBinaryFromFile(&model, &error, &warning, path);
    if (!warning.empty()) {
        std::cerr << "glTF warning: " << warning << '\n';
    }
    if (!isLoaded) {
        throw std::runtime_error("failed to load glTF model (" + path + "): " + error);
    }
    return model;
}

void gltf::saveGeneratedTangents(tinygltf::Model& model, std::string const& path) {
    namespace fs = std::filesystem;
    embedDataUriImages(model);
    auto temporaryPath = temporaryModelPath(path);
    try {
        writeBinaryModel(model, temporaryPath);
        fs::permissions(temporaryPath, fs::status(path).permissions());
        // Replace only after a complete write, preserving the source on write failure.
        fs::rename(temporaryPath, path);
        std::cout << "Generated tangents saved (" << path << ")\n";
    } catch (std::exception const& error) {
        // Persistence is optional: rendering can still use the generated tangents in memory.
        std::error_code ignored;
        fs::remove(temporaryPath, ignored);
        std::cerr << "glTF warning: could not save generated tangents (" << path
                  << "): " << error.what() << '\n';
    }
}

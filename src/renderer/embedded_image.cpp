#include "renderer/embedded_image.hpp"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <stb_image.h>

namespace {

DecodedImage flipPixels(std::span<uint8_t const> pixels, uint32_t width, uint32_t height) {
    size_t rowBytes = size_t(width) * STBI_rgb_alpha;
    if (width == 0 || height == 0 || pixels.size() != rowBytes * height) {
        throw std::runtime_error("embedded glTF textures require decoded RGBA8 images");
    }
    DecodedImage output{ .pixels = std::vector<uint8_t>(pixels.size()),
        .width = width,
        .height = height };
    for (uint32_t row = 0; row < height; ++row) {
        memcpy(output.pixels.data() + row * rowBytes, pixels.data() + (height - 1 - row) * rowBytes,
                rowBytes);
    }
    return output;
}

void validateEncodedImage(std::span<uint8_t const> encoded) {
    if (encoded.empty() || encoded.size() > size_t(std::numeric_limits<int>::max())) {
        throw std::runtime_error("embedded glTF image has no data or exceeds decoder size limits");
    }
    int size = static_cast<int>(encoded.size());
    if (stbi_is_16_bit_from_memory(encoded.data(), size)) {
        throw std::runtime_error("embedded glTF textures require decoded RGBA8 images");
    }
}

struct LoadedImage {
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{ nullptr, stbi_image_free };
    int width = 0;
    int height = 0;
};

LoadedImage loadRgbaPixels(std::span<uint8_t const> encoded) {
    LoadedImage image;
    int channels = 0;
    image.pixels.reset(stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
            &image.width, &image.height, &channels, STBI_rgb_alpha));
    if (!image.pixels) {
        auto reason = stbi_failure_reason();
        throw std::runtime_error(std::string("could not decode embedded glTF image: ") +
                                 (reason ? reason : "unknown image format"));
    }
    return image;
}

DecodedImage decodeImage(std::span<uint8_t const> encoded) {
    validateEncodedImage(encoded);
    auto image = loadRgbaPixels(encoded);
    size_t byteCount = size_t(image.width) * image.height * STBI_rgb_alpha;
    return flipPixels({ image.pixels.get(), byteCount }, static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height));
}

} // namespace

EmbeddedImage::EmbeddedImage(std::vector<uint8_t> encoded)
        : mData(std::move(encoded)) {}

EmbeddedImage::EmbeddedImage(DecodedImage decoded)
        : mData(std::move(decoded)) {}

EmbeddedImage EmbeddedImage::fromPixels(std::span<uint8_t const> pixels, uint32_t width,
        uint32_t height) {
    return EmbeddedImage(flipPixels(pixels, width, height));
}

DecodedImage const& EmbeddedImage::decode() const {
    if (!isDecoded()) {
        mData = decodeImage(std::get<std::vector<uint8_t>>(mData));
    }
    return std::get<DecodedImage>(mData);
}

bool EmbeddedImage::isDecoded() const { return std::holds_alternative<DecodedImage>(mData); }

bool EmbeddedImage::empty() const {
    return !isDecoded() && std::get<std::vector<uint8_t>>(mData).empty();
}

#pragma once

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

struct DecodedImage {
    std::vector<uint8_t> pixels; // RGBA8, vertically flipped to match imported UVs.
    uint32_t width = 0;
    uint32_t height = 0;
};

class EmbeddedImage {
public:
    EmbeddedImage() = default;
    explicit EmbeddedImage(std::vector<uint8_t> encoded);

    static EmbeddedImage fromPixels(std::span<uint8_t const> pixels, uint32_t width,
            uint32_t height);

    // Decode once on first use, replacing compressed storage with the cached pixels.
    DecodedImage const& decode() const;
    bool isDecoded() const;
    bool empty() const;

private:
    explicit EmbeddedImage(DecodedImage decoded);
    mutable std::variant<std::vector<uint8_t>, DecodedImage> mData;
};

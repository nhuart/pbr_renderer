#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "screenshot.h"

#include <filament/Viewport.h>
#include <iostream>

std::filesystem::path resolveOutputDir(const char* executablePath) {
    auto repoRoot = std::filesystem::canonical(std::filesystem::path(executablePath) / "../..");
    return repoRoot / "results" / "filament";
}

void captureScreenshot(filament::Renderer& renderer, filament::View& view,
        const std::string& path) {
    auto const& viewport = view.getViewport();
    uint32_t width = viewport.width;
    uint32_t height = viewport.height;
    auto* pixels = new uint8_t[width * height * 4];

    auto descriptor = filament::backend::PixelBufferDescriptor::make(pixels, width * height * 4,
            filament::backend::PixelBufferDescriptor::PixelDataFormat::RGBA,
            filament::backend::PixelBufferDescriptor::PixelDataType::UBYTE,
            [pixels, width, height, path](void*, size_t) {
                if (stbi_write_png(path.c_str(), width, height, 4, pixels, width * 4)) {
                    std::cout << "Screenshot saved: " << path << "\n";
                } else {
                    std::cerr << "Failed to write screenshot: " << path << "\n";
                }
                delete[] pixels;
            });

    renderer.readPixels(viewport.left, viewport.bottom, width, height, std::move(descriptor));
}

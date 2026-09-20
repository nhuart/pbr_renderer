#pragma once

#include <backend/PixelBufferDescriptor.h>
#include <filament/Renderer.h>
#include <filament/View.h>

#include <filesystem>
#include <string>

std::filesystem::path resolveOutputDir(const char* executablePath);

void captureScreenshot(filament::Renderer& renderer, filament::View& view, const std::string& path);

#pragma once

#include "renderer/cubemap_atlas.hpp"
#include "renderer/texture_atlas.hpp"
#include "scene/types.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>

struct VulkanContext;
struct CommandService;

struct IblEnvironment {
    CubemapAtlas prefilter;
    TextureAtlas brdfLut;
    IblSHUBO sh{};

    IblEnvironment(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& prefilterPath, std::string const& shPath)
            : prefilter(ctx, cmds, prefilterPath),
              brdfLut(ctx, cmds, "textures/ibl/brdf_lut.ktx2") {
        parseSH(shPath);
    }

private:
    void parseSH(std::string const& path) {
        std::ifstream f(path);
        if (!f) {
            throw std::runtime_error("failed to open SH file: " + path);
        }
        for (int i = 0; i < 9; ++i) {
            std::string line;
            if (!std::getline(f, line)) {
                throw std::runtime_error("sh.txt: expected 9 lines");
            }
            float r, g, b;
            if (sscanf(line.c_str(), "( %f, %f, %f)", &r, &g, &b) != 3) {
                throw std::runtime_error("sh.txt: parse error on line " + std::to_string(i));
            }
            sh.sh[i] = glm::vec4(r, g, b, 0.0f);
        }
    }
};

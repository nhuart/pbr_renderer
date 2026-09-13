#pragma once

#include "renderer/cubemap_atlas.hpp"
#include "renderer/texture_atlas.hpp"

struct VulkanContext;
struct CommandService;

struct IblEnvironment {
    CubemapAtlas irradiance;
    CubemapAtlas prefilter;
    TextureAtlas brdfLut;

    IblEnvironment(VulkanContext const& ctx, CommandService const& cmds,
            std::string const& irradiancePath, std::string const& prefilterPath,
            std::string const& brdfLutPath)
            : irradiance(ctx, cmds, irradiancePath),
              prefilter(ctx, cmds, prefilterPath),
              brdfLut(ctx, cmds, brdfLutPath) {}
};

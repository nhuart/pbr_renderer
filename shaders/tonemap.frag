#version 450

layout(binding = 0) uniform sampler2D hdrColor;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(hdrColor, inUV).rgb;
#ifndef NO_TONEMAP
    color = color / (color + vec3(1.0));
#endif
    // The sRGB swapchain encodes the final linear color, regardless of tone-mapping mode.
    outColor = vec4(color, 1.0);
}

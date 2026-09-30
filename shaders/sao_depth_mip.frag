#version 450

layout(binding = 0) uniform sampler2D previousDepthMip;

// Filament's rotated-grid subsample: retain one depth value from each 2x2 area.
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 sourcePixel = 2 * pixel + ivec2(pixel.y & 1, pixel.x & 1);
    gl_FragDepth = texelFetch(previousDepthMip, sourcePixel, 0).r;
}

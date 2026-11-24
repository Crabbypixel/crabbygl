#pragma once

#include <glm/glm.hpp>

// Exactly 32 bytes per vertex, tightly packed for optimal GPU upload and cache performance
struct Vertex
{
    glm::vec3 pos;          // World-space vertex position
    glm::vec2 baseUV;       // Greedy-mesh UV coordinates

    uint8_t   tileBase;     // Atlas tile for base texture
    uint8_t   tileOverlay;  // Atlas tile for overlay texture
    uint8_t   packed;       // [0-2] normal, [3] overlay, [4-5] AO, [6-7] unused
    uint8_t   lightValue;   // per - vertex smoothed sky << 4 | block light
    uint32_t  tint;         // RGBA8 tint color

    uint32_t  _padding2;
};

#ifdef SHADER_VERTEX

// Vertex attributes
layout (location = 0) in vec3  aPos;
layout (location = 1) in vec2  aUV;
layout (location = 2) in uint  aTileBase;
layout (location = 3) in uint  aTileOverlay;
layout (location = 4) in uint  aPacked;
layout (location = 5) in uint  aLightValue;
layout (location = 6) in vec4  aTint;

// Shared camera matrices
layout (std140) uniform Matrices
{
    mat4 matProjection;
    mat4 matView;
};

out vec3  fWorldPos;
out vec2  fUV;
out vec3  fTint;
out float fAo;
out float fAlpha;

flat out uint fUseOverlay;          // GLSL version 330 doesn't support flat bools, so we use uint instead
flat out uint fNormalIndex;
flat out uint fTileBase;
flat out uint fTileOverlay;
out vec2 fLight;                     // x = sky, y = block, 0..1, interpolated -> smooth lighting

void main()
{
    fWorldPos    = aPos;
    fUV          = aUV;

    // Atlas tile indices
    fTileBase    = aTileBase;
    fTileOverlay = aTileOverlay;

    // Packed face metadata:
    // bits 0-2 : normal index
    // bit  3   : overlay enabled
    // bits 4-5 : AO level (0-3)
    fNormalIndex = aPacked & 0x7u;
    fUseOverlay  = (aPacked >> 3u) & 0x1u;
    fAo          = float((aPacked >> 4u) & 0x3u) / 3.0f;

    // Sunlight & Torchlight
    fLight = vec2(float(aLightValue >> 4u), float(aLightValue & 0xFu)) / 15.0f;

    // RGBA8 tint arrives normalized to 0..1
    fTint  = aTint.rgb;
    fAlpha = aTint.a;

    gl_Position = matProjection * matView * vec4(aPos, 1.0f);
}

#endif

#ifdef SHADER_FRAGMENT

struct UVRect
{
    vec2 _min;
    vec2 _max;
};

uniform sampler2D u_atlas;

// Directional lighting
uniform vec3 u_lightDir;
uniform vec3 u_ambient;
uniform vec3 u_diffuse;

uniform float u_skyFactor;      // 0 night, 1 day
uniform float u_minLight;       // Floor when no sky and no torch

// Block selection highlight
uniform bool  u_isSelected;
uniform ivec3 u_selectedBlock;

// Runtime feature toggles
uniform bool u_isAOEnabled;

// Sky and fog
uniform vec3 u_cameraPos;
uniform float u_fogStart;
uniform float u_fogEnd;

uniform vec3 u_zenithColor;
uniform vec3 u_horizonColor;
uniform vec3 u_sunDir;
uniform vec4 u_sunsetColor;

in vec3  fWorldPos;
in vec2  fUV;
in vec3  fTint;
in float fAo;
in float fAlpha;

flat in uint fUseOverlay;
flat in uint fNormalIndex;
flat in uint fTileBase;
flat in uint fTileOverlay;
in vec2 fLight;

out vec4 FragColor;

// Face normals:
// +Y -Y +X -X +Z -Z
const vec3 NORMALS[6] = vec3[6](
    vec3( 0.0f,  1.0f,  0.0f),
    vec3( 0.0f, -1.0f,  0.0f),

    vec3( 1.0f,  0.0f,  0.0f),
    vec3(-1.0f,  0.0f,  0.0f),

    vec3( 0.0f,  0.0f,  1.0f),
    vec3( 0.0f,  0.0f, -1.0f)
);

// Convert greedy-mesh UVs into atlas UVs
// fract() repeats the texture across merged quads
vec2 tileUV(vec2 tileLocal, vec2 tileMin, vec2 tileMax)
{
    return tileMin + fract(tileLocal) * (tileMax - tileMin);
}

// Recover the block owning this fragment
// Used by the block selection highlight
ivec3 fragBlockPos()
{
    return ivec3(floor(fWorldPos - NORMALS[fNormalIndex] * 0.01f));
}

// This mirrors Sky.glsl's main() (except the stars), any drift shows a seam
vec3 SkyColor(vec3 dir)
{
    float up = clamp(dir.y, 0.0f, 1.0f);
    vec3 color = mix(u_horizonColor, u_zenithColor, sqrt(up));
    
    float dLen = length(dir.xz);
    float sLen = length(u_sunDir.xz);
    float facing = (dLen > 1e-4f && sLen > 1e-4f) ? dot(dir.xz / dLen, u_sunDir.xz / sLen) : 0.0f;
    float side = max(facing, 0.0f);
    side *= side;
    
    float band = 1.0f - smoothstep(0.0f, 0.5f, abs(dir.y));
    return mix(color, u_sunsetColor.rgb, u_sunsetColor.a * side * band);
}

// Atlas tile lookup (16x16 atlas)
UVRect Tile(uint i)
{
    const float TILE_W = 0.0625f;
    const float TILE_H = 0.0625f;

    uint col = i % 16u;
    uint row = i / 16u;

    float u0 = float(col) * TILE_W;
    float v0 = 1.0f - (float(row) + 1.0f) * TILE_H;

    UVRect uv;
    uv._min = vec2(u0, v0);
    uv._max = vec2(u0 + TILE_W, v0 + TILE_H);

    return uv;
}

void main()
{
    // Fetch atlas rectangles
    UVRect baseUV    = Tile(fTileBase);
    UVRect overlayUV = Tile(fTileOverlay);

    vec2 baseTileMin    = baseUV._min;
    vec2 baseTileMax    = baseUV._max;

    vec2 overlayTileMin = overlayUV._min;
    vec2 overlayTileMax = overlayUV._max;

    // Base texture sample
    vec2 atlasUV = tileUV(fUV, baseTileMin, baseTileMax);

    // Preserve mipmap correctness when repeating UVs
    vec2 tileSize = baseTileMax - baseTileMin;
    vec2 dx = dFdx(fUV) * tileSize;
    vec2 dy = dFdy(fUV) * tileSize;

    vec4 baseTex = textureGrad(u_atlas, atlasUV, dx, dy);

    if (baseTex.a < 0.1f)
        discard;

    vec3 color = baseTex.rgb;

    // Overlay texture (grass sides, etc.)
    if (fUseOverlay == 1u)
    {
        vec2 overlayAtlasUV = tileUV(fUV, overlayTileMin, overlayTileMax);
        vec2 overlaySize = overlayTileMax - overlayTileMin;
        vec2 overlayDx = dFdx(fUV) * overlaySize;
        vec2 overlayDy = dFdy(fUV) * overlaySize;
        vec4 overlayTex = textureGrad(u_atlas, overlayAtlasUV, overlayDx, overlayDy);

        color = mix(color, overlayTex.rgb * fTint, overlayTex.a);
    }
    else
    {
        color *= fTint;
    }

   float skyExposure = fLight.x;
   float torch       = fLight.y;
   
   float NdotL = max(dot(normalize(NORMALS[fNormalIndex]), -normalize(u_lightDir)), 0.0f);
   
   // Sky contribution: exposure × time-of-day × directional face angle
   // Sky nibble = static exposure; u_skyFactor = per-frame time of day
   float sky = skyExposure * u_skyFactor;
   float sunLight = sky * u_diffuse.r * NdotL + mix(u_minLight, u_ambient.r, sky);
   
   // Torch overrides directional: torch=1 -> full omni, torch=0 -> sun only
   float finalLight = mix(sunLight, 1.0f, torch);
   
   if(u_isAOEnabled)
        color *= mix(0.8f, 1.0f, fAo * fAo);

   color *= finalLight;

    // Selected block highlight
    if (u_isSelected && fragBlockPos() == u_selectedBlock)
    {
        if(color == vec3(0.0f))     // Show some highlight if the color is completely zero
            color += 0.1f;
        else
            color *= 1.2f;
    }

    // Horizontal distance only, no fog at increasing height
    vec3 toFrag = fWorldPos - u_cameraPos;
    float fog = smoothstep(u_fogStart, u_fogEnd, length(toFrag.xz));
    color = mix(color, SkyColor(normalize(toFrag)), fog);       // Blend terrain color & sky color to obtain fogged pixel (fog present depending on dist)

    //FragColor = vec4(color, baseTex.a);
    float finalAlpha = fAlpha * baseTex.a;
    FragColor = vec4(color, finalAlpha);
}

#endif
// ChunkMeshBuilder.cpp — Binary Greedy Meshing
//
// ALGORITHM OVERVIEW
// // ===============
// For each face direction (6) and each layer along the normal axis:
//   1. Build Cell Grid: scan every (row, col) in the layer.
//      Each visible opaque face gets a FaceCell with:
//        > key   — packed {blockType, ao[4], useOverlay, flip} identical key ⟹ safe to merge
//        > ao[4] — raw [0..3] AO per vertex (same across all merged cells)
//        > type  — for texture/tint lookup at emit time
//      Simultaneously build rowMask[row] — a uint16_t bitmask where bit col
//      is set iff grid[row][col].key != 0.
//
//   2. Binary Greed Sweep:
//      For each row:
//        while rowMask[row] != 0:
//          col = ctz(rowMask[row] <- O(1) jump to first unprocessed bit
//          W   = run of consecutive 1-bits with same key starting at col
//          H   = consecutive rows where the W-wide run exists with the same key
//          emit one quad for (layer, row, col, H, W)
//          clear consumed bits from rowMask
//
// WHY uint16_t bitmasks?
//   CX = CZ = 16 always. The "col" dimension (Z for Y-faces, Z for X-faces,
//   X for Z-faces) is always 16 wide -> fits exactly in uint16_t.
//   "row" dimension is CX=16 for Y-faces or CY=256 for X/Z-faces.
//   We always have 16-bit masks regardless of face direction.
//
// AO + Greedy compatibility
//   Two faces merge only if ALL of {blockType, ao[0..3], useOverlay, flip}
//   are identical (packed into key). This preserves AO visual fidelity at the
//   cost of ~30-40% fewer merges vs. ignoring AO — an acceptable trade-off
//   since large uniform surfaces (the common case) still merge perfectly.
//
//   Because all cells in a merged rectangle share the same key (same ao[]),
//   the 4 outer-corner AO values are just ref.ao[0..3] directly — no per-corner
//   block lookup needed.
//
// UB Tiling
//   Tile-local UV goes 0..W (col direction) × 0..H (row direction).
//   Shader samples: atlasUV = uvTileMin + fract(uv) * (uvTileMax - uvTileMin)
//   This tiles the 16×16 atlas tile W×H times across the merged quad.
//   Col UV is inverted for negative-normal faces (−Y, −X, −Z) to preserve
//   the original winding-order UV orientation.
//
// Non-opaque blocks
//   Translucent (glass, leaves) and cross (flowers, saplings) blocks bypass
//   greedy and use the original per-face / EmitCross path unchanged.

#include <glm/glm.hpp>
#include <bit>          // std::countr_zero — C++20
#include <cstring>      // memcpy
#include <algorithm>

#include "world/BlockRegistry.h"
#include "world/Chunk.h"
#include "rendering/ChunkMesh.h"
#include "world/ChunkMeshBuilder.h"
#include "world/World.h"
#include "rendering/Vertex.h"

// =========================================================================
// Face geometry tables
// =========================================================================

// 6 faces: +Y −Y +X −X +Z −Z
static const glm::ivec3 NORMALS[6] = {
    { 0, 1, 0}, { 0,-1, 0},
    { 1, 0, 0}, {-1, 0, 0},
    { 0, 0, 1}, { 0, 0,-1}
};

// Used for AO sampling only (unchanged from original)
static const glm::ivec3 TANGENT_U[6] = {
    {1,0,0},{1,0,0},  {0,0,1},{0,0,1},  {1,0,0},{1,0,0}
};
static const glm::ivec3 TANGENT_V[6] = {
    {0,0,1},{0,0,1},  {0,1,0},{0,1,0},  {0,1,0},{0,1,0}
};

// 4 local vertex offsets per face — define winding order (CCW from outside)
static const glm::ivec3 FACE_VERTS[6][4] = {
    {{0,1,0},{1,1,0},{1,1,1},{0,1,1}},  // +Y
    {{0,0,1},{1,0,1},{1,0,0},{0,0,0}},  // −Y
    {{1,0,0},{1,0,1},{1,1,1},{1,1,0}},  // +X
    {{0,0,1},{0,0,0},{0,1,0},{0,1,1}},  // −X
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},  // +Z
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}   // −Z
};

// =========================================================================
// Axis configuration per face
//
// For each face we define three in/out axes:
//   layerAxis — the axis the normal points along (we iterate layers along this)
//   rowAxis   — first in-plane axis (up to CY=256 rows)
//   colAxis   — second in-plane axis (always 16 wide -> uint16_t bitmask)
//
// Face/Axis  layerAxis rowAxis colAxis  normalDir layerCount rowCount
// +Y           Y(1)     X(0)   Z(2)      +1       CY=256    CX=16
// -Y           Y(1)     X(0)   Z(2)      -1       CY=256    CX=16
// +X           X(0)     Y(1)   Z(2)      +1       CX=16     CY=256
// -X           X(0)     Y(1)   Z(2)      -1       CX=16     CY=256
// +Z           Z(2)     Y(1)   X(0)      +1       CZ=16     CY=256
// -Z           Z(2)     Y(1)   X(0)      -1       CZ=16     CY=256
// =========================================================================

struct FaceAxis {
    int layerAxis, rowAxis, colAxis;
    int normalDir;
    int layerCount, rowCount;
};

static constexpr FaceAxis FACE_AXES[6] = {
    {1, 0, 2, +1, CY, CX},  // +Y
    {1, 0, 2, -1, CY, CX},  // -Y
    {0, 1, 2, +1, CX, CY},  // +X
    {0, 1, 2, -1, CX, CY},  // -X
    {2, 1, 0, +1, CZ, CY},  // +Z
    {2, 1, 0, -1, CZ, CY},  // -Z
};

// =========================================================================
// Per-face vertex layout
// For a merged quad at (layer, row0, col0) with H rows and W cols, vertex k is at:
//   world[rowAxis]   = row0 + (ROW_MAX[face][k] ? H : 0)
//   world[colAxis]   = col0 + (COL_MAX[face][k] ? W : 0)
//   world[layerAxis] = layer + (normalDir > 0 ? 1 : 0)
//
// Positive-normal faces start at (rmin,cmin); negative-normal faces
// reverse the col order so winding stays CCW from outside
//
// UV derivation per face (verified against original FACE_VERTS):
//   UV_ROW_IS_U: if true -> uv = (rowDelta, colUV); else -> uv = (colUV, rowDelta)
//   UV_INV_COL:  if true -> colUV = W - colDelta (col reversed in UV space)
//                else   -> colUV = colDelta
// =========================================================================

static constexpr bool ROW_MAX[6][4] = {
    // v0    v1     v2     v3
    {false, true,  true,  false},  // +Y
    {false, true,  true,  false},  // -Y
    {false, false, true,  true },  // +X
    {false, false, true,  true },  // -X
    {false, false, true,  true },  // +Z
    {false, false, true,  true },  // -Z
};

static constexpr bool COL_MAX[6][4] = {
    {false, false, true,  true },  // +Y   positive col
    {true,  true,  false, false},  // -Y   col reversed (negative face)
    {false, true,  true,  false},  // +X   positive col
    {true,  false, false, true },  // -X   col reversed
    {false, true,  true,  false},  // +Z   positive col
    {true,  false, false, true },  // -Z   col reversed
};

// Y faces: uv = (rowDelta, colUV);  X/Z faces: uv = (colUV, rowDelta)
static constexpr bool UV_ROW_IS_U[6] = { true, true, false, false, false, false };

// Negative-normal faces reverse col in UV space to keep texture orientation
static constexpr bool UV_INV_COL[6]  = { false, true, false, true, false, true };

// =========================================================================
// Face cell
// =========================================================================
struct alignas(8) FaceCell {
    uint64_t  key;      // 0 = invisible; packed key used for merge decisions
    BlockType type;     // block type at this cell
    uint8_t   ao[4];    // raw AO [0..3] for vertices 0..3
    uint8_t   light[4]; // smoothed (sky << 4 | block) per vertex 0..3
};


[[nodiscard]] static uint64_t MakeKey(
    BlockType type, const uint8_t ao[4],
    bool useOverlay, bool flip, const uint8_t light[4]) noexcept
{
    const uint32_t aoPacked =  ((uint32_t)ao[0] & 0x3)
                            | (((uint32_t)ao[1] & 0x3) << 2)
                            | (((uint32_t)ao[2] & 0x3) << 4)
                            | (((uint32_t)ao[3] & 0x3) << 6);

    uint32_t lightPacked;
    memcpy(&lightPacked, light, 4);

    // bits 0-7 type | 8-15 ao | 16 overlay | 17 flip | 18-49 light[4]
    const uint64_t k = (uint64_t)(uint8_t)type
                             | ((uint64_t)aoPacked               <<  8)
                             | ((uint64_t)(useOverlay ? 1u : 0u) << 16)
                             | ((uint64_t)(flip       ? 1u : 0u) << 17)
                             | ((uint64_t)lightPacked            << 18);

    return k == 0u ? 1u : k;  // key=0 means invisible; shift non-zero AIR edge case
}


// =========================================================================
// Helpers
// =========================================================================
static uint32_t PackRGBA(float r, float g, float b, float a) noexcept
{
    // Clamp to [0.0, 1.0], multiply, add 0.5f for perfect rounding, then cast
    uint32_t R = static_cast<uint32_t>((std::clamp(r, 0.0f, 1.0f) * 255.0f) + 0.5f);
    uint32_t G = static_cast<uint32_t>((std::clamp(g, 0.0f, 1.0f) * 255.0f) + 0.5f);
    uint32_t B = static_cast<uint32_t>((std::clamp(b, 0.0f, 1.0f) * 255.0f) + 0.5f);
    uint32_t A = static_cast<uint32_t>((std::clamp(a, 0.0f, 1.0f) * 255.0f) + 0.5f);

	// LE layout: R at lowest address -> GL reads as vec4.x. Correct on x86/ARM - little-endian platforms
    // GL shader expects RGBA in uint, so this matches perfectly
    return ((uint32_t)A << 24) | ((uint32_t)B << 16) | ((uint32_t)G << 8) | (uint32_t)R;
}

static uint8_t PackAO(const uint8_t ao[4]) noexcept
{
    return (ao[0] & 0x3)
        | ((ao[1] & 0x3) << 2)
        | ((ao[2] & 0x3) << 4)
        | ((ao[3] & 0x3) << 6);
}

static void UnpackAO(uint8_t packed, uint8_t ao[4]) noexcept
{
    ao[0] = packed & 0x3;
    ao[1] = (packed >> 2) & 0x3;
    ao[2] = (packed >> 4) & 0x3;
    ao[3] = (packed >> 6) & 0x3;
}


// =========================================================================
// AO helpers 
// =========================================================================

static int GetAOState(int side1, int side2, int corner) noexcept
{
    if (side1 + side2 == 2) return 0;
    return 3 - (side1 + side2 + corner);
}

// Checks opacity at chunk-local (x,y,z) with cross-chunk support for AO sampling
// Used only for AO (corner chunks needed) — mirrors original IsSolidLocal exactly
static bool IsSolidLocal(
    const Chunk& chunk,
    int x, int y, int z,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ) noexcept
{
    if (y < 0 || y >= CY) return false;

    if (x >= 0 && x < CX && z >= 0 && z < CZ)
        return IsOpaque(chunk.GetUnchecked(x, y, z));

    if (nPX    && x >= CX && z >= 0 && z < CZ) return IsOpaque(   nPX->GetUnchecked(0,    y, z   ));
    if (nNX    && x  <  0 && z >= 0 && z < CZ) return IsOpaque(   nNX->GetUnchecked(CX-1, y, z   ));
    if (nPZ    && z >= CZ && x >= 0 && x < CX) return IsOpaque(   nPZ->GetUnchecked(x,    y, 0   ));
    if (nNZ    && z  <  0 && x >= 0 && x < CX) return IsOpaque(   nNZ->GetUnchecked(x,    y, CZ-1));
    if (nPX_PZ && x >= CX && z >= CZ)          return IsOpaque(nPX_PZ->GetUnchecked(0,    y, 0   ));
    if (nPX_NZ && x >= CX && z  <  0)          return IsOpaque(nPX_NZ->GetUnchecked(0,    y, CZ-1));
    if (nNX_PZ && x  <  0 && z >= CZ)          return IsOpaque(nNX_PZ->GetUnchecked(CX-1, y, 0   ));
    if (nNX_NZ && x  <  0 && z  <  0)          return IsOpaque(nNX_NZ->GetUnchecked(CX-1, y, CZ-1));

    return false;
}

// Checks opacity for face-visibility test — only needs 4 direct neighbors,
// not corners (we never query a diagonal for visibility, only for AO)
static bool IsNeighborOpaque(
    const Chunk& chunk,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    int x, int y, int z) noexcept
{
    if (y < 0 || y >= CY) return false;

    if (x >= 0 && x < CX &&  z >= 0 && z < CZ) return IsOpaque(chunk.GetUnchecked(x, y, z));
    if (x >= CX && nPX) return IsOpaque(nPX->GetUnchecked(0,    y, z));
    if (x  <  0 && nNX) return IsOpaque(nNX->GetUnchecked(CX-1, y, z));
    if (z >= CZ && nPZ) return IsOpaque(nPZ->GetUnchecked(x,    y, 0));
    if (z  <  0 && nNZ) return IsOpaque(nNZ->GetUnchecked(x,    y, CZ-1));
    return false;  // no neighbor chunk -> face is exposed to void -> visible
}

// Compute raw AO [0..3] for all 4 vertices of face at chunk-local chunkLocalBlockPos
// Fills out AO[0..3] aligned with FACE_VERTS[face][0..3]
static void ComputeAO(
    const Chunk& chunk,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ,
    const glm::ivec3& chunkLocalBlockPos, int face,
    uint8_t outAO[4]) noexcept
{
    const glm::ivec3& U = TANGENT_U[face];
    const glm::ivec3& V = TANGENT_V[face];
    const glm::ivec3& N = NORMALS[face];

    for (int i = 0; i < 4; ++i)
    {
        const glm::ivec3& v = FACE_VERTS[face][i];
        const int du = (glm::dot(static_cast<glm::vec3>(v), static_cast<glm::vec3>(U)) > 0.5f) ? 1 : -1;
        const int dv = (glm::dot(static_cast<glm::vec3>(v), static_cast<glm::vec3>(V)) > 0.5f) ? 1 : -1;

        const glm::ivec3 base   = chunkLocalBlockPos + N;
        const glm::ivec3 side1  = base + du * U;
        const glm::ivec3 side2  = base + dv * V;
        const glm::ivec3 corner = base + du * U + dv * V;

        const int s1 = IsSolidLocal(chunk, side1.x, side1.y, side1.z,
                                    nPX, nNX, nPZ, nNZ,
                                    nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ);

        const int s2 = IsSolidLocal(chunk, side2.x,  side2.y,  side2.z,
                                    nPX, nNX, nPZ, nNZ,
                                    nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ);

        const int c  = IsSolidLocal(chunk, corner.x, corner.y, corner.z,
                                    nPX, nNX, nPZ, nNZ,
                                    nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ);

        outAO[i] = static_cast<uint8_t>(GetAOState(s1, s2, c));
    }
}


// =========================================================================
// Smooth lighting helpers
// =========================================================================

static uint8_t GetLightLocal(
    const Chunk& chunk, int x, int y, int z,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ) noexcept
{
    if (y >= CY) return 0xF0;
    if (y < 0) return 0;

    if (x >= 0 && x < CX && z >= 0 && z < CZ)
        return chunk.GetLightMap(x, y, z);

    if (nPX && x >= CX && z >= 0 && z < CZ) return    nPX->GetLightMap(0,      y,      z);
    if (nNX && x  <  0 && z >= 0 && z < CZ) return    nNX->GetLightMap(CX - 1, y,      z);
    if (nPZ && z >= CZ && x >= 0 && x < CX) return    nPZ->GetLightMap(x,      y,      0);
    if (nNZ && z  <  0 && x >= 0 && x < CX) return    nNZ->GetLightMap(x,      y, CZ - 1);
    if (nPX_PZ && x >= CX && z >= CZ)       return nPX_PZ->GetLightMap(0,      y,      0);
    if (nPX_NZ && x >= CX && z  <  0)       return nPX_NZ->GetLightMap(0,      y, CZ - 1);
    if (nNX_PZ && x  <  0 && z >= CZ)       return nNX_PZ->GetLightMap(CX - 1, y,      0);
    if (nNX_NZ && x  <  0 && z  <  0)       return nNX_NZ->GetLightMap(CX - 1, y, CZ - 1);
    
    // Missing neighbor - replace by center by SmoothChannel
    return 0;
}

// A 0-sample is almost always an opaque block, averaging it would darken every
// wall-adjacent vertex a second time when AO is applied, so substitute the center
static int SmoothChannel(int c, int s1, int s2, int k) noexcept
{
    if (s1 == 0) s1 = c;
    if (s2 == 0) s2 = c;
    if (k == 0) k = c;
    return (c + s1 + s2 + k + 2) >> 2;
}

// Per-vertex light for one face. Samples the same 4 cells ComputeAO queries
// (face-neighbor, side1, side2, corner) so light and AO agree spatially
static void ComputeSmoothLight(
    const Chunk& chunk,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ,
    const glm::ivec3& p, int face,
    const uint8_t ao[4],
    uint8_t outlight[4]) noexcept
{
    const glm::ivec3& U = TANGENT_U[face];
    const glm::ivec3& V = TANGENT_V[face];
    const glm::ivec3& N = NORMALS[face];
    const glm::ivec3 base = p + N;

    auto L = [&](const glm::ivec3& q) {
        return GetLightLocal(chunk, q.x, q.y, q.z, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ);
    };

    const uint8_t c = L(base);

    for (int i = 0; i < 4; ++i)
    {
        const glm::ivec3& v = FACE_VERTS[face][i];
        const int du = (glm::dot(glm::vec3(v), glm::vec3(U)) > 0.5f) ? 1 : -1;
        const int dv = (glm::dot(glm::vec3(v), glm::vec3(V)) > 0.5f) ? 1 : -1;

        const uint8_t s1 = L(base + du * U);
        const uint8_t s2 = L(base + dv * V);
        const uint8_t k = (ao[i] == 0) ? 0 : L(base + du * U + dv * V);

        const int sky = SmoothChannel(c >> 4, s1 >> 4, s2 >> 4, k >> 4);
        const int blk = SmoothChannel(c & 0xF, s1 & 0xF, s2 & 0xF, k & 0xF);
        outlight[i] = static_cast<uint8_t>((sky << 4) | blk);
    }
}

static bool ShouldFlip(const uint8_t ao[4], const uint8_t light[4]) noexcept
{
    int b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = (ao[i] + 1) * (1 + std::max(light[i] >> 4, light[i] & 0xF));

    return b[0] + b[2] > b[1] + b[3];
}

// =========================================================================
// EmitCross - from earlier version
// =========================================================================
void ChunkMeshBuilder::EmitCross(
    std::vector<Vertex>& verts,
    const glm::ivec3& worldPos,
    uint8_t lightValue,
    BlockType type)
{
    const BlockDef& crossItem = GetDef(type);
    const glm::vec4& tint     = crossItem.tint;

    const glm::vec2 uvs[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };

    static const glm::ivec3 CROSS_VERTS1[4] = {{0,0,1},{1,0,0},{1,1,0},{0,1,1}};
    static const glm::ivec3 CROSS_VERTS2[4] = {{0,0,0},{1,0,1},{1,1,1},{0,1,0}};

    constexpr int FWD[6] = {0,1,2, 0,2,3};
    constexpr int REV[6] = {0,2,1, 0,3,2};

    auto emit = [&](const glm::ivec3 quad[4], const int idx[6]) {
        for (int i = 0; i < 6; ++i)
        {
            uint8_t packed = ((uint8_t)0 & 0x7)
                            | (false) << 3
                            | ((uint8_t)1 & 0x3) << 4;

            verts.emplace_back(Vertex{
                .pos         = glm::vec3(worldPos + quad[idx[i]]),
                .baseUV      = uvs[idx[i]],
                .tileBase    = (uint8_t)crossItem.faces[0],
                .tileOverlay = (uint8_t)0,
                .packed      = packed,
                .lightValue  = lightValue,
                .tint        = PackRGBA(tint.r, tint.g, tint.b, tint.a),
            });
        }
    };

    emit(CROSS_VERTS1, FWD);  emit(CROSS_VERTS1, REV);
    emit(CROSS_VERTS2, FWD);  emit(CROSS_VERTS2, REV);
}


// =========================================================================
// Kept for translucent blocks (glass/leaves)
// Identical to original, updated to emit new Vertex fields
// =========================================================================
void ChunkMeshBuilder::AddTranslucentFace(
    std::vector<Vertex>& verts,
    const glm::ivec3& worldPos,
    const glm::ivec3& chunkLocalPos,
    int face,
    BlockType type,
    const Chunk& chunk,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ)
{
    const BlockDef& blockInfo = GetDef(type);
    const bool useOverlay = blockInfo.useOverlay && face > 1;

    const glm::vec2 baseUVs[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };

    uint8_t aoRaw[4] = { 3, 3, 3, 3 };
        ComputeAO(chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ, chunkLocalPos, face, aoRaw);

    const bool flip = (aoRaw[0] + aoRaw[2] > aoRaw[1] + aoRaw[3]);
    const int tri[2][6] = {
        {0,1,2, 0,2,3},  // normal
        {0,1,3, 1,2,3},  // flipped
    };
    const int* idx = flip ? tri[1] : tri[0];

    for (int i = 0; i < 6; ++i)
    {
        const int k = idx[i];

        //packed: normal (3b), useOverlay(1b), ao(2b)
        uint8_t packed = ((uint8_t)face & 0x7)                  // lowest 3 bits
                       | ((useOverlay ? 1u : 0u) << 3)          // next bit
                       | ((0 & 3u) << 4);                       // next 2 bits

        verts.emplace_back(Vertex{
            .pos         = worldPos + FACE_VERTS[face][k],
            .baseUV      = baseUVs[k],            // tile-local (0..1 for 1×1 face)
            .tileBase    = (uint8_t)blockInfo.faces[face],
            .tileOverlay = (uint8_t)blockInfo.overlay,
            .packed      = packed,
            .lightValue  = chunk.lightMap[chunkLocalPos.x][chunkLocalPos.y][chunkLocalPos.z],
            .tint        = PackRGBA(1.0f, 1.0f, 1.0f, blockInfo.tint.a),
        });
    }
}


// =========================================================================
// Emits 6 vertices (2 triangles) for a merged H-row × W-col quad
// `grid` is the CY×CX FaceCell array; ref cell is at [row0][col0]
//
// KEY INSIGHT: since all merged cells share the same key (same ao[4]),
// ref.ao[] is the canonical AO for the entire quad. No per-corner lookup
// =========================================================================
void ChunkMeshBuilder::EmitGreedyQuad(
    int face, int layer,
    int row0, int col0, int H, int W,
    const GridArray& grid,
    int chunkWX, int chunkWZ,
    std::vector<Vertex>& out)
{
    const FaceAxis&  axes = FACE_AXES[face];
    const FaceCell&  ref  = grid[row0][col0];
    const BlockDef&  def  = GetDef(ref.type);
    const bool useOverlay = def.useOverlay && face > 1;
    
    // Grass uses a green tint for its top and side overlay
    // The bottom face is dirt and must remain untinted
    uint32_t tint = PackRGBA(def.tint.r, def.tint.g, def.tint.b, def.tint.a);

    if (ref.type == BlockType::GRASS_BLOCK)
    {
        const glm::vec4& dirtTint = GetDef(BlockType::DIRT).tint;
        tint = (face == BOTTOM) ? PackRGBA(dirtTint.r, dirtTint.g, dirtTint.b, dirtTint.a) : PackRGBA(def.tint.r, def.tint.g, def.tint.b, def.tint.a);
    }

    // Face-plane layer coord: positive normal, one step forward
    const int layerFace = layer + (axes.normalDir > 0 ? 1 : 0);

    // AO: all merged cells identical, use ref directly
    const bool flip = ShouldFlip(ref.ao, ref.light);

    // Build 4 vertex positions and UVs
    Vertex verts[4] = {};
    for (int k = 0; k < 4; ++k)
    {
        const int rowDelta = ROW_MAX[face][k] ? H : 0;
        const int colDelta = COL_MAX[face][k] ? W : 0;

        // Chunk-local coords -> world coords
        int lc[3] = {0, 0, 0};
        lc[axes.layerAxis] = layerFace;
        lc[axes.rowAxis]   = row0 + rowDelta;
        lc[axes.colAxis]   = col0 + colDelta;

        verts[k].pos = {(float)(chunkWX + lc[0]), (float) lc[1], (float)(chunkWZ + lc[2])};

        // Tile-local UV:
        //   Y  faces:  uv = (rowDelta, colUV)  — U=X,  V=Z
        //   X/Z faces: uv = (colUV, rowDelta) — U=Z/X, V=Y
        // colUV inverted for negative-normal faces to preserve texture orientation
        const float colUV = UV_INV_COL[face] ? (float)(W - colDelta) : (float)colDelta;
        const float rowUV = (float)rowDelta;
        verts[k].baseUV      = UV_ROW_IS_U[face] ? glm::vec2{rowUV, colUV} : glm::vec2{colUV, rowUV};

        verts[k].tileBase    = def.faces[face];
        verts[k].tileOverlay = static_cast<uint8_t>(def.overlay);

		uint8_t packed = ((uint8_t)face & 0x7) | (useOverlay << 3) | ((ref.ao[k] & 0x3) << 4);
        verts[k].packed      = packed;
        verts[k].lightValue  = ref.light[k];
        verts[k].tint        = tint;
    }

    // Emit triangles — flip diagonal based on AO to minimize gradient banding
    static constexpr int TRI_NORM[6] = {0,1,2, 0,2,3};
    static constexpr int TRI_FLIP[6] = {0,1,3, 1,2,3};
    const int* tri = flip ? TRI_FLIP : TRI_NORM;

    for (int i = 0; i < 6; ++i)
        out.push_back(verts[tri[i]]);
}


// =========================================================================
// Processes one face direction at one layer:
//   1. Builds FaceCell grid + uint16_t rowMask[] bitmasks
//   2. Greedy sweeps using ctz (count trailing zeros) for O(1) bit scanning
// =========================================================================
void ChunkMeshBuilder::BuildLayer(
    const Chunk& chunk,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ,
    int face, int layer,
    int chunkWX, int chunkWZ,
    std::vector<Vertex>& out)
{
    // Extract axis rules based on face direction to treat any 3D slice as a 2D plane
    const FaceAxis& ax = FACE_AXES[face];
    const int       rowCount = ax.rowCount;
    const int       colCount = CX;

    // Static thread local allocation avoids hitting the OS heap allocator on every frame
    static thread_local auto grid_ptr = std::make_unique<GridArray>();
    auto& grid = *grid_ptr;

    // Bitmasks where a set bit means a face is visible and needs to be processed
    uint16_t rowMask[CY] = {};

    // Check all neighbor atomic dirty flags once using relaxed memory order to avoid loop stalls
    const bool rebuildAO = chunk.aoDirty.load(std::memory_order_relaxed)
        || (nPX    && nPX   ->aoDirty.load(std::memory_order_relaxed))
        || (nNX    && nNX   ->aoDirty.load(std::memory_order_relaxed))
        || (nPZ    && nPZ   ->aoDirty.load(std::memory_order_relaxed))
        || (nNZ    && nNZ   ->aoDirty.load(std::memory_order_relaxed))
        || (nPX_PZ && nPX_PZ->aoDirty.load(std::memory_order_relaxed))
        || (nPX_NZ && nPX_NZ->aoDirty.load(std::memory_order_relaxed))
        || (nNX_PZ && nNX_PZ->aoDirty.load(std::memory_order_relaxed))
        || (nNX_NZ && nNX_NZ->aoDirty.load(std::memory_order_relaxed));

    // =========================================================================
    // Step 1: Populate cell grid
    // =========================================================================

    for (int row = 0; row < rowCount; ++row)
    {
        rowMask[row] = 0u;
        for (int col = 0; col < colCount; ++col)
        {
            grid[row][col].key = 0u;

            // Map the 2D grid coordinates back into 3D chunk local space
            int lc[3] = { 0, 0, 0 };
            lc[ax.layerAxis] = layer;
            lc[ax.rowAxis] = row;
            lc[ax.colAxis] = col;
            const int localX = lc[0], localY = lc[1], localZ = lc[2];

            // Air blocks do not have faces to render
            if (!IsOpaque(chunk.GetUnchecked(localX, localY, localZ)))
                continue;

            // Skip drawing this face if it is entirely hidden by a solid neighbor block
            const glm::ivec3 neighborBlock = glm::ivec3(localX, localY, localZ) + NORMALS[face];
            if (IsNeighborOpaque(chunk, nPX, nNX, nPZ, nNZ, neighborBlock.x, neighborBlock.y, neighborBlock.z))
                continue;

            // Calculate or fetch ambient occlusion values based on neighbor modifications
            uint8_t ao[4];
            ComputeAO(chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ, { localX, localY, localZ }, face, ao);
#ifdef CRABBYDEBUGTEST
            //chunk.aoCache[localX][localY][localZ][face] = PackAO(ao);
            if (rebuildAO)
            {
                // Structural neighborhood changed so calculate new AO values and update cache
                ComputeAO(chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ, { localX, localY, localZ }, face, ao);
                chunk.aoCache[localX][localY][localZ][face] = PackAO(ao);
            }
            else
            {
                // Neighborhood is unchanged so read packed values straight from cache
                UnpackAO(chunk.aoCache[localX][localY][localZ][face], ao);
            }
#endif

            // Flip quad diagonal setup if needed to prevent rendering artifacts
            const BlockType blockType = chunk.GetUnchecked(localX, localY, localZ);
            const BlockDef& def = GetDef(blockType);
            const bool useOverlay = def.useOverlay && face > 1;
            
            // Light is not cached in aoCache, light changes don't change aoDity, cache would go stale
            uint8_t light[4];
            ComputeSmoothLight(chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ,
                { localX, localY, localZ }, face, ao, light);

            const bool flip = ShouldFlip(ao, light);

            grid[row][col].type = blockType;
            memcpy(grid[row][col].ao, ao, 4);
            memcpy(grid[row][col].light, light, 4);
            grid[row][col].key = MakeKey(blockType, ao, useOverlay, flip, light);

            // Mark this cell bit as active and ready for the greedy scan phase
            rowMask[row] |= static_cast<uint16_t>(1u << col);
        }
    }

    // =========================================================================
    // Step 2: Greedy sweep
    // =========================================================================

    for (int row = 0; row < rowCount; ++row)
    {
        uint16_t mask = rowMask[row];

        while (mask != 0u)
        {
            // Hardware instruction counts trailing zeros to jump to the next unmeshed face in one cycle
            const int col = std::countr_zero(mask);
            const uint64_t cellKey = grid[row][col].key;

            // Expand horizontally across the row as long as adjacent block properties match exactly
            int W = 1;
            while (col + W < colCount && ((mask >> (col + W)) & 1u) && grid[row][col + W].key == cellKey)
                ++W;

            // Create a temporary bitmask for the discovered horizontal run
            const uint16_t runMask = static_cast<uint16_t>(((1u << W) - 1u) << col);

            // Expand vertically down neighboring rows if they contain matching active bits and keys
            int H = 1;
            while (row + H < rowCount)
            {
                if ((rowMask[row + H] & runMask) != runMask) break;

                bool keysMatch = true;
                for (int c = col; c < col + W && keysMatch; ++c)
                    if (grid[row + H][c].key != cellKey) keysMatch = false;

                if (!keysMatch)
                    break;

                ++H;
            }

            // Generate vertex data for the combined greedy quad geometry size
            EmitGreedyQuad(face, layer, row, col, H, W, grid, chunkWX, chunkWZ, out);

            // Clear the consumed bits from all processed rows using bitwise bitmasks
            for (int r = row; r < row + H; ++r)
                rowMask[r] &= ~runMask;

            mask &= ~runMask;
        }
    }
}


// =========================================================================
// Two passes:
//   Pass 1 — non-opaque blocks (cross + translucent): old per-face logic
//   Pass 2 — opaque blocks: binary greedy meshing per face per layer
// =========================================================================
void ChunkMeshBuilder::Build(
    const Chunk& chunk,
    const Chunk* nPX, const Chunk* nNX,
    const Chunk* nPZ, const Chunk* nNZ,
    const Chunk* nPX_PZ, const Chunk* nPX_NZ,
    const Chunk* nNX_PZ, const Chunk* nNX_NZ,
    std::vector<Vertex>& outVertices,
    std::vector<Vertex>& waterVertices)
{
    // SAFETY: shared_lock allows N concurrent readers — no deadlock possible between workers
    // even with overlapping neighbor sets. Invariant: workers NEVER acquire unique_lock.
    // Main thread write ops (SetUnchecked) only run when no active MeshJob holds that chunk
    // (enforced by m_chunkMeshUsageGuards). Do NOT change to unique_lock without a full
    // lock-ordering audit.
    // 
    // 1. Always lock the main chunk unconditionally
    // This is a shared lock as ChunhMeshBuilder occasionally builds MUTABLE aoCache and write it into chunk class
    // This file makes sure that it does not read neighboring chunks AO array
    std::shared_lock<std::shared_mutex> lock(chunk.chunkMutex);

    // 2. Declare optional locks for neighbors (initially empty/unlocked)
    // Shared locks as CMB ONLY reads from the neighboring chunks
    // GUARANTEE THAT CMB DOESN'T WRITE ANYTHING INTO NEIGHBORING CHUNKS
    std::optional<std::shared_lock<std::shared_mutex>> lockPX, lockNX, lockPZ, lockNZ;
    std::optional<std::shared_lock<std::shared_mutex>> lockPXPZ, lockPXNZ, lockNXPZ, lockNXNZ;

    // 3. Construct and lock simultaneously only if the pointer is valid
    if (nPX) lockPX.emplace(nPX->chunkMutex);
    if (nNX) lockNX.emplace(nNX->chunkMutex);
    if (nPZ) lockPZ.emplace(nPZ->chunkMutex);
    if (nNZ) lockNZ.emplace(nNZ->chunkMutex);

    if (nPX_PZ) lockPXPZ.emplace(nPX_PZ->chunkMutex);
    if (nPX_NZ) lockPXNZ.emplace(nPX_NZ->chunkMutex);
    if (nNX_PZ) lockNXPZ.emplace(nNX_PZ->chunkMutex);
    if (nNX_NZ) lockNXNZ.emplace(nNX_NZ->chunkMutex);

    outVertices.clear();

    const int chunkWX = chunk.chunkPos.x * CX;
    const int chunkWZ = chunk.chunkPos.y * CZ;

    // ================================================================================
    // Pass 1: Non-opaque blocks (cross + translucent) - Create vertices normally
    // ================================================================================

    for (int y = 0; y < CY; ++y)
    for (int x = 0; x < CX; ++x)
    for (int z = 0; z < CZ; ++z)
    {
        const BlockType blockType = chunk.GetUnchecked(x, y, z);
        const glm::ivec3 worldPos{ chunkWX + x, y, chunkWZ + z };
        const glm::ivec3 localPos{ x, y, z };

        // Skip if the block is air, which is most likely (most of the chunk is filled with air
        if (blockType == BlockType::AIR) [[likely]]
            continue;

        // Emit cross faces (2 textures in a cross fashion) if the block found it a cross item
        if (IsCross(blockType)) [[unlikely]]
        {
            // Final rendering call for rendering cross items
            EmitCross(outVertices, {chunkWX + x, y, chunkWZ + z}, chunk.lightMap[x][y][z], blockType);
            continue;
        }

        // Translucent blocks are rendered separately, no greedy meshing
        if (IsTranslucent(blockType)) [[unlikely]]
        {
            for (int face = 0; face < 6; ++face)
            {
                // Store the position of block perpendicular to the face being looped
                const int neighborX = x + NORMALS[face].x;
                const int neighborY = y + NORMALS[face].y;
                const int neighborZ = z + NORMALS[face].z;

                bool shouldRenderFace = true;

                if (Chunk::InBounds(neighborX, neighborY, neighborZ))
                {
                    const BlockType neighborBlock = chunk.GetUnchecked(neighborX, neighborY, neighborZ);
                    const bool isTranslucent = IsTranslucent(neighborBlock);
                    const bool isSolid       = IsSolid(neighborBlock);

                    shouldRenderFace = !((isSolid || isTranslucent) && !(isTranslucent && neighborBlock != blockType));

                    if (IsWater(blockType) && IsWater(neighborBlock))
                    {
                        shouldRenderFace = false;
                    }

                }
                else
                {
                    // If the neighbor is NOT in the current chunk, query properly
                    BlockType neighborBlock{};
                    bool hasNeighbor = true;

                    if      (neighborX <  0  && nNX) neighborBlock = nNX->GetUnchecked(CX-1     , neighborY, neighborZ);
                    else if (neighborX >= CX && nPX) neighborBlock = nPX->GetUnchecked(0        , neighborY, neighborZ);
                    else if (neighborZ <  0  && nNZ) neighborBlock = nNZ->GetUnchecked(neighborX, neighborY,      CZ-1);
                    else if (neighborZ >= CZ && nPZ) neighborBlock = nPZ->GetUnchecked(neighborX, neighborY,         0);
                    else hasNeighbor = false;

                    if (hasNeighbor)
                    {
                        const bool isTranslucent = IsTranslucent(neighborBlock);
                        const bool isSolid = IsSolid(neighborBlock);
                        shouldRenderFace = !((isSolid || isTranslucent) && !(isTranslucent && neighborBlock != blockType));

                        if (IsWater(blockType) && IsWater(neighborBlock))
                        {
                            shouldRenderFace = false;
                        }
                    }
                    else 
                    {
                        shouldRenderFace = false;  // no neighbor -> face hidden
                    }
                }

                // Render the face regardless if the neighbor is at the world height or 0
                if (neighborY == -1 || neighborY == CY)
                    shouldRenderFace = true;

                if (IsWater(blockType))
                {
                    // Render water at the end
                    if (shouldRenderFace)
                    {
                        AddTranslucentFace(waterVertices, worldPos, localPos, static_cast<Face>(face), blockType, chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ);
                    }
                    continue;
                }

                // Final rendering call to render faces of translucent blocks
                if (shouldRenderFace)
                    AddTranslucentFace(outVertices, worldPos, localPos, static_cast<Face>(face), blockType, chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ);
            }
        }

        // Opaque blocks are handled in Pass 2 (binary greedy meshing)
    }

    // =========================================================================
    // Pass 2: Opaque blocks — binary greedy meshing
    // =========================================================================
    // Outer loop order: face direction -> layer
    // Inner loop (inside BuildLayer): row -> greedy col/height expansion
    //
    // Total layers processed: 6 × (CY + CX + CX + CZ + CZ) = 6 × (256+16+16+16+16)
    // = worst case 1920 layer passes, each on a 16× (16 or 256) grid

    for (int face = 0; face < 6; ++face)
    {
        const int layerCount = FACE_AXES[face].layerCount;
        for (int layer = 0; layer < layerCount; ++layer)
        {
            BuildLayer(chunk, nPX, nNX, nPZ, nNZ, nPX_PZ, nPX_NZ, nNX_PZ, nNX_NZ, face, layer, chunkWX, chunkWZ, outVertices);
        }
    }

    // Safe: main thread's Set() thread-blocks on unique_lock until we release shared_lock
    // If a block changed mid-build, aoDirty=true is already set -> another rebuild follows
    chunk.aoDirty = false;
}
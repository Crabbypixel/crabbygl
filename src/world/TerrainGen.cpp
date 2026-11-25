#include "world/TerrainGen.h"
#include "world/Chunk.h"
#include "world/BlockType.h"

#include "stb/stb_perlin.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
    // Noise fields
    // Continentialness
    constexpr float CONT_FREQ = 1.0f / 1024.0f;     // huge continents/oceans
    constexpr int   CONT_OCT = 5;                   // high octaves = ragged coastlines

    // Erosion
    constexpr float EROS_FREQ = 1.0f / 512.0f;      // mountainous vs flat regions
    constexpr int   EROS_OCT = 4;

    // Peak and Valleys
    constexpr float PV_FREQ = 1.0f / 256.0f;
    constexpr int   PV_OCT = 4;                     // wiggly ridgelines and valley channels
    constexpr bool  PV_FOLD = true;                 // false = raw noise as PV: valleys become 

    // Detailing
    constexpr float DETAIL_FREQ = 1.0f / 64.0f;     // small bumps so splines don't look like clay
    constexpr int   DETAIL_OCT = 4;
    constexpr float DETAIL_AMP = 4.0f;              // blocks, everywhere on land (sea floor gets a quarter)

    // Added only to peaks and mountainous terrain to make it jaggy, not applied to plains and low-lying rolling areas
    constexpr float JAGGED_FREQ = 1.0f / 40.0f;
    constexpr int   JAGGED_OCT = 3;
    constexpr float JAGGED_AMP = 28.0f;             // blocks at full mountainness

    // Spreads narrow fBm output
    constexpr float SPREAD = 2.5f;

    // Surface constants
    constexpr int   DIRT_DEPTH = 3;
    constexpr int   BEACH_HEIGHT = 2;               // land up to SEA_LEVEL + this is sand
    constexpr int   GRAVEL_DEPTH = 12;              // sea floor deeper than this below sea level is gravel
    constexpr int   STONE_LINE = 150;               // bare stone above this
    constexpr int   CLIFF_SLOPE = 3;                // height step to a neighbour >= this = cliff, bare stone
    constexpr bool  DEBUG_AXES = true;              // brick line along world x = 0 / z = 0

    // Salts: one per noise layer so layers never share a field. Never renumber (changes every world)
    constexpr uint32_t SALT_HEIGHT = 1;    // stage 1, now the detail layer
    constexpr uint32_t SALT_CONTINENTAL = 2;
    constexpr uint32_t SALT_EROSION = 3;
    constexpr uint32_t SALT_PEAKS_VALLEYS = 4;
    constexpr uint32_t SALT_JAGGED = 5;

    // lowbias32 (Wellons): cheap integer hash with full avalanche
    constexpr uint32_t Hash(uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352dU;
        x ^= x >> 15; x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    /* stb's own seed param only shifts the first hash lookup by (seed & 255): seed 1 is seed 0 moved
     * one lattice cell in x, and different layers would be correlated. Instead, offset the sample point.
     * stb noise repeats every 256 lattice units, so an offset in [0, 256) reaches every distinct field
     * while keeping coordinates small (float precision stays good far from origin) */
    float SeedOffset(uint32_t seed, uint32_t salt, uint32_t axis)
    {
        const uint32_t h = Hash(seed ^ Hash(salt * 3U + axis));
        return float(h & 0xFFFFFFU) * (256.0f / 16777216.0f);
    }

    float Stretch(float n) { return std::clamp(n * SPREAD, -1.0f, 1.0f); }

    /* Fold: | n | near 2 / 3 -> + 1 (peak), n near 0 -> - 1 (valley).Valleys follow the noise's zero contour,
     * a long winding line, not a blob: that's why valleys read as river channels.
     * (Minecraft calls the unfolded noise "weirdness" and reuses it for biome variants) */
    float FoldPeaksValleys(float n) { return 1.0f - std::abs(3.0f * std::abs(n) - 2.0f); }

    // Surface material from height and local steepness
    // Biomes (last stage) will replace this
    BlockType SurfaceBlock(int height, int slope)
    {
        if (height < TerrainGen::SEA_LEVEL - GRAVEL_DEPTH)  return BlockType::GRAVEL;
        if (height <= TerrainGen::SEA_LEVEL + BEACH_HEIGHT) return BlockType::SAND;
        if (height >= STONE_LINE || slope >= CLIFF_SLOPE)   return BlockType::STONE;
        return BlockType::GRASS_BLOCK;
    }

    // Block below surface
    BlockType SubsurfaceBlock(BlockType surface)
    {
        switch (surface)
        {
        case BlockType::GRASS_BLOCK: return BlockType::DIRT;
        case BlockType::SAND:
        case BlockType::GRAVEL:      return surface;
        default:                     return BlockType::STONE;
        }
    }
}

// ----- Noise2D -----
TerrainGen::Noise2D::Noise2D(uint32_t seed, uint32_t salt)
    : ox(SeedOffset(seed, salt, 0)), oy(SeedOffset(seed, salt, 1)), oz(SeedOffset(seed, salt, 2)) {
}

// 2D field = horizontal slice of 3D Perlin at a seed-chosen y
float TerrainGen::Noise2D::Sample(float x, float z) const
{
    return stb_perlin_noise3(x + ox, oy, z + oz, 0, 0, 0);
}

// Fractal sum, normalized back to [-1, 1] so octave/gain tweaks don't rescale the terrain
float TerrainGen::Noise2D::Fbm(float x, float z, int octaves, float lacunarity, float gain) const
{
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += Sample(x * freq, z * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

// Ridged multifractal-lite: 1 - |n| per octave, squared to sharpen crests, outputs 0..1
float TerrainGen::Noise2D::RidgedFbm(float x, float z, int octaves) const
{
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        const float r = 1.0f - std::abs(Sample(x * freq, z * freq));
        sum += r * r * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

// ----- Splines ---------------
// Fritsch-Carlson monotone cubic Hermite. Plain Catmull-Rom overshoots near sharp steps
// (Kniberg's 0.3 -> 0.4 cliff): a coast would dip below the sea floor next to the step
TerrainGen::Spline::Spline(std::initializer_list<glm::vec2> points) : m_points(points), m_tangents(points.size(), 0.0f)
{
    const size_t n = m_points.size();
    assert(n >= 2);

    std::vector<float> slope(n - 1);
    for (size_t i = 0; i + 1 < n; ++i)
    {
        assert(m_points[i + 1].x > m_points[i].x && "spline x must be strictly ascending");
        slope[i] = (m_points[i + 1].y - m_points[i].y) / (m_points[i + 1].x - m_points[i].x);
    }

    m_tangents[0] = slope[0];
    m_tangents[n - 1] = slope[n - 2];
    for (size_t i = 1; i + 1 < n; ++i)
    {
        if (slope[i - 1] * slope[i] <= 0.0f)    // Local extremum or flat neighbour: zero tangent keeps the curve from overshooting it
            m_tangents[i] = 0.0f;
        else                                    // Harmonic mean: tangent never exceeds 3x either neighbouring slope (monotonicity bound)
            m_tangents[i] = 2.0f / (1.0f / slope[i - 1] + 1.0f / slope[i]);
    }
}

float TerrainGen::Spline::Eval(float x) const
{
    if (x <= m_points.front().x) return m_points.front().y;
    if (x >= m_points.back().x)  return m_points.back().y;

    // Specs have < 10 points, do linear search
    size_t i = 0;
    while (x > m_points[i + 1].x)
        ++i;

    const glm::vec2 p0 = m_points[i], p1 = m_points[i + 1];
    const float h = p1.x - p0.x;
    const float t = (x - p0.x) / h;
    const float t2 = t * t, t3 = t2 * t;

    return (2 * t3 - 3 * t2 + 1) * p0.y + (t3 - 2 * t2 + t) * h * m_tangents[i]
        + (-2 * t3 + 3 * t2) * p1.y + (t3 - t2) * h * m_tangents[i + 1];
}

// ----- TerrainGen -----
// The splines ARE the knobs: move the dots, rebuild, delete saves/, fly.
// x = noise value (-1..1). y = see member comment in TerrainGen.h
TerrainGen::TerrainGen(uint32_t seed)
    : m_seed(seed)
    , m_continentalNoise(seed, SALT_CONTINENTAL), m_erosionNoise(seed, SALT_EROSION), m_peaksValleysNoise(seed, SALT_PEAKS_VALLEYS) , m_detailNoise(seed, SALT_HEIGHT), m_jaggedNoise(seed, SALT_JAGGED)
    , m_baseHeight({            // continentalness -> blocks
        { -1.00f,  24.0f },     // deep ocean
        { -0.45f,  34.0f },
        { -0.20f,  50.0f },     // continental shelf
        { -0.08f,  60.0f },     // shallows
        { 0.00f,  64.0f },      // coastline (sea = 62)
        { 0.25f,  70.0f },      // lowlands
        { 0.30f,  82.0f },      // Kniberg's step: jump onto a plateau...
        { 0.55f,  86.0f },      // ...that stays flat
        { 1.00f, 100.0f } }),   // far inland
    m_inland({                  // continentalness -> 0..1
        { -1.00f, 0.0f },
        { -0.10f, 0.0f },       // no mountains rising out of the sea
        { 0.10f, 0.5f },
        { 0.40f, 1.0f },
        { 1.00f, 1.0f } }),
    m_relief({                  // erosion -> amplitude in blocks
        { -1.00f, 95.0f },      // low erosion: big mountains
        { -0.50f, 60.0f },
        { -0.15f, 28.0f },
        { 0.25f, 10.0f },
        { 0.60f,  4.0f },
        { 1.00f,  2.0f } }),    // high erosion: flat no matter what
    m_peaksValleys({            // PV -> multiplier on relief
        { -1.00f, -0.55f },     // valley floor: cuts below base, under sea level = rivers/lakes
        { -0.40f, -0.10f },
        { 0.00f,  0.15f },
        { 0.40f,  0.55f },
        { 1.00f,  1.00f } })    // summit
{}

TerrainGen::ColumnInfo TerrainGen::SampleColumn(int wx, int wz) const
{
    const float fwx = float(wx), fwz = float(wz);

    ColumnInfo info{};
    const float continentalNoise = Stretch(m_continentalNoise .Fbm(fwx * CONT_FREQ, fwz * CONT_FREQ, CONT_OCT, 2.0f, 0.5f));
    const float erosionNoise     = Stretch(m_erosionNoise     .Fbm(fwx * EROS_FREQ, fwz * EROS_FREQ, EROS_OCT, 2.0f, 0.5f));
    const float pvNoise          = Stretch(m_peaksValleysNoise.Fbm(fwx * PV_FREQ,   fwz * PV_FREQ,   PV_OCT,   2.0f, 0.5f));

    info.continentalness = continentalNoise;
    info.erosion         = erosionNoise;
    info.peaksValleys    = PV_FOLD ? FoldPeaksValleys(pvNoise) : pvNoise;

    const float base     = m_baseHeight  .Eval(info.continentalness);
    const float inland   = m_inland      .Eval(info.continentalness);
    const float erosion  = m_relief      .Eval(info.erosion);
    const float pv       = m_peaksValleys.Eval(info.peaksValleys);

    const float detail   = m_detailNoise.Fbm(fwx * DETAIL_FREQ, fwz * DETAIL_FREQ, DETAIL_OCT, 2.0f, 0.5f) * SPREAD * DETAIL_AMP * (0.25f + 0.75f * inland);
    const float relief   = erosion * inland;

    // Make jagged peaks
    // 0 on plains/coast, 1 on a low-erosion summit. Valleys (PV < 0) get none: keeps valley floors walkable
    const float mountainness = std::clamp(erosion / 95.0f, 0.0f, 1.0f) * inland * std::max(info.peaksValleys, 0.0f);
    const float jagged = mountainness > 0.0f   // skips 3 samples for most columns
        ? m_jaggedNoise.RidgedFbm(fwx * JAGGED_FREQ, fwz * JAGGED_FREQ, JAGGED_OCT) * JAGGED_AMP * mountainness
        : 0.0f;

    // Simplification of Minecraft's nested splines (each C point owning an E spline owning a PV spline):
    // here the three act independently and multiply. Enough to get oceans, plains, plateaus, ranges, valleys
    const int h = int(std::floor(base + relief * pv + detail + jagged));
    info.height = std::clamp(h, 1, CY - 2);
    return info;
}

int TerrainGen::SurfaceHeight(int wx, int wz) const
{
    return SampleColumn(wx, wz).height;
}

// Runs on a gen worker; chunk is private until staged, so unchecked writes are race-free
void TerrainGen::GenerateChunk(Chunk& chunk, glm::ivec2 coord) const
{
    // Heightmap with a 1-column border: slope at the chunk edge needs the neighbour chunk's heights.
    // Recomputed, not read from the neighbour, so chunks stay independent (no cross-chunk reads)
    constexpr int W = CX + 2, D = CZ + 2;
    int heights[W][D];

    const int baseX = coord.x * CX, baseZ = coord.y * CZ;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            heights[x][z] = SurfaceHeight(baseX + x - 1, baseZ + z - 1);

    for (int x = 0; x < CX; ++x)
        for (int z = 0; z < CZ; ++z)
        {
            const int height = heights[x + 1][z + 1];
            const int slope = std::max({ std::abs(height - heights[x][z + 1]), std::abs(height - heights[x + 2][z + 1]),
                                          std::abs(height - heights[x + 1][z]), std::abs(height - heights[x + 1][z + 2]) });

            const BlockType surface = SurfaceBlock(height, slope);
            const BlockType subsurface = SubsurfaceBlock(surface);
            const int dirtBase = std::max(1, height - DIRT_DEPTH);

            chunk.SetUnchecked(x, 0, z, BlockType::BEDROCK);

            for (int y = 1; y < dirtBase; ++y)
                chunk.SetUnchecked(x, y, z, BlockType::STONE);

            for (int y = dirtBase; y < height; ++y)
                chunk.SetUnchecked(x, y, z, subsurface);

            chunk.SetUnchecked(x, height, z, surface);

            // Source blocks only: a flat still surface. Physics wakes the border ones on load, they
            // each evaluate once and settle (unloaded neighbours read as walls)
            for (int y = height + 1; y <= SEA_LEVEL; ++y)
                chunk.SetUnchecked(x, y, z, BlockType::WATER);

            if (DEBUG_AXES && (baseX + x == 0 || baseZ + z == 0) && height >= SEA_LEVEL)
                chunk.SetUnchecked(x, height + 1, z, BlockType::BRICK);
        }
}

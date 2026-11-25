#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <glm/glm.hpp>

class Chunk;

// One per world, owned by World. Immutable after construction: every member is const and every
// method is const, so the gen workers call it concurrently with zero locking. Construction happens
// in World's ctor, before StartChunkLoadWorkers, so thread start publishes it (happens-before).
// Never add mutable state (caches, counters) here: that needs per-thread storage or a lock.
class TerrainGen
{
public:
    static constexpr int SEA_LEVEL = 62;

    // Everything that decided one column's height; for the debug overlay (Kniberg's F3 "C E PV")
    struct ColumnInfo
    {
        float continentalness;
        float erosion;
        float peaksValleys;
        int   height;
    };

    explicit TerrainGen(uint32_t seed);

    TerrainGen(const TerrainGen&) = delete;
    TerrainGen& operator=(const TerrainGen&) = delete;

    void GenerateChunk(Chunk& chunk, glm::ivec2 coord) const;

    [[nodiscard]] int        SurfaceHeight(int wx, int wz) const;
    [[nodiscard]] ColumnInfo SampleColumn(int wx, int wz) const;
    [[nodiscard]] uint32_t   Seed() const noexcept { return m_seed; }

private:
    // 2D Perlin field decorrelated per (seed, salt); see TerrainGen.cpp
    struct Noise2D
    {
        float ox, oy, oz;

        Noise2D(uint32_t seed, uint32_t salt);
        [[nodiscard]] float Sample(float x, float z) const;
        [[nodiscard]] float Fbm(float x, float z, int octaves, float lacunarity, float gain) const;
        [[nodiscard]] float RidgedFbm(float x, float z, int octaves) const;
    };

    const uint32_t m_seed;

    const Noise2D m_continentalNoise;
    const Noise2D m_erosionNoise;
    const Noise2D m_peaksValleysNoise;
    const Noise2D m_detailNoise;
    const Noise2D m_jaggedNoise;

    // Piecewise monotone cubic through the points (x ascending) to define various noise values
    class Spline
    {
    public:
        Spline(std::initializer_list<glm::vec2> points);
        [[nodiscard]] float Eval(float x) const;

    private:
        std::vector<glm::vec2> m_points;
        std::vector<float>     m_tangents;  // precomputed once; Eval is read-only
    };

    const Spline m_baseHeight;      // continentalness -> height (oceans, coast, inland)
    const Spline m_inland;          // continentalness -> 0..1, how much relief is allowed (0 at sea)
    const Spline m_relief;          // erosion -> relief amplitude in blocks (low erosion = mountains)
    const Spline m_peaksValleys;    // PV -> -1..1 multiplier on relief (valley floor .. summit)
};

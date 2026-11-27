#pragma once
#include "world/BlockType.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <atomic>
#include <shared_mutex>
#include <array>
#include <cstring>
#include <iosfwd>

constexpr int CX = 16;
constexpr int CY = 256;
constexpr int CZ = 16;
constexpr int CHUNK_VOLUME = CX * CY * CZ;

class Chunk
{
private:
	// Core block data
	// Linearized access for better cache performance in meshing and serialization
	// ** Z is the fastest moving axis, then X, then Y (YXZ order) to optimize for horizontal locality during meshing **
	std::array<BlockType, CHUNK_VOLUME> blocks{};

	// Index - YXZ ordering
	[[nodiscard]] static constexpr int GetIndex(int x, int y, int z) noexcept
	{
		return (y * CX * CZ) + (x * CZ) + z;
	}

	// Light buffer - most significant 4 bits for skylight, least significant 4 bits for block light
	// Singular color (white) for block light, to be expanded later
	// TODO: Maybe consider linearizing this
	// IMP TODO: Add separate lightMutex for lightMap, doesn't stall chunk writes/reads as lightMap is constantly re-written
	uint8_t lightMap[CX][CY][CZ];

	// Friend class, they mainly read/write aoCache and lightMap directly
	friend class ChunkMeshBuilder;
	friend class LightingSystem;

public:
	explicit Chunk(glm::ivec2 pos) : chunkPos(pos) { memset(lightMap, 0, sizeof(lightMap)); }
	Chunk(const Chunk&) = delete;
	Chunk(Chunk&&) = delete;
	Chunk& operator=(const Chunk&) = delete;
	Chunk& operator=(Chunk&&) = delete;

	// Protects: blocks, aoCache & lightMap
	mutable std::shared_mutex chunkMutex;

	// World position of this chunk
	const glm::ivec2 chunkPos;

	// CRITICAL: Main thread owns the world state, workers CANNOT modify
	bool dirty = true;				// Needs mesh rebuild
	bool modified = false;			// Has unsaved changes (never cleared except after SaveChunkToDisk)
	mutable std::atomic<bool> aoDirty = true;			// For computing AO values

	[[nodiscard]] BlockType Get(int x, int y, int z) const noexcept;
	[[nodiscard]] BlockType GetUnchecked(int x, int y, int z) const;
	void Set(int x, int y, int z, BlockType type);
	void SetUnchecked(int x, int y, int z, BlockType type);

	[[nodiscard]] uint8_t GetLightMap(int x, int y, int z) const noexcept { return lightMap[x][y][z]; }

	void Serialize(std::ofstream& outputStream) const;
	[[nodiscard]] bool Deserialize(std::ifstream& inputStream);

	// Range check
	[[nodiscard]] static constexpr bool InBounds(int x, int y, int z) noexcept { return (x >= 0 && x < CX) 
																					 && (y >= 0 && y < CY) 
																					 && (z >= 0 && z < CZ); }
};

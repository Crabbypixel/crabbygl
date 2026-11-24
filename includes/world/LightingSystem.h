#pragma once

#include "world/Chunk.h"

#include <queue>
#include <unordered_map>

class World;

struct LightNode
{
	LightNode(uint16_t index, Chunk* chunk) : index(index), chunk(chunk) {}

	uint16_t index;
	Chunk* chunk;
};

struct LightRemovalNode
{
	LightRemovalNode(uint16_t index, uint8_t v, Chunk* chunk) : index(index), val(v), chunk(chunk) {}

	uint16_t index;
	uint8_t val;
	Chunk* chunk;
};

class LightingSystem
{
private:
	World* m_world = nullptr;

	std::queue<LightNode> m_torchlightBFSQueue;
	std::queue<LightRemovalNode> m_torchlightRemovalBFSQueue;

	std::queue<LightNode> m_sunlightBFSQueue;
	std::queue<LightRemovalNode> m_sunlightRemovalBFSQueue;

	std::unordered_map<Chunk*, uint8_t> m_visitedChunks;

	void Touch(Chunk* c, int x, int z);
	void FlushDirty();

	Chunk* m_lastVisited = nullptr;
	void MarkVisited(Chunk* c);

	// Non-copyable, non-movable
	LightingSystem(const LightingSystem&) = delete;
	LightingSystem(LightingSystem&&) = delete;
	LightingSystem& operator=(const LightingSystem&) = delete;
	LightingSystem& operator=(LightingSystem&&) = delete;

	// Light accessors
	int  GetTorchLight(Chunk* chunk, int x, int y, int z);
	int  GetSunlight(Chunk* chunk, int x, int y, int z);
	void SetTorchLight(Chunk* chunk, int x, int y, int z, int val);
	void SetSunlight(Chunk* chunk, int x, int y, int z, int val);

	void SetTorchLightUnsafe(Chunk* chunk, int x, int y, int z, int val);
	void SetSunlightUnsafe(Chunk* chunk, int x, int y, int z, int val);

	// Torchlight BFS passes:
	void PropagateTorch();
	void RemoveTorch();

	// Sunlight BFS passes:
	void PropagateSunlight();
	void RemoveSunlight();

public:
	LightingSystem(World* world) : m_world(world) {}

	void InitChunkLight(Chunk* chunk);
	static void BuildLocalLight(Chunk& chunk);	// Worker-side, chunk must be exclusively owned by the caller

	void NotifyBlockPlaced(int wx, int wy, int wz, BlockType type);
	void NotifyBlockRemoved(int wx, int wy, int wz);
	void Update();
};
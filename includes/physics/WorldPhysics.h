#pragma once
#include <glm/glm.hpp>

#include <unordered_map>
#include <queue>
#include <unordered_set>

class World;
class Chunk;
enum class BlockType : uint8_t;

class WorldPhysics
{
public:
    WorldPhysics(World* world) : m_world(world) {}

	// No copying or moving - physics state is tightly coupled to the world and shouldn't be duplicated
	WorldPhysics(const WorldPhysics&) = delete;
	WorldPhysics(WorldPhysics&&) = delete;
    WorldPhysics& operator=(const WorldPhysics&) = delete;
	WorldPhysics& operator=(WorldPhysics&&) = delete;

	static constexpr float TICK_RATE = 20.0f;		            // 20 ticks per second
	static constexpr float TICK_DT = 1.0f / TICK_RATE;
    static constexpr int MAX_WATER_SPREAD = 7;

    // Init water blocks on chunk load
    void InitChunkWater(Chunk* chunk);

	// Call every frame with real delta time, but physics will only update at fixed intervals (TICK_DT)
	void Update(float dt);

	// Call when any block is placed or broken by the player
	void NotifyBlockChanged(int wx, int wy, int wz);

    // Call when player places water source block
    void NotifyWaterSourcePlaced(int wx, int wy, int wz);

    // Call when player removes a block - handles both source and flowing water blocks
    void NotifyBlockRemoved(int wx, int wy, int wz);

private:
    World* m_world = nullptr;
	float m_accumulator = 0.0f;		// Accumulated time since last physics update

    // Boost-combine hash for glm::ivec3
    // Simple XOR hashes cluster badly — positions on the same axis collide
    struct IVec3Hash
    {
        size_t operator()(const glm::ivec3& v) const noexcept
        {
            size_t h = 0;
            auto combine = [&](int n)
            {
                // Spreads bits across the full size_t range
                // Breaks the symmetry that XOR lacks
                h ^= std::hash<int>()(n) + 0x9e3779b9u + (h << 6) + (h >> 2);
            };

            combine(v.x);
            combine(v.y);
            combine(v.z);

            return h;
        }
    };

    using DirtySet = std::unordered_set<glm::ivec3, IVec3Hash>;
    using LevelMap = std::unordered_map<glm::ivec3, int, IVec3Hash>;

	DirtySet m_dirtyBlocks;		// Set of block positions that changed since last physics update
	DirtySet m_nextDirtyBlocks;	// Set of block positions that will be dirty in the next physics update

    std::unordered_set<glm::ivec3, IVec3Hash> m_waterSources;   // Permanent water source positions, not moved by physics
    LevelMap m_waterLevels;         // Spread level for each flowing block -> level 7 -> just left a source, level 0 -> stops

    DirtySet m_waterDirty;     // Set of flowing water positions that changed since last physics update
    DirtySet m_nextDirtyWater; // Set of flowing water positions that will be dirty in the next physics update

    std::queue<glm::ivec3> m_waterRemovalQueue;     // Water removal BFS

	// Runs one tick of physics updates, processing all blocks in m_dirtyBlocks and 
    // updating m_nextDirtyBlocks with any blocks that need to be re-evaluated on the next tick
    void Tick();

    void TickWater();
    void ProcessWaterRemoval();

	// Returns true if the block type is affected by gravity (e.g. sand, gravel)
	[[nodiscard]] static bool IsGravityBlock(BlockType type) noexcept;
    [[nodiscard]] static bool IsWaterBlock(BlockType type) noexcept;
};
#include "physics/WorldPhysics.h"
#include "world/BlockType.h"
#include "world/World.h"
#include "world/BlockRegistry.h"

#include <iostream>

void WorldPhysics::InitChunkWater(Chunk* chunk)
{
	if (!chunk)
		return;

	const glm::ivec2& chunkPos = chunk->chunkPos;	

	for(int y = 0; y < CY; ++y)
	for(int x = 0; x < CX; ++x)
	for(int z = 0; z < CZ; ++z)
	{
		BlockType type = chunk->GetUnchecked(x, y, z);
		if (type == BlockType::AIR)
			continue;

		int wx = chunkPos.x * CX + x;
		int wz = chunkPos.y * CZ + z;
		glm::ivec3 pos(wx, y, wz);

		if (type == BlockType::WATER)
		{
			m_waterSources.insert(pos);
			m_waterLevels[pos] = MAX_WATER_SPREAD;
			m_nextDirtyWater.insert(pos);
		}
		else if (type == BlockType::WATER_FLOWING)
		{
			m_waterRemovalQueue.push(pos);

		}
	}
}

void WorldPhysics::Update(float dt)
{
	// After a long stall (expensive frame), simulate this many steps only
	// This makes the world to briefly run slow instead of fast-forwarding or spiralling
	static constexpr int MAX_TICKS_PER_FRAME = 5;

	// Only report stalls of 1s+; shorter hitches are normal and would spam the log
	static constexpr int SKIP_LOG_THRESHOLD = static_cast<int>(TICK_RATE);

	m_accumulator += dt;

	int ticks = 0;
	while (m_accumulator >= TICK_DT && ticks < MAX_TICKS_PER_FRAME)
	{
		Tick();
		m_accumulator -= TICK_DT;
		++ticks;
	}

	if (m_accumulator >= TICK_DT)
	{
		const int skippedTicks = static_cast<int>(m_accumulator / TICK_DT);

		if (skippedTicks >= SKIP_LOG_THRESHOLD)
		{
			std::cerr << "Can't keep up! Running " << static_cast<int>(m_accumulator * 1000.0f)
				<< " ms behind, skipping " << skippedTicks << " ticks\n";

			m_accumulator = 0.0f;
		}
	}
}

void WorldPhysics::NotifyBlockChanged(int wx, int wy, int wz)
{
	m_nextDirtyBlocks.insert({ wx, wy, wz });
	m_nextDirtyBlocks.insert({ wx, wy + 1, wz });
}


void WorldPhysics::NotifyWaterSourcePlaced(int wx, int wy, int wz)
{
	glm::ivec3 pos{ wx, wy, wz };
	m_waterSources.insert(pos);
	m_waterLevels[pos] = MAX_WATER_SPREAD;		// Source has max spread
	m_nextDirtyWater.insert(pos);
}

void WorldPhysics::NotifyBlockRemoved(int wx, int wy, int wz)
{
	glm::ivec3 pos{ wx, wy, wz };

	// If a source was removed, queue removal BFS
	if (m_waterSources.count(pos))
	{
		m_waterSources.erase(pos);
		m_waterLevels.erase(pos);
		
		static const glm::ivec3 ALL_DIRS[] = {
			{1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,-1,0}
		};
		for (const auto& d : ALL_DIRS)
		{
			glm::ivec3 nb = pos + d;
			if (IsWaterBlock(m_world->GetBlock(nb.x, nb.y, nb.z)) && !m_waterSources.count(nb))
			{
				m_waterRemovalQueue.push(nb);
			}
		}
	}

	static const glm::ivec3 WATER_DIRS[] = {
		{1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,1,0}
	};

	for (const auto& d : WATER_DIRS)
	{
		glm::ivec3 nb = pos + d;
		if (IsWaterBlock(m_world->GetBlock(nb.x, nb.y, nb.z)))
			m_nextDirtyWater.insert(nb);
	}

	// If block above is water, it may now fall (mark it dirty)
	glm::ivec3 above{ wx, wy + 1, wz };
	if(IsWaterBlock(m_world->GetBlock(above.x, above.y, above.z)))
		m_nextDirtyWater.insert(above);

	NotifyBlockChanged(wx, wy, wz);
}

void WorldPhysics::Tick()
{
	// Process each dirty block and determine if it needs to fall due to gravity, and if so, 
	// update the world and mark any affected blocks as dirty for the next tick
	for (const glm::ivec3& pos : m_dirtyBlocks)
	{
		// Get the block type at the current position
		BlockType type = m_world->GetBlock(pos.x, pos.y, pos.z);

		// Check if gravity block
		if (!IsGravityBlock(type))
			continue;

		// Prevent falling into the void
		if (pos.y <= 0)
			continue;

		// Get the block type below
		glm::ivec3 belowPos = { pos.x, pos.y - 1, pos.z };
		BlockType belowType = m_world->GetBlock(belowPos.x, belowPos.y, belowPos.z);

		// If the block below is air, fall down else continue
		if (belowType != BlockType::AIR)
			continue;

		// The below block is air, so move the gravity block down by one and mark the new position dirty for the next tick
		m_world->SetBlock(pos.x, pos.y, pos.z, BlockType::AIR);
		m_world->SetBlock(belowPos.x, belowPos.y, belowPos.z, type);

		// Falling blocks can trigger additional falling blocks above them on subsequent ticks
		// Mark the source position dirty so the block above gets re-evaluated,
		// and mark the block below the new position dirty for future gravity updates
		m_nextDirtyBlocks.insert({ pos.x, pos.y + 1, pos.z });
		m_nextDirtyBlocks.insert({ pos.x, pos.y - 1, pos.z });
		m_nextDirtyBlocks.insert({ pos.x + 1, pos.y, pos.z });
		m_nextDirtyBlocks.insert({ pos.x - 1, pos.y, pos.z });
		m_nextDirtyBlocks.insert({ pos.x, pos.y, pos.z + 1 });
		m_nextDirtyBlocks.insert({ pos.x, pos.y, pos.z - 1 });
	}

	std::swap(m_dirtyBlocks, m_nextDirtyBlocks);
	m_nextDirtyBlocks.clear();

	ProcessWaterRemoval();

	TickWater();
}

void WorldPhysics::TickWater()
{
	// Process all water-dirty blocks this tick
	// Each block gets one action: fall or spread (fall is more prioritized)

	for (const glm::ivec3& pos : m_waterDirty)
	{
		BlockType type = m_world->GetBlock(pos.x, pos.y, pos.z);

		// If the block is changed other than water
		if (!IsWaterBlock(type))
			continue;

		// Don't go beyond world limits
		if (pos.y <= 0)
			continue;

		bool isSource = m_waterSources.count(pos) > 0;
		int myLevel = isSource ? MAX_WATER_SPREAD : m_waterLevels.count(pos) ? m_waterLevels[pos] : 0;


		// -- Try to fall --
		glm::ivec3 below{ pos.x, pos.y - 1, pos.z };
		BlockType belowType = m_world->GetBlock(below.x, below.y, below.z);

		if (belowType == BlockType::AIR)
		{
			// Place flowing water below - falling inherits MAX spread
			m_world->SetBlock(below.x, below.y, below.z, BlockType::WATER_FLOWING);
			m_waterLevels[below] = myLevel;
			m_nextDirtyWater.insert(below);

			// Source blocks stay, flowing blocks that fall leave nothing above
			// Check if the above block is not a water source, then leave it as air
			if (!isSource)
			{
				m_world->SetBlock(pos.x, pos.y, pos.z, BlockType::AIR);
				m_waterLevels.erase(pos);
			}
			else
			{
				m_nextDirtyWater.insert(pos);
			}

			continue;
		}

		if (belowType == BlockType::WATER_FLOWING)
		{
			m_nextDirtyWater.insert(pos);
			continue;
		}

		// Can't fall - try horizontal spread

		// Maximum spread reached, no more spreading
		if (myLevel <= 0)
			continue;

		static const glm::ivec3 HORIZONTAL[] = {
	{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}
		};

		// Collect candidates: separate fall paths from flat paths
		std::vector<glm::ivec3> fallTargets;
		std::vector<glm::ivec3> flatTargets;

		for (const auto& dir : HORIZONTAL)
		{
			glm::ivec3 n = pos + dir;
			if (m_world->GetBlock(n.x, n.y, n.z) != BlockType::AIR) continue;

			glm::ivec3 nBelow{ n.x, n.y - 1, n.z };
			if (m_world->GetBlock(nBelow.x, nBelow.y, nBelow.z) == BlockType::AIR)
				fallTargets.push_back(n);   // this direction leads to a drop
			else
				flatTargets.push_back(n);   // this direction is flat
		}

		// Prefer fall targets — only spread flat if no fall path exists
		const auto& targets = fallTargets.empty() ? flatTargets : fallTargets;

		for (const auto& n : targets)
		{
			int newLevel = myLevel - 1;

			m_world->SetBlock(n.x, n.y, n.z, BlockType::WATER_FLOWING);

			auto it = m_waterLevels.find(n);
			if (it == m_waterLevels.end() || it->second < newLevel)
				m_waterLevels[n] = newLevel;

			m_nextDirtyWater.insert(n);
		}

		if (isSource)
			m_nextDirtyWater.insert(pos);
	}

	std::swap(m_waterDirty, m_nextDirtyWater);
	m_nextDirtyWater.clear();
}

void WorldPhysics::ProcessWaterRemoval()
{
	while (!m_waterRemovalQueue.empty())
	{
		glm::ivec3 pos = m_waterRemovalQueue.front();
		m_waterRemovalQueue.pop();

		BlockType type = m_world->GetBlock(pos.x, pos.y, pos.z);
		if (!IsWaterBlock(type)) continue;

		// Surviving source — don't remove, re-propagate from it instead
		if (m_waterSources.count(pos))
		{
			m_nextDirtyWater.insert(pos);
			continue;
		}

		// Remove unconditionally — no neighbor check needed
		m_world->SetBlock(pos.x, pos.y, pos.z, BlockType::AIR);
		m_waterLevels.erase(pos);

		// Cascade to ALL connected water blocks
		static const glm::ivec3 ALL_DIRS[] = {
			{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,-1,0}
		};
		for (const auto& d : ALL_DIRS)
		{
			glm::ivec3 nb = pos + d;
			if (IsWaterBlock(m_world->GetBlock(nb.x, nb.y, nb.z)))
				m_waterRemovalQueue.push(nb);
		}
	}

	// Re-seed ALL surviving sources after removal
	// Handles case where another source was feeding some of the removed blocks
	for (const auto& src : m_waterSources)
		m_nextDirtyWater.insert(src);
}

bool WorldPhysics::IsGravityBlock(BlockType type) noexcept
{
	return type == BlockType::SAND || type == BlockType::GRAVEL;
}

bool WorldPhysics::IsWaterBlock(BlockType type) noexcept
{
	return type == BlockType::WATER || type == BlockType::WATER_FLOWING;
}
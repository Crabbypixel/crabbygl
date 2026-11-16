#include "physics/WorldPhysics.h"
#include "world/BlockType.h"
#include "world/World.h"
#include "world/BlockRegistry.h"

#include <iostream>

namespace
{
	constexpr glm::ivec3 UP{ 0, 1, 0 };
	constexpr glm::ivec3 DOWN{ 0, -1, 0 };
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

void WorldPhysics::Tick()
{
	// Process each dirty block and determine if it needs to fall due to gravity, and if so, 
	// update the world and mark any affected blocks as dirty for the next tick
	for (const glm::ivec3& pos : m_dirtyBlocks)
	{
		// Get the block type at the current position
		BlockType type = Read(pos);

		// Check if gravity block
		if (!IsGravityBlock(type))
			continue;

		// Get the block type below
		glm::ivec3 belowPos = { pos.x, pos.y - 1, pos.z };
		BlockType belowType = Read(belowPos);

		// If the block below is air, fall down else continue
		if (belowType != BlockType::AIR)
			continue;

		// The below block is air, so move the gravity block down by one and mark the new position dirty for the next tick
		m_world->SetBlock(pos.x, pos.y, pos.z, BlockType::AIR);
		m_world->SetBlock(belowPos.x, belowPos.y, belowPos.z, type);

		// Falling blocks can trigger additional falling blocks above them on subsequent ticks
		// Mark the source position dirty so the block above gets re-evaluated,
		// and mark the block below the new position dirty for future gravity updates
		m_nextDirtyBlocks.insert(pos + UP);
		m_nextDirtyBlocks.insert(belowPos);
	}

	std::swap(m_dirtyBlocks, m_nextDirtyBlocks);
	m_nextDirtyBlocks.clear();
}

bool WorldPhysics::IsGravityBlock(BlockType type) noexcept
{
	return type == BlockType::SAND || type == BlockType::GRAVEL;
}

BlockType WorldPhysics::Read(const glm::ivec3& p) const
{
	if (p.y < 0)
		return BlockType::BEDROCK;

	if (p.y >= CY)
		return BlockType::AIR;

	// Unloaded = wall
	const Chunk* chunk = m_world->GetChunk(p.x, p.z);
	if (!chunk)
		return BlockType::BEDROCK;

	const glm::ivec3 l = World::ChunkLocalCoord(p.x, p.y, p.z);
	return chunk->Get(l.x, l.y, l.z);
}

void WorldPhysics::OnChunkLoaded(const Chunk& chunk)
{
	(void)chunk;
}
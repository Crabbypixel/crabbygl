#include "physics/WorldPhysics.h"
#include "world/BlockType.h"
#include "world/World.h"
#include "world/BlockRegistry.h"

#include <iostream>

namespace
{
	constexpr glm::ivec3 UP{ 0, 1, 0 };
	constexpr glm::ivec3 DOWN{ 0, -1, 0 };
	constexpr glm::ivec3 HORIZONTAL[4] = { { 1,0,0 },{ -1,0,0 },{ 0,0,1 },{ 0,0,-1 } };

	// Water can move into these
	bool IsReplaceable(BlockType t) noexcept { return t == BlockType::AIR; }

	// A floor water can rest and spread, if water below is NOT a floor: flowing water
	// sitting on water must not skate across the surface
	bool BlocksFlow(BlockType t) noexcept { return !IsReplaceable(t) && !IsWater(t); }

	bool IsSource(BlockType t) { return t == BlockType::WATER; }

	// Returns 0 for source and falling, 1 to 7 for flowing water
	int FlowDistance(BlockType t) noexcept
	{
		if (t == BlockType::WATER || t == BlockType::WATER_FALLING)
			return 0;

		return static_cast<int>(t) - static_cast<int>(BlockType::WATER_FLOWING_1) + 1;
	}

	BlockType MakeFlowing(int dist) noexcept
	{
		return static_cast<BlockType>(static_cast<int>(BlockType::WATER_FLOWING_1) + dist - 1);
	}

	// Used for merging - prioritize the better-fed flowing water when two proposals hit the same cell
	int Strength(BlockType t)
	{
		if (t == BlockType::WATER) return 9;
		if (t == BlockType::WATER_FALLING) return 8;
		if (IsWater(t)) return 8 - FlowDistance(t);
		return 0;
	}

	static_assert(static_cast<int>(BlockType::WATER_FLOWING_7) - static_cast<int>(BlockType::WATER_FLOWING_1) + 1 
		== WorldPhysics::MAX_FLOW_DIST, "Flowing water ids must be contiguous and match MAX_FLOW_DIST");
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
	DirtyWaterAround({ wx, wy, wz });
}

void WorldPhysics::Tick()
{
	++m_tickCount;

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

	if (m_tickCount % WATER_TICK_INTERVAL == 0)
		TickWater();
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

void WorldPhysics::DirtyWaterAround(const glm::ivec3& p)
{
	// The cell itself (may be water) plus every neighbor that may flow into it or lost its feed
	m_nextWaterDirty.insert(p);
	m_nextWaterDirty.insert(p + UP);
	m_nextWaterDirty.insert(p + DOWN);
	for (const auto& d : HORIZONTAL)
		m_nextWaterDirty.insert(p + d);
}

void WorldPhysics::ProposeWrite(const glm::ivec3& p, BlockType type)
{
	auto [it, inserted] = m_waterWrites.try_emplace(p, type);
	if (!inserted && Strength(type) > Strength(it->second))
		it->second = type;
}

bool WorldPhysics::SpreadSideways(const glm::ivec3& p, BlockType type) const
{
	const BlockType	below = Read(p + DOWN);

	// Water that can fall down always falls, does not spread sideways
	if (IsReplaceable(below))
		return false;

	// Sources spread over anything, flowing/falling water over a real floor
	return IsSource(type) || BlocksFlow(below);
}

void WorldPhysics::ComputeOutflow(const glm::ivec3& p, BlockType state)
{
	const glm::ivec3 belowPos = p + DOWN;
	const BlockType	belowBlock = Read(belowPos);

	// Falling has a priority and excludes sideways spread: a source on a ledge makes waterfall, not spread
	if (IsReplaceable(belowBlock))
	{
		ProposeWrite(belowPos, BlockType::WATER_FALLING);
		return;
	}

	// If doesn't spread sideways, return
	if (!SpreadSideways(p, state))
		return;

	const int nextFlow = FlowDistance(state) + 1;
	if (nextFlow > MAX_FLOW_DIST)
		return;

	const BlockType nextWaterFlow = MakeFlowing(nextFlow);
	for (const auto& d : HORIZONTAL)
	{
		const glm::ivec3 n = p + d;
		const BlockType t = Read(n);

		// Upgrade weaker flowing water if a stronger one exists, a second source can raise this flow
		const bool isFlowWeaker = IsWater(t) && !IsSource(t) && t != BlockType::WATER_FALLING && FlowDistance(t) > nextFlow;
		if (IsReplaceable(t) || isFlowWeaker)
			ProposeWrite(n, nextWaterFlow);
	}
}

void WorldPhysics::TickWater()
{
	// Swap first: everything collected since the last step is this step's work, and anything this step 
	// dirties lands in the fresh set for the next step, similar to sand/gravel physics
	std::swap(m_waterDirty, m_nextWaterDirty);
	m_nextWaterDirty.clear();

	// Phase 1: Evaulate against pre-step world, no writes here
	for (const glm::ivec3& pos : m_waterDirty)
	{
		const BlockType currentCell = Read(pos);
		if (!IsWater(currentCell))		// Air cells get water through their neighbors' outflow
			continue;

		ComputeOutflow(pos, currentCell);
	}
	m_waterDirty.clear();

	// Phase 2: Apply merged writes
	for (const auto& [pos, type] : m_waterWrites)
	{
		if (!m_world->GetChunk(pos.x, pos.z))
			continue;

		if (Read(pos) == type)
			continue;

		m_world->SetBlock(pos.x, pos.y, pos.z, type);
		DirtyWaterAround(pos);
	}
	m_waterWrites.clear();
}
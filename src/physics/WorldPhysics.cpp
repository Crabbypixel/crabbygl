#include "physics/WorldPhysics.h"
#include "world/BlockType.h"
#include "world/World.h"
#include "world/BlockRegistry.h"

#include <iostream>
#include <climits>
#include <algorithm>

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
	// Renderer thread is the only writer, so unlocked reads of this chunk and its neighbours are race-free here
	const int baseX = chunk.chunkPos.x * CX;
	const int baseZ = chunk.chunkPos.y * CZ;

	// Interior-only check; border cells are woken unconditionally below
	auto isOpenAround = [&](int x, int y, int z)
	{
		if (y > 0	   && IsReplaceable(chunk.GetUnchecked(x, y - 1, z))) return true;
		if (x > 0	   && IsReplaceable(chunk.GetUnchecked(x - 1, y, z))) return true;
		if (x < CX - 1 && IsReplaceable(chunk.GetUnchecked(x + 1, y, z))) return true;
		if (z > 0	   && IsReplaceable(chunk.GetUnchecked(x, y, z - 1))) return true;
		if (z < CZ - 1 && IsReplaceable(chunk.GetUnchecked(x, y, z + 1))) return true;
		return false;
	};

	for (int y = 0; y < CY; ++y)
	for (int x = 0; x < CX; ++x)
	for (int z = 0; z < CZ; ++z)
	{
		const BlockType t = chunk.GetUnchecked(x, y, z);
		if (!IsWater(t))
			continue;

		const bool onBorder = (x == 0 || x == CX - 1 || z == 0 || z == CZ - 1);

		// Flowing water from disk keeps its level (it's in the id) but must re-verify its feed;
		// stable cells cost one evaluation and drop out. Deep ocean sources are never touched
		if (!IsSource(t) || onBorder || isOpenAround(x, y, z))
			m_nextWaterDirty.insert({ baseX + x, y, baseZ + z });
	}

	// Water in already-loaded neighbours that touches this chunk saw a wall (Read) until now
	auto wakeColumn = [&](const glm::ivec2& nCoord, int lx0, int lx1, int lz0, int lz1)
	{
		const Chunk* n = m_world->GetChunk(nCoord.x * CX, nCoord.y * CZ);
		if (!n)
			return;

		for (int y = 0; y < CY; ++y)
		for (int x = lx0; x <= lx1; ++x)
		for (int z = lz0; z <= lz1; ++z)
		{
			if (IsWater(n->GetUnchecked(x, y, z)))
				m_nextWaterDirty.insert({ nCoord.x * CX + x, y, nCoord.y * CZ + z });
		}
	};

	const glm::ivec2 c = chunk.chunkPos;
	wakeColumn(c + glm::ivec2(-1, 0), CX - 1, CX - 1,      0, CZ - 1);	// -X neighbour: its x = 15 face
	wakeColumn(c + glm::ivec2(1,  0),      0,      0,      0, CZ - 1);	// +X neighbour: its x = 0 face
	wakeColumn(c + glm::ivec2(0, -1),      0, CX - 1, CZ - 1, CZ - 1);	// -Z neighbour: its z = 15 face
	wakeColumn(c + glm::ivec2(0,  1),      0, CX - 1,      0,	   0);		// +Z neighbour: its z = 0 face
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

void WorldPhysics::FindFlowDirections(const glm::ivec3& p, bool outDirs[4]) const
{
	// Flow only toward the nearest drop within DROP_SEARCH_RADIUS:
	// if no drop is in reach, flow in every open direction
	// One BFS over a small fixed grid, each cell tagged with which first step(s) reach it

	constexpr int R = DROP_SEARCH_RADIUS;
	constexpr int W = 2 * R + 1;		// Grid centered on p: grid(R,R) == world p

	uint8_t mask[W][W] = {};		// bit i = reachable on a shortest path whose first step is horizontal[i]
	int8_t depth[W][W];				// -1 unvisited, -2 wall, else steps from p

	for (auto& row : depth)
		for (auto& d : row)
			d = -1;

	auto passable = [&](BlockType t) { return IsReplaceable(t) || (IsWater(t) && !IsSource(t)); };
	auto isDrop = [&](const glm::ivec3& c) { return !BlocksFlow(Read(c + DOWN)); };

	glm::ivec2 frontier[W * W];
	int frontierCount = 0;

	// Depth 1: the four neighbours, each tagged with its own direction bit
	for (int i = 0; i < 4; ++i)
	{
		outDirs[i] = false;
		const glm::ivec3 n = p + HORIZONTAL[i];
		if (!passable(Read(n)))
			continue;

		const int gx = R + HORIZONTAL[i].x, gz = R + HORIZONTAL[i].z;
		depth[gx][gz] = 1;
		mask[gx][gz] = static_cast<uint8_t>(1u << i);
		frontier[frontierCount++] = { gx, gz };
	}

	if (frontierCount == 0)
		return;		// Boxed in

	depth[R][R] = 0;	// Never route back through the origin

	uint8_t open = 0;
	for (int k = 0; k < frontierCount; ++k)
		open |= mask[frontier[k].x][frontier[k].y];

	for (int d = 1; d <= R && frontierCount > 0; ++d)
	{
		// Any drop at this depth? The union of their first steps is the answer
		uint8_t dropMask = 0;
		for (int k = 0; k < frontierCount; ++k)
		{
			const glm::ivec2 g = frontier[k];
			if (isDrop({ p.x + g.x - R, p.y, p.z + g.y - R }))
				dropMask |= mask[g.x][g.y];
		}

		if (dropMask)
		{
			for (int i = 0; i < 4; ++i)
				outDirs[i] = (dropMask >> i) & 1u;
			return;
		}

		// Expand to depth d + 1
		glm::ivec2 next[W * W];
		int nextCount = 0;
		for (int k = 0; k < frontierCount; ++k)
		{
			const glm::ivec2 g = frontier[k];
			for (const auto& h : HORIZONTAL)
			{
				const int nx = g.x + h.x, nz = g.y + h.z;
				if (nx < 0 || nx >= W || nz < 0 || nz >= W)
					continue;

				if (depth[nx][nz] == -1)
				{
					if (!passable(Read({ p.x + nx - R, p.y, p.z + nz - R })))
					{
						depth[nx][nz] = -2;		// Wall, don't re-read
						continue;
					}

					depth[nx][nz] = static_cast<int8_t>(d + 1);
					next[nextCount++] = { nx, nz };
				}

				// Equal-length paths from different first steps both count
				if (depth[nx][nz] == d + 1)
					mask[nx][nz] |= mask[g.x][g.y];
			}
		}

		for (int k = 0; k < nextCount; ++k)
			frontier[k] = next[k];
		frontierCount = nextCount;
	}

	// No drop in reach: spread everywhere open
	for (int i = 0; i < 4; ++i)
		outDirs[i] = (open >> i) & 1u;
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

	bool dirs[4];
	FindFlowDirections(p, dirs);

	const BlockType nextWaterFlow = MakeFlowing(nextFlow);
	for (int i = 0; i < 4; i++)
	{
		if (!dirs[i])
			continue;

		const glm::ivec3 n = p + HORIZONTAL[i];
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

		// Sources are fixed, everything else re-derives its state from its neighbors
		const BlockType	state = IsSource(currentCell) ? currentCell : ComputeWaterState(pos);
		if (state != currentCell)
			ProposeWrite(pos, state);		// May be air, loses to any neighbor feeding this cell

		if (state != BlockType::AIR)
			ComputeOutflow(pos, state);		// Push from the NEW state, so a weakening cell pushes weaker water
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

BlockType WorldPhysics::ComputeWaterState(const glm::ivec3& p) const
{
	/* Works using pull model: A non-source cell is whatever the neighbors can sustain.
	*  Removing a source therefore drains its flow one step at a time with no BFS, and
	*  two sources feeding one cell never fight, it is already resolved previously.
	* */

	if (IsWater(Read(p + UP)))
		return BlockType::WATER_FALLING;

	int best = INT_MAX;
	for (const auto& d : HORIZONTAL)
	{
		const glm::ivec3 n = p + d;
		const BlockType t = Read(n);

		// Only neighbors that actually spread sideways can feel this cell: a neighbor that is
		// falling through air or flowing on top of water doesn't push water out to its sides
		if (IsWater(t) && SpreadSideways(n, t))
			best = std::min(best, FlowDistance(t));
	}

	if (best == INT_MAX || best + 1 > MAX_FLOW_DIST)
		return BlockType::AIR;

	return MakeFlowing(best + 1);
}
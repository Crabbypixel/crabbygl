#include "world/LightingSystem.h"
#include "world/BlockRegistry.h"
#include "world/World.h"

#include <iostream>
#include <mutex>

enum class Blocktype;

static const glm::ivec3 BLOCK_NEIGHBORS[] = {
	{1, 0, 0}, {-1, 0, 0},	// +X, -X
	{0, 0, 1}, { 0, 0,-1},	// +Z, -Z
	{0, 1, 0}, { 0,-1, 0},	// +Y, -Y
};

// Index helpers
static constexpr uint16_t Encode(uint8_t x, uint8_t y, uint8_t z) noexcept
{
	return ((uint16_t)x << 12) | ((uint16_t)y << 4) | (uint16_t)z;
}

static constexpr int DecodeX(uint16_t index) noexcept
{
	return (index >> 12) & 0xF;
}

static constexpr int DecodeY(uint16_t index) noexcept
{
	return (index >> 4) & 0xFF;
}

static constexpr int DecodeZ(uint16_t index) noexcept
{
	return (index) & 0xF;
}

void LightingSystem::InitChunkLight(Chunk* chunk)
{
	if (!chunk)
		return;

	const glm::ivec2 pos = chunk->chunkPos;

	// BFS
	{
		std::unique_lock lock(chunk->chunkMutex);

		for (int x = 0; x < CX; ++x)
		for (int z = 0; z < CZ; ++z)
		{
			SetSunlightUnsafe(chunk, x, CY - 1, z, 15);
			m_sunlightBFSQueue.emplace(Encode(x, CY - 1, z), chunk);
		}
	}

	chunk->dirty = true;			// Dirty it here

	// Torchlight
	{
		std::unique_lock lock(chunk->chunkMutex);

		for(int y = 0; y < CY; ++y)
		for(int x = 0; x < CX; ++x)
		for(int z = 0; z < CZ; ++z)
		{
			int lightValue = GetDef(chunk->GetUnchecked(x, y, z)).lightEmission;

			if (lightValue > 0)
			{
				SetTorchLightUnsafe(chunk, x, y, z, lightValue);
				m_visitedChunks.insert(chunk);
				m_torchlightBFSQueue.emplace(Encode(x, y, z), chunk);
			}
		}
	}

	// Check if the chunk's neighbors edge blocks have light -> seed BFS
	// Neighbor bleed-in (for both sunlight and torchlight)
	static const struct {
		glm::ivec2 ND;          // Neighbor chunk direction offset
		glm::ivec2 borderPos;   // Target coordinate inside the neighboring chunk
		glm::ivec2 edgePos;     // Boundary coordinate inside the current chunk
	} SIDES[] = {
		// -1 means that index is run by a loop variable
		{ {  1,  0 }, { 0,      -1 }, { CX - 1, -1     } },        // +X neighbor
		{ { -1,  0 }, { CX - 1, -1 }, { 0,      -1     } },        // -X neighbor
		{ {  0,  1 }, { -1,     0  }, { -1,     CZ - 1 } },        // +Z neighbor
		{ {  0, -1 }, { -1, CZ - 1 }, { -1,     0      } }         // -Z neighbor
	};

	for (const auto& side : SIDES)
	{
		std::unique_lock lock(chunk->chunkMutex);

		const glm::ivec2 nPos = pos + side.ND;
		Chunk* neighborChunk = m_world->GetChunk(nPos.x * CX, nPos.y * CZ);

		if (!neighborChunk)
			continue;

		// Traverse layer by layer (increasing height)
		// The inner (i) loop is only for +/- X/Z so it always runs 16 times
		for(int y = 0; y < CY; ++y)
		for(int i = 0; i < CX; ++i)
		{
			// Border pos - coord inside neighboring chunk
			glm::ivec3 borderPos = (side.borderPos.x != -1) ? glm::ivec3(side.borderPos.x, y, i) : glm::ivec3(i, y, side.borderPos.y);
			glm::ivec3 edgePos = (side.edgePos.x != -1) ? glm::ivec3(side.edgePos.x, y, i) : glm::ivec3(i, y, side.edgePos.y);

			int borderTorchlight = GetTorchLight(neighborChunk, borderPos.x, borderPos.y, borderPos.z);
			if (borderTorchlight > 1)
			{
				int incomingTorchlight = borderTorchlight - 1;
				int edgeTorchlight = GetTorchLight(chunk, edgePos.x, edgePos.y, edgePos.z);

				// Bleed-in torchlight from chunk boundaries
				if (edgeTorchlight < incomingTorchlight)
				{
					SetTorchLightUnsafe(chunk, edgePos.x, edgePos.y, edgePos.z, incomingTorchlight);
					m_visitedChunks.insert(chunk);
					m_torchlightBFSQueue.emplace(Encode(edgePos.x, edgePos.y, edgePos.z), chunk);
				}
			}

			int borderSunlight = GetSunlight(neighborChunk, borderPos.x, borderPos.y, borderPos.z);
			if (borderSunlight > 1)
			{
				int incomingSunlight = borderSunlight - 1;
				int edgeSunlight = GetSunlight(chunk, edgePos.x, edgePos.y, edgePos.z);

				// Bleed-in sunlight from chunk boundaries
				if (edgeSunlight < incomingSunlight)
				{
					SetSunlightUnsafe(chunk, edgePos.x, edgePos.y, edgePos.z, incomingSunlight);
					m_visitedChunks.insert(chunk);
					m_sunlightBFSQueue.emplace(Encode(edgePos.x, edgePos.y, edgePos.z), chunk);
				}
			}
		}
	}
}

void LightingSystem::NotifyBlockPlaced(int wx, int wy, int wz, BlockType type)
{
	int emission = GetDef(type).lightEmission;

	glm::ivec3 local = World::ChunkLocalCoord(wx, wy, wz);
	Chunk* chunk = m_world->GetChunk(wx, wz);

	if (!chunk)
		return;

	if (emission > 0)
	{
		SetTorchLight(chunk, local.x, local.y, local.z, emission);
		m_visitedChunks.insert(chunk);
		m_torchlightBFSQueue.emplace(Encode(local.x, local.y, local.z), chunk);
	}

	else if (IsOpaque(type))
	{
		int existingLightLevel = GetTorchLight(chunk, local.x, local.y, local.z);
		SetTorchLight(chunk, local.x, local.y, local.z, 0);
		m_visitedChunks.insert(chunk);
		m_torchlightRemovalBFSQueue.emplace(Encode(local.x, local.y, local.z), existingLightLevel, chunk);

		int existingSunlight = GetSunlight(chunk, local.x, local.y, local.z);
		SetSunlight(chunk, local.x, local.y, local.z, 0);
		m_visitedChunks.insert(chunk);
		m_sunlightRemovalBFSQueue.emplace(Encode(local.x, local.y, local.z), existingSunlight, chunk);
	}
}

void LightingSystem::NotifyBlockRemoved(int wx, int wy, int wz)
{
	Chunk* chunk = m_world->GetChunk(wx, wz);
	if (!chunk)
	{
		std::cerr << "NotifyBlockRemoved() - invalid chunk\n";
		return;
	}

	glm::ivec3 local = World::ChunkLocalCoord(wx, wy, wz);

	uint16_t index = Encode(local.x, local.y, local.z);

	// Torchlight
	int lightValue = GetTorchLight(chunk, local.x, local.y, local.z);
	m_torchlightRemovalBFSQueue.emplace(index, lightValue, chunk);
	SetTorchLight(chunk, local.x, local.y, local.z, 0);
	m_visitedChunks.insert(chunk);

	// Sunlight
	int sunValue = GetSunlight(chunk, local.x, local.y, local.z);
	m_sunlightRemovalBFSQueue.emplace(index, sunValue, chunk);
	SetSunlight(chunk, local.x, local.y, local.z, 0);
	m_visitedChunks.insert(chunk);
}

void LightingSystem::Update()
{
	RemoveTorch();
	RemoveSunlight();

	PropagateTorch();
	PropagateSunlight();
}

void LightingSystem::PropagateTorch()
{
	// Process BFS
	while (!m_torchlightBFSQueue.empty())
	{
		// Pop an element from the queue
		uint16_t index = 0;
		Chunk* chunk = nullptr;
		{
			LightNode& node = m_torchlightBFSQueue.front();
			index = node.index;
			chunk = node.chunk;
			m_torchlightBFSQueue.pop();
		}

		if (!chunk)
		{
			std::cerr << "PropagateTorch() - invalid chunk\n";
			continue;
		}

		glm::ivec3 local = { DecodeX(index), DecodeY(index), DecodeZ(index) };

		// Get light level of this pos
		int lightLevel = GetTorchLight(chunk, local.x, local.y, local.z);

		// Look at all adjacent voxels to this position
		// If its light level is 2 or more levels less than
		// to the current one, add them to the queue

		// Visit all neighbors
		for (const auto& ND : BLOCK_NEIGHBORS)
		{
			glm::ivec3 adjacentBlockPos = local + ND;
			Chunk* adjacentBlockChunk = chunk;

			// Resolve if the block is in adjacent chunk
			if (!Chunk::InBounds(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z))
			{
				// Allowing blocks outside vertical world limit makes query into the same chunk, so don't allow
				if (adjacentBlockPos.y < 0 || adjacentBlockPos.y >= CY)
					continue;

				glm::ivec2 neighboringBlockChunkPos = chunk->chunkPos + glm::ivec2(ND.x, ND.z);
				adjacentBlockChunk = m_world->GetChunk(neighboringBlockChunkPos.x * CX, neighboringBlockChunkPos.y * CZ);

				if (!adjacentBlockChunk)
					continue;

				// Get local block pos
				if		(adjacentBlockPos.x == -1) adjacentBlockPos.x = CX - 1;
				else if (adjacentBlockPos.x == CX) adjacentBlockPos.x = 0;
				else if (adjacentBlockPos.z == -1) adjacentBlockPos.z = CZ - 1;
				else if (adjacentBlockPos.z == CZ) adjacentBlockPos.z = 0;
			}

			// Change light levels and add into queue
			// Only propagate light into non-opaque blocks
			if (!IsOpaque(adjacentBlockChunk->GetUnchecked(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z))
				&& GetTorchLight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z) + 2 <= lightLevel)
			{
				// Set torch light of adjacent block
				SetTorchLight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z, lightLevel - 1);
				m_visitedChunks.insert(adjacentBlockChunk);

				// Add adjacent block into queue
				m_torchlightBFSQueue.emplace(Encode(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z), adjacentBlockChunk);
			}
		}
	}

	// Dirty chunks
	for (Chunk* c : m_visitedChunks)
		c->dirty = true;
	m_visitedChunks.clear();
}

void LightingSystem::RemoveTorch()
{
	// Process BFS
	while (!m_torchlightRemovalBFSQueue.empty())
	{
		// Pop an element from the queue
		uint16_t index = 0;
		int lightLevel = 0;
		Chunk* chunk = nullptr;
		{
			LightRemovalNode& node = m_torchlightRemovalBFSQueue.front();
			index = node.index;
			lightLevel = node.val;
			chunk = node.chunk;
			m_torchlightRemovalBFSQueue.pop();
		}

		if (!chunk)
		{
			std::cerr << "RemoveTorch() - invalid chunk\n";
			continue;
		}

		glm::ivec3 local = { DecodeX(index), DecodeY(index), DecodeZ(index) };

		// Visit all neighbors
		for (const auto& ND : BLOCK_NEIGHBORS)
		{
			glm::ivec3 adjacentBlockPos = local + ND;
			Chunk* adjacentBlockChunk = chunk;

			// Resolve if the block is in adjacent chunk
			if (!Chunk::InBounds(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z))
			{
				if (adjacentBlockPos.y < 0 || adjacentBlockPos.y >= CY)
					continue;

				glm::ivec2 neighboringBlockChunkPos = chunk->chunkPos + glm::ivec2(ND.x, ND.z);
				adjacentBlockChunk = m_world->GetChunk(neighboringBlockChunkPos.x * CX, neighboringBlockChunkPos.y * CZ);
				
				if (!adjacentBlockChunk)
					continue;

				// Get local block pos
				if		(adjacentBlockPos.x == -1) adjacentBlockPos.x = CX - 1;
				else if (adjacentBlockPos.x == CX) adjacentBlockPos.x = 0;
				else if (adjacentBlockPos.z == -1) adjacentBlockPos.z = CZ - 1;
				else if (adjacentBlockPos.z == CZ) adjacentBlockPos.z = 0;
			}

			int neighborLevel = GetTorchLight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z);
			
			if (neighborLevel != 0 && neighborLevel < lightLevel)
			{
				// Set adjacent block light level
				SetTorchLight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z, 0);
				m_visitedChunks.insert(adjacentBlockChunk);
				m_torchlightRemovalBFSQueue.emplace(Encode(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z), neighborLevel, adjacentBlockChunk);
			}
			else if(neighborLevel >= lightLevel)
			{
				// Kind of becomes a light source, propagate light
				m_torchlightBFSQueue.emplace(Encode(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z), adjacentBlockChunk);
			}
		}
	}

	// Dirty chunks
	for (Chunk* c : m_visitedChunks)
		c->dirty = true;
	m_visitedChunks.clear();
}

// Sunlight flood fill is almost similar to PropagateTorchlight() but don't attenuate vertically down
void LightingSystem::PropagateSunlight()
{
	// Process BFS
	while (!m_sunlightBFSQueue.empty())
	{
		// Pop an element from the queue
		uint16_t index = 0;
		Chunk* chunk = nullptr;
		{
			LightNode& node = m_sunlightBFSQueue.front();
			index = node.index;
			chunk = node.chunk;
			m_sunlightBFSQueue.pop();
		}

		if (!chunk)
		{
			std::cerr << "PropagateSunlight() - invalid chunk\n";
			continue;
		}

		glm::ivec3 local = { DecodeX(index), DecodeY(index), DecodeZ(index) };
		int lightLevel = GetSunlight(chunk, local.x, local.y, local.z);

		// Visit all neighbors
		for (const auto& ND : BLOCK_NEIGHBORS)
		{
			glm::ivec3 adjacentBlockPos = local + ND;
			Chunk* adjacentBlockChunk = chunk;

			// Resolve if the block is in neighboring chunk
			if (!Chunk::InBounds(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z))
			{
				if (adjacentBlockPos.y < 0 || adjacentBlockPos.y >= CY)
					continue;

				glm::ivec2 nChunkPos = chunk->chunkPos + glm::ivec2(ND.x, ND.z);
				adjacentBlockChunk = m_world->GetChunk(nChunkPos.x * CX, nChunkPos.y * CZ);
				
				if (!adjacentBlockChunk)
					continue;

				if		(adjacentBlockPos.x == -1) adjacentBlockPos.x = CX - 1;
				else if (adjacentBlockPos.x == CX) adjacentBlockPos.x = 0;
				else if (adjacentBlockPos.z == -1) adjacentBlockPos.z = CZ - 1;
				else if (adjacentBlockPos.z == CZ) adjacentBlockPos.z = 0;
			}

			// If neighboring block is opaque, skip this neighbot
			if (IsOpaque(adjacentBlockChunk->GetUnchecked(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z)))
				continue;

			// Propagate sunlight fully only if the floodfill goes downwards
			const bool propagateSunlight = (ND.y == -1 && lightLevel == 15);

			// Neighbor block light and new light
			const int newLight = propagateSunlight ? 15 : lightLevel - 1;
			const int neighhborLight = GetSunlight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z);

			// Fill anything below max
			const bool shouldPropagate = propagateSunlight ? (neighhborLight < 15) : (neighhborLight + 2 <= lightLevel);	

			if (shouldPropagate)
			{
				SetSunlight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z, newLight);
				m_visitedChunks.insert(adjacentBlockChunk);
				
				// Add adjacent block into the queue
				m_sunlightBFSQueue.emplace(Encode(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z), adjacentBlockChunk);
			}
		}
	}

	// Dirty chunks
	for (Chunk* c : m_visitedChunks)
		c->dirty = true;
	m_visitedChunks.clear();
}

// Also similar to RemoveTorchlight()
void LightingSystem::RemoveSunlight()
{
	// Process BFS
	while (!m_sunlightRemovalBFSQueue.empty())
	{
		// Pop an element from the queue
		uint16_t index = 0;
		int lightLevel = 0;
		Chunk* chunk = nullptr;
		{
			LightRemovalNode& node = m_sunlightRemovalBFSQueue.front();
			index = node.index;
			lightLevel = node.val;
			chunk = node.chunk;
			m_sunlightRemovalBFSQueue.pop();
		}

		if (!chunk)
		{
			std::cerr << "RemoveSunlight() - invalid chunk\n";
			continue;
		}

		glm::ivec3 local = { DecodeX(index), DecodeY(index), DecodeZ(index) };

		// Visit all neighbors
		for (const auto& ND : BLOCK_NEIGHBORS)
		{
			glm::ivec3 adjacentBlockPos = local + ND;
			Chunk* adjacentBlockChunk = chunk;

			// Resolve if the block is in neighboring chunk
			if (!Chunk::InBounds(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z))
			{
				if (adjacentBlockPos.y < 0 || adjacentBlockPos.y >= CY) continue;

				glm::ivec2 nChunkPos = chunk->chunkPos + glm::ivec2(ND.x, ND.z);
				adjacentBlockChunk = m_world->GetChunk(nChunkPos.x * CX, nChunkPos.y * CZ);
				if (!adjacentBlockChunk) continue;

				if		(adjacentBlockPos.x == -1) adjacentBlockPos.x = CX - 1;
				else if (adjacentBlockPos.x == CX) adjacentBlockPos.x = 0;
				else if (adjacentBlockPos.z == -1) adjacentBlockPos.z = CZ - 1;
				else if (adjacentBlockPos.z == CZ) adjacentBlockPos.z = 0;
			}

			// Neighbor block sunlight level
			int neighborLevel = GetSunlight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z);

			// Special case: For vertical light columns
			// Full-strength sunlight propagates downwards without attenuation
			// Therefore when the column is blocked, removal must continue
			// This is the code responsible for shadows
			const bool sunbeamRemoval = (ND.y == -1 && lightLevel == 15);

			// Attenuate when neighbor has some light level and if its light level is lesser than current OR going to downwards
			if (neighborLevel != 0 && (neighborLevel < lightLevel || sunbeamRemoval))
			{
				SetSunlight(adjacentBlockChunk, adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z, 0);
				m_visitedChunks.insert(adjacentBlockChunk);

				// Spread removal
				m_sunlightRemovalBFSQueue.emplace(Encode(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z), neighborLevel, adjacentBlockChunk);
			}
			else if (!sunbeamRemoval && neighborLevel >= lightLevel)
			{
				// Independent light source — re-propagate to fill gaps
				m_sunlightBFSQueue.emplace(Encode(adjacentBlockPos.x, adjacentBlockPos.y, adjacentBlockPos.z), adjacentBlockChunk);
			}
		}
	}

	// Dirty chunks
	for (Chunk* c : m_visitedChunks)
		c->dirty = true;
	m_visitedChunks.clear();
}

// Get bits 0000XXXX
int LightingSystem::GetTorchLight(Chunk* chunk, int x, int y, int z)
{
	if (!chunk)
		return 0;

	return chunk->lightMap[x][y][z] & 0xF;
}

// Set bits 0000XXXX
void LightingSystem::SetTorchLight(Chunk* chunk, int x, int y, int z, int val)
{
	if (!chunk)
		return;
	
	std::unique_lock lock(chunk->chunkMutex);
	chunk->lightMap[x][y][z] = (chunk->lightMap[x][y][z] & 0xF0) | static_cast<uint8_t>(val);
}
void LightingSystem::SetTorchLightUnsafe(Chunk* chunk, int x, int y, int z, int val)
{
	if (!chunk)
		return;

	chunk->lightMap[x][y][z] = (chunk->lightMap[x][y][z] & 0xF0) | static_cast<uint8_t>(val);
}

// Get bits XXXX0000
int LightingSystem::GetSunlight(Chunk* chunk, int x, int y, int z)
{
	if (!chunk)
		return 0;

	return (chunk->lightMap[x][y][z] >> 4) & 0xF;
}

// Set bits XXXX0000
void LightingSystem::SetSunlight(Chunk* chunk, int x, int y, int z, int val)
{
	if (!chunk)
		return;

	std::unique_lock lock(chunk->chunkMutex);
	chunk->lightMap[x][y][z] = (chunk->lightMap[x][y][z] & 0xF) | (static_cast<uint8_t>(val) << 4);
}
void LightingSystem::SetSunlightUnsafe(Chunk* chunk, int x, int y, int z, int val)
{
	if (!chunk)
		return;

	chunk->lightMap[x][y][z] = (chunk->lightMap[x][y][z] & 0xF) | (static_cast<uint8_t>(val) << 4);
}
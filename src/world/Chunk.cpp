#include "world/Chunk.h"

#include <vector>
#include <mutex>
#include <fstream>

BlockType Chunk::Get(int x, int y, int z) const noexcept
{
	if (!InBounds(x, y, z))
		return BlockType::AIR;

	std::shared_lock lock(chunkMutex);

	return GetUnchecked(x, y, z);
}

void Chunk::Set(int x, int y, int z, BlockType type)
{
	if (!InBounds(x, y, z))
		return;
	
	std::unique_lock lock(chunkMutex);

	SetUnchecked(x, y, z, type);
}

BlockType Chunk::GetUnchecked(int x, int y, int z) const
{
	return blocks[GetIndex(x, y, z)];
}

void Chunk::SetUnchecked(int x, int y, int z, BlockType type)
{
	int index = GetIndex(x, y, z);

	// Avoid unnecessary writes and dirtying the chunk if the block type is unchanged
	if(blocks[index] == type)
		return;
	
	blocks[index] = type;

	dirty = true;
	modified = true;
	aoDirty = true;
}

void Chunk::Serialize(std::ofstream& outputStream) const
{
	if (!outputStream)
		return;

	outputStream.write(reinterpret_cast<const char*>(blocks.data()), sizeof(blocks));
}

bool Chunk::Deserialize(std::ifstream& inputSteam)
{
	if (!inputSteam)
		return false;

	inputSteam.read(reinterpret_cast<char*>(blocks.data()), sizeof(blocks));
	return inputSteam.gcount() == sizeof(blocks);
}

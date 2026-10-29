#include "world/BlockRegistry.h"
#include "world/Chunk.h"

using namespace Tiles;

// +Y -Y +X -X +Z -Z
const BlockDef BLOCK_DEFS[] =
{
	/* 0*/	{ "Air", { 0, 0, 0, 0, 0, 0 }, -1, { 1, 1, 1, 1 }, BLOCK_TRANSPARENT, 0.0f, false, 0 },
	/* 1*/	{ "Dirt", { DIRT, DIRT, DIRT, DIRT, DIRT, DIRT }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false },
	/* 2*/	{ "Grass block", { GRASS_TOP, DIRT, DIRT, DIRT, DIRT, DIRT }, GRASS_OVERLAY, { 0.55f, 0.90f, 0.28f, 1.0f }, BLOCK_OPAQUE | BLOCK_SOLID, 0.6f, true, 0 },
	/* 3*/	{ "Stone", { STONE, STONE, STONE, STONE, STONE, STONE }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/* 4*/	{ "Bedrock", { BEDROCK, BEDROCK, BEDROCK, BEDROCK, BEDROCK, BEDROCK }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 10000.0f, false, 0 },
	/* 5*/	{ "Brick", { BRICK, BRICK, BRICK, BRICK, BRICK, BRICK }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/* 6*/	{ "Log", { TREE_LOG_TOP, TREE_LOG_TOP, TREE_LOG_SIDES_1, TREE_LOG_SIDES_1, TREE_LOG_SIDES_1, TREE_LOG_SIDES_1 }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/* 7*/	{ "Leaves", { TREE_LEAVES, TREE_LEAVES, TREE_LEAVES, TREE_LEAVES, TREE_LEAVES, TREE_LEAVES }, -1, { 1, 1, 1, 1 }, BLOCK_TRANSLUCENT | BLOCK_SOLID, 10000.0f, false, 0 },
	/* 8*/	{ "Cobblestone", { COBBLESTONE, COBBLESTONE, COBBLESTONE, COBBLESTONE, COBBLESTONE, COBBLESTONE }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/* 9*/	{ "Planks", { PLANK, PLANK, PLANK, PLANK, PLANK, PLANK }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*10*/	{ "Sand", { SAND, SAND, SAND, SAND, SAND, SAND }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*11*/	{ "Gravel", { GRAVEL, GRAVEL, GRAVEL, GRAVEL, GRAVEL, GRAVEL }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*12*/	{ "Glass", { GLASS, GLASS, GLASS, GLASS, GLASS, GLASS }, -1, { 1, 1, 1, 1 }, BLOCK_TRANSLUCENT | BLOCK_SOLID, 10000.0f, false, 0 },
	/*13*/	{ "Gold ore", { ORE_GOLD, ORE_GOLD, ORE_GOLD, ORE_GOLD, ORE_GOLD, ORE_GOLD }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*14*/	{ "Iron ore", { ORE_IRON, ORE_IRON, ORE_IRON, ORE_IRON, ORE_IRON, ORE_IRON }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*15*/	{ "Coal ore", { ORE_COAL, ORE_COAL, ORE_COAL, ORE_COAL, ORE_COAL, ORE_COAL }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*16*/	{ "Diamond ore", { ORE_DIAMOND, ORE_DIAMOND, ORE_DIAMOND, ORE_DIAMOND, ORE_DIAMOND, ORE_DIAMOND }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*17*/	{ "Smooth stone", { SMOOTH_STONE, SMOOTH_STONE, SMOOTH_STONE, SMOOTH_STONE, SMOOTH_STONE, SMOOTH_STONE }, -1, { 1, 1, 1, 1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },

	/*18*/  { "Sapling",    { SAPLING, SAPLING, SAPLING, SAPLING, SAPLING, SAPLING }, -1, { 1,1,1,1 }, BLOCK_TRANSPARENT | BLOCK_CROSS, 0.0f, false, 0 },
	/*19*/  { "Rose",       { ROSE, ROSE, ROSE, ROSE, ROSE, ROSE }, -1, { 1,1,1,1 }, BLOCK_TRANSPARENT | BLOCK_CROSS, 0.0f, false, 0 },
	/*20*/  { "Dandelion",  { DANDELION, DANDELION, DANDELION, DANDELION, DANDELION, DANDELION }, -1, { 1,1,1,1 }, BLOCK_TRANSPARENT | BLOCK_CROSS, 0.0f, false, 0 },
	/*21*/  { "Brown mushroom",  {BROWN_MUSHROOM, BROWN_MUSHROOM, BROWN_MUSHROOM, BROWN_MUSHROOM, BROWN_MUSHROOM, BROWN_MUSHROOM }, -1, { 1,1,1,1 }, BLOCK_TRANSPARENT | BLOCK_CROSS, 0.0f, false, 0 },
	/*22*/  { "Red mushroom",  {RED_MUSHROOM, RED_MUSHROOM, RED_MUSHROOM, RED_MUSHROOM, RED_MUSHROOM, RED_MUSHROOM }, -1, { 1,1,1,1 }, BLOCK_TRANSPARENT | BLOCK_CROSS, 0.0f, false, 0 },
	/*23*/  { "Grass",  {GRASS, GRASS, GRASS, GRASS, GRASS, GRASS }, -1, { 0.592f, 0.902f, 0.239f, 1.0f }, BLOCK_TRANSPARENT | BLOCK_CROSS, 0.0f, false, 0 },

	/*24*/  { "Log", { TREE_LOG_SIDES_2, TREE_LOG_SIDES_2, TREE_LOG_TOP, TREE_LOG_TOP, TREE_LOG_SIDES_2, TREE_LOG_SIDES_2 }, -1, { 1,1,1,1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*25*/  { "Log", { TREE_LOG_SIDES_1, TREE_LOG_SIDES_1, TREE_LOG_SIDES_2, TREE_LOG_SIDES_2, TREE_LOG_TOP, TREE_LOG_TOP }, -1, { 1,1,1,1 }, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 0 },
	/*26*/	{ "Glowstone", { GLOWSTONE, GLOWSTONE, GLOWSTONE, GLOWSTONE, GLOWSTONE, GLOWSTONE }, -1, { 1, 1, 1, 1}, BLOCK_OPAQUE | BLOCK_SOLID, 1.5f, false, 15},
	/*27*/  { "Water", { WATER, WATER, WATER, WATER, WATER, WATER }, -1, { 1, 1, 1, 1.0f }, BLOCK_WATER | BLOCK_TRANSLUCENT, 0.0f, false, 0},
	/*28*/  { "Flowing water", { WATER, WATER, WATER, WATER, WATER, WATER }, -1, { 1, 1, 1, 0.4f }, BLOCK_WATER | BLOCK_TRANSLUCENT, 0.0f, false, 0 }
};

static_assert(std::size(BLOCK_DEFS) == (size_t)BlockType::MAX_VALUE, "BlockType enum and BLOCK_DEFS[] out of sync");

const BlockDef& GetDef(BlockType t) noexcept
{
	return BLOCK_DEFS[(uint8_t)t];
}

bool IsCross(BlockType t) noexcept
{
	return GetDef(t).flags & BLOCK_CROSS;
}

bool IsOpaque(BlockType t) noexcept
{
	return GetDef(t).flags & BLOCK_OPAQUE;
}

bool IsTransparent(BlockType t) noexcept
{
	return GetDef(t).flags & BLOCK_TRANSPARENT;
}

bool IsSolid(BlockType t) noexcept
{
	return GetDef(t).flags & BLOCK_SOLID;
}

bool IsTranslucent(BlockType t) noexcept
{
	return GetDef(t).flags & BLOCK_TRANSLUCENT;
}

bool IsWater(BlockType t) noexcept
{
	return GetDef(t).flags & BLOCK_WATER;
}
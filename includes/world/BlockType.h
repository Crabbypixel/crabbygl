#pragma once
#include <cstdint>

enum class BlockType : uint8_t
{
	/* 0*/	AIR,
	/* 1*/	DIRT,
	/* 2*/	GRASS_BLOCK,
	/* 3*/	STONE,
	/* 4*/	BEDROCK,
	/* 5*/	BRICK,
	/* 6*/	TREE_LOG_Y,
	/* 7*/	TREE_LEAVES,
	/* 8*/	COBBLESTONE,
	/* 9*/	PLANK,
	/*10*/	SAND,
	/*11*/  GRAVEL,
	/*12*/  GLASS,
	/*13*/  GOLD_ORE,
	/*14*/  IRON_ORE,
	/*15*/  COAL_ORE,
	/*16*/  DIAMOND_ORE,
	/*17*/  SMOOTH_STONE,
	/*18*/  SAPLING,
	/*19*/  ROSE,
	/*20*/  DANDELION,
	/*21*/  BROWN_MUSHROOM,
	/*22*/  RED_MUSHROOM,
	/*23*/  GRASS,
	/*24*/  TREE_LOG_X,
	/*25*/  TREE_LOG_Z,
	/*26*/  GLOWSTONE,
	/*27*/  WATER,
	/*28*/  WATER_FLOWING_1,
	/*29*/  WATER_FLOWING_2,
	/*30*/  WATER_FLOWING_3,
	/*31*/  WATER_FLOWING_4,
	/*32*/	WATER_FLOWING_5,
	/*33*/	WATER_FLOWING_6,
	/*34*/	WATER_FLOWING_7,
	/*35*/	WATER_FALLING,		// Fed from above, spreads like a source once it lands
	MAX_VALUE			// Ending sentinel - not a real block type, used for validation and iteration
};

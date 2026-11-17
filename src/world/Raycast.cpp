#include "world/Raycast.h"
#include "world/BlockType.h"
#include "world/World.h"
#include "world/BlockRegistry.h"

#include <vector>
#include <limits>

RaycastHit RaycastDDA(const glm::vec3& origin, const glm::vec3& direction, const World& world, float maxDistance)
{
	RaycastHit result;

	// Current block
	glm::vec3 start = origin + direction * 0.001f;
	glm::ivec3 currentBlock = glm::floor(start);

	// Step direction
	glm::ivec3 step;
	step.x = (direction.x > 0) ? 1 : (direction.x < 0 ? -1 : 0);
	step.y = (direction.y > 0) ? 1 : (direction.y < 0 ? -1 : 0);
	step.z = (direction.z > 0) ? 1 : (direction.z < 0 ? -1 : 0);

	// Avoid division by zero
	glm::vec3 invDir = glm::vec3(
		(direction.x != 0) ? 1.0f / direction.x : std::numeric_limits<float>::infinity(),
		(direction.y != 0) ? 1.0f / direction.y : std::numeric_limits<float>::infinity(),
		(direction.z != 0) ? 1.0f / direction.z : std::numeric_limits<float>::infinity()
	);

	glm::vec3 absInvDir = glm::abs(invDir);

	// Distance to the first block boundary
	glm::vec3 tMax{ 0 };
	//tMax.x = (step.x > 0 ? (currentBlock.x + 1.0f - origin.x) : (origin.x - currentBlock.x)) * absInvDir.x;
	//tMax.y = (step.y > 0 ? (currentBlock.y + 1.0f - origin.y) : (origin.y - currentBlock.y)) * absInvDir.y;
	//tMax.z = (step.z > 0 ? (currentBlock.z + 1.0f - origin.z) : (origin.z - currentBlock.z)) * absInvDir.z;

	tMax.x = (step.x > 0 ? (currentBlock.x + 1.0f - start.x) : (start.x - currentBlock.x)) * absInvDir.x;
	tMax.y = (step.y > 0 ? (currentBlock.y + 1.0f - start.y) : (start.y - currentBlock.y)) * absInvDir.y;
	tMax.z = (step.z > 0 ? (currentBlock.z + 1.0f - start.z) : (start.z - currentBlock.z)) * absInvDir.z;

	// Distance between block crossings
	glm::vec3 tDelta = abs(invDir);

	float t = 0.0f;
	glm::ivec3 lastNormal(0);

	while (t <= maxDistance)
	{
		const BlockType& blockType = world.GetBlock(currentBlock.x, currentBlock.y, currentBlock.z);
		const BlockDef& def = GetDef(blockType);

		if ((def.flags & (BLOCK_SOLID | BLOCK_CROSS | BLOCK_TRANSLUCENT)) && !(def.flags & BLOCK_WATER))
		{
			result.hit = true;
			result.blockPos = currentBlock;
			result.normal = lastNormal;
			return result;
		}

		// Advance along the smallest axis -> next block
		if (tMax.x <= tMax.y && tMax.x <= tMax.z)
		{
			currentBlock.x += step.x;
			t = tMax.x;
			tMax.x += tDelta.x;
			lastNormal = glm::ivec3(-step.x, 0, 0);
		}
		else if (tMax.y <= tMax.z) {
			currentBlock.y += step.y;
			t = tMax.y;
			tMax.y += tDelta.y;
			lastNormal = glm::ivec3(0, -step.y, 0);
		}
		else
		{
			currentBlock.z += step.z;
			t = tMax.z;
			tMax.z += tDelta.z;
			lastNormal = glm::ivec3(0, 0, -step.z);
		}
	}

	return result;
}
// Important clarification: main thread here means the global renderer thread, not the main thread which deals with capturing inputs (see OpenGL_3D.h/.cpp)
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_PERLIN_IMPLEMENTATION
#include "stb/stb_perlin.h"

#include "rendering/ChunkMesh.h"
#include "world/BlockType.h"
#include "world/Chunk.h"
#include "world/Raycast.h"
#include "world/World.h"
#include "world/BlockRegistry.h"
#include "world/ChunkMeshBuilder.h"
#include "world/LightingSystem.h"
#include "rendering/Shader.h"

static constexpr glm::ivec2 GUARDED_NEIGHBORS[] =
{
    { 1,  0 },
    {-1,  0 },
    { 0,  1 },
    { 0, -1 },

    { 1,  1 },
    { 1, -1 },
    {-1,  1 },
    {-1, -1 }
};

World::World() : m_worldPhysics(this), m_lightingSystem(this)
{
    chunks.reserve(1000);
    m_chunkSaveWorker = std::thread(&World::SaveWorkerLoop, this);
}

World::~World()
{
	// If exception path or early exit, call StopAllWorkers to ensure clean shutdown
    if (!m_shutdown)
        StopAllWorkers();
}

// ───── Coord helpers ─────────────────────────────────────────────────
// Get chunk coord from world coords
glm::ivec2 World::ChunkCoord(float worldX, float worldZ)
{
    return {
        (int)std::floor(worldX / (float)CX),
        (int)std::floor(worldZ / (float)CZ)
    };
}

// Get local chunk coord from world coords
glm::ivec3 World::ChunkLocalCoord(int worldX, int worldY, int worldZ)
{
    return {
        (worldX % CX + CX) % CX,
        worldY,
        (worldZ % CZ + CZ) % CZ
    };
}

// ───── Chunk access ──────────────────────────────────────────────────
Chunk* World::GetChunk(int worldX, int worldZ)
{
    auto it = chunks.find(ChunkCoord(worldX, worldZ));
    return (it != chunks.end()) ? it->second.get() : nullptr;
}

const Chunk* World::GetChunk(int worldX, int worldZ) const
{
    auto it = chunks.find(ChunkCoord(worldX, worldZ));
    return (it != chunks.end()) ? it->second.get() : nullptr;
}

// ───── Block access ──────────────────────────────────────────────────
bool World::IsSolid(int worldX, int worldY, int worldZ) const
{
    return ::IsSolid(GetBlock(worldX, worldY, worldZ));
}

BlockType World::GetBlock(int worldX, int worldY, int worldZ) const
{
    if (worldY < 0 || worldY >= CY)
        return BlockType::AIR;

    const Chunk* chunk = GetChunk(worldX, worldZ);
    if (!chunk)
        return BlockType::AIR;

    auto l = ChunkLocalCoord(worldX, worldY, worldZ);
    return chunk->Get(l.x, l.y, l.z);               // Locks before accessing
}

void World::SetBlock(int worldX, int worldY, int worldZ, BlockType type)
{
    if (worldY < 0 || worldY >= CY)
        return;

    Chunk* chunk = GetChunk(worldX, worldZ);
    if (!chunk)
        return;

    auto l = ChunkLocalCoord(worldX, worldY, worldZ);
    chunk->Set(l.x, l.y, l.z, type);                // Locks before accessing

    // TODO: Mark this & neighboring chunks dirty - this is the reason for seam issue in world physics, to be done later
    // NOTE: This still has a visual bug
    MarkAdjacentChunksDirty(worldX, worldY, worldZ);
    MarkAdjacentChunksAODirty(worldX, worldY, worldZ);
}

// ───── World generation ────────────────────────────────────────────────────
float World::GetTerrainHeight(int wx, int wz)
{
    const float MIN_H = 20.0f;
    const float MAX_H = 120.0f;

    float fx = wx * 0.01f;
    float fz = wz * 0.01f;

    float n = stb_perlin_noise3(fx, 0.0f, fz, 0, 0, 0) * 0.5f + 0.5f;
    n += (stb_perlin_noise3(fx * 2.0f, 0.0f, fz * 2.0f, 0, 0, 0) * 0.5f + 0.5f) * 0.50f;
    n += (stb_perlin_noise3(fx * 4.0f, 0.0f, fz * 4.0f, 0, 0, 0) * 0.5f + 0.5f) * 0.25f;
    n /= 1.75f;
    n = pow(n, 2.0f);

    return MIN_H + n * (MAX_H - MIN_H);
}

// Invoked by worker
void World::FillChunk(Chunk& chunk, glm::ivec2 coord)
{
    // Try loading from disk first
    if (LoadChunkFromDisk(chunk, coord))
    {
        return;
    }

    // TODO: Add trees and grass
	// Do all this in worldgen phase of development, not right now - to be done later
    // Fresh Perlin gen
    int cx = coord.x;
    int cz = coord.y;

    for (int x = 0; x < CX; ++x)
    {
        for (int z = 0; z < CZ; ++z)
        {
            int worldX = cx * CX + x;
            int worldZ = cz * CZ + z;
            int height = (int)GetTerrainHeight(worldX, worldZ);

            float n = (height - 20.0f) / (100.0f - 20.0f);
            int   thickness = 4 + (int)((1.0f - n) * 10.0f);
            int   base = std::max(1, height - thickness);

            chunk.SetUnchecked(x, 0, z, BlockType::BEDROCK);

            for (int y = 1; y < base; ++y)
                chunk.SetUnchecked(x, y, z, BlockType::STONE);

            for (int y = base; y < height; ++y)
                chunk.SetUnchecked(x, y, z, BlockType::DIRT);

            chunk.SetUnchecked(x, height, z, BlockType::GRASS_BLOCK);

            if (cx * CX + x == 0 || cz * CZ + z == 0)
                chunk.SetUnchecked(x, height + 1, z, BlockType::BRICK);
        }
    }   
}

// ───── File IO ──────────────────────────────────────────────────
std::string World::ChunkFilePath(glm::ivec2 coord)
{
    return "saves/" + std::to_string(coord.x) + "_" + std::to_string(coord.y) + ".bin";
}

void World::SaveChunkToDisk(const Chunk& chunk)
{
    std::filesystem::create_directories("saves");

    const auto path = ChunkFilePath(chunk.chunkPos);

    std::ofstream file(path, std::ios::binary);

    if (!file)
    {
        std::cerr << "Failed to open chunk file for writing: " << path << '\n';
        return;
    }

	chunk.Serialize(file);
}

bool World::LoadChunkFromDisk(Chunk& chunk, glm::ivec2& coord)
{
    const auto path = ChunkFilePath(coord);

    std::ifstream file(path, std::ios::binary);

    if (!file)
        return false;

    if (!chunk.Deserialize(file))
    {
        std::cerr << "Chunk file corrupted at: " << path << '\n';
        return false;
    }

    return true;
}

void World::UnloadChunks()
{
    // Save modified chunks
    for (auto& [coord, chunk] : chunks)
    {
        if (chunk->modified)
        {
            SaveChunkToDisk(*chunk);
            chunk->modified = false;
        }
    }

    // Unload chunks from memory
    for (auto& [coord, chunk] : chunks)
    {
        if (m_chunkMeshes.contains(coord))
        {
            m_chunkMeshes[coord].Destroy();
            m_chunkMeshes.erase(coord);
        }

        if (m_waterMeshes.contains(coord))
        {
            m_waterMeshes[coord].Destroy();
            m_waterMeshes.erase(coord);
        }
    }

    chunks.clear();
    m_chunkMeshes.clear();
    m_waterMeshes.clear();
    m_deferredLightingChunks.clear();
}

// ───── Raycast CRUD ──────────────────────────────────────────────────
bool World::PlaceBlock(const RaycastHit& hit, BlockType type)
{
    if (!hit.hit)
        return false;

    BlockType hitBlockType = GetBlock(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z);

    // Prevent placing beside non-solid blocks (cross-face blocks
    if (IsCross(hitBlockType))
        return false;

    glm::ivec3 target = hit.blockPos + hit.normal;

    // Prevent placing inside solid blocks
    if (IsSolid(target.x, target.y, target.z))
        return false;

    // If hit block type is water, then set the block there directly
    if (IsWater(hitBlockType))
    {
        SetBlock(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z, type);
        return true;
    }

    // Log blocks have directional variants based on placement face
    const bool isLog = type == BlockType::TREE_LOG_Y ||
                       type == BlockType::TREE_LOG_X ||
                       type == BlockType::TREE_LOG_Z;

    if (isLog)
    {
        type =
            hit.normal.x != 0 ? BlockType::TREE_LOG_X :
            hit.normal.z != 0 ? BlockType::TREE_LOG_Z :
            BlockType::TREE_LOG_Y;
    }

    SetBlock(target.x, target.y, target.z, type);

    return true;
}

bool World::BreakBlock(const RaycastHit& hit)
{
    if (!hit.hit)
        return false;

    const BlockType& blockType = GetBlock(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z);

    SetBlock(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z, BlockType::AIR);

    return true;
}

void World::MarkAdjacentChunksAODirty(int wx, int wy, int wz)
{
    glm::ivec3 local = ChunkLocalCoord(wx, wy, wz);

    const bool minX = (local.x == 0);
    const bool maxX = (local.x == CX - 1);

    const bool minZ = (local.z == 0);
    const bool maxZ = (local.z == CZ - 1);

    auto markAODirty = [&](int dx, int dz) {
        if (Chunk* c = GetChunk(wx + dx, wz + dz))
            c->aoDirty = true;
    };

    // Cross neighbors
    if (minX) markAODirty(-1, 0);
    if (maxX) markAODirty(1, 0);

    if (minZ) markAODirty(0, -1);
    if (maxZ) markAODirty(0, 1);

    // Diagonal neighbors
    if (minX && minZ) markAODirty(-1, -1);
    if (minX && maxZ) markAODirty(-1, 1);

    if (maxX && minZ) markAODirty(1, -1);
    if (maxX && maxZ) markAODirty(1, 1);
}

void World::MarkAdjacentChunksDirty(int wx, int wy, int wz)
{
	glm::ivec3 local = ChunkLocalCoord(wx, wy, wz);

    const bool minX = (local.x == 0);
	const bool maxX = (local.x == CX - 1);

	const bool minZ = (local.z == 0);
	const bool maxZ = (local.z == CZ - 1);

	auto markDirty = [&](int dx, int dz) {
        if (Chunk* c = GetChunk(wx + dx, wz + dz))
            c->dirty = true;
	};

	// Cross neighbors
	if (minX) markDirty(-1, 0);
	if (maxX) markDirty(1, 0);
    
	if (minZ) markDirty(0, -1);
	if (maxZ) markDirty(0, 1);

	// Diagonal neighbors
	if (minX && minZ) markDirty(-1, -1);
	if (minX && maxZ) markDirty(-1, 1);

	if (maxX && minZ) markDirty(1, -1); 
	if (maxX && maxZ) markDirty(1, 1);
}

// ───── Textures ───────────────────────────────────────────────
void World::SetChunkShader(Shader& shader, Shader& waterShader)
{
    m_chunkShader = &shader;
    m_waterShader = &waterShader;

    waterShader.use();
    waterShader.setInt("u_atlas", 1);

    shader.use();
    shader.setInt("u_atlas", 1);   // single sampler, slot 1
}

void World::LoadAtlasTexture(const char* path)
{
    int w, h, channels;
    unsigned char* data = stbi_load(path, &w, &h, &channels, 0);
    if (!data)
    {
        std::cerr << "Atlas load failed: " << path << '\n';
        return;
    }

    unsigned int id;
    glGenTextures(1, &id);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, id);

    GLenum fmt = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Nearest-neighbor keeps pixel art crisp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);

    m_atlasTexture = id;
}

// ───── Frustum Culling ───────────────────────────────────────────────
// Calculate planes
void Frustum::Extract(const glm::mat4& vp)
{
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // left
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // right
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // bottom
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // top
    planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // near
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // far

    for (auto& p : planes) p /= glm::length(glm::vec3(p));
}

bool Frustum::ContainsAABB(const glm::vec3& minP, const glm::vec3& maxP) const
{
    for (const auto& plane : planes)
    {
        glm::vec3 pv = {
            plane.x >= 0 ? maxP.x : minP.x,
            plane.y >= 0 ? maxP.y : minP.y,
            plane.z >= 0 ? maxP.z : minP.z
        };

        if (glm::dot(glm::vec3(plane), pv) + plane.w < 0.0f)
            return false;
    }

    return true;
}

// ───── Infinite World ───────────────────────────────────────────────
void World::UpdateChunkStreaming(const glm::vec3& playerPos)
{
    m_playerPos = playerPos;
    glm::ivec2 playerChunkCoord = ChunkCoord(playerPos.x, playerPos.z);

	// Optimization: If the player is still in the same chunk as last update, and all chunks in the view distance are
    // already loaded (or queued for loading), then skip the entire function to avoid unnecessary load/unload operations
    if (playerChunkCoord == m_lastPlayerChunk)
    {
        bool hasAllChunksLoaded = true;
        std::lock_guard<std::mutex> lock(m_chunkLoadReservationsMutex);
        {
            for (int dx = -m_viewDist; dx <= m_viewDist && hasAllChunksLoaded; ++dx)
            {
                for (int dz = -m_viewDist; dz <= m_viewDist && hasAllChunksLoaded; ++dz)
                {
                    if (dx * dx + dz * dz > m_viewDist * m_viewDist)
                        continue;

                    glm::ivec2 c = { playerChunkCoord.x + dx, playerChunkCoord.y + dz };

                    // If player chunk is NOT found in both core chunk data OR queued data for loading -> not loaded
                    // Else, proceed with load/unload below
                    if (!chunks.count(c) && !m_chunkLoadReservations.count(c))
                        hasAllChunksLoaded = false;
                }
            }

			// If the player is still in the same chunk and all chunks in the view distance are loaded
            // (or yet to be loaded in the queue), then no need to load/unload anything, directly skip the entire function
            if (hasAllChunksLoaded)
            {
                return;
            }
        }
    }
    else
    {
        m_lastPlayerChunk = playerChunkCoord;
    }

    // Make list of chunks to unload
    std::vector<glm::ivec2> chunksToUnload;
    for (auto& [chunkCoord, _] : chunks)
    {
        glm::ivec2 d = chunkCoord - playerChunkCoord;

		// If chunk is outside the unload distance, mark for unload
        if (d.x * d.x + d.y * d.y > m_unloadDist * m_unloadDist)
            chunksToUnload.push_back(chunkCoord);
    }

    // Actually unload
    for (auto& chunkCoord : chunksToUnload)
    {
        // Guard check: if chunkCoord is also present in meshQueue, don't unload
        {
            std::lock_guard<std::mutex> lock(m_chunkMeshUsageGuardMutex);
            if (m_chunkMeshUsageGuards.count(chunkCoord))
                continue;               // Deferred, retry unloading this chunk next time when worker is done
        }

        // Else, proceed with unloading (only modified chunks)
        auto it = chunks.find(chunkCoord);
        if (it != chunks.end() && it->second->modified)
        {
            // Move the chunk to the save queue where the save worker thread will unload
            std::lock_guard<std::mutex> lock(m_chunkSaveMutex);
            m_chunkSaveQueue.push(std::move(it->second));
            m_chunkSaveCV.notify_all();
        }

        // Remove chunk from memory
        m_chunkMeshes[chunkCoord].Destroy();
        m_chunkMeshes.erase(chunkCoord);

        m_waterMeshes[chunkCoord].Destroy();
        m_waterMeshes.erase(chunkCoord);

        chunks.erase(chunkCoord);
        m_deferredLightingChunks.erase(chunkCoord);             // Remove from deferred list if out of render distance
    }

    // Make list of chunks to load
    std::vector<glm::ivec2> chunksToLoad;
    for (int dx = -m_viewDist; dx <= m_viewDist; ++dx)
    {
        for (int dz = -m_viewDist; dz <= m_viewDist; ++dz)
        {
			// If chunk is outside the view distance, skip
            if (dx * dx + dz * dz > m_viewDist * m_viewDist)
                continue;

			// Candidate chunk coord to load
            glm::ivec2 chunkCoord = { playerChunkCoord.x + dx, playerChunkCoord.y + dz };

            // If chunk coord isn't present, push to load list.
            if (!chunks.count(chunkCoord))
                chunksToLoad.push_back(chunkCoord);
        }
    }

	// Sort based on the distance to player - closer chunks get loaded first for better player experience
    std::sort(
        chunksToLoad.begin(),
        chunksToLoad.end(),
        [&playerChunkCoord](const glm::ivec2& a, const glm::ivec2& b)
        {
            glm::ivec2 deltaA = a - playerChunkCoord;
            glm::ivec2 deltaB = b - playerChunkCoord;
            return deltaA.x * deltaA.x + deltaA.y * deltaA.y < deltaB.x * deltaB.x + deltaB.y * deltaB.y;
        }
    );

    // Actually load
    {
        std::lock_guard<std::mutex> lockQ(m_chunkLoadJobMutex);
        std::lock_guard<std::mutex> lockS(m_chunkLoadReservationsMutex);

        for (auto& chunkCoord : chunksToLoad)
        {
            // If chunks is already queued for loading then skip
            if (m_chunkLoadReservations.count(chunkCoord))
                continue;

            // Queue the coord of the to-be-loaded chunk
            m_chunkLoadJobQueue.push(chunkCoord);
            m_chunkLoadReservations.insert(chunkCoord);

            // Mark neighboring chunks dirty because faces at chunk borders depend on adjacent chunk data
            // When a chunk changes, neighbors may need to rebuild meshes for correct face culling
            // Hence mark them dirty so the renderer will re-build the neighbor chunk meshes
            auto it = chunks.find(chunkCoord + glm::ivec2{ 1, 0 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
                 it = chunks.find(chunkCoord + glm::ivec2{ 0, 1 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
                 it = chunks.find(chunkCoord + glm::ivec2{-1, 0 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
                 it = chunks.find(chunkCoord + glm::ivec2{ 0,-1 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }

            // Same for diagonal neighbors
                 it = chunks.find(chunkCoord + glm::ivec2{ 1, 1 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
                 it = chunks.find(chunkCoord + glm::ivec2{ 1,-1 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
                 it = chunks.find(chunkCoord + glm::ivec2{-1, 1 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
                 it = chunks.find(chunkCoord + glm::ivec2{-1,-1 }); if (it != chunks.end()) { it->second->dirty = true; it->second->aoDirty = true; }
        }
    }
    m_chunkLoadJobCV.notify_all();
}

void World::CommitGeneratedChunks()
{
    // Move the chunks from the staging region (done by loading workers)
    // to local main thread memory - directly accessing staging region leads to data races
    std::unordered_map<glm::ivec2, std::unique_ptr<Chunk>, IVec2Hash> queued;
    {
        std::lock_guard<std::mutex> lock(m_generatedChunkStagingMutex);
        queued.swap(m_generatedChunkStaging);
    }

    // Actually move to the core chunk data
    // Mark the chunks dirty for meshing
    for (auto& [coord, chunkPtr] : queued)
    {
        static constexpr glm::ivec2 NEIGHBORING_CHUNK_OFFSET[] = {
            {1, 0}, {-1, 0}, {0, 1}, {0, -1},
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        };

        // Uncomment these lines to make all generated chunks modified -> so that they can be stored to disk
        // But this causes a huge performance drop because of the disk IO, so only enable this when you want
        // to test the chunk saving/loading functionality or when there's complex worldgen, for now worldgen
        // is simple enough to run without significant frame drops
        // chunkPtr->modified = true;

        chunkPtr->dirty = true;
        chunkPtr->modified = false;
        chunkPtr->aoDirty = true;

        chunks[coord] = std::move(chunkPtr);
		m_chunkMeshes.try_emplace(coord);   // default construct mesh for this chunk
        m_waterMeshes.try_emplace(coord);

        // Build light values for present chunk and 4 neighboring chunks
        // Add current chunk
        m_lightingSystem.InitChunkLight(chunks.find(coord)->second.get());
        m_worldPhysics.InitChunkWater(chunks.find(coord)->second.get());

        // Try adding 4 neighbors
        for (int i = 0; i < 4; i++)
        {
            const glm::ivec2 neighboringChunkCoord = coord + NEIGHBORING_CHUNK_OFFSET[i];

            // If this neighboring chunk is already present in coord, it will initalize itself later
            if (queued.count(neighboringChunkCoord))
                continue;

            auto neighboringChunk = chunks.find(neighboringChunkCoord);
            if (neighboringChunk != chunks.end())
            {
                m_lightingSystem.InitChunkLight(neighboringChunk->second.get());
                m_worldPhysics.InitChunkWater(neighboringChunk->second.get());
            }
            else
            {
                // Deferred loading, add the coord to the vector to be loaded later
                m_deferredLightingChunks.insert(neighboringChunkCoord);
            }
        }

        // Load deferred lighting chunks, remove if init successful
        std::erase_if(m_deferredLightingChunks, [&](const glm::ivec2& deferredCoord) {
            // This chunk has just been light initalized, remove it from deferred list
            if (deferredCoord == coord)
                return true;

            // If coord is present in queued, don't remove
            // as its light will be initalized later
            if (queued.count(deferredCoord))
                return false;

            auto it = chunks.find(deferredCoord);
            if (it != chunks.end())
            {
                m_lightingSystem.InitChunkLight(it->second.get());
                m_worldPhysics.InitChunkWater(it->second.get());
                return true;
            }
            return false;
        });

        // Re-dirty all 8 neighbours NOW (after chunks have been loaded) that 
        // this chunk is actually in the live map, UpdateChunkStreaming 
        // already marked them dirty when chunk was *queued*, but the 
        // neighbours may have been re-meshed with null neighbour pointers before we committed
        // This guarantees they rebuild with the correct neighbour data

        for (auto& off : NEIGHBORING_CHUNK_OFFSET)
        {
            auto neighboringChunk = chunks.find(coord + off);
            if (neighboringChunk != chunks.end())
            {
                neighboringChunk->second->dirty = true;
                neighboringChunk->second->aoDirty = true;
            }
        }

        /*
         * Release the reservation AFTER promotion, not before
         *
         * m_chunkLoadReservations prevents the main thread from re-scheduling
         * a coord that is already in-flight (queued or sitting in staging)
         *
         * If the worker erased the reservation inside the thread itself, a race opens:
         *   Worker  -> pushes to staging, erases reservation
         *   Main    -> sees coord absent from chunks AND reservations
		 *           -> re-schedules same coord -> double generation -> Undefined Behavior
         *
         * Safe ordering:
         *   Worker  -> generate -> push to staging  (reservation held)
         *   Main    -> promote staging -> insert into chunks
         *           -> THEN erase reservation here  <- earliest safe point
         */
        {
            std::lock_guard<std::mutex> lock(m_chunkLoadReservationsMutex);
            m_chunkLoadReservations.erase(coord);
        }
    }
}

void World::SyncRenderer()
{
    auto getChunkFromChunkCoords = [&](glm::ivec2 c) -> Chunk* {
        auto it = chunks.find(c);
        return it != chunks.end() ? it->second.get() : nullptr;
        };

    // Phase 1: Enqueue dirty chunks to mesh workers
    {
        std::lock_guard<std::mutex> lockQ(m_meshJobMutex);
        std::lock_guard<std::mutex> lockR(m_chunkMeshUsageGuardMutex);

        for (auto& [chunkPos, chunk] : chunks)
        {
            // Only mesh dirty chunks
            if (!chunk->dirty)
                continue;

            // Skip if already present
            if (m_chunkMeshUsageGuards.count(chunkPos))
                continue;

            // Protect coord (and +4 neighbors) from unload/deletion later by the main thread
            m_chunkMeshUsageGuards[chunkPos]++;
            for (auto& neighborPos : GUARDED_NEIGHBORS)
                ++m_chunkMeshUsageGuards[chunkPos + neighborPos];    // We check if this coord is present in worker thread, no need to check now

            // Do this to ensure the worker thread can access the chunk data without worrying about concurrent deletion by the main thread
            // This is used in ChunkMeshBuilder when it accesses neighbor chunk data for Ambient Occlusion (and later greedy meshing)
            MeshJob job;
            job.coord = chunkPos;
            job.chunk = chunk.get();
            job.nPX    = getChunkFromChunkCoords(chunkPos + glm::ivec2{ 1,  0 });
            job.nNX    = getChunkFromChunkCoords(chunkPos + glm::ivec2{-1,  0 });
            job.nPZ    = getChunkFromChunkCoords(chunkPos + glm::ivec2{ 0,  1 });
            job.nNZ    = getChunkFromChunkCoords(chunkPos + glm::ivec2{ 0, -1 });
            job.nPX_PZ = getChunkFromChunkCoords(chunkPos + glm::ivec2{ 1,  1 });   // (+X, +Z)
            job.nPX_NZ = getChunkFromChunkCoords(chunkPos + glm::ivec2{ 1, -1 });   // (+X, -Z)
            job.nNX_PZ = getChunkFromChunkCoords(chunkPos + glm::ivec2{-1,  1 });   // (-X, +Z)
            job.nNX_NZ = getChunkFromChunkCoords(chunkPos + glm::ivec2{-1, -1 });   // (-X, -Z)

            m_meshJobQueue.push(job);
            chunk->dirty = false;
        }
    }
    m_meshJobCV.notify_all();

    // Phase 2: Drain mesh staging, move the meshes from the staging region to local main thread memory
    // Amortized GPU vertex upload
    const int MAX_UPLOADS_PER_FRAME = 4;
    int uploadsThisFrame = 0;

    // Opaque pass
    std::vector<std::pair<glm::ivec2, std::vector<Vertex>>> chunksToUpload;
    {
        std::lock_guard<std::mutex> lock(m_meshStagingMutex);

        auto it = m_meshStaging.begin();
        while (it != m_meshStaging.end() && uploadsThisFrame < MAX_UPLOADS_PER_FRAME)
        {
            chunksToUpload.push_back({ it->first, std::move(it->second) });
            it = m_meshStaging.erase(it);
            uploadsThisFrame++;
        }
    }

    // Water pass
    std::vector<std::pair<glm::ivec2, std::vector<Vertex>>> waterChunksToUpload;
    {
        std::lock_guard<std::mutex> lock(m_meshStagingMutex);

        auto it = m_waterMeshStaging.begin();
        while (it != m_waterMeshStaging.end())
        {
            waterChunksToUpload.push_back({ it->first, std::move(it->second) });
            it = m_waterMeshStaging.erase(it);
        }
    }

    // Phase 3: Upload meshes to the GPU
    // Opaque pass
    for (auto& [coord, verts] : chunksToUpload)
    {
        auto it = m_chunkMeshes.find(coord);
        if (it != m_chunkMeshes.end())
            it->second.Upload(verts);
    }

    // Water pass
    for (auto& [coord, verts] : waterChunksToUpload)
    {
        auto it = m_waterMeshes.find(coord);
        if (it != m_waterMeshes.end())
            it->second.Upload(verts);
    }
}

void World::DrawAll(const glm::mat4& proj, const glm::mat4& view)
{
    if (!m_chunkShader)
        return;

    m_chunkShader->use();

    // Bind textures once — shared across all chunk draw calls
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);

    // Extract frustum planes
    m_frustum.Extract(proj * view);

    // Draw all meshes (computed earlier)
    for (auto& [chunkPos, mesh] : m_chunkMeshes)
    {
        glm::vec3 minP = { chunkPos.x * CX,    0,  chunkPos.y * CZ };
        glm::vec3 maxP = { chunkPos.x * CX + CX, CY, chunkPos.y * CZ + CZ };

        // Implement frustum culling
        if (!m_frustum.ContainsAABB(minP, maxP))
            continue;

        mesh.Draw();
    }

    //if (!m_waterShader)
    //    return;

    //m_waterShader->use();

    //// Bind textures once — shared across all chunk draw calls
    //glActiveTexture(GL_TEXTURE1);
    //glBindTexture(GL_TEXTURE_2D, m_atlasTexture);

    m_frustum.Extract(proj * view);

    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto& [chunkPos, mesh] : m_waterMeshes)
    {
        mesh.Draw();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}


// ───── Multithreading ───────────────────────────────────────────────
// Start and Stop workers
void World::StartChunkLoadWorkers(int count)
{
    m_shutdown = false;
    for (int i = 0; i < count; ++i)
        m_chunkLoadWorkers.emplace_back([this] { ChunkLoadWorkerLoop(); });
}

void World::StartMeshWorkers(int count)
{
    for (int i = 0; i < count; ++i)
        m_meshWorkers.emplace_back([this] { MeshWorkerLoop(); });
}

void World::StopAllWorkers()
{
    // Initiate shutdown
    m_shutdown = true;

    // Wake all sleeping workers so they exit
    m_chunkLoadJobCV.notify_all();
    m_chunkSaveCV.notify_all();
    m_meshJobCV.notify_all();

    // Join the save worker
    if (m_chunkSaveWorker.joinable())
        m_chunkSaveWorker.join();

    // Join mesh workers
    for (auto& t : m_meshWorkers)
        if (t.joinable())
            t.join();

    m_meshWorkers.clear();

    // Join chunk load workers
    for (auto& t : m_chunkLoadWorkers)
        if (t.joinable())
            t.join();

    m_chunkLoadWorkers.clear();
}

// Worker loops
void World::ChunkLoadWorkerLoop()
{
    while (true)
    {
        // Get the coord of the chunk to be loaded safely
        glm::ivec2 coord;
        {
            std::unique_lock<std::mutex> lock(m_chunkLoadJobMutex);
            m_chunkLoadJobCV.wait(lock, [&] {         // Wakeup when queue is NOT empty or when shutdown triggered
                return !m_chunkLoadJobQueue.empty() || m_shutdown;
            });

            if (m_shutdown && m_chunkLoadJobQueue.empty())
                break;

            coord = m_chunkLoadJobQueue.front();
            m_chunkLoadJobQueue.pop();
        }

        // Fill into worker-local chunk
        auto chunk = std::make_unique<Chunk>(coord);                  // Make this on heap

        // No need to lock this, no shared resource used in this function
        FillChunk(*chunk, coord);

        // Move the chunk to staged section and remove the coord from queue
        {
            std::lock_guard<std::mutex> lock(m_generatedChunkStagingMutex);
            m_generatedChunkStaging[coord] = std::move(chunk);        // Constant-time operation, just moving the unique_ptr
        }
    }
}

void World::MeshWorkerLoop()
{
    std::vector<Vertex> opaqueVerts;
    std::vector<Vertex> waterVerts;
    opaqueVerts.reserve(CX * CZ * 64);
    waterVerts.reserve(CX * CZ);

    while (true)
    {
        // Contains information about chunk coord, pointers to 
        // current chunk and all its 4 neighbors 
        // (no need to access map, this results in reduced hashing)

		// 1. Obtain chunk to mesh
        MeshJob job;
        {
            std::unique_lock<std::mutex> lock(m_meshJobMutex);
            m_meshJobCV.wait(lock, [&] {          // Wait until mesh queue is empty or shutdown is NOT triggered
                return !m_meshJobQueue.empty() || m_shutdown;
            });

            if (m_shutdown && m_meshJobQueue.empty())
                break;

            // Pop out a mesh job from the queue for further processing: generate mesh
            job = m_meshJobQueue.front();
            m_meshJobQueue.pop();
        }

        // 2. Now chunk coord is owned, build the vertices
        {
            opaqueVerts.clear();
            waterVerts.clear();

            if (job.chunk)
            {
                ChunkMeshBuilder::Build(
                    *job.chunk,
                    job.nPX, job.nNX,
                    job.nPZ, job.nNZ,
                    job.nPX_PZ, job.nPX_NZ,
                    job.nNX_PZ, job.nNX_NZ,
                    opaqueVerts, waterVerts
                );
            }
        }

        // 3. Push built vertices to staging
        {
            std::lock_guard<std::mutex> lock(m_meshStagingMutex);

            std::vector<Vertex> toStageOpaqueVerts;
            toStageOpaqueVerts.swap(opaqueVerts);

            std::vector<Vertex> toStageWaterVerts;
            toStageWaterVerts.swap(waterVerts);

            m_meshStaging[job.coord] = std::move(toStageOpaqueVerts);
            m_waterMeshStaging[job.coord] = std::move(toStageWaterVerts);
        }

        // 4. Decrement refcounts, so that the chunk can be unloaded (unguard now)
        {
            std::lock_guard<std::mutex> lock(m_chunkMeshUsageGuardMutex);

            // Decrement coord itself
            m_chunkMeshUsageGuards[job.coord]--;
            if (m_chunkMeshUsageGuards[job.coord] == 0)
                m_chunkMeshUsageGuards.erase(job.coord);

            // Decrement neighbors
            for (auto& neighborPos : GUARDED_NEIGHBORS)
            {
                glm::ivec2 nb = job.coord + neighborPos;
                auto it = m_chunkMeshUsageGuards.find(nb);
                if (it != m_chunkMeshUsageGuards.end())
                {
                    (it->second)--;
                    if (it->second <= 0)
                        m_chunkMeshUsageGuards.erase(it->first);
                }
            }
        }
    }
}

void World::SaveWorkerLoop()
{
    while (true)
    {
        std::unique_ptr<Chunk> chunk;

        // Safely get the chunk from the save queue
        {
            std::unique_lock<std::mutex> lock(m_chunkSaveMutex);
            m_chunkSaveCV.wait(lock, [&] {                       // Wakeup when queue is NOT empty or when shutdown triggered
                return !m_chunkSaveQueue.empty() || m_shutdown;
            });

            if (m_shutdown && m_chunkSaveQueue.empty())
                break;

            chunk = std::move(m_chunkSaveQueue.front());
            m_chunkSaveQueue.pop();
        }

        // Save the chunk to disk
        SaveChunkToDisk(*chunk);
    }
}

// Important clarification: main thread here means the global renderer thread, not the main thread which deals with capturing inputs (see OpenGL_3D.h/.cpp)

#include <glm/glm.hpp>

#include "rendering/ChunkMesh.h"
#include "physics/WorldPhysics.h"
#include "world/LightingSystem.h"

#include <cmath>
#include <array>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <condition_variable>
#include <optional>

class Shader;

class Chunk;
struct RaycastHit;

enum class BlockType : uint8_t;

struct IVec2Hash
{
    size_t operator()(const glm::ivec2& v) const noexcept
    {
        // Murmur3 — breaks clustering on grid coords
        // XOR-shift + multiply scrambles bit patterns from axis-aligned sequences
        size_t h = (size_t)(uint32_t)v.x;
        h ^= (size_t)(uint32_t)v.y + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }
};

struct Frustum
{
    std::array<glm::vec4, 6> planes {};

    void Extract(const glm::mat4& vp);
    bool ContainsAABB(const glm::vec3& min, const glm::vec3& max) const;
};

struct MeshJob
{ 
    glm::ivec2 coord;
    Chunk* chunk;

    // Pointers for chunks neighboring the current chunk
    // This prevents this class to access the thread & time critical World object
    // This also decouples the mesh worker from the world state 
    // and removes unnecessary duplicate map lookups 
    Chunk* nPX = nullptr;       // +X neighbor
    Chunk* nNX = nullptr;       // -X neighbor
    Chunk* nPZ = nullptr;       // +Z neighbor
    Chunk* nNZ = nullptr;       // -Z neighbor

    Chunk* nPX_PZ = nullptr;    // (+X, +Z) neighbor
    Chunk* nPX_NZ = nullptr;    // (+X, -Z) neighbor
    Chunk* nNX_PZ = nullptr;    // (-X, +Z) neighbor
    Chunk* nNX_NZ = nullptr;    // (-X, -Z) neighbor
};

class World
{
public:
    // Core chunk data
    std::unordered_map<glm::ivec2, std::unique_ptr<Chunk>, IVec2Hash> chunks;

    World(); // Constructor
    ~World(); // Destructor

	// Non-copyable, non-movable as it manages worker threads and has unique ownership of chunks
	World(const World&) = delete;
	World(World&&) = delete;
	World& operator=(const World&) = delete;
	World& operator=(World&&) = delete;

    // Unload chunks
    void UnloadChunks();

    // Load shader and texture file
    void SetChunkShader(Shader& shader, Shader& waterShader);
    void LoadAtlasTexture(const char* path);

    // Block access & Chunk coord functions
    [[nodiscard]] BlockType GetBlock(int worldX, int worldY, int worldZ) const;
    void SetBlock(int worldX, int worldY, int worldZ, BlockType type);
    [[nodiscard]] static glm::ivec2 ChunkCoord(float worldX, float worldZ);
    [[nodiscard]] static glm::ivec3 ChunkLocalCoord(int worldX, int worldY, int worldZ);
    [[nodiscard]] bool IsSolid(int worldX, int worldY, int worldZ) const;

    // Player interaction
    [[nodiscard]] bool PlaceBlock(const RaycastHit& hit, BlockType type);
    [[nodiscard]] bool BreakBlock(const RaycastHit& hit);

    // Chunk access
    [[nodiscard]] Chunk* GetChunk(int worldX, int worldZ);
    [[nodiscard]] const Chunk* GetChunk(int worldX, int worldZ) const;

    [[nodiscard]] WorldPhysics& GetWorldPhysics() noexcept { return m_worldPhysics; }
    [[nodiscard]] LightingSystem& GetLightingSystem() noexcept { return m_lightingSystem; }

    // 1) Generate and unload chunks by sending jobs to chunk job threads
    void UpdateChunkStreaming(const glm::vec3& playerPos);

	// 2) Move the loaded chunks from staging region to core chunk data & mark the chunks "dirty" for meshing
    void CommitGeneratedChunks();

	// 3) Generate meshes (by mesh job threads), stage, push to local and upload to GPU
    void SyncRenderer();

	// 4) Draw all generated meshes - final call
    void DrawAll(const glm::mat4& proj, const glm::mat4& view);

    // Worker thread loops
    void StartChunkLoadWorkers(int count);
    void StartMeshWorkers(int count = 2);
    void StopAllWorkers();

private:
    // Global world variables - meshes for all loaded chunks, chunk shader and atlas texture index
	// Each chunk (chunk coord) has a corresponding chunk mesh
    std::unordered_map<glm::ivec2, ChunkMesh, IVec2Hash> m_chunkMeshes;
    std::unordered_map<glm::ivec2, ChunkMesh, IVec2Hash> m_waterMeshes;
    Shader* m_chunkShader = nullptr;
    Shader* m_waterShader = nullptr;
    unsigned int m_atlasTexture = 0;

	// Frustum planes for Frustum Culling
	Frustum m_frustum;

    // World physics and lighting system manager
    WorldPhysics m_worldPhysics;
    LightingSystem m_lightingSystem;

    // Chunk coordinates with lighting initialization postponed until their neighbors are loaded into the map
    std::unordered_set<glm::ivec2, IVec2Hash> m_deferredLightingChunks;

	// World-player variables
    // TODO: Make this dynamic and make user to control - to be done later
    // Chunk load and unload distance
    int m_viewDist = 8;
    int m_unloadDist = 12;
    glm::vec3 m_playerPos;
    glm::ivec2 m_lastPlayerChunk = { INT_MAX, INT_MAX };	// Previous frame player chunk pos

	// Global atomic shutdown flag for workers to exit
    std::atomic<bool> m_shutdown{ false };

    // Mark the adjacent chunk dirty if the world coord passed is at a chunk boundary (same with AO)
    void MarkAdjacentChunksDirty(int wx, int wy, int wz);
    void MarkAdjacentChunksAODirty(int wx, int wy, int wz);

    // Returns height at location using Perlin noise
    [[nodiscard]] static float GetTerrainHeight(int wx, int wz);

    // Chunk streaming functions
    [[nodiscard]] static std::string ChunkFilePath(glm::ivec2 coord);
    static void SaveChunkToDisk(const Chunk& chunk);
    static bool LoadChunkFromDisk(Chunk& chunk, glm::ivec2& coord);

    // -------- Load workers --------
    // Chunk Job queue: main thread pushes coords to load, workers pop
    std::queue<glm::ivec2> m_chunkLoadJobQueue;
    std::mutex m_chunkLoadJobMutex;
    std::condition_variable m_chunkLoadJobCV;

    // Staging: workers push, main promotes
    std::unordered_map<glm::ivec2, std::unique_ptr<Chunk>, IVec2Hash> m_generatedChunkStaging;
    std::mutex m_generatedChunkStagingMutex;

	// Prevents duplicate load scheduling: main thread only
    std::unordered_set<glm::ivec2, IVec2Hash> m_chunkLoadReservations;
    std::mutex m_chunkLoadReservationsMutex;

	// Chunk workers
    std::vector<std::thread> m_chunkLoadWorkers;
    void ChunkLoadWorkerLoop();

	// Called by worker thread to fill chunk - fetch from disk or generate terrain (if new chunk)
    void FillChunk(Chunk& chunk, glm::ivec2 coord);

    // -------- Mesh workers --------
    // Mesh Job queue: main thread pushes "dirty" chunks to mesh, workers pop
    std::queue<MeshJob> m_meshJobQueue;
    std::mutex m_meshJobMutex;
    std::condition_variable m_meshJobCV;

    // Staging: workers push generated meshes, main promotes
    std::unordered_map<glm::ivec2, std::vector<Vertex>, IVec2Hash> m_meshStaging;
    std::unordered_map<glm::ivec2, std::vector<Vertex>, IVec2Hash> m_waterMeshStaging;
    std::mutex m_meshStagingMutex;

	// List of chunks to not unload while being meshed
    std::unordered_map<glm::ivec2, int, IVec2Hash> m_chunkMeshUsageGuards;
    std::mutex m_chunkMeshUsageGuardMutex;

    // Mesh workers
    std::vector<std::thread> m_meshWorkers;
    void MeshWorkerLoop();

    // -------- Save worker --------
	// Queue holds pointers to chunk to be saved to disk
    std::queue<std::unique_ptr<Chunk>> m_chunkSaveQueue;
    std::mutex m_chunkSaveMutex;
    std::condition_variable m_chunkSaveCV;

	// Use a single thread for saving chunks to disk
    std::thread m_chunkSaveWorker;
    void SaveWorkerLoop();
};

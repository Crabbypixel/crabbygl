#pragma once
#include "rendering/Shader.h"

#include "rendering/GL.h"

#include <string>

class ChunkDebug
{
public:
    ChunkDebug() = default;

	// Disable copy and move semantics to prevent accidental copying of OpenGL resources
	ChunkDebug(const ChunkDebug&) = delete;
	ChunkDebug(ChunkDebug&&) = delete;
	ChunkDebug& operator=(const ChunkDebug&) = delete;
	ChunkDebug& operator=(ChunkDebug&&) = delete;

    void Init(const std::string& shaderFile);
    void DrawChunkBoundary(const glm::vec3& playerPos);
    ~ChunkDebug();

    bool visible = false;

private:
    unsigned int m_VAO = 0, m_VBO = 0;
    int    m_lineCount = 0;
    Shader m_shader;
    unsigned int m_boxVAO = 0, m_boxVBO = 0;
};
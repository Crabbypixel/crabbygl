#pragma once
#include "rendering/Shader.h"
#include <glm/glm.hpp>
#include <string>

class Sun
{
public:
	Sun() = default;
	~Sun();

	// Non-copyable and non-movable as it owns OpenGL resources
	Sun(const Sun&) = delete;
	Sun(Sun&&) = delete;
	Sun& operator=(const Sun&) = delete;
	Sun& operator=(Sun&&) = delete;

	void Init(const std::string& shaderFile);
	void Draw(const glm::vec3& cameraPos, const glm::vec3& lightDir);
	
	float distance = 400.0f;	// Distance from camera to sun
	float size = 30.0f;			// Size of the sun

	glm::vec3 color = glm::vec3(1.0f, 0.81f, 0.4f);		// Halo color
	glm::vec3 coreColor = glm::vec3(1.0f);				// Disc color
	float haloStrength = 0.2f;

private:
	unsigned int m_VAO = 0;
	unsigned int m_VBO = 0;
	Shader m_shader;
};
#pragma once
#include "rendering/Shader.h"
#include <glm/glm.hpp>

class Sun
{
public:
	Sun() = default;
	~Sun();

	// Non-copyable and non-movable
	Sun(const Sun&) = delete;
	Sun(Sun&&) = delete;
	Sun& operator=(const Sun&) = delete;
	Sun& operator=(Sun&&) = delete;

	void Init(const std::string& shaderFile);
	void Draw(const glm::vec3 cameraPos, const glm::vec3& lightDir);

	float distance = 400.0f;		// Distance from camera to sun
	float size = 50.0f;				// Size of sun in world units
	glm::vec3 color = glm::vec3(1.0f, 0.95f, 0.8f);

private:
	unsigned int m_VAO, m_VBO = 0;
	Shader m_shader;
};
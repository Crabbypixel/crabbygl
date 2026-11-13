#include "rendering/Sun.h"
#include <glad/glad.h>

Sun::~Sun()
{
	if(m_VBO)
		glDeleteBuffers(1, &m_VBO);
	
	if(m_VAO)
		glDeleteVertexArrays(1, &m_VAO);
}

void Sun::Init(const std::string& shaderFile)
{
	static constexpr float quad[] =
	{
		-1.0f,  1.0f,
		-1.0f, -1.0f,
		 1.0f, -1.0f,

		-1.0f,  1.0f,
		 1.0f, -1.0f,
		 1.0f,  1.0f
	};

	m_shader.load(shaderFile);
	
	// Set up UBO for this shader
	glUniformBlockBinding(m_shader.getID(), glGetUniformBlockIndex(m_shader.getID(), "Matrices"), 0);
	
	glGenVertexArrays(1, &m_VAO);
	glGenBuffers(1, &m_VBO);

	glBindVertexArray(m_VAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glBindVertexArray(0);
}

void Sun::Draw(const glm::vec3& cameraPos, const glm::vec3& lightDir)
{
	const glm::vec3 toSun = -glm::normalize(lightDir);

	// Camera-anchored -> no parallax, reads as infinitely far
	const glm::vec3 sunPos = cameraPos + toSun * distance;

	// Edges follow the orbit (DayCycle orbits around world Z), like MC's sky-fixed quad.
	// toSun.z <= ~0.29 due to orbit tilt, so the cross never degenerates.
	const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toSun));
	const glm::vec3 up = glm::cross(toSun, right);

	// Sun is always drawn before the terrain with no depth - terrain overdraws it
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	m_shader.use();
	m_shader.setVec3("uSunWorldPos", sunPos);
	m_shader.setVec3("uSunRight", right);
	m_shader.setVec3("uSunUp", up);
	m_shader.setFloat("uSunSize", size);
	m_shader.setVec3("uSunColor", color);

	glBindVertexArray(m_VAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);

	// Restore state
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}
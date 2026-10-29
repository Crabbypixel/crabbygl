#include "rendering/Sun.h"
#include <glad/glad.h>

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
}

void Sun::Draw(const glm::vec3 cameraPos, const glm::vec3& lightDir)
{

}
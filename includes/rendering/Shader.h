#pragma once
#include "rendering/GL.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

class Shader
{
public:
	Shader() = default;

	// Non-copyable, non-movable
	Shader(const Shader&) = delete;
	Shader(Shader&&) = delete;
	Shader& operator=(const Shader&) = delete;
	Shader& operator=(Shader&&) = delete;

	void load(const std::string& shaderPath);

	void use() const noexcept;

	[[nodiscard]]
	unsigned int getID() const noexcept { return m_id; }

	void setBool(const std::string& name, bool value) noexcept;
	void setInt(const std::string& name, int value) noexcept;
	void setFloat(const std::string& name, float value) noexcept;
	void setMat4(const std::string& name, const glm::mat4& mat) noexcept;
	void setVec3(const std::string& name, float f1, float f2, float f3) noexcept;
	void setVec3(const std::string& name, const glm::vec3& vec) noexcept;
	void setIvec3(const std::string& name, const glm::ivec3& ivec) noexcept;
	void setIvec3(const std::string& name, int i1, int i2, int i3) noexcept;
	void setVec2(const std::string& name, float f1, float f2) noexcept;
	void setVec2(const std::string& name, const glm::vec2& vec) noexcept;
	void setIvec2(const std::string& name, const glm::ivec2& ivec) noexcept;
	void setIvec2(const std::string& name, int i1, int i2) noexcept;
	void setVec4(const std::string& name, const glm::vec4& vec) noexcept;
	void setVec4(const std::string& name, float f1, float f2, float f3, float f4) noexcept;
	void setIvec4(const std::string& name, const glm::ivec4& vec) noexcept;
	void setIvec4(const std::string& name, int f1, int f2, int f3, int f4) noexcept;

	~Shader();

private:
	unsigned int m_id = 0;
	std::unordered_map<std::string, int> m_uniformLocationCache;
	int GetUniformLocation(const std::string& name) noexcept;
	int GetUniformLocationChecked(const std::string& name) noexcept;
	unsigned int CompileShader(unsigned int type, const std::string& source, const std::string& shaderPath);
};
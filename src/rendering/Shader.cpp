
#include "rendering/Shader.h"

#ifdef __EMSCRIPTEN__
static const std::string GLSL_HEADER = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
#else
static const std::string GLSL_HEADER = "#version 330 core\n";
#endif

void Shader::load(const std::string& shaderPath)
{
	bool isGeometryShaderPresent = false;
	std::ifstream stream(shaderPath);

	if (!stream.is_open())
	{
		std::cerr << shaderPath << " not found.\n";
		glfwTerminate();
		exit(0);
	}

	enum class ShaderType
	{
		NONE = -1,
		VERTEX = 0,
		FRAGMENT = 1,
		GEOMETRY = 2
	};

	std::string line;
	ShaderType type = ShaderType::NONE;
	std::stringstream ss[3];

	while (getline(stream, line))
	{
		if (line.find("SHADER") != std::string::npos)
		{
			if (line.find("VERTEX") != std::string::npos)
				type = ShaderType::VERTEX;
			else if (line.find("FRAGMENT") != std::string::npos)
				type = ShaderType::FRAGMENT;
			else if (line.find("GEOMETRY") != std::string::npos)
			{
				type = ShaderType::GEOMETRY;
				isGeometryShaderPresent = true;
			}
		}
		else
		{
			ss[(int)type] << line << '\n';
		}
	}

	std::string vertexShader = GLSL_HEADER + "#define SHADER_VERTEX\n #ifdef SHADER_VERTEX\n" + ss[(int)ShaderType::VERTEX].str();
	std::string fragmentShader = GLSL_HEADER + "#define SHADER_FRAGMENT\n #ifdef SHADER_FRAGMENT\n" + ss[(int)ShaderType::FRAGMENT].str();
	std::string geometryShader;

	if (isGeometryShaderPresent)
		geometryShader = GLSL_HEADER + "#define SHADER_GEOMETRY\n #ifdef SHADER_GEOMETRY\n" + ss[(int)ShaderType::GEOMETRY].str();

	// Compile shaders
	unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexShader, shaderPath);
	unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader, shaderPath);
	unsigned int gs;

	if (isGeometryShaderPresent)
		gs = CompileShader(GL_GEOMETRY_SHADER, geometryShader, shaderPath);

	// Link shaders
	m_id = glCreateProgram();
	glAttachShader(m_id, vs);
	glAttachShader(m_id, fs);
	if (isGeometryShaderPresent)
		glAttachShader(m_id, gs);

	glLinkProgram(m_id);
	glValidateProgram(m_id);

	// Check for linking errors
	int result;
	glGetProgramiv(m_id, GL_LINK_STATUS, &result);

	if (!result)
	{
		int length;
		glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &length);

		std::string message(length, '\0');
		glGetProgramInfoLog(m_id, length, &length, message.data());

		std::cerr << "[OpenGL Error] Linking error in \'" << shaderPath << "\'" << std::endl;
		std::cerr << "Log: " << message << std::endl;

		glDeleteShader(vs);
		glDeleteShader(fs);

		if (isGeometryShaderPresent)
			glDeleteShader(gs);

		glfwTerminate();
		exit(0);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	if (isGeometryShaderPresent)
		glDeleteShader(gs);
}

void Shader::use() const noexcept
{
	glUseProgram(m_id);
}

void Shader::setBool(const std::string& name, bool value) noexcept
{
	glUniform1i(GetUniformLocationChecked(name), (int)value);
}

void Shader::setInt(const std::string& name, int value) noexcept
{
	glUniform1i(GetUniformLocationChecked(name), value);
}

void Shader::setFloat(const std::string& name, float value) noexcept
{
	glUniform1f(GetUniformLocationChecked(name), value);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) noexcept
{
	glUniformMatrix4fv(GetUniformLocationChecked(name), 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::setVec3(const std::string& name, float f1, float f2, float f3) noexcept
{
	glUniform3f(GetUniformLocationChecked(name), f1, f2, f3);
}

void Shader::setVec3(const std::string& name, const glm::vec3& vec) noexcept
{
	glUniform3f(GetUniformLocationChecked(name), vec.x, vec.y, vec.z);
}

void Shader::setIvec3(const std::string& name, const glm::ivec3& ivec) noexcept
{
	glUniform3i(GetUniformLocationChecked(name), ivec.x, ivec.y, ivec.z);
}

void Shader::setIvec3(const std::string& name, int i1, int i2, int i3) noexcept
{
	glUniform3i(GetUniformLocationChecked(name), i1, i2, i3);
}

void Shader::setVec4(const std::string& name, const glm::vec4& vec) noexcept
{
	glUniform4f(GetUniformLocationChecked(name), vec.x, vec.y, vec.z, vec.w);
}

void Shader::setVec4(const std::string& name, float f1, float f2, float f3, float f4) noexcept
{
	glUniform4f(GetUniformLocationChecked(name), f1, f2, f3, f4);
}

void Shader::setIvec4(const std::string& name, const glm::ivec4& vec) noexcept
{
	glUniform4i(GetUniformLocationChecked(name), vec.x, vec.y, vec.z, vec.w);
}

void Shader::setIvec4(const std::string& name, int f1, int f2, int f3, int f4) noexcept
{
	glUniform4i(GetUniformLocationChecked(name), f1, f2, f3, f4);
}

void Shader::setVec2(const std::string& name, float f1, float f2) noexcept
{
	glUniform2f(GetUniformLocationChecked(name), f1, f2);
}

void Shader::setVec2(const std::string& name, const glm::vec2& vec) noexcept
{
	glUniform2f(GetUniformLocationChecked(name), vec.x, vec.y);
}

void Shader::setIvec2(const std::string& name, const glm::ivec2& ivec) noexcept
{
	glUniform2i(GetUniformLocationChecked(name), ivec.x, ivec.y);
}

void Shader::setIvec2(const std::string& name, int i1, int i2) noexcept
{
	glUniform2i(GetUniformLocationChecked(name), i1, i2);
}

// Private utility function - to get uniform location with caching
int Shader::GetUniformLocation(const std::string& name) noexcept
{
	auto it = m_uniformLocationCache.find(name);

	if (it != m_uniformLocationCache.end())
		return it->second;

	int location = glGetUniformLocation(m_id, name.c_str());
	m_uniformLocationCache[name] = location;

	return location;
}

int Shader::GetUniformLocationChecked(const std::string& name) noexcept
{
	const int location = GetUniformLocation(name);

	if (location == -1)
		std::cerr << "Shader uniform not found: " << name << '\n';

	return location;
}

// Private utility function - to compile vertex and fragment shader
unsigned int Shader::CompileShader(unsigned int type, const std::string& source, const std::string& shaderPath)
{
	unsigned int shader = glCreateShader(type);
	const char* shaderSource = source.c_str();
	glShaderSource(shader, 1, &shaderSource, NULL);
	glCompileShader(shader);

	int result;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

	if (!result)
	{
		int length;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

		// Generate info log
		std::string message(length, '\0');
		glGetShaderInfoLog(shader, length, &length, message.data());

		std::cerr << "[OpenGL Error] Failed to compile ";

		if (type == GL_VERTEX_SHADER)
			std::cerr << "vertex";
		else if (type == GL_FRAGMENT_SHADER)
			std::cerr << "fragment";
		else if (type == GL_GEOMETRY_SHADER)
			std::cerr << "geometry";

		std::cerr << " shader in \'" << shaderPath << "\'" << std::endl;
		std::cerr << "Log: " << message << std::endl;

		glDeleteShader(shader);

		glfwTerminate();
		exit(0);
	}

	return shader;
}

Shader::~Shader()
{
	if(m_id != 0)
		glDeleteProgram(m_id);
}
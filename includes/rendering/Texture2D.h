#pragma once

#include "rendering/GL.h"
#include <string>

class Texture2D
{
private:
    unsigned int m_TextureID = 0;
    int m_width = 0, m_height = 0;
    unsigned char* data = 0;
    int m_nrChannels = 0;

public:
    Texture2D() = default;

	// Non-copyable, non-movable
	Texture2D(const Texture2D&) = delete;
	Texture2D(Texture2D&&) = delete;
	Texture2D& operator=(const Texture2D&) = delete;
	Texture2D& operator=(Texture2D&&) = delete;

    ~Texture2D() noexcept;

    void load(GLenum wrapType, GLint minFilter, GLint magFilter,
        const std::string& textureFile,
        GLint internalFormat, GLenum format);

	[[nodiscard]]
    unsigned int getTextureID() const noexcept;

    void bindTexture() const noexcept;

    void loadTexture(char const* path);
};
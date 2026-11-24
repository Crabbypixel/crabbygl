#include "rendering/Texture2D.h"

#include <iostream>
#include "stb/stb_image.h"

// Load with parameters
void Texture2D::load(GLenum wrapType, GLint minFilter, GLint magFilter, const std::string& textureFile, GLint internalFormat, GLenum format)
{
    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapType);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapType);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

    data = stbi_load(textureFile.c_str(), &m_width, &m_height, &m_nrChannels, 0);

    if (data)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
            m_width, m_height, 0,
            format, GL_UNSIGNED_BYTE, data);

        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cerr << "Failed to load texture: " << textureFile << std::endl;
    }

    stbi_image_free(data);
}

unsigned int Texture2D::getTextureID() const noexcept
{
    return m_TextureID;
}

// Bind texture
void Texture2D::bindTexture() const noexcept
{
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
}

// Simple load function
void Texture2D::loadTexture(char const* path)
{
    glGenTextures(1, &m_TextureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);

    if (data)
    {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;
        else
            exit(-1);

        glBindTexture(GL_TEXTURE_2D, m_TextureID);

        // GLES3/WebGL2 rejects unsized GL_RED as internalformat; GL_R8 is valid everywhere.
        GLint internalFormat = (format == GL_RED) ? GL_R8 : (GLint)format;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
            width, height, 0,
            format, GL_UNSIGNED_BYTE, data);

        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
            format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
            format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cerr << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }
}

// Destructor
Texture2D::~Texture2D()
{
    if(m_TextureID != 0)
        glDeleteTextures(1, &m_TextureID);
}
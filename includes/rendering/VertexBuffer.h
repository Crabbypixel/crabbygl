#pragma once

#include "rendering/GL.h"

template<typename T = float>
class VertexBuffer
{
private:
	unsigned int m_VertexBufferID = 0;
	size_t m_BufferBytes = 0;
	size_t m_VertexCount = 0;
public:
	static constexpr size_t typeSize = sizeof(T);

	VertexBuffer() = default;

	// Non-copyable, non-movable
	VertexBuffer(const VertexBuffer&) = delete;
	VertexBuffer(VertexBuffer&&) = delete;
	VertexBuffer& operator=(const VertexBuffer&) = delete;
	VertexBuffer& operator=(VertexBuffer&&) = delete;

	void generate(size_t vertexCount) noexcept;

	void bind() const noexcept;

	void unbind() const noexcept;

	void setBuffer(size_t bytes, const void* data) noexcept;

	[[nodiscard]] size_t getBufferBytes() const noexcept;

	[[nodiscard]] size_t getVertexCount() const noexcept;

	[[nodiscard]] unsigned int getID() const noexcept;

	~VertexBuffer() noexcept;
};

template<typename T>
void VertexBuffer<T>::generate(size_t vertexCount) noexcept
{
	m_VertexCount = vertexCount;

	glGenBuffers(1, &m_VertexBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferID);
}

template<typename T>
void VertexBuffer<T>::bind() const noexcept
{
	glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferID);
}

template<typename T>
void VertexBuffer<T>::unbind() const noexcept
{
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

template<typename T>
void VertexBuffer<T>::setBuffer(size_t bytes, const void* data) noexcept
{
	m_BufferBytes = bytes;
	glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_STATIC_DRAW);
}

template<typename T>
size_t VertexBuffer<T>::getBufferBytes() const noexcept
{
	return m_BufferBytes;
}

template<typename T>
size_t VertexBuffer<T>::getVertexCount() const noexcept
{
	return m_VertexCount;
}

template<typename T>
unsigned int VertexBuffer<T>::getID() const noexcept
{
	return m_VertexBufferID;
}

template<typename T>
VertexBuffer<T>::~VertexBuffer() noexcept
{
	if (m_VertexBufferID != 0)
		glDeleteBuffers(1, &m_VertexBufferID);
}
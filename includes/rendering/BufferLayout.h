#pragma once

#include "rendering/GL.h"

#include "VertexArray.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"

enum class BufferType
{
	FLOAT = GL_FLOAT,
	INT = GL_INT
};

class BufferLayout
{
private:
	int location;
	int stride;
	//VertexArray m_va;

public:
	BufferLayout() : stride{ 0 }, location{ 0 }
	{}

	// Non-copyable, non-movable
	BufferLayout(const BufferLayout&) = delete;
	BufferLayout(BufferLayout&&) = delete;
	BufferLayout& operator=(const BufferLayout&) = delete;
	BufferLayout& operator=(BufferLayout&&) = delete;

	template<typename T>
	void setBufferLayout(const VertexArray& va, const VertexBuffer<T>& buffer, IndexBuffer& indexBuffer, int count, BufferType type, bool resetStride = false);

	template<typename T>
	void setBufferLayout(const VertexArray& va, const VertexBuffer<T>& buffer, int count, BufferType type);

	[[nodiscard]]
	int getLocation() const noexcept { return location; }

private:
	int getSizeFromType(BufferType type);
};

template<typename T>
void BufferLayout::setBufferLayout(const VertexArray& va, const VertexBuffer<T>& buffer, IndexBuffer& indexBuffer, int count, BufferType type, bool resetStride)
{
	va.bind();

	buffer.bind();
	indexBuffer.bind();

	int sz = buffer.getVertexCount() * buffer.typeSize;
	glVertexAttribPointer(location, count, (GLenum)type, GL_FALSE, sz, (const void*)stride);
	glEnableVertexAttribArray(location);

	if (!resetStride)
		stride += count * getSizeFromType(type);

	location++;
}

template<typename T>
void BufferLayout::setBufferLayout(const VertexArray& va, const VertexBuffer<T>& buffer, int count, BufferType type)
{
	va.bind();

	buffer.bind();

	size_t sz = buffer.getVertexCount() * buffer.typeSize;
	glVertexAttribPointer(location, count, (GLenum)type, GL_FALSE, sz, reinterpret_cast<const void*>(stride));
	glEnableVertexAttribArray(location);

	stride += count * getSizeFromType(type);
	location++;
}

inline int BufferLayout::getSizeFromType(BufferType type)
{
	switch (type)
	{
	case BufferType::FLOAT:
		return sizeof(float);

	case BufferType::INT:
		return sizeof(int);

	default:
		return 0;
	}
}

#pragma once

#include "rendering/GL.h"

#include <cstddef>

class IndexBuffer
{
private:
	unsigned int m_IndexBufferID = 0;

public:
	IndexBuffer() = default;
	~IndexBuffer() noexcept;

	// Non-copyable, non-movable
	IndexBuffer(const IndexBuffer&) = delete;
	IndexBuffer(IndexBuffer&&) = delete;
	IndexBuffer& operator=(const IndexBuffer&) = delete;
	IndexBuffer& operator=(IndexBuffer&&) = delete;

	void generate() noexcept;

	void bind() const noexcept;

	void unbind() const noexcept;

	void setBuffer(size_t bytes, const void* data) const noexcept;

	[[nodiscard]]
	unsigned int getID() const noexcept;
};

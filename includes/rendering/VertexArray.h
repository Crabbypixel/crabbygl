#pragma once

#include "rendering/GL.h"

class VertexArray
{
private:
	unsigned int m_VertexArrayID = 0;

public:
	VertexArray() = default;

	// Non-copyable, non-movable
	VertexArray(const VertexArray&) = delete;
	VertexArray(VertexArray&&) = delete;
	VertexArray& operator=(const VertexArray&) = delete;
	VertexArray& operator=(VertexArray&&) = delete;

	void generate() noexcept;

	void bind() const noexcept;

	void unbind() const noexcept;

	unsigned int& getID() noexcept { return m_VertexArrayID; }

	~VertexArray() noexcept;
};
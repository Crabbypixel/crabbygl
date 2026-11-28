#pragma once

#include <array>

template<size_t N>
class Ring
{
public:
	void Push(float value) noexcept		// O(1), overwrites oldest when full
	{ 
		m_data[m_head] = value;
		m_head = (m_head + 1) % N;
		if (m_count < N) ++m_count;
	}

	int Count() const noexcept		// Valid samples,<= N
	{ 
		return static_cast<int>(m_count);
	}

	const float* Data() const
	{ 
		return m_data.data();
	}

	int Oldest() const				// Index of oldest sample
	{ 
		if (m_count == 0)
			return 0;
		
		return m_data[(m_head - 1) % N];
	};		

private:
	std::array<float, N> m_data{};
	size_t m_head = 0;		// next write slot
	size_t m_count = 0;
};
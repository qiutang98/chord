#pragma once

#include <utils/utils.h>

namespace chord::nanite
{
	class NaniteBuilder
	{
	public:
		explicit NaniteBuilder(std::vector<uint32>&& indices, uint32 verticesCount, const math::vec3* positions);


	private:

	private:
		// Indices of triangles.
		std::vector<uint32> m_indices;

		// Vertices fetch.
		const uint32 m_verticesCount;
		const math::vec3* m_positions;
	};
}
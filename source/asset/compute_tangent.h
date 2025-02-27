#pragma once

#include <asset/nanite_builder.h>

namespace chord
{
	extern bool computeTangent(std::vector<nanite::Vertex>& rawVerticesPtr, std::vector<uint32>& rawIndicesPtr);
}
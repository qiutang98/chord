#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>

namespace chord::graphics
{
	GPUResource::GPUResource(const std::string& name, VkDeviceSize size)
		: m_name(name)
		, m_size(size)
	{
	
	}

	GPUResource::~GPUResource()
	{
	
	}

}
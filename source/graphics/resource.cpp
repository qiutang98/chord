#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <application/application.h>

namespace chord::graphics
{
	GPUResource::GPUResource(const std::string& name, VkDeviceSize size)
		: m_size(size)
	{
		setName(name);
	}

	GPUResource::~GPUResource()
	{
	
	}

	bool GPUResource::setName(const std::string& name)
	{
		if (m_setNameFrame != getFrameCounter())
		{
			m_names.clear();

			// Insert new name.
			m_names.insert(name);
			m_flattenName = name;

			m_setNameFrame = getFrameCounter();
			return true;
		}

		if (!m_names.contains(name))
		{
			// Clear cache name avoid too large.
			if (m_flattenName.size() > 256)
			{
				m_flattenName = {};
				m_names.clear();
			}

			// Insert new name.
			m_names.insert(name);

			// Update flatten name.
			if (!m_flattenName.empty())
			{
				m_flattenName += ";";
			}
			m_flattenName += name;

			// Name already update.
			return true;
		}

		// No update name.
		return false;
	}

}
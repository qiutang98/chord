#pragma once
#include <utils/utils.h>

namespace chord
{
	class LocalizationString
	{
	private:
		// Common name: english name.
		std::string m_name; 

		// Localization utf8 name. 
		std::string m_localizationName;  

	public:
		explicit LocalizationString(const std::string& name)
			: m_name(name), m_localizationName(name)
		{

		}

		explicit LocalizationString(const std::string& name, const std::string& localizationName)
			: m_name(name), m_localizationName(localizationName)
		{

		}

		// Dynamic switch localization name.
		const char* name() const
		{
			return m_localizationName.data();
		}
	};
}
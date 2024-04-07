#include <utils/cvar.h>

namespace chord
{
	CVarSystem& CVarSystem::get()
	{
		static CVarSystem cvars { };
		return cvars;
	}

	bool CVarSystem::setValueIfExistGeneric(std::string_view name, const std::string& value)
	{
		if (CVarStorage* storage = getCVarIfExistGeneric(name))
		{
			if (storage->isValueTypeMatch(getTypeName<float>()))
			{
				((CVarStorageInterface<float>*)storage)->set(std::stof(value));
				return true;
			}
			else if (storage->isValueTypeMatch(getTypeName<std::string>()))
			{
				((CVarStorageInterface<std::string>*)storage)->set(value);
				return true;
			}
			else if (storage->isValueTypeMatch(getTypeName<double>()))
			{
				((CVarStorageInterface<double>*)storage)->set(std::stod(value));
				return true;
			}
			else if (storage->isValueTypeMatch(getTypeName<int32>()))
			{
				((CVarStorageInterface<int32>*)storage)->set(std::stoi(value));
				return true;
			}
			else if (storage->isValueTypeMatch(getTypeName<uint32>()))
			{
				((CVarStorageInterface<uint32>*)storage)->set(std::stoi(value));
				return true;
			}
			else if (storage->isValueTypeMatch(getTypeName<bool>()))
			{
				const bool bTrue  = (value == "true")  || (value == "1");
				const bool bFalse = (value == "false") || (value == "0");
				if (bTrue || bFalse)
				{
					((CVarStorageInterface<bool>*)storage)->set(bTrue);
					return true;
				}
			}
		}

		return false;
	}

	std::string CVarSystem::getValueIfExistGeneric(std::string_view name)
	{
		if (CVarStorage* storage = getCVarIfExistGeneric(name))
		{
			if (storage->isValueTypeMatch(getTypeName<float>()))
			{
				return std::to_string(((CVarStorageInterface<float>*)storage)->get());
			}
			else if (storage->isValueTypeMatch(getTypeName<std::string>()))
			{
				return ((CVarStorageInterface<std::string>*)storage)->get();
			}
			else if (storage->isValueTypeMatch(getTypeName<double>()))
			{
				return std::to_string(((CVarStorageInterface<double>*)storage)->get());
			}
			else if (storage->isValueTypeMatch(getTypeName<int32>()))
			{
				return std::to_string(((CVarStorageInterface<int32>*)storage)->get());
			}
			else if (storage->isValueTypeMatch(getTypeName<uint32>()))
			{
				return std::to_string(((CVarStorageInterface<uint32>*)storage)->get());
			}
			else if (storage->isValueTypeMatch(getTypeName<bool>()))
			{
				return std::to_string(((CVarStorageInterface<bool>*)storage)->get());
			}
		}

		return {};
	}
}
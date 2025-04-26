#include <utils/cvar.h>
#include <utils/log.h>

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
			if (hasFlag(storage->getFlags(), EConsoleVarFlags::ReadOnly))
			{
				LOG_ERROR("CVar '{}' is read only.", name);
				return false;
			}

			// Lock now.
			std::lock_guard lock(m_lock);

			if (storage->isValueTypeMatch(getTypeName<float>()))
			{
				try
				{
					((CVarStorageInterface<float>*)storage)->set(std::stof(value));
					return true;
				}
				catch (...)
				{
					LOG_ERROR("Can't convert input param '{}' to float.", value);
				}
			}
			else if (storage->isValueTypeMatch(getTypeName<u16str>()))
			{
				// String is utf8 encode.
				((CVarStorageInterface<u16str>*)storage)->set(u16str(value));
				return true;
			}
			else if (storage->isValueTypeMatch(getTypeName<double>()))
			{
				try
				{
					((CVarStorageInterface<double>*)storage)->set(std::stod(value));
					return true;
				}
				catch (...)
				{
					LOG_ERROR("Can't convert input param '{}' to double.", value);
				}
			}
			else if (storage->isValueTypeMatch(getTypeName<int32>()))
			{
				if (isDigitString(value))
				{
					((CVarStorageInterface<int32>*)storage)->set(std::stoi(value));
					return true;
				}
				else
				{
					LOG_ERROR("Input param '{}' can't convert to digit format.", value);
				}
			}
			else if (storage->isValueTypeMatch(getTypeName<uint32>()))
			{
				if (isDigitString(value))
				{
					((CVarStorageInterface<uint32>*)storage)->set(std::stoi(value));
					return true;
				}
				else
				{
					LOG_ERROR("Input param '{}' can't convert to digit format.", value);
				}
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
				else
				{
					LOG_ERROR("Input param '{}' can't convert to bool format.", value);
				}
			}
			else
			{
				checkEntry();
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
			else if (storage->isValueTypeMatch(getTypeName<u16str>()))
			{
				// Default return u8 string.
				return ((CVarStorageInterface<u16str>*)storage)->get().u8();
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
			else
			{
				checkEntry();
			}
		}

		return {};
	}
}
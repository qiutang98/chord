#include <graphics/shader.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>

namespace chord::graphics
{
	ShaderInterface::~ShaderInterface()
	{
		clear();
	}

	void ShaderInterface::clear()
	{
		for (auto& shaderPair : m_shaders)
		{
			auto& optionalShaderModule = shaderPair.second;
			if (optionalShaderModule.isValid())
			{
				helper::destroyShaderModule(optionalShaderModule.get());
			}
		}
		m_shaders.clear();


	}

	void ShaderLibrary::release()
	{
		m_shaders.clear();
	}

	void ShaderCompileDefinitions::setDefine(const std::string& name, const std::string& v)
	{
		m_definitions[name] = v;
	}

	void ShaderCompileDefinitions::setDefine(const std::string& name, bool v)
	{
		m_definitions[name] = v ? "1" : "0";
	}

	void ShaderCompileDefinitions::setDefine(const std::string& name, int32 v)
	{
		switch (v)
		{
		case 0:  m_definitions[name] = "0"; break;
		case 1:  m_definitions[name] = "1"; break;

		// Fallback utils.
		default: m_definitions[name] = std::to_string(v);
		}
	}

	void ShaderCompileDefinitions::setDefine(const std::string& name, uint32 v)
	{
		switch (v)
		{
		case 0u:  m_definitions[name] = "0"; break;
		case 1u:  m_definitions[name] = "1"; break;

		// Fallback utils.
		default: m_definitions[name] = std::to_string(v);
		}
	}

	void ShaderCompileDefinitions::setDefine(const std::string& name, float v)
	{
		m_definitions[name] = std::format("{:.9f}", v);
	}

	void ShaderCompileDefinitions::add(const ShaderCompileDefinitions& o)
	{
		m_definitions.insert(o.m_definitions.begin(), o.m_definitions.end());
	}

}


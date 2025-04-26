#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <graphics/graphics.h>
#include <utils/cityhash.h>

namespace chord::graphics
{
	// Store definitions and its' values.
	class ShaderCompileDefinitions
	{
	public:
		void setDefine(const std::string& name, const std::string& v)
		{
			m_definitions[name] = v;
		}

		void setDefine(const std::string& name, bool v)
		{
			m_definitions[name] = v ? "1" : "0";
		}

		void setDefine(const std::string& name, int32 v)
		{
			switch (v)
			{
			case 0:  m_definitions[name] = "0"; break;
			case 1:  m_definitions[name] = "1"; break;

				// Fallback utils.
			default: m_definitions[name] = std::to_string(v);
			}
		}

		void setDefine(const std::string& name, uint32 v)
		{
			switch (v)
			{
			case 0u:  m_definitions[name] = "0"; break;
			case 1u:  m_definitions[name] = "1"; break;

				// Fallback utils.
			default: m_definitions[name] = std::to_string(v);
			}
		}

		void setDefine(const std::string& name, float v)
		{
			m_definitions[name] = std::format("{:.9f}", v);
		}

		void add(const ShaderCompileDefinitions& o)
		{
			m_definitions.insert(o.m_definitions.begin(), o.m_definitions.end());
		}

		uint64 computeHash(uint64 seed) const
		{
			for (const auto& pair : m_definitions)
			{
				cityhash::ctyhash64WithSeed(pair.first.c_str(), pair.first.size(), seed);
				cityhash::ctyhash64WithSeed(pair.second.c_str(), pair.second.size(), seed);
			}
			return seed;
		}

		const auto& getMap() const { return m_definitions; }

	private:
		std::map<std::string, std::string> m_definitions;
	};

	class ShaderCompileInstruction
	{
	public:
		void add(const std::string& type)
		{
			m_instruction.insert(type);
		}

		uint64 computeHash(uint64 seed) const
		{
			for (const auto& s : m_instruction)
			{
				cityhash::ctyhash64WithSeed(s.c_str(), s.size(), seed);
			}

			return seed;
		}

		const auto& getInstructions() const { return m_instruction; }

	private:
		std::set<std::string> m_instruction;
	};

	class GlobalShaderRegisteredInfo
	{
	private:
		// Runtime cache shader file hash.
		uint64 m_shaderFileHash;



	public:
		// 
		const std::string shaderName;
		const std::string shaderFilePath;
		const std::string entry;
		const EShaderStage stage;
		//
		const uint32 shaderFileNameHashId;

		static bool shouldRegisterGlobalShader();

		explicit GlobalShaderRegisteredInfo(
			const std::string&  shaderNameIn,
			const std::string&  shaderFilePathIn,
			const std::string&  entryIn,
			const EShaderStage& stageIn,
			const uint32 shaderFileNameHashIdIn)
			: shaderName(shaderNameIn)
			, shaderFilePath(shaderFilePathIn)
			, entry(entryIn)
			, stage(stageIn)
			, shaderFileNameHashId(shaderFileNameHashIdIn)
		{
			updateShaderFileHash();
		}

		void updateShaderFileHash(); 

		uint64 getHash() const
		{
			return m_shaderFileHash;
		}
	};

	// Shader compile environment.
	class ShaderCompileEnvironment
	{
	public:
		explicit ShaderCompileEnvironment(const GlobalShaderRegisteredInfo& info)
			: m_metaInfo(info)
		{

		}

		void setDefine(const std::string& name, const std::string& v) { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, bool   v) { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, int32  v) { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, uint32 v) { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, float  v) { m_definitions.setDefine(name, v); }

		const auto& getDefinitions() const { return m_definitions; }
		const auto& getInstructions() const { return m_instructions; }

		const auto& getName() const { return m_metaInfo.shaderFilePath; }
		const auto& getEntry() const { return m_metaInfo.entry; }
		const auto  getShaderStage() const { return m_metaInfo.stage; }

		const GlobalShaderRegisteredInfo& getMetaInfo() const { return m_metaInfo; }
		void buildArgs(ShaderCompileArguments& out) const;

		void enableDebugSource();

	private:
		const GlobalShaderRegisteredInfo& m_metaInfo;

		// Additional define.
		ShaderCompileDefinitions m_definitions;

		// Additional instruction.
		ShaderCompileInstruction m_instructions;
	};

	// Bool type variant.
	class ShaderVariantBool
	{
	public:
		using Type = bool;
		static constexpr uint32 kCount = 2; // 0 or 1.
		static constexpr bool bMultiVariant = false;

		static int32 toId(bool b)
		{
			return b ? 1 : 0;
		}

		static bool fromId(int32 value)
		{
			// Bool only support 0 or 1.
			checkGraphics(value == 0 || value == 1);

			// Convert to bool.
			return value == 1;
		}

		static bool toDefined(Type b)
		{
			return b;
		}
	};
	// Define TEST_SHADER_VARIANT 0/1
	// Class SV_TestVariant : SHADER_VARIANT_BOOL("TEST_SHADER_VARIANT");
	#define SHADER_VARIANT_BOOL(x) public chord::graphics::ShaderVariantBool { public: static constexpr const char* kName = x; }

	// Continuous int range variant.
	template<typename T, int32 kDimSize, int32 kFirstValue = 0>
	class ShaderVariantInt
	{
	public:
		using Type = T;
		static constexpr uint32 kCount = kDimSize;
		static constexpr bool bMultiVariant = false;

		static int32 toId(Type b)
		{
			int32 id = static_cast<int32>(b) - kFirstValue;
			checkGraphics(id >= 0 && id < kCount);
			return id;
		}

		static Type fromId(int32 id)
		{
			checkGraphics(id >= 0 && id < kCount);
			return static_cast<Type>(id + kFirstValue);
		}

		static int32 toDefined(Type b)
		{
			return toId(b) + kFirstValue;
		}
	};
	// Define TEST_SHADER_VARIANT [0, N)
	// Class SV_TestVariant : SHADER_VARIANT_INT("TEST_SHADER_VARIANT", N);
	#define SHADER_VARIANT_INT(x, count) \
	public chord::graphics::ShaderVariantInt<int32, count> { public: static constexpr const char* kName = x; }

	// Define TEST_SHADER_VARIANT [X, N + X)
	// Class SV_TestVariant : SHADER_VARIANT_RANGE_INT("TEST_SHADER_VARIANT", X, N);
	#define SHADER_VARIANT_RANGE_INT(x, start, count) \
	public chord::graphics::ShaderVariantInt<int32, count, start> { public: static constexpr const char* kName = x; }

	// Ensure enum end with MAX, it will define [0, MAX)
	#define SHADER_VARIANT_ENUM(x, T) \
	public chord::graphics::ShaderVariantInt<T, static_cast<int32>(T::MAX)> { public: static constexpr const char* kName = x; }

	template<int32... Ts>
	class ShaderVariantSparseInt
	{
	public:
		using Type = int32;
		static constexpr uint32 kCount = 0;
		static constexpr bool bMultiVariant = false;

		static int32 toId(Type b)
		{
			checkEntry();
			return 0;
		}

		static Type fromId(int32 id)
		{
			checkEntry();
			return Type(0);
		}
	};

	template<int32 Value, int32... Ts>
	class ShaderVariantSparseInt<Value, Ts...>
	{
	public:
		using Type = int32;
		static constexpr uint32 kCount = ShaderVariantSparseInt<Ts...>::kCount + 1;
		static constexpr bool bMultiVariant = false;

		static int32 toId(Type b)
		{
			if (b == Value)
			{
				return kCount - 1;
			}

			return ShaderVariantSparseInt<Ts...>::toId(b);
		}

		static Type fromId(int32 id)
		{
			if (id == kCount - 1)
			{
				return Value;
			}
			return ShaderVariantSparseInt<Ts...>::fromId(id);
		}

		static int32 toDefined(Type b)
		{
			return int32(b);
		}
	};

	#define SHADER_VARIANT_SPARSE_INT(x, ...) \
	public chord::graphics::ShaderVariantSparseInt<__VA_ARGS__> { public: static constexpr const char* kName = x; }

	// Shader variant vector merge all type of variant.
	template<typename... Ts>
	class TShaderVariantVector
	{
	public:
		using Type = TShaderVariantVector<Ts...>;
		static constexpr uint32 kCount = 1;
		static constexpr bool bMultiVariant = true;

		TShaderVariantVector<Ts...>()
		{

		}

		// Utils template class so don't support non-zero id, it should not use for compile.
		explicit TShaderVariantVector<Ts...>(int32 id)
		{
			checkGraphics(id == 0);
		}

		// Set value.
		template<typename T>
		void set(const typename T::Type& Value)
		{
			checkEntry();
		}

		// Get value.
		template<typename T>
		const typename T::Type get() const
		{
			checkEntry();
			return { };
		}

		void modifyCompileEnvironment(ShaderCompileEnvironment& o) const
		{
			// Do nothing.
		}

		static int32 toId(const Type& vector)
		{
			return 0;
		}

		int32 toId() const
		{
			return toId(*this);
		}

		static Type fromId(int32 id)
		{
			return Type(id);
		}

		bool operator==(const Type& o) const
		{
			return true;
		}
	};

	template<typename T, typename... Ts>
	class TShaderVariantVector<T, Ts...>
	{
	public:
		using Type = TShaderVariantVector<T, Ts...>;
		using Next = TShaderVariantVector<Ts...>;

		static constexpr uint32 kCount = Next::kCount * T::kCount;
		static constexpr bool bMultiVariant = true;

		TShaderVariantVector<T, Ts...>()
			: m_value(T::fromId(0))
		{ }

		explicit TShaderVariantVector<T, Ts...>(int32 id)
			: m_value(T::fromId(id% T::kCount))
			, m_next(id / T::kCount)
		{
			checkGraphics(id >= 0 && id < kCount);
		}

		// Set value.
		template<typename ToSet>
		void set(const auto& value)
		{
			if constexpr (std::is_same_v<T, ToSet>)
			{
				// When type match, just set it's value.
				m_value = value;
			}
			else
			{
				// Iter next type.
				m_next.set<ToSet>(value);
			}
		}

		// Get value.
		template<typename ToGet>
		const auto& get() const
		{
			if constexpr (std::is_same_v<T, ToGet>)
			{
				// When type match, just return it's value.
				return m_value;
			}
			else
			{
				// Iter next type.
				return m_next.get<ToGet>();
			}
		}

		void modifyCompileEnvironment(ShaderCompileEnvironment& o) const
		{
			if constexpr (T::bMultiVariant)
			{
				// Multi variant case, which meaning T is a variant vector, so deeping.
				m_value.modifyCompileEnvironment(o);

				// Also need to call next.
				m_next.modifyCompileEnvironment(o);
			}
			else
			{
				// When enter single type, just set value define.
				o.setDefine(T::kName, T::toDefined(m_value));

				// Also call next type if exist.
				m_next.modifyCompileEnvironment(o);
			}
		}

		static int32 toId(const Type& vector)
		{
			return vector.toId();
		}

		int32 toId() const
		{
			return T::toId(m_value) + T::kCount * m_next.toId();
		}

		static Type fromId(int32 id)
		{
			return Type(id);
		}

		bool operator==(const Type& o) const
		{
			return (m_value == o.m_value) && (m_next == o.m_next);
		}

		bool operator!=(const Type& o) const
		{
			return !(*this == o);
		}

	private:
		template<bool> friend class TShaderVariantImpl;

		typename T::Type m_value;
		Next m_next;
	};
}
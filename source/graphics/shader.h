#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <graphics/graphics.h>

namespace chord::graphics
{
	class ShaderCompileDefinitions
	{
	public:
		void setDefine(const std::string& name, const std::string& v);
		void setDefine(const std::string& name, bool v);
		void setDefine(const std::string& name, int32 v);
		void setDefine(const std::string& name, uint32 v);
		void setDefine(const std::string& name, float v);

		void add(const ShaderCompileDefinitions& o);

	private:
		std::map<std::string, std::string> m_definitions;
	};

	// Shader compile environment.
	class ShaderCompileEnvironment
	{
	public:
		void setDefine(const std::string& name, const std::string& v) { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, bool v)   { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, int32 v)  { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, uint32 v) { m_definitions.setDefine(name, v); }
		void setDefine(const std::string& name, float v)  { m_definitions.setDefine(name, v); }

	private:
		ShaderCompileDefinitions m_definitions;
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
	#define SHADER_VARIANT_BOOL(x) public ShaderVariantBool { public: static constexpr const char* kName = x; }

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
	public ShaderVariantInt<int32, count> { public: static constexpr const char* kName = x; }

	// Define TEST_SHADER_VARIANT [X, N + X)
	// Class SV_TestVariant : SHADER_VARIANT_RANGE_INT("TEST_SHADER_VARIANT", X, N);
	#define SHADER_VARIANT_RANGE_INT(x, start, count) \
	public ShaderVariantInt<int32, count, start> { public: static constexpr const char* kName = x; }

	// Ensure enum end with MAX, it will define [0, MAX)
	#define SHADER_VARIANT_ENUM(x, T) \
	public ShaderVariantInt<T, static_cast<int32>(T::MAX)> { public: static constexpr const char* kName = x; }

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
	public ShaderVariantSparseInt<__VA_ARGS__> { public: static constexpr const char* kName = x; }

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

	template<bool bNext>
	class TShaderVariantImpl
	{
	public:
		template<typename VariantVector, typename NextToGet>
		static const typename NextToGet::Type& get(const VariantVector& vector)
		{
			return vector.m_next.template get<NextToGet>();
		}

		template<typename VariantVector, typename NextToSet>
		static void set(VariantVector& vector, const typename NextToSet::Type& value)
		{
			return vector.m_next.template set<NextToSet>(value);
		}

		template<typename VariantVector, typename T>
		static void modifyCompileEnvironment(const VariantVector& vector, ShaderCompileEnvironment& outEnvironment)
		{
			outEnvironment.setDefine(T::kName, T::toDefined(vector.m_value));
			return vector.m_next.modifyCompileEnvironment(outEnvironment);
		}
	};

	template<>
	class TShaderVariantImpl<true>
	{
	public:
		template<typename VariantVector, typename NextToGet>
		static const typename NextToGet::Type& get(const VariantVector& vector)
		{
			return vector.m_value;
		}

		template<typename VariantVector, typename NextToSet>
		static void set(VariantVector& vector, const typename NextToSet::Type& value)
		{
			vector.m_value = value;
		}

		template<typename VariantVector, typename T>
		static void modifyCompileEnvironment(const VariantVector& vector, ShaderCompileEnvironment& outEnvironment)
		{
			vector.m_value.modifyCompileEnvironment(outEnvironment);
			return vector.m_next.modifyCompileEnvironment(outEnvironment);
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
			: m_value(T::fromId(id % T::kCount))
			, m_next(id / T::kCount)
		{
			checkGraphics(id >= 0 && id < kCount);
		}

		// Set value.
		template<typename ToSet>
		void set(const typename ToSet::Type& value)
		{
			return TShaderVariantImpl<std::is_same_v<T, ToSet>>::template set<Type, ToSet>(*this, value);
		}

		// Get value.
		template<typename ToGet>
		const typename ToGet::Type& get() const
		{ 
			return TShaderVariantImpl<std::is_same_v<T, ToGet>>::template get<Type, ToGet>(*this);
		}

		void modifyCompileEnvironment(ShaderCompileEnvironment& o) const
		{
			TShaderVariantImpl<T::bMultiVariant>::template modifyCompileEnvironment<Type, T>(*this, o);
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
			return m_value == o.m_value && m_next == o.m_next;
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

	class ShaderKey
	{
	public:
		// Shader inner hash code.
		uint64 hash;

		// Shader name hash.
		uint32 name;

		// Shader type: builtin/material etc.
		uint32 type;
	};

	class ShaderPermutationType
	{
	public:
		std::string name;



	};

	template<typename...Args>
	class ShaderPermutation
	{

	};

	// Shader contain multi permutation.
	class ShaderInterface : NonCopyable
	{
	public:
		virtual ~ShaderInterface();

	protected:
		void clear();

	protected:
		// Cache shader modules.
		std::unordered_map<uint64, OptionalVkShaderModule> m_shaders;


	};

	class BuiltinShader : public ShaderInterface
	{
	private:


	};

	class ShaderFile
	{
	public:


	private:
		std::string m_filePath;

		std::map<uint64, std::unique_ptr<ShaderInterface>> m_shaderCollection;
	};


	// All shader cache library.
	class ShaderLibrary : NonCopyable
	{
	public:
		void release();

	private:
		std::map<uint64, std::unique_ptr<ShaderFile>> m_shaders;
	};
}